// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "fs.h"

#include "aanyptr.h"
#include "aregex_pcre2.h"
#include "astream.h"
#include "averify.h"
#include "stop.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iterator>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "config.h" // the module switch (src/config.h)

#if ALXRUN_FS_READ_ENABLE || ALXRUN_FS_WRITE_ENABLE

namespace capability {

    namespace {

        // One match is bounded as in regex.cpp -- a pattern the host wrote could otherwise
        // backtrack across a whole run's window.
        constexpr alx::uint_64 kStepLimit = 10000000;

        // One descriptor, closed on every path out, including the throws below.
        struct fd_guard {
            int fd = -1;
            ~fd_guard() {
                if (fd >= 0) ::close(fd);
            }
        };

        // The wall (design.md §6): one rule set per direction, compiled once from config.h. Roots
        // first -- a path no root covers is refused without the deny pass seeing it, so the patterns
        // cannot be probed from outside. Accepted cost: resolving happens before the check, so errno
        // still says whether an out-of-reach path exists.

        // realpath, so the rules judge what open() will touch; false leaves errno set.
        bool canonical(const std::string& _path, std::string& _out) {
            char* rp = ::realpath(_path.c_str(), nullptr);
            if (!rp) return false;
            _out.assign(rp);
            ::free(rp);
            return true;
        }

        // The placeholders only the running host can fill in (src/config.h). A root may be one on
        // its own or start with one ("{home}/data"); anything else is left alone and the
        // canonicalization below drops it.
        std::string expand(const char* _root) {
            const std::string text(_root);
            const size_t brace = text.find('}');
            if (text.size() < 2 || text[0] != '{' || brace == std::string::npos) return text;
            const std::string word = text.substr(1, brace - 1);
            std::string head;
            if (word == "home") {
                const char* home = ::getenv("HOME");
                if (home && *home) head = home;
            } else if (word == "tmp") {
                const char* tmp = ::getenv("TMPDIR");
                head = tmp && *tmp ? tmp : "/tmp";
            } else if (word == "cwd") {
                char* cwd = ::getcwd(nullptr, 0);
                if (cwd) {
                    head = cwd;
                    ::free(cwd);
                }
            } else {
                return text;
            }
            if (head.empty()) return std::string(); // unresolved: the root is dropped, not guessed
            return head + text.substr(brace + 1);
        }

        // The same flag the regex module reads: the wall's own matching runs inside a run, so the
        // run's window reaches it too.
        bool still_wanted(void*) {
            return !g_stop.load(std::memory_order_relaxed);
        }

        struct rules {
            std::vector<std::string> roots; // canonical, no trailing "/"
            std::vector<alx::regex_pcre2> deny;
            std::vector<std::string> deny_text; // the same patterns as written, for the refusals
            bool closed = false; // a pattern that will not compile: nothing passes
        };

        std::string join(const std::vector<std::string>& _v, const char* _sep) {
            std::string out;
            for (const std::string& s : _v) out += (out.empty() ? "" : _sep) + s;
            return out;
        }

        rules build(const char* const* _root_text, size_t _root_count, const char* const* _deny_text,
                    size_t _deny_count, const char* _kind) {
            rules r;
            for (size_t i = 0; i < _root_count; ++i) {
                std::string root;
                if (!canonical(expand(_root_text[i]), root) || root.empty()) {
                    std::fprintf(stderr, "alxrun: fs %s root %s does not resolve: dropped\n", _kind,
                                 _root_text[i]);
                    continue;
                }
                while (root.size() > 1 && root.back() == '/')
                    root.pop_back();
                r.roots.push_back(std::move(root));
            }
            for (size_t i = 0; i < _deny_count; ++i) {
                alx::regex_pcre2 re(_deny_text[i]);
                if (!re.is_valid()) {
                    std::fprintf(stderr, "alxrun: fs %s pattern %s is not a valid regex: %s denied\n",
                                 _kind, _deny_text[i], _kind);
                    r.closed = true;
                    continue;
                }
                re.set_step_limit(kStepLimit);
                re.set_callout(&still_wanted, nullptr);
                r.deny.push_back(std::move(re));
                r.deny_text.emplace_back(_deny_text[i]);
            }
            return r;
        }

#    if ALXRUN_FS_READ_ENABLE
        const rules& read_rules() {
            static const rules r = build(config::kFsReadRoots, std::size(config::kFsReadRoots),
                                         config::kFsReadDeny, std::size(config::kFsReadDeny), "read");
            return r;
        }
#    endif

#    if ALXRUN_FS_WRITE_ENABLE
        const rules& write_rules() {
            static const rules r = build(config::kFsWriteRoots, std::size(config::kFsWriteRoots),
                                         config::kFsWriteDeny, std::size(config::kFsWriteDeny), "write");
            return r;
        }
#    endif

