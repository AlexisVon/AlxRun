// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

/// The MCP face's process boundary (doc/design.md §4): every request runs in its own child, so
/// nothing the engine does takes the session down. Deliberately dumb -- no JSON, no variants, no
/// engine; it owns the channel, the clock, the verdict and the block.
#include "mcp.h"

#include "alogger.h"
#include "aplatform.h"
#include "block.h"
#include "config.h"
#include "kernel.h"

#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include <list>
#include <memory>

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <poll.h>
#    include <unistd.h>
#endif

namespace mcp {
    namespace {

        // Past the child's own window, this much before the parent kills it: the stop-and-commit
        // path is bounded by the block, so the kill only backs up a blocked syscall.
        constexpr uint32_t kReapGraceMs = 1000;
        constexpr size_t kMaxChildren = config::kMaxChildren; // in flight at once
        static_assert(kMaxChildren >= 1, "config::kMaxChildren must be at least 1");
        constexpr size_t kErrKeep = 4096; // of the child's stderr, kept for the verdict
        constexpr const char* kAck = "#ack "; // the child's opening stderr line carries the id

        unsigned self_pid() {
#if defined(_WIN32)
            return static_cast<unsigned>(GetCurrentProcessId());
#else
            return static_cast<unsigned>(::getpid());
#endif
        }

        // The supervisor spawns its own binary; find_process answers "where am I" on both
        // platforms (checked in test/platform).
        std::string self_path() {
            const std::vector<alx::process_info> me = alx::process_info::find_process(self_pid());
            return me.empty() ? std::string() : me.front().path;
        }

        // POSIX reports it as a field; Windows only as an exception exit code.
        bool died_by_signal(const alx::process_ctrl::wait_status& _st) {
#ifndef _WIN32
            return _st.signaled;
#else
            return _st.exit_code < 0; // 0xC0000000 and up, as a signed int
#endif
        }

        void write_line(const std::string& _text) {
            std::cout << _text << "\n";
            std::cout.flush();
        }

        // A protocol error: this message itself does not hold (an unknown method, a line that
        // could not be read). The run never entered the picture.
        void error_reply(const std::string& _id_json, const std::string& _message,
                         long long _code = -32603) {
            alx::json_object err;
            err.insert("code", alx::json_value(_code));
            err.insert("message", alx::json_value(_message));
            write_line("{\"jsonrpc\":\"2.0\",\"id\":" + _id_json + ",\"error\":" +
                       alx::json_doc::to_json(err, true) + "}");
        }

        // "The run did not produce a result" (design.md §4): a tool result with isError, not a
        // protocol error, so the model reads it as that call's own failure and can correct itself.
        // The text goes through the JSON writer, which escapes it; the id is spliced as in
        // error_reply (no parsing on this side).
        void tool_error_reply(const std::string& _id_json, const std::string& _text,
                              const std::string& _out = std::string()) {
            alx::json_object item;
            item.insert("type", alx::json_value("text"));
            item.insert("text", alx::json_value(_text));
            alx::json_array content;
            content.append(alx::json_value(item));
            alx::json_object res;
            res.insert("content", alx::json_value(content));
            // The run's output, in the field and the escaping a successful reply uses -- so a run
            // that died is read the same way as one that answered.
            if (!_out.empty()) {
                alx::json_object sc;
                sc.insert("out", alx::json_value(_out));
                res.insert("structuredContent", alx::json_value(sc));
            }
            res.insert("isError", alx::json_value(true));
            write_line("{\"jsonrpc\":\"2.0\",\"id\":" + _id_json + ",\"result\":" +
                       alx::json_doc::to_json(res, true) + "}");
        }

        // What the ring held when the child died, less any leading half character: the ring drops
        // bytes off its front without knowing about encoding, and the JSON writer passes raw bytes
        // through, so half a code point would make the whole reply invalid.
        std::string excerpt(const std::string& _frag) {
            size_t from = 0;
            while (from < _frag.size() && (static_cast<unsigned char>(_frag[from]) & 0xC0) == 0x80)
                ++from; // a continuation byte: its character starts earlier
            return _frag.substr(from);
        }

