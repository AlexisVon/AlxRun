// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "io.h"

#include "ajson.h"
#include "astream.h"

#include "config.h" // the module switches (src/config.h)

#if ALXRUN_IO_ENABLE || ALXRUN_FS_READ_ENABLE || ALXRUN_FS_WRITE_ENABLE

namespace capability {

#    if ALXRUN_IO_ENABLE
    // Strings verbatim, everything else as compact JSON: the engine's own display() gives "?".
    static std::string print_value(const alx::variant& _v) {
        if (_v.is<std::string>()) return _v.to<std::string>();
        return alx::json_doc::to_string(alx::json_value::from_variant(_v), true);
    }

    // Concatenates (no separator, no format string -- formatting is `string(fmt, ...)`), appends a
    // newline, then hands the text to the host channel.
    void io_print(alx::script::fwrap& _fw) {
        std::string out;
        for (size_t i = 0; i < _fw.size(); ++i)
            out += print_value(_fw[i]);
        out += "\n";

        const alx::script::engine_config& cfg = _fw.config();
        if (cfg.pipe_out_ptr) cfg.pipe_out_ptr(&out, cfg.pipe_out_ud);
    }
#    endif

#    if ALXRUN_FS_READ_ENABLE
    // (ofst = 0, len = the rest; len 0 is an empty read, not a failure). A short read at EOF is
    // empty, not an error, but nothing coming back where the file has bytes is named. The stream's
    // size is st_size, which procfs reports as 0 while the content is there -- so a zero-size
    // handle is read window by window to EOF, exactly as $fs_read does it.
    void io_read(alx::script::fwrap& _fw) {
        alx::istream* s = as_handle<alx::istream>(_fw[0], "io_read handle");
        const alx::int_64 ofst = _fw.size() > 1 ? as_int(_fw[1], "io_read ofst") : 0;
        const alx::int_64 asked = _fw.size() > 2 ? as_int(_fw[2], "io_read len") : -1; // -1 = the rest
        if (asked == 0) {
            _fw.freturn(alx::variant(std::string()));
            return;
        }
        const alx::uint_64 from = ofst < 0 ? 0 : static_cast<alx::uint_64>(ofst);
        const alx::uint_64 total = s->total();
        if (total > 0) { // a regular file: its size is the truth, one window does it
            if (from >= total) {
                _fw.freturn(alx::variant(std::string()));
                return;
            }
            alx::uint_64 len = total - from;
            if (asked > 0 && static_cast<alx::uint_64>(asked) < len) len = static_cast<alx::uint_64>(asked);
            const alx::bytes_view bv = s->get(from, len);
            if (bv.size() == 0) fail("io_read", "handle", "read returned nothing");
            _fw.freturn(alx::variant(std::string(reinterpret_cast<const char*>(bv.data()), bv.size())));
            return;
        }
        constexpr alx::uint_64 kChunk = 65536;
        std::string out;
        alx::uint_64 at = from;
        while (out.size() < kMaxReadBytes) {
            alx::uint_64 want = kChunk;
            if (asked > 0)
                want = static_cast<alx::uint_64>(asked) - out.size() < want
                           ? static_cast<alx::uint_64>(asked) - out.size()
                           : want;
            const alx::bytes_view bv = s->get(at, want);
            if (bv.size() == 0) break;
            out.append(reinterpret_cast<const char*>(bv.data()), bv.size());
            at += bv.size();
            if (asked > 0 && out.size() >= static_cast<size_t>(asked)) break;
        }
        _fw.freturn(alx::variant(out));
    }
#    endif

#    if ALXRUN_FS_WRITE_ENABLE
    // Append and flush; the stream's false becomes a named failure, not a quiet one.
    void io_write(alx::script::fwrap& _fw) {
        alx::ostream* s = as_handle<alx::ostream>(_fw[0], "io_write handle");
        const auto& text = as_str(_fw[1], "io_write text");
        if (!s->append(text.data(), text.size()) || !s->flush())
            fail("io_write", "handle", "write failed");
        _fw.freturn(alx::variant(static_cast<alx::int_64>(text.size())));
    }
#    endif

    // The form table (capability.h); ex lines are asserted by test/run.sh.
    const form s_io[] = {
#    if ALXRUN_IO_ENABLE
        {"io_print(v: any, ...)", R"EX($io_print("n=", 3); => n=3)EX", io_print, ""},
#    endif
#    if ALXRUN_FS_READ_ENABLE
        {"io_read(h: handle, ofst?: int, len?: int): string", R"EX(return $io_read($fs_istream("test/fixture.txt"), 5); => "")EX", io_read, "", clan_file},
        {"io_read(h: handle, ofst?: int, len?: int): string", R"EX(return $io_read($fs_istream("test/fixture.txt"), 0, 0); => "")EX", io_read, "", clan_file},
        {"io_read(h: handle, ofst?: int, len?: int): string", R"EX(return $io_read("no", 0); => !expected a handle)EX", io_read, "", clan_file},
#    endif
#    if ALXRUN_FS_WRITE_ENABLE
#        if ALXRUN_FS_READ_ENABLE
        {"io_write(h: handle, text: string): int", R"EX(return $io_write($fs_istream("test/fixture.txt"), "x"); => !handle type mismatch)EX", io_write, "", clan_file},
#        else
        {"io_write(h: handle, text: string): int", R"EX(return $io_write("no", "x"); => !expected a handle)EX", io_write, "", clan_file},
#        endif
        {"io_write(h: handle, text: string): int", R"EX(return $io_write($fs_ostream("bin/.fs_stream_probe"), "!"); => 1)EX", io_write, "", clan_file},
#    endif
    };

    const table& io_table() {
        static const table t = {"io", s_io, sizeof(s_io) / sizeof(s_io[0])};
        return t;
    }

} // namespace capability

#endif // ALXRUN_IO_ENABLE || ALXRUN_FS_READ_ENABLE || ALXRUN_FS_WRITE_ENABLE