        // "/" is the boundary: /a/b must not hand over /a/bc.
        bool allowed(const rules& _r, const std::string& _p) {
            for (const std::string& root : _r.roots) {
                if (root == "/") return true;
                if (_p.size() < root.size() || _p.compare(0, root.size(), root) != 0) continue;
                if (_p.size() == root.size() || _p[root.size()] == '/') return true;
            }
            return false;
        }

        // Containment anywhere in the path ("/\.git/" catches one at any depth); a pattern the
        // engine cannot finish is a refusal, never a silent pass. $fs_grep matches with it too.
        bool hits(const alx::regex_pcre2& _re, const std::string& _p, const char* _cap) {
            _re.find(_p);
            switch (_re.last_status()) {
            case alx::regex_pcre2::match_status::ok:
                return true;
            case alx::regex_pcre2::match_status::no_match:
                return false;
            case alx::regex_pcre2::match_status::interrupted:
                // The kernel rewrites this one when its watchdog was the reason the flag went up.
                throw alx::script::script_exception{alx::script::error_type::InterruptedError,
                                                    "time budget exceeded"};
            case alx::regex_pcre2::match_status::limit:
                throw alx::script::script_exception{
                    alx::script::error_type::ResourceError,
                    std::string(_cap) + ": backtracking step limit (" + std::to_string(kStepLimit) +
                        ") reached"};
            default:
                throw alx::script::script_exception{
                    alx::script::error_type::RuntimeError,
                    std::string(_cap) + ": regex engine error " + std::to_string(_re.last_error_code())};
            }
        }

        // The one door; the refusal names the canonical path and the rule that judged it.
        void check(const rules& _r, const std::string& _p, const char* _cap, const char* _kind) {
            if (_r.closed) fail(_cap, _p, "the host's fs rules did not load");
            if (!allowed(_r, _p))
                fail(_cap, _p,
                     std::string("outside the allowed ") + _kind + " roots (" + join(_r.roots, ", ") + ")");
            for (size_t i = 0; i < _r.deny.size(); ++i)
                if (hits(_r.deny[i], _p, _cap))
                    fail(_cap, _p, "denied by the host rules (" + _r.deny_text[i] + ")");
        }