        // The JSON scalar that follows the given key, verbatim ("" when there is none). The
        // supervisor parses no JSON; this one shape is all it needs, and it is deliberately
        // forgiving: the first matching key wins, anything unreadable means "".
        std::string scalar_after(const std::string& _line, const char* _key) {
            const size_t key = _line.find(_key);
            if (key == std::string::npos) return std::string();
            const size_t colon = _line.find(':', key + std::strlen(_key));
            if (colon == std::string::npos) return "null";
            size_t i = colon + 1;
            while (i < _line.size() && std::isspace(static_cast<unsigned char>(_line[i]))) ++i;
            if (i >= _line.size()) return std::string();
            if (_line[i] == '"') { // a string scalar: up to the closing quote, escapes skipped
                size_t j = i + 1;
                while (j < _line.size() && _line[j] != '"') j += _line[j] == '\\' ? 2 : 1;
                return j < _line.size() ? _line.substr(i, j - i + 1) : std::string();
            }
            if (_line.compare(i, 4, "null") == 0) return std::string();
            size_t j = i;
            while (j < _line.size() &&
                   (std::isdigit(static_cast<unsigned char>(_line[j])) || _line[j] == '-' ||
                    _line[j] == '+' || _line[j] == '.' || _line[j] == 'e' || _line[j] == 'E'))
                ++j;
            return j == i ? std::string() : _line.substr(i, j - i);
        }

        // The fallback when a child died before it could ack: read the id off the request itself.
        // Cold path, and "null" when it cannot be read.
        std::string id_from_request(const std::string& _line) {
            const std::string id = scalar_after(_line, "\"id\"");
            return id.empty() ? "null" : id;
        }

        // The requestId of a `notifications/cancelled`, or "" for anything else. The parent must
        // recognise this one *itself*: spawning a child for it would burn a process to do
        // nothing, and the point is to stop a child that is already running.
        std::string cancel_target(const std::string& _line) {
            if (_line.find("notifications/cancelled") == std::string::npos) return std::string();
            return scalar_after(_line, "\"requestId\"");
        }

        std::string last_line(const std::string& _s) {
            const size_t end = _s.find_last_not_of("\n\r \t");
            if (end == std::string::npos) return std::string();
            const size_t start = _s.find_last_of('\n', end);
            std::string out = _s.substr(start == std::string::npos ? 0 : start + 1, end - start);
            if (out.size() > 200) out = out.substr(0, 200) + "...";
            return out;
        }

        // The request's time window in ms (an empty scalar = it asked for nothing). Read from the
        // arguments key on, so a `timeout` inside the script cannot shadow the real one; whole
        // seconds only, by hand -- no parser runs on this side, and nothing here throws.
        bool window_from(const std::string& _line, uint32_t& _ms, std::string& _why) {
            const size_t at = _line.find("\"arguments\"");
            const std::string scalar =
                at == std::string::npos ? std::string() : scalar_after(_line.substr(at), "\"timeout\"");
            if (scalar.empty()) {
                _ms = static_cast<uint32_t>(kernel::kDefaultSeconds * 1000);
                return true;
            }
            uint32_t want = 0;
            if (!kernel::parse_wall_seconds(scalar, want)) {
                _why = "timeout must be " + std::to_string(static_cast<int>(kernel::kMinSeconds)) +
                       " to " + std::to_string(config::kMaxSeconds) +
                       " whole seconds";
                return false;
            }
            _ms = want * 1000;
            return true;
        }

        // What a dead, silent child means. The evidence decides, never the wish: a crash that is
        // not memory must not be laundered as one (doc/issue.md I-4).
        std::string classify(bool _killed, const alx::process_ctrl::wait_status& _st,
                             const std::string& _err, uint32_t _wall_ms) {
            if (_killed)
                return "time budget exceeded — the engine did not stop itself; its " +
                       std::to_string(_wall_ms / 1000) + " s window was used up";
            if (_err.find("memory budget exceeded") != std::string::npos ||
                _err.find("out of memory") != std::string::npos)
                return "memory budget exceeded — the engine hit its memory allowance";
            std::string out = "engine died";
            if (died_by_signal(_st)) {
#ifndef _WIN32
                out += " (signal " + std::to_string(_st.term_sig) + ")";
#endif
            } else {
                out += " (exit code " + std::to_string(_st.exit_code) + ")";
            }
            const std::string tail = last_line(_err);
            if (!tail.empty()) out += " — " + tail;
            return out;
        }

        // Bytes read past the last newline. read_line fills it, stdin_ready consults it: one
        // read() usually brings several lines, and the rest must be visible to both.
        static std::string s_carry;

