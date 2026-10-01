// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "mcp.h"

#include "ajson.h"
#include "aplatform.h"
#include "astream.h"
#include "block.h"
#include "config.h"
#include "kernel.h"
#include "registry.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace mcp {

    namespace {

        constexpr const char* kVersion = "0.1.0";
        constexpr const char* kDescription = "AScript execution kernel";
        // One line may not exceed the payload the block carries: the only place the client's own
        // memory is bounded (doc/issue.md I-4). The supervisor checks the same number before it
        // even reads the line; this is the standalone face's own door.
        constexpr size_t kMaxRequest = block::kMaxPayload;
        // Handshake era only -- 2025-11-25 is the last revision that has one; the stateless
        // 2026-07-28 era is not implemented. First entry = what we answer for unknown requests.
        constexpr const char* kProtocols[] = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};

        bool protocol_ok(const std::string& _v) {
            for (const char* p : kProtocols)
                if (_v == p) return true;
            return false;
        }

        // send's destinations, one per face: stdout (the server), a string (in-process), the shared
        // block (under a supervisor -- there the payload never travels a pipe, and never lands in a
        // heap either).
        std::string* s_reply = nullptr;
        alx::ostream* s_stream = nullptr;
        void send_stream(alx::json_object&& _msg); // the block face's half, defined below
        // The named failure is being built; a second refusal must not recurse (that one commits
        // nothing), and the first cannot itself overflow at a few hundred bytes.
        static bool s_overflowed = false;

        // Where the script's output is read back from when the reply is built: result.output
        // (stdout face), or the block the child streamed it into (block face; oneshot installs it).
        std::string (*s_output_from)() = nullptr;
        // Bytes the ring let fall off (set by read_back): the reply says how much of the run's
        // output is not being shown, and that count belongs to the same story.
        static size_t s_out_dropped = 0;
        // The id of the request in hand: failures reported outside dispatch (a reply that cannot
        // fit the block) still have to name the call they belong to.
        alx::json_value s_last_id;
        // Fallbacks for the mutable accessors below: to(T&) hands back the caller's object on a
        // type mismatch (as<T>() would overwrite the request), and run() takes its inputs by
        // reference -- so each needs an lvalue to fall back on.
        alx::json_object s_no_object;
        alx::json_object s_no_data;
        std::string s_no_text;

        // The tree goes out as it is written: the peak is max(tree, text), not the sum.
        void send(alx::json_object&& _msg) {
            if (s_stream) {
                send_stream(std::move(_msg));
                return;
            }
            std::string line = alx::json_doc::to_json(std::move(_msg), true);
            if (s_reply) {
                if (s_reply->empty()) *s_reply = std::move(line); // one reply per child: no copy
                else s_reply->append(line);
                return;
            }
            std::cout << line << '\n'; // the block face adds the terminator at its far end instead
            std::cout.flush();
        }

        // The envelope; the result goes in by move -- this layer is where copies used to pile up
        // (measured: an assembly pass peaked at 16x its own output).
        void reply(const alx::json_value& _id, alx::json_object&& _result) {
            alx::json_object msg;
            msg["jsonrpc"] = alx::json_value("2.0");
            msg["id"] = _id;
            msg["result"] = alx::json_value(std::move(_result));
            send(std::move(msg));
        }

        void reply_error(const alx::json_value& _id, long long _code, const std::string& _text) {
            alx::json_object err;
            err.insert("code", alx::json_value(_code));
            err.insert("message", alx::json_value(_text));
            alx::json_object msg;
            msg.insert("jsonrpc", alx::json_value("2.0"));
            msg.insert("id", _id);
            msg.insert("error", alx::json_value(err));
            send(std::move(msg));
        }

        // Text plus an optional structured value (_value = nullptr omits it). Every hop moves: the
        // text can be tens of MB, and the tree holds it twice by the client's own shape (content
        // text and sc.out), so the copies here are the only ones left.
        void reply_tool(const alx::json_value& _id, std::string&& _text, alx::json_value* _value,
                        bool _is_error) {
            alx::json_object item;
            item["type"] = alx::json_value("text");
            item["text"] = alx::json_value(std::move(_text));
            alx::json_array content;
            content.append(alx::json_value(std::move(item)));

            alx::json_object res;
            res["content"] = alx::json_value(std::move(content));
            if (_value) {
                // structuredContent is an object by spec -- scalars get wrapped
                if (_value->is<alx::json_object>()) {
                    res["structuredContent"] = std::move(*_value);
                } else {
                    alx::json_object wrap;
                    wrap["result"] = std::move(*_value);
                    res["structuredContent"] = alx::json_value(std::move(wrap));
                }
            }
            res["isError"] = alx::json_value(_is_error);
            reply(_id, std::move(res));
        }

        // Says the output is not all here: only the ring's latest bytes ride out, so a tail must
        // never read as the whole of what the script printed.
        std::string marker(size_t _kept, size_t _printed) {
            return "…(output trimmed: showing the last " + std::to_string(_kept) + " of " +
                   std::to_string(_printed) + " bytes)\n";
        }

        // The transcript's channel for a returned value no output accompanied; past the inline size
        // it gets the note the output gets, for the same reason: the reply must not carry every
        // returned byte twice, and the value is read from structuredContent.
        std::string result_text(const alx::json_value& _value) {
            std::string rendered = alx::json_doc::to_string(_value, true);
            return rendered.size() <= kTextInline
                       ? std::move(rendered)
                       : "[result: " + std::to_string(rendered.size()) +
                             " bytes — see structuredContent.result]\n";
        }

        // The resident material: language demo + capability list. It rides in the tool description
        // because MCP hands that to the model for free; without it the model assumes JavaScript and
        // burns round trips (prelude.md §2.7).
        std::string run_description() {
            std::string out = "Run an AScript script and return its value. AScript is C-like.\n";
            // Saying the concurrency lets the model batch independent calls (they really do run
            // side by side) instead of paying a round trip each; it is compiled in, so it holds.
            out += "Up to " + std::to_string(config::kMaxChildren) +
                   " calls run at once; further ones queue.\n\n";
            out += registry::demo();
            // The notation rules cover the language surface the capability list does not
            // (design.md §6): without them the model avoids container writes as "unsure syntax"
            // and works around them (prelude.md §2.8).
            out += "\n\nCapabilities (call `capabilities` for signatures and examples):\n";
            out += registry::brief();
            return out;
        }

        alx::json_object capabilities_tool() {
            alx::json_object name;
            name.insert("type", alx::json_value("string"));
            name.insert("description", alx::json_value("A clan name, or any part of a key — e.g. \"notation\", \"str_split\", \"[null]\". Empty returns the clan index; \".\" returns everything."));

            alx::json_object props;
            props.insert("name", alx::json_value(name));
            alx::json_object schema;
            schema.insert("type", alx::json_value("object"));
            schema.insert("properties", alx::json_value(props));

            alx::json_object tool;
            tool.insert("name", alx::json_value("capabilities"));
            tool.insert("description",
                        alx::json_value("Capability reference: each key is a declaration "
                                        "\"name(p: type): ret\" or a language form (`def f(a, b) {}`), "
                                        "its value the runnable examples (\"code => result\"). "
                                        "Ask by clan (" +
                                        registry::clans_line() +
                                        "), by name/prefix, or by pattern — \".\" takes everything."));
            tool.insert("inputSchema", alx::json_value(schema));
            return tool;
        }

        alx::json_object run_tool() {
            alx::json_object code;
            code.insert("type", alx::json_value("string"));
            code.insert("description", alx::json_value("AScript source. The value the script returns is the answer."));

            alx::json_object data;
            data.insert("type", alx::json_value("object"));
            data.insert("description",
                        alx::json_value("Each key becomes a variable of that name, bound to its value — "
                                        "{\"csv\": \"a,b\"} binds `csv`, not `data`."));

            alx::json_object timeout;
            timeout.insert("type", alx::json_value("integer"));
            timeout.insert("description",
                           alx::json_value("Timeout in whole seconds, " +
                                           std::to_string(static_cast<int>(kernel::kMinSeconds)) + " to " +
                                           std::to_string(config::kMaxSeconds) +
                                           ". Default " +
                                           std::to_string(static_cast<int>(kernel::kDefaultSeconds)) +
                                           ". Ask for more when the job is long-running (large data, a "
                                           "big loop) instead of splitting it up."));

            alx::json_object props;
            props.insert("code", alx::json_value(code));
            props.insert("data", alx::json_value(data));
            props.insert("timeout", alx::json_value(timeout));

            alx::json_array required;
            required.append(alx::json_value("code"));

            alx::json_object schema;
            schema.insert("type", alx::json_value("object"));
            schema.insert("properties", alx::json_value(props));
            schema.insert("required", alx::json_value(required));

            alx::json_object tool;
            tool.insert("name", alx::json_value("run"));
            tool.insert("description", alx::json_value(run_description()));
            tool.insert("inputSchema", alx::json_value(schema));
            return tool;
        }

        void on_initialize(const alx::json_value& _id, const alx::json_object& _params) {
            std::string want = _params.value("protocolVersion").to_string();
            if (!protocol_ok(want)) want = kProtocols[0]; // unknown -> our latest, client decides

            alx::json_object tools;
            tools.insert("listChanged", alx::json_value(false));
            alx::json_object caps;
            caps.insert("tools", alx::json_value(tools));

            alx::json_object info;
            info.insert("name", alx::json_value("alxrun"));
            info.insert("version", alx::json_value(kVersion));
            info.insert("description", alx::json_value(kDescription));

            alx::json_object res;
            res.insert("protocolVersion", alx::json_value(want));
            res.insert("capabilities", alx::json_value(caps));
            res.insert("serverInfo", alx::json_value(info));
            reply(_id, std::move(res));
        }

        void on_tools_list(const alx::json_value& _id) {
            alx::json_array tools;
            tools.append(alx::json_value(run_tool()));
            tools.append(alx::json_value(capabilities_tool()));
            alx::json_object res;
            res.insert("tools", alx::json_value(tools));
            reply(_id, std::move(res));
        }

        void on_capabilities(const alx::json_value& _id, const alx::json_object& _params) {
            const std::string name = _params.value("name").to_string();
            const alx::json_object doc = name.empty() ? registry::index() : registry::document(name);
            alx::json_value value(doc);
            reply_tool(_id, alx::json_doc::to_string(value, false), &value, false);
        }

        void on_tools_call(const alx::json_value& _id, alx::json_object& _params) {
            const std::string name = _params.value("name").to_string();
            if (name == "capabilities") {
                on_capabilities(_id, _params.value("arguments").to_object());
                return;
            }
            if (name != "run") {
                reply_error(_id, -32602, "unknown tool: " + name);
                return;
            }
            // Fallbacks for the mutable accessors (to(T&) hands back the caller's lvalue on a type
            // mismatch, and run() takes its inputs by reference). Local on purpose: nothing here
            // outlives the call, so no state can leak between requests.
            alx::json_object no_object;
            std::string no_text;

            // Everything below is read where it lies: the code and the data can each be most of the
            // door's 64 MB, and a copy per hop used to cost a legal request several times its own
            // size before the run even started. One representation, one place (design.md §4).
            if (!_params.contain("arguments") || !_params.value("arguments").is<alx::json_object>()) {
                reply_tool(_id, std::string("run: arguments must be an object"), nullptr, true);
                return;
            }
            alx::json_object& args = _params["arguments"].to(no_object); // contained: checked above
            // Bad input is a tool execution error, not a protocol error (2025-11-25).
            std::string& code = args.contain("code") ? args["code"].to(no_text) : no_text;
            if (code.empty()) {
                reply_tool(_id, std::string("run: code must be a non-empty string"), nullptr, true);
                return;
            }
            if (args.contain("data") && !args.value("data").is<alx::json_object>()) {
                reply_tool(_id, std::string("run: data must be an object"), nullptr, true);
                return;
            }

            // The run takes both by reference and lets them go once it has its own copies (bytes,
            // root variables) -- the request tree's are dead weight from that moment on (design.md §4).
            alx::json_object& data = args.contain("data") ? args["data"].to(no_object) : no_object;
            kernel::result r = kernel::run(code, data);

            // text is the transcript's human channel; the model reads only structuredContent -- the
            // client drops content when that is present (measured: $io_print came back invisible).
            const bool is_error = r.error != alx::script::error_type::NoError;
            std::string out = s_output_from ? s_output_from() : r.output;
            const size_t printed = out.size() + s_out_dropped; // what the run printed in all

            // What comes out is the ring -- at most an eighth of the block, so the reply always
            // fits: nothing is sized, trimmed or retried. The one overflow left is a return value
            // too big for what the ring leaves, and send_stream() names it.
            if (s_out_dropped) out.insert(0, marker(out.size(), printed)); // in place: no second buffer
            // Past a point a second copy of the output is transport nobody reads (the model reads
            // structuredContent): the transcript's text gets a note instead -- one representation
            // per byte, one channel that matters (design.md §8).
            std::string text = out.size() <= kTextInline
                                   ? out
                                   : "[output: " + std::to_string(out.size()) +
                                         " bytes — see structuredContent.out]\n";
            alx::json_object sc;
            if (is_error) {
                const std::string kind(alx::script::error_type_name(r.error));
                if (!text.empty() && text.back() != '\n') text += "\n";
                text += kind + ": " + r.diag;
                sc["error"] = alx::json_value(kind);
                sc["msg"] = alx::json_value(r.diag);
            } else {
                if (text.empty() && r.has_value) text = result_text(r.value);
                sc["result"] = r.has_value ? std::move(r.value) : alx::json_value();
            }
            if (!out.empty()) sc["out"] = alx::json_value(std::move(out));
            alx::json_value sc_value(std::move(sc));
            reply_tool(_id, std::move(text), &sc_value, is_error);
        }

        void dispatch(const alx::json_object& _req) {
            const std::string method = _req.value("method").to_string();
            if (method.rfind("notifications/", 0) == 0) return; // no id, no reply
            if (!_req.contain("id")) return;

            const alx::json_value id = _req.value("id");
            s_last_id = id;
            alx::json_object params;
            if (_req.contain("params")) params = _req.value("params").to_object();

            if (method == "initialize") on_initialize(id, params);
            else if (method == "ping") reply(id, alx::json_object());
            else if (method == "tools/list") on_tools_list(id);
            else if (method == "tools/call") on_tools_call(id, params);
            else reply_error(id, -32601, "method not found: " + method);
        }

        // Tell the supervisor which request we took, so it can answer for us if we die before
        // writing a reply. On stderr: stdout stays a byte-exact protocol channel.
        void ack(const alx::json_object& _req) {
            if (!_req.contain("id")) return; // a notification: nobody is waiting
            std::cerr << "#ack " << alx::json_doc::to_string(_req.value("id"), true) << "\n";
            std::cerr.flush();
        }

        // Parse one request line and dispatch it; always answers, or stays silent for a
        // notification (no id to answer to). The line is handed back the moment it is parsed:
        // everything the request needs lives in the tree from then on, and the line can be most of
        // the door's 64 MB (one representation at a time, design.md §4).
        void handle_line(std::string& _line, bool _ack) {
            if (_line.size() > kMaxRequest) { // refuse before parsing: no tree, no copy
                reply_error(alx::json_value(), -32600, "request too large");
                return;
            }
            bool ok = false;
            const alx::json_value req = alx::json_doc::from_value(_line, &ok);
            std::string().swap(_line); // parsed: the text's copy has reached its end of life
            if (!ok) {
                reply_error(alx::json_value(), -32700, "parse error");
                return;
            }
            if (!req.is<alx::json_object>()) {
                reply_error(alx::json_value(), -32600, "invalid request");
                return;
            }
            const alx::json_object obj = req.to_object();
            if (_ack) ack(obj);
            dispatch(obj);
        }

        // ---- the block face: the child's output goes straight into the shared block ------------
        // Output that only lives in the child's heap is charged to the script's allowance and dies
        // with the child; in the block it is the parent's scaffolding (granted on top of the wall),
        // and a killed child leaves it behind (design.md §4).

        static alx::mmap* s_block = nullptr; // the block this child was started with, if any

        // The ring cursor and the totals behind the header fields.
        struct sink_state {
            alx::mmap* shm = nullptr;
            size_t pos = 0; // where the next byte goes in the ring
            size_t total = 0; // printed in all
        };

        // The header after the bytes it describes: the release fence is the point, a reader must
        // never see a count before the bytes it vouches for (ARM is a target, ordering is weak).
        void sink_publish(const sink_state& _s) {
            __atomic_thread_fence(__ATOMIC_RELEASE);
            const uint32_t len = static_cast<uint32_t>(_s.total < block::kRingBytes ? _s.total
                                                                                    : block::kRingBytes);
            const uint32_t pos = static_cast<uint32_t>(_s.pos);
            const uint32_t drop = static_cast<uint32_t>(_s.total - len);
            _s.shm->write(&len, block::kOutOff, sizeof(len));
            _s.shm->write(&pos, block::kRingPosOff, sizeof(pos));
            _s.shm->write(&drop, block::kDroppedOff, sizeof(drop));
        }

        // One printed chunk into the ring; nothing is refused -- the oldest bytes fall off, because
        // what a reader wants from a runaway printer is its latest output.
        void sink_write(void* _ud, const char* _data, size_t _size) {
            auto* s = static_cast<sink_state*>(_ud);
            if (!s || !s->shm || !_data || _size == 0) return;
            size_t off = 0, left = _size;
            s->total += _size; // what was printed, not what survived: the ring is smaller than a chunk
            if (left > block::kRingBytes) { // only the tail can survive one huge print
                off = left - block::kRingBytes;
                left = block::kRingBytes;
            }
            while (left > 0) {
                const size_t room = block::kRingBytes - s->pos;
                const size_t n = left < room ? left : room;
                if (!s->shm->write(_data + off, block::kDataOff + s->pos, n)) return;
                s->pos = (s->pos + n) % block::kRingBytes;
                off += n;
                left -= n;
            }
            sink_publish(*s);
        }

        // The ring in the order it was printed (oldest first): the assembly wants one contiguous
        // tail, and the wrap is this side's business.
        std::string read_back() {
            if (!s_block) return std::string();
            uint32_t len = 0, pos = 0, drop = 0;
            if (!s_block->read(&len, block::kOutOff, sizeof(len)) ||
                !s_block->read(&pos, block::kRingPosOff, sizeof(pos)) ||
                !s_block->read(&drop, block::kDroppedOff, sizeof(drop)))
                return std::string();
            s_out_dropped = drop;
            if (len == 0) return std::string();
            if (len > block::kRingBytes) len = static_cast<uint32_t>(block::kRingBytes);
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            const size_t start = (pos + block::kRingBytes - len) % block::kRingBytes;
            std::string out(len, '\0');
            const size_t first = block::kRingBytes - start < len ? block::kRingBytes - start : len;
            if (!s_block->read(&out[0], block::kDataOff + start, first)) return std::string();
            if (first < len && !s_block->read(&out[first], block::kDataOff, len - first))
                return std::string();
            return out;
        }

        // The ring as a reply carries it, marker in front when the front fell off, so a tail never
        // reads as the whole of what the script printed.
        std::string log_tail() {
            std::string out = read_back();
            if (s_out_dropped)
                out.insert(0, marker(out.size(), out.size() + s_out_dropped));
            return out;
        }

        // The reply's region (block.h), beginning where the log ends -- a script that printed
        // little lends its reply the ring it left empty. It refuses the byte it cannot hold
        // instead of growing: that refusal is how "does not fit" is heard before anything is
        // committed.
        class block_reply_sink : public alx::ostream {
        public:
            explicit block_reply_sink(alx::mmap* _shm)
                : shm_(_shm) {
                reset();
            }
            using alx::ostream::append; // the base's templates would otherwise be hidden
            virtual bool append(const void* _ptr, alx::uint_64 _size) override {
                // Zero size must succeed (the ostream contract): the serializer pushes an empty
                // run at every escape, and a false here would read as "no room".
                if (_size == 0) return true;
                if (n_ + _size > cap_) return false;
                if (!shm_->write(_ptr, base_ + n_, _size)) return false;
                n_ += _size;
                return true;
            }
            inline virtual bool flush() override { return true; }
            // To the first byte, which is where the log ends *now*: the log is only known once the
            // run has printed, and every reply starts from there.
            virtual void reset() override {
                uint32_t out = 0;
                if (!shm_->read(&out, block::kOutOff, sizeof(out))) out = 0;
                if (out > block::kRingBytes) out = static_cast<uint32_t>(block::kRingBytes);
                base_ = block::kDataOff + out;
                cap_ = block::kBytes - base_ - 1; // the commit appends the line terminator
                n_ = 0;
            }
            inline virtual alx::uint_64 total() const override { return n_; }
            inline size_t base() const { return base_; }

        private:
            alx::mmap* shm_ = nullptr;
            size_t base_ = block::kDataOff;
            size_t cap_ = 0;
            size_t n_ = 0;
        };

        // The block face's send: the reply is written as the serializer walks the tree (no text
        // ever exists in a heap). A reply the block cannot hold is refused mid-write, and nothing
        // is committed until reply_len goes down -- the ring stays intact.
        void send_stream(alx::json_object&& _msg) {
            s_stream->reset();
            if (alx::json_doc::to_json(std::move(_msg), *s_stream, true)) return;
            if (s_overflowed) return; // the failure itself did not fit: commit nothing
            s_overflowed = true;
            const std::string why = "the reply does not fit the block: " +
                                    std::to_string(s_stream->total()) + " bytes written, more to come";
            s_stream->reset();
            std::string out = log_tail();
            alx::json_object sc;
            if (!out.empty()) sc["out"] = alx::json_value(std::move(out));
            alx::json_value scv(std::move(sc));
            reply_tool(s_last_id, std::string(why), &scv, true);
            s_overflowed = false;
        }

    } // namespace

    int serve() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            handle_line(line, false); // no supervisor above us: nobody to ack to
        }
        return 0;
    }

    int oneshot(const std::string& _block_name) {
        if (_block_name.empty()) { // no block: the debug shape, straight off stdin
            std::string line;
            if (!std::getline(std::cin, line) || line.empty()) return 0;
            handle_line(line, true);
            return 0;
        }
        // A parent filled this block and owns the memory (doc/design.md §2): we run under its wall,
        // and do not impose our own on top of it.
        kernel::set_own_wall(false);
        // The request is already in the block the supervisor filled before starting us: read it
        // out, then that area takes the script's output as it is printed, then the reply overwrites
        // both (doc/design.md §4).
        alx::mmap shm(_block_name);
        if (!shm.open(static_cast<alx::uint_32>(block::kBytes), false)) return 0;
        uint32_t len = 0;
        if (!shm.read(&len, block::kReqOff, sizeof(len)) || len > block::kMaxPayload) return 0;
        std::string line(len, '\0');
        if (!shm.read(&line[0], block::kDataOff, len)) return 0;
        // The window the parent granted (block.h): one number, so the watchdog here and the kill
        // clock above it cannot disagree about when the run is over.
        uint32_t wall_ms = 0;
        if (!shm.read(&wall_ms, block::kWallOff, sizeof(wall_ms)) || wall_ms == 0 ||
            wall_ms > static_cast<uint32_t>(config::kMaxSeconds) * 1000)
            return 0; // no agreement on the window, no run
        kernel::set_wall_seconds(wall_ms / 1000.0);

        s_block = &shm;
        sink_state sink;
        sink.shm = &shm;
        kernel::set_output(&sink, sink_write);
        s_output_from = read_back;

        block_reply_sink sink_reply(&shm);
        s_stream = &sink_reply;
        handle_line(line, true);

        kernel::set_output(nullptr, nullptr);
        s_output_from = nullptr;
        s_out_dropped = 0;
        s_block = nullptr;
        s_stream = nullptr;

        // The bytes are already in place -- the sink wrote them as they were serialized. What is
        // left is the terminator and the length that commits them, written last: a child that dies
        // mid-write leaves the parent a fragment, never half a reply.
        const size_t n = sink_reply.total();
        if (n == 0) return 0; // a notification: silence is the answer, so nothing is committed
        if (!shm.write("\n", sink_reply.base() + n, 1)) return 0;
        const uint32_t rlen = static_cast<uint32_t>(n + 1);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        if (!shm.write(&rlen, block::kReplyLenOff, sizeof(rlen))) return 0;
        return 0;
    }

} // namespace mcp