        // A path reaches the kernel as a C string, so a NUL would split judge from open.
        void no_nul(const std::string& _path, const char* _cap) {
            if (_path.find('\0') != std::string::npos) fail(_cap, _path, "path contains a NUL byte");
        }

#    if ALXRUN_FS_READ_ENABLE
        // The read door: canonicalize, judge, hand back what the caller opens.
        std::string read_target(const std::string& _given, const char* _cap) {
            no_nul(_given, _cap);
            std::string p;
            if (!canonical(_given, p)) {
                if (errno == ENOENT) fail(_cap, _given, "no such file");
                fail(_cap, _given, std::strerror(errno));
            }
            check(read_rules(), p, _cap, "read");
            return p;
        }
#    endif

#    if ALXRUN_FS_WRITE_ENABLE
        // The write door: the parent must resolve (nothing here creates directories) and the last
        // component must be a plain name, so ".." cannot slip a target past the rules.
        std::string write_target(const std::string& _given, const char* _cap) {
            no_nul(_given, _cap);
            const size_t slash = _given.rfind('/');
            const std::string name = slash == std::string::npos ? _given : _given.substr(slash + 1);
            if (name.empty() || name == "." || name == "..") fail(_cap, _given, "not a file name");
            std::string parent = slash == std::string::npos ? std::string(".") : _given.substr(0, slash);
            if (parent.empty()) parent = "/";
            std::string dir;
            if (!canonical(parent, dir)) fail(_cap, _given, "no such directory: " + parent);
            const std::string target = dir + "/" + name;
            check(write_rules(), target, _cap, "write");
            // The stream opens have no O_NOFOLLOW of their own (measured: a symlink at the target
            // landed content outside the root); $fs_write's open() refuses it, this covers the
            // other three. What remains is the check-to-open race, which only O_NOFOLLOW inside
            // AlxLib's streams can close.
            struct stat st{};
            if (::lstat(target.c_str(), &st) == 0 && S_ISLNK(st.st_mode))
                fail(_cap, _given, "is a symbolic link");
            return target;
        }
#    endif

#    if ALXRUN_FS_READ_ENABLE
        // Open the judged path itself (not the one the script wrote), O_NOFOLLOW.
        int open_regular(const std::string& _given, const char* _cap) {
            const std::string p = read_target(_given, _cap);
            // O_NONBLOCK matters before the type check, not after: open() on a FIFO with no writer
            // blocks until one shows up -- a syscall no budget can cut (measured: it hung until the
            // test's own timeout). It is a no-op for the regular files we do accept.
            const int fd = ::open(p.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
            if (fd < 0) fail(_cap, _given, std::strerror(errno));
            struct stat st{};
            if (::fstat(fd, &st) != 0) {
                const std::string why = std::strerror(errno);
                ::close(fd);
                fail(_cap, _given, why);
            }
            if (S_ISDIR(st.st_mode)) {
                ::close(fd);
                fail(_cap, _given, "is a directory");
            }
            if (!S_ISREG(st.st_mode)) {
                ::close(fd);
                fail(_cap, _given, "not a regular file (only regular files can be read)");
            }
            return fd;
        }
#    endif

        // A handle is a descriptor the door opened, and that is all these two carry. AlxLib's
        // istream_file/ostream_file open the path themselves (no O_NOFOLLOW: a symlink is followed)
        // and trust st_size (procfs reports 0 and still has content, so a read through their handle
        // came back empty while $fs_read had it). Both measured: the door owns the fd, these move
        // bytes.
#    if ALXRUN_FS_READ_ENABLE
        class file_istream final : public alx::istream {
        public:
            file_istream(int _fd, alx::uint_64 _total) : fd_(_fd), total_(_total) {}
            ~file_istream() override {
                if (fd_ >= 0) ::close(fd_);
            }
            alx::bytes_view get(alx::uint_64 _ofst, alx::uint_64 _size) const override {
                buf_.resize(static_cast<size_t>(_size));
                for (;;) {
                    const ssize_t n = ::pread(fd_, buf_.data(), buf_.size(), static_cast<off_t>(_ofst));
                    if (n < 0 && errno == EINTR) continue;
                    if (n <= 0) return alx::bytes_view();
                    return alx::bytes_view(buf_, 0, static_cast<alx::uint_64>(n));
                }
            }
            alx::uint_64 total() const override { return total_; }

        private:
            int fd_;
            alx::uint_64 total_; // st_size: 0 for a file that has content but does not report a size
            mutable alx::bytes buf_;
        };
#    endif

#    if ALXRUN_FS_WRITE_ENABLE
        class file_ostream final : public alx::ostream {
        public:
            explicit file_ostream(int _fd) : fd_(_fd) {}
            ~file_ostream() override {
                if (fd_ >= 0) ::close(fd_);
            }
            bool append(const void* _ptr, alx::uint_64 _size) override {
                const char* at = static_cast<const char*>(_ptr);
                alx::uint_64 left = _size;
                while (left > 0) {
                    const ssize_t n = ::write(fd_, at, static_cast<size_t>(left));
                    if (n < 0) {
                        if (errno == EINTR) continue;
                        return false;
                    }
                    at += n;
                    left -= static_cast<alx::uint_64>(n);
                    written_ += static_cast<alx::uint_64>(n);
                }
                return true;
            }
            bool flush() override { return true; } // nothing is buffered: the door writes through
            void reset() override {}
            alx::uint_64 total() const override { return written_; }

        private:
            int fd_;
            alx::uint_64 written_ = 0;
        };
#    endif

#    if ALXRUN_FS_READ_ENABLE
        // The three stream entries. The only difference from the whole-file pair is where the fd
        // goes: to a handle instead of a close.
        alx::anyptr read_handle(const std::string& _given, const char* _cap) {
            const int fd = open_regular(_given, _cap); // canonical + wall + O_NOFOLLOW + type gate
            struct stat st{};
            const alx::uint_64 total =
                ::fstat(fd, &st) == 0 ? static_cast<alx::uint_64>(st.st_size) : 0;
            std::unique_ptr<alx::istream> s(new file_istream(fd, total));
            return alx::anyptr_ex<alx::istream>::make_ref(s.release());
        }
#    endif

#    if ALXRUN_FS_WRITE_ENABLE
        alx::anyptr write_handle(const std::string& _given, const char* _cap, bool _append) {
            const std::string target = write_target(_given, _cap); // wall + plain name + no symlink
            const int flags = O_WRONLY | O_CREAT | O_NOFOLLOW | (_append ? O_APPEND : O_TRUNC);
            const int fd = ::open(target.c_str(), flags, 0644);
            if (fd < 0) {
                if (errno == ELOOP) fail(_cap, _given, "is a symbolic link");
                fail(_cap, _given, std::strerror(errno));
            }
            std::unique_ptr<alx::ostream> s(new file_ostream(fd));
            return alx::anyptr_ex<alx::ostream>::make_ref(s.release());
        }
#    endif

#    if ALXRUN_FS_READ_ENABLE
        // One read() that survives a signal interruption; -1 means "gave up".
        ssize_t read_chunk(int _fd, char* _buf, size_t _cap) {
            for (;;) {
                const ssize_t n = ::read(_fd, _buf, _cap);
                if (n >= 0) return n;
                if (errno != EINTR) return -1;
            }
        }
#    endif

    } // namespace

#    if ALXRUN_FS_READ_ENABLE
    // Raises instead of returning silently empty: for an agent, "empty" and "missing" must not look
    // alike -- the failure shape prelude.md §2.6 warns about.
    void fs_read(alx::script::fwrap& _fw) {
        const auto& path = as_str(_fw[0], "fs_read path");
        fd_guard g;
        g.fd = open_regular(path, "fs_read");

        // Stream to EOF, not "read st_size bytes": procfs reports 0 and still has content.
        std::string out;
        char buf[65536];
        for (;;) {
            const ssize_t n = read_chunk(g.fd, buf, sizeof(buf));
            if (n < 0) fail("fs_read", path, std::strerror(errno));
            if (n == 0) break;
            if (out.size() + static_cast<size_t>(n) > kMaxReadBytes)
                fail("fs_read", path,
                     "larger than " + std::to_string(kMaxReadBytes >> 20) + " MB");
            out.append(buf, static_cast<size_t>(n));
        }
        _fw.freturn(alx::variant(out));
    }