        // One line off stdin, never more than the door in memory, and never through iostream: a
        // line already sitting in the stream's own buffer is invisible to poll() on the
        // descriptor, and a blocking poll on it then never returns (measured: the loop stalled
        // with a request in hand). One carry buffer, one read() loop, no hidden state.
        bool read_line(std::string& _line, bool& _overflow) {
            std::string& carry = s_carry;
            _line.clear();
            _overflow = false;
            for (;;) {
                const size_t nl = carry.find('\n');
                if (nl != std::string::npos) {
                    if (!_overflow) _line.assign(carry, 0, nl);
                    carry.erase(0, nl + 1);
                    return true;
                }
                if (carry.size() > block::kMaxPayload) {
                    _overflow = true; // past the door: keep consuming, stop remembering
                    if (carry.size() > 4096) carry.erase(0, carry.size() - 4096);
                }
                char buf[64 * 1024];
                const ssize_t n = ::read(0, buf, sizeof(buf));
                if (n > 0) {
                    carry.append(buf, static_cast<size_t>(n));
                    continue;
                }
                if (n == 0) return false; // the client is gone
                if (errno != EINTR) return false;
            }
        }

        // The child's stderr: the ack (which request it took) and its last words. It arrives on
        // the lib's loop thread; the verdict is read on ours.
        struct err_sink {
            std::mutex mx;
            std::string ack; // the id we may have to answer for, verbatim JSON
            std::string err;

            void on_err(const std::string& _block) {
                std::lock_guard<std::mutex> lk(mx);
                err += _block;
                if (err.size() > kErrKeep) err.erase(0, err.size() - kErrKeep);
                if (ack.empty() && err.compare(0, 5, kAck) == 0) {
                    const size_t nl = err.find('\n');
                    if (nl != std::string::npos) {
                        ack = err.substr(5, nl - 5);
                        err.erase(0, nl + 1); // the ack is ours, not the child's last words
                    }
                }
            }
        };

        // Is there a request waiting? Blocking when nothing is in flight (a quiet session costs
        // zero wakeups), a short timeout otherwise, so in-flight children keep being harvested
        // while we wait for the client.
        bool stdin_ready(bool _block) {
            // A whole line may already be in the carry (one read() usually brings several): the
            // descriptor then has nothing to say and a blocking poll on it would never return
            // (measured: the loop stalled with a request in hand). Ask the buffer first.
            if (s_carry.find('\n') != std::string::npos) return true;
#if defined(_WIN32)
            const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
            return WaitForSingleObject(in, _block ? INFINITE : 10) == WAIT_OBJECT_0;
#else
            struct pollfd p{};
            p.fd = 0; // stdin
            p.events = POLLIN;
            return ::poll(&p, 1, _block ? -1 : 10) > 0;
#endif
        }

        // The parent's half of one execution (design.md §4); the block is all that crosses. The
        // child's own wall stops it first, kill_at is the backstop. Declaration order matters at
        // teardown: the child dies first and joins its thread, so sink cannot outlive it.
        struct task {
            task(const std::string& _name, const std::string& _self)
                : name(_name), shm(_name), child(_self) {}

            std::string name; // <session pid>-<seq>: the block's name and the child's argument
            std::string line;
            uint32_t wall_ms = 0; // the window granted to this run (block.h)
            std::chrono::steady_clock::time_point kill_at; // past the child's own wall on purpose
            alx::mmap shm;
            err_sink sink;
            alx::process_ctrl child;
            bool killed = false; // we ran out of time and stopped it
            bool cancelled = false; // the client asked us to stop it
        };

        // Make a block for this request, start its child, park it in the live list.
        void dispatch(std::list<std::unique_ptr<task>>& _live, uint64_t _seq,
                      const std::string& _self, const std::string& _line) {
            // The window first: a request asking for one outside the range gets a named failure
            // and never reaches a block (the range is the deployment's, design.md §2).
            uint32_t wall_ms = 0;
            std::string why;
            if (!window_from(_line, wall_ms, why)) {
                tool_error_reply(id_from_request(_line), why);
                return;
            }
            const std::string name = "alxrun-" + std::to_string(self_pid()) + "-" +
                                     std::to_string(_seq);
            auto req = std::make_unique<task>(name, _self);
            req->line = _line;
            req->shm.destroy(); // nothing should be there (the name is ours and seq is fresh)
            if (!req->shm.open(static_cast<alx::uint_32>(block::kBytes), true)) {
                tool_error_reply(id_from_request(_line), "alxrun: cannot make the engine block");
                return;
            }
            const uint32_t len = static_cast<uint32_t>(_line.size());
            // The whole header is zeroed: out_len 0 = nothing printed yet, reply_len 0 = no reply
            // yet (the child writes both as it goes); the reserved slots stay clean for the next
            // resource dimension (design.md §4).
            std::vector<char> head(block::kHeadBytes, 0);
            std::memcpy(&head[block::kReqOff], &len, sizeof(len));
            std::memcpy(&head[block::kWallOff], &wall_ms, sizeof(wall_ms)); // the granted window
            if (!req->shm.write(head.data(), 0, head.size()) ||
                !req->shm.write(_line.data(), block::kDataOff, len)) {
                tool_error_reply(id_from_request(_line),
                                 "alxrun: cannot hand the request to the engine block");
                return;
            }

            task* r = req.get();
            r->child.sig_errput.connect([r](const std::string& _b) { r->sink.on_err(_b); });
            // The parent owns the memory: the child gets the deployment's allowance as a hard cap
            // it cannot lift, and it dies with the session (doc/design.md §2, §4). The block is
            // scaffolding, not script memory (measured: attaching it costs the child a full 64 MB
            // of address space), so the grant is the wall *plus* it.
            alx::process_ctrl::process_limits limits;
            limits.mem_bytes = kernel::memory_limit_bytes() + block::kBytes;
            limits.kill_on_parent_exit = true;
            if (!r->child.start({"mcp-cli", r->name}, limits)) {
                tool_error_reply(id_from_request(_line), "alxrun: cannot start the engine process");
                return;
            }
            r->wall_ms = wall_ms;
            r->kill_at = std::chrono::steady_clock::now() +
                         std::chrono::milliseconds(wall_ms + kReapGraceMs);
            _live.push_back(std::move(req));
        }

        // Has this one finished? Answer it if so (and only then): the reply is read out of its
        // block, the block is destroyed, and the verdict is written to the client.
        bool settle(task& _r) {
            if (!_r.child.wait(0)) { // 0 = check only
                if (std::chrono::steady_clock::now() < _r.kill_at) return false;
                _r.killed = true;
                _r.child.kill();
            }
            const alx::process_ctrl::wait_status st = _r.child.status();

            // What the child left: reply_len non-zero means a reply (written last -- that write
            // *is* the commit), zero means the run never got there and only the ring's bytes are
            // real. The ring is read whole: the reply never wrote there.
            uint32_t out_len = 0, reply_len = 0, pos = 0, dropped = 0;
            const bool head = _r.shm.read(&out_len, block::kOutOff, sizeof(out_len)) &&
                              _r.shm.read(&reply_len, block::kReplyLenOff, sizeof(reply_len)) &&
                              _r.shm.read(&pos, block::kRingPosOff, sizeof(pos)) &&
                              _r.shm.read(&dropped, block::kDroppedOff, sizeof(dropped));
            const size_t logged = out_len < block::kRingBytes ? out_len : block::kRingBytes;
            std::string reply, fragment;
            if (head && reply_len > 0 && reply_len <= block::kMaxPayload) {
                __atomic_thread_fence(__ATOMIC_ACQUIRE); // the bytes before the count that vouches for them
                reply.resize(reply_len);
                // The reply begins where the log ends: the two regions are known to both halves.
                if (!_r.shm.read(&reply[0], block::kDataOff + logged, reply_len)) reply.clear();
            } else if (head && logged > 0) {
                __atomic_thread_fence(__ATOMIC_ACQUIRE); // the bytes before the count that vouches for them
                const size_t start = (pos + block::kRingBytes - logged) % block::kRingBytes;
                fragment.resize(logged);
                const size_t first =
                    block::kRingBytes - start < logged ? block::kRingBytes - start : logged;
                if (!_r.shm.read(&fragment[0], block::kDataOff + start, first)) fragment.clear();
                else if (first < logged &&
                         !_r.shm.read(&fragment[first], block::kDataOff, logged - first))
                    fragment.clear();
            }
            _r.shm.destroy(); // used and done: names and the companion lock leave with it

            std::string ack, err;
            {
                std::lock_guard<std::mutex> lk(_r.sink.mx);
                ack = _r.sink.ack;
                err = _r.sink.err;
            }
            if (!reply.empty()) { // the verdict is the child's own; relay it verbatim
                std::cout << reply;
                std::cout.flush();
                return true;
            }
            if (!_r.killed && st.exited && st.exit_code == 0 && !died_by_signal(st)) {
                // A notification: silence is the answer. Anything the child printed on its way
                // out is still worth seeing -- it is how a child that does nothing gets caught.
                if (!err.empty())
                    std::cerr << "alxrun: engine said nothing: " << last_line(err) << "\n";
                return true;
            }
            const std::string id = ack.empty() ? id_from_request(_r.line) : ack;
            if (_r.cancelled) { // the client asked for this one to stop; that is the verdict
                error_reply(id, "cancelled by the client", -32800);
                return true;
            }
            // The run did not produce a result: the verdict, and (when the child got as far as
            // printing) the tail of what it printed before it stopped.
            std::string verdict = classify(_r.killed, st, err, _r.wall_ms);
            std::string log = excerpt(fragment); // whole, less a leading half character
            if (!log.empty()) {
                // The same shape a successful reply gives it: the bytes ride in
                // structuredContent.out, the transcript gets a note past the inline size, and the
                // ellipsis shows only when the ring really did drop bytes off its front.
                verdict += "\nthe run's last output:\n";
                if (dropped) verdict += "…\n";
                verdict += log.size() <= kTextInline
                               ? log
                               : "[" + std::to_string(log.size()) +
                                     " bytes — see structuredContent.out]";
            }
            tool_error_reply(id, verdict, log);
            return true;
        }