    void fs_sha256(alx::script::fwrap& _fw) {
        const auto& path = as_str(_fw[0], "fs_sha256 path");
        fd_guard g;
        g.fd = open_regular(path, "fs_sha256");

        std::unique_ptr<alx::verify> h(alx::verify::create("sha_256"));
        if (!h) fail("fs_sha256", path, "sha_256 unavailable");
        char buf[65536];
        for (;;) { // chunked, so nothing accumulates but the digest
            const ssize_t n = read_chunk(g.fd, buf, sizeof(buf));
            if (n < 0) fail("fs_sha256", path, std::strerror(errno));
            if (n == 0) break;
            h->update(reinterpret_cast<const alx::uint_8*>(buf), static_cast<alx::uint_64>(n));
        }
        _fw.freturn(alx::variant(h->hexdigest()));
    }

    // The lines that match, streamed: the file never lands in memory whole, so a log bigger than
    // the wall is still searchable. Same door and same rules as $fs_read. `limit` is the cap (0 or
    // absent = no cap, as in $io_read's len), `start` skips that many matches first -- with both,
    // paging reads a whole file. A CRLF ending is trimmed, so such a file greps like a POSIX one.
    void fs_grep(alx::script::fwrap& _fw) {
        const auto& path = as_str(_fw[0], "fs_grep path");
        const auto& pattern = as_str(_fw[1], "fs_grep pattern");
        const alx::int_64 want_at = _fw.size() > 2 ? as_int(_fw[2], "fs_grep limit") : 0;
        const alx::uint_64 want = want_at > 0 ? static_cast<alx::uint_64>(want_at) : 0;
        const alx::int_64 skip_at = _fw.size() > 3 ? as_int(_fw[3], "fs_grep start") : 0;
        const alx::uint_64 skip = skip_at < 0 ? 0 : static_cast<alx::uint_64>(skip_at);

        alx::regex_pcre2 re(pattern);
        if (!re.is_valid()) fail("fs_grep", path, "invalid regex pattern");
        re.set_step_limit(kStepLimit);
        re.set_callout(&still_wanted, nullptr);

        fd_guard g;
        g.fd = open_regular(path, "fs_grep");

        alx::varvec out;
        alx::uint_64 seen = 0; // matches so far, before the ones we keep
        std::string line;
        char buf[65536];
        for (;;) {
            const ssize_t n = read_chunk(g.fd, buf, sizeof(buf));
            if (n < 0) fail("fs_grep", path, std::strerror(errno));
            if (n == 0) break;
            const char* at = buf;
            const char* end = buf + n;
            while (at < end) {
                const void* nl = std::memchr(at, '\n', static_cast<size_t>(end - at));
                if (!nl) {
                    line.append(at, static_cast<size_t>(end - at));
                    break;
                }
                line.append(at, static_cast<size_t>(static_cast<const char*>(nl) - at));
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (hits(re, line, "fs_grep")) {
                    if (seen++ >= skip) out.push_back(alx::variant(line));
                }
                line.clear();
                at = static_cast<const char*>(nl) + 1;
                if (want && out.size() >= want) {
                    _fw.freturn(alx::variant(out));
                    return; // the cap is reached: the rest of the file is not read
                }
            }
        }
        if (!line.empty()) {
            if (line.back() == '\r') line.pop_back();
            if (hits(re, line, "fs_grep") && seen++ >= skip && (!want || out.size() < want))
                out.push_back(alx::variant(line));
        }
        _fw.freturn(alx::variant(out));
    }

    // The stream opens: the same door, then the handle carries the file until the script drops it --
    // a slice of a file bigger than the memory wall, and a product written in pieces.
    void fs_istream(alx::script::fwrap& _fw) {
        const auto& path = as_str(_fw[0], "fs_istream path");
        _fw.freturn(alx::variant(read_handle(path, "fs_istream")));
    }
#    endif

#    if ALXRUN_FS_WRITE_ENABLE
    // One whole file, created or replaced: no append, no remove, no bytes; a failure part-way leaves
    // what was written (not staging a copy is what keeps it out of memory).
    void fs_write(alx::script::fwrap& _fw) {
        const auto& path = as_str(_fw[0], "fs_write path");
        const auto& text = as_str(_fw[1], "fs_write text");
        const std::string target = write_target(path, "fs_write");
        const int fd = ::open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
        if (fd < 0) {
            if (errno == ELOOP) fail("fs_write", path, "is a symbolic link");
            fail("fs_write", path, std::strerror(errno));
        }
        fd_guard g;
        g.fd = fd;
        size_t off = 0;
        while (off < text.size()) {
            const ssize_t n = ::write(fd, text.data() + off, text.size() - off);
            if (n < 0) {
                if (errno == EINTR) continue;
                fail("fs_write", path, std::strerror(errno));
            }
            off += static_cast<size_t>(n);
        }
        _fw.freturn(alx::variant(static_cast<alx::int_64>(text.size())));
    }

    void fs_ostream(alx::script::fwrap& _fw) {
        const auto& path = as_str(_fw[0], "fs_ostream path");
        _fw.freturn(alx::variant(write_handle(path, "fs_ostream", false)));
    }

    void fs_append(alx::script::fwrap& _fw) {
        const auto& path = as_str(_fw[0], "fs_append path");
        _fw.freturn(alx::variant(write_handle(path, "fs_append", true)));
    }
#    endif