        // Stop the request this line names, and only that one. False for anything that is not a
        // cancellation (the caller then treats the line as work).
        bool handle_cancel(std::list<std::unique_ptr<task>>& _live, const std::string& _line) {
            const std::string target = cancel_target(_line);
            if (target.empty()) return false;
            for (auto& r : _live) {
                std::string rid;
                {
                    std::lock_guard<std::mutex> lk(r->sink.mx);
                    rid = r->sink.ack; // what the child said it took
                }
                if (rid.empty()) rid = id_from_request(r->line);
                if (rid == target) {
                    r->cancelled = true;
                    r->child.kill();
                    break;
                }
            }
            return true; // a cancellation is the parent's to handle: it gets no child
        }

        // The library's own sink is stdout, which on this face is the protocol channel: this handle
        // drops engine and library logs alike, so nothing but replies reaches the wire.
        void drop_log(const std::string&, alx::log_level) {}

        // Take out everything that has finished.
        void harvest(std::list<std::unique_ptr<task>>& _live) {
            for (auto it = _live.begin(); it != _live.end();) {
                if (settle(**it)) it = _live.erase(it);
                else ++it;
            }
        }

    } // namespace

    int supervise() {
        alx::global_logger::instance()->set_log_handle(drop_log);
        const std::string self = self_path();
        if (self.empty()) { // no self, no children: refuse before the client leans on us
            error_reply("null", "alxrun: cannot resolve its own path");
            return 1;
        }
        // Up to kMaxChildren requests run at once, each with its own block (made for it, destroyed
        // when answered -- nothing sleeps in /dev/shm between requests). Replies leave as they
        // finish: JSON-RPC carries the id, so the client puts the order back together itself.
        std::list<std::unique_ptr<task>> live;
        uint64_t seq = 0;
        std::string line, waiting;
        bool overflow = false, has_waiting = false;
        for (;;) {
            harvest(live);
            if (has_waiting && live.size() < kMaxChildren) { // the queued one goes first
                dispatch(live, ++seq, self, waiting);
                waiting.clear();
                has_waiting = false;
                continue;
            }
            // At capacity a request waits here (one deep -- anything further stays in the pipe,
            // which is the client's own backpressure). stdin is polled either way: a cancellation
            // must never wait for a free slot, that is the whole point of cancelling.
            if (has_waiting) {
                alx::high_precision_sleep(5);
                continue;
            }
            if (!stdin_ready(live.empty())) continue; // timeout: harvest again
            if (!read_line(line, overflow)) break; // the client is gone
            if (overflow) { // the door: refused without ever holding the line (so no id to name)
                tool_error_reply("null", "request too large — one line may not exceed " +
                                             std::to_string(block::kMaxPayload >> 20) + " MB");
                continue;
            }
            if (line.empty()) continue;
            if (handle_cancel(live, line)) continue;
            if (live.size() >= kMaxChildren) {
                waiting = line;
                has_waiting = true;
                continue;
            }
            dispatch(live, ++seq, self, line);
        }
        // EOF: the client will send no more, but the requests it did send are still answers it is
        // owed (a "write the batch, close, read" client is legitimate, and so is a client that
        // closes while a run is finishing). Drain them, bounded by their own deadlines.
        while (!live.empty()) {
            harvest(live);
            if (!live.empty()) alx::high_precision_sleep(5);
        }
        return 0;
    }

} // namespace mcp