    // The form table (capability.h); ex lines are asserted by test/run.sh.
    const form s_fs[] = {
#    if ALXRUN_FS_READ_ENABLE
        {"fs_read(path: string): string", R"EX(return $fs_read("test/fixture.txt"); => "hello")EX", fs_read, "io_fread"},
        {"fs_read(path: string): string", R"EX(return $fs_read("test"); => !is a directory)EX", fs_read, "io_fread"},
        {"fs_read(path: string): string", R"EX(return $fs_read("test/nope-missing.txt"); => !no such file)EX", fs_read, "io_fread"},
        {"fs_read(path: string): string", R"EX(return $fs_read("/etc/passwd"); => !outside the allowed read roots)EX", fs_read, "io_fread"},
        {"fs_read(path: string): string", R"EX(return $fs_read(".git/config"); => !denied by the host rules)EX", fs_read, "io_fread"},
        {"fs_read(path: string): string", R"EX(return $fs_read(".git/config"); => !denied by the host rules (/\.git/)EX", fs_read, "io_fread"},
        {"fs_sha256(path: string): string", R"EX(return $fs_sha256("test/fixture.txt"); => "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824")EX", fs_sha256, "hash_file"},
        {"fs_sha256(path: string): string", R"EX(return $fs_sha256("/etc/passwd"); => !outside the allowed read roots)EX", fs_sha256, "hash_file"},
        {"fs_grep(path: string, pattern: string, limit?: int, start?: int): vec", R"EX(return $fs_grep("test/fixture_lines.txt", "beta"); => ["beta 1","beta 2","beta 3"])EX", fs_grep, "-"},
        {"fs_grep(path: string, pattern: string, limit?: int, start?: int): vec", R"EX(return $fs_grep("test/fixture_lines.txt", "zzz"); => [])EX", fs_grep, "-"},
        {"fs_grep(path: string, pattern: string, limit?: int, start?: int): vec", R"EX(return $fs_grep("test/fixture_lines.txt", "beta", 2); => ["beta 1","beta 2"])EX", fs_grep, "-"},
        {"fs_grep(path: string, pattern: string, limit?: int, start?: int): vec", R"EX(return $fs_grep("test/fixture_lines.txt", "beta", 2, 1); => ["beta 2","beta 3"])EX", fs_grep, "-"},
        {"fs_istream(path: string): handle", R"EX(return $io_read($fs_istream("test/fixture.txt")); => "hello")EX", fs_istream, "fs_open"},
        {"fs_istream(path: string): handle", R"EX(return $io_read($fs_istream("test/fixture.txt"), 1, 3); => "ell")EX", fs_istream, "fs_open"},
        {"fs_istream(path: string): handle", R"EX(return $fs_istream("/etc/passwd"); => !outside the allowed read roots)EX", fs_istream, "fs_open"},
        {"fs_istream(path: string): handle", R"EX(var a = $fs_istream("test/fixture.txt"); var b = a; delete a; return $io_read(b, 1, 3); => "ell")EX", fs_istream, "fs_open"},
#    endif
#    if ALXRUN_FS_WRITE_ENABLE
        {"fs_write(path: string, text: string): int", R"EX(return $fs_write("bin/.fs_write_probe", "hi"); => 2)EX", fs_write, "io_fwrite"},
        {"fs_write(path: string, text: string): int", R"EX(return $fs_write("/etc/alxrun-probe", "hi"); => !outside the allowed write roots)EX", fs_write, "io_fwrite"},
        {"fs_write(path: string, text: string): int", R"EX(return $fs_write(".env", "x"); => !denied by the host rules)EX", fs_write, "io_fwrite"},
#        if ALXRUN_FS_READ_ENABLE
        {"fs_ostream(path: string): handle", R"EX(var h = $fs_ostream("bin/.fs_stream_probe"); $io_write(h, "hi"); return $fs_read("bin/.fs_stream_probe"); => "hi")EX", fs_ostream, "fs_open"},
#        else
        {"fs_ostream(path: string): handle", R"EX(return $io_write($fs_ostream("bin/.fs_stream_probe"), "hi"); => 2)EX", fs_ostream, "fs_open"},
#        endif
        {"fs_ostream(path: string): handle", R"EX(return $fs_ostream("/etc/alxrun-probe"); => !outside the allowed write roots)EX", fs_ostream, "fs_open"},
#        if ALXRUN_FS_READ_ENABLE
        {"fs_append(path: string): handle", R"EX(var h = $fs_ostream("bin/.fs_append_probe"); $io_write(h, "a"); delete h; var g = $fs_append("bin/.fs_append_probe"); $io_write(g, "b"); return $fs_read("bin/.fs_append_probe"); => "ab")EX", fs_append, "fs_open"},
#        else
        {"fs_append(path: string): handle", R"EX(return $io_write($fs_append("bin/.fs_append_probe"), "b"); => 1)EX", fs_append, "fs_open"},
#        endif
#    endif
    };

    // The resident line: the rules are compiled in, and the roots exist only at run time.
    std::string fs_rules() {
        std::string read, write;
#    if ALXRUN_FS_READ_ENABLE
        read = join(read_rules().roots, ", ") + " · deny " + join(read_rules().deny_text, " ");
#    endif
#    if ALXRUN_FS_WRITE_ENABLE
        write = join(write_rules().roots, ", ") + " · deny " + join(write_rules().deny_text, " ");
#    endif
        if (read.empty()) return "fs rules: write " + write + "\n";
        if (write.empty()) return "fs rules: read " + read + "\n";
        if (read == write) return "fs rules: read+write " + read + "\n";
        return "fs rules: read " + read + " · write " + write + "\n";
    }

    const table& fs_table() {
        static const table t = {"fs", s_fs, sizeof(s_fs) / sizeof(s_fs[0]), clan_file};
        return t;
    }

} // namespace capability

#endif // ALXRUN_FS_READ_ENABLE || ALXRUN_FS_WRITE_ENABLE
