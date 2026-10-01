// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "kernel.h"

#include "registry.h"
#include "stop.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <new>
#include <thread>
#include <sys/resource.h>

namespace kernel {

    // One engine per process -- MCP serves one request at a time, so no locking.
    static alx::script::engine* s_engine = nullptr;
    static std::string* s_out = nullptr;
    static std::string* s_diag = nullptr;
    static bool s_debug = true;
    static bool s_own_wall = true; // set_own_wall: the MCP child runs under its parent's wall
    static void* s_out_ud = nullptr;
    static out_fn s_out_fn = nullptr;

    static void on_out(const std::string* _out, void* _ud) {
        if (!_out) return;
        if (s_out_fn) {
            s_out_fn(s_out_ud, _out->data(), _out->size());
            return;
        }
        if (s_out) s_out->append(*_out);
    }

    void set_output(void* _ud, out_fn _fn) {
        s_out_ud = _ud;
        s_out_fn = _fn;
    }

    static constexpr long kMaxRssKb = config::kMemMb * 1024L; // 0 = no cap (fuzz build)
    static constexpr alx::uint_64 kMaxVecfill = 10000000; // fill-syntax element cap
    static uint32_t s_wall_seconds = kDefaultSeconds; // this call's window; set_wall_seconds names the range

    // A deployment must set both walls; only the fuzz build may go uncapped (test/fuzz).
    static_assert(config::kMaxSeconds > 0, "config::kMaxSeconds must be set");
#ifndef ALXRUN_FUZZ_BUILD
    static_assert(config::kMaxSeconds >= kMinSeconds,
                  "a request's window cannot be below the minimum: config::kMaxSeconds >= 1");
    static_assert(config::kMemMb > 0, "config::kMemMb must be set (0 is only the fuzz build)");
    // The request, its parse tree and the bound data live under the wall; the block is granted on
    // top (src/mcp/), and 4x is the generous side of that tree.
    static_assert(config::kMemMb >= 4 * config::kBlockMb,
                  "a run needs room for its reply: config::kMemMb >= 4 * config::kBlockMb");
#endif

    // The half of a task the kernel owns (design.md §4; the supervisor holds the other half, in
    // its own process, under the same name). Only these fields cross threads.
    struct task {
        uint64_t gen = 0; // identity: bumped when a run is armed and when it ends
        std::chrono::steady_clock::time_point wall; // interrupt at this instant
        bool live = false; // a run is in flight
        uint64_t fired_gen = 0; // gen the watchdog already interrupted (0 = none)
    };

    // Polling, not a predicate wait -- the wait must never key on state a later run rewrites; the
    // cv carries teardown only, and the engine's interrupt lands when the current call returns.
    static constexpr int kWdTickMs = 10; // in flight: a tick stays under one checkpoint
    static constexpr int kWdIdleMs = 100; // nothing in flight: no reason to tick faster
    static std::mutex s_wd_mutex; // guards s_task and s_wd_stop
    static std::condition_variable s_wd_cv;
    static bool s_wd_stop = false; // teardown
    static task s_task;
    static std::atomic<bool> s_wd_alive{false};

    static void watchdog_main() {
        std::unique_lock<std::mutex> lk(s_wd_mutex);
        for (;;) {
            if (s_wd_stop) break;
            int tick = kWdIdleMs;
            if (s_task.live) {
                const auto now = std::chrono::steady_clock::now();
                if (now < s_task.wall) {
                    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          s_task.wall - now)
                                          .count();
                    tick = left < kWdTickMs ? static_cast<int>(left) + 1 : kWdTickMs;
                } else {
                    tick = kWdTickMs; // past the wall: keep ticking until the run unwinds
                    // Fire once per run, and only at an engine that is executing: a flag for a run
                    // that already returned would be left for the next run to trip over; the 10 ms
                    // tick makes retrying safe.
                    if (s_task.fired_gen != s_task.gen && s_engine && s_engine->running()) {
                        s_task.fired_gen = s_task.gen;
                        // The same wall for capabilities: a call with no engine checkpoint reads
                        // this flag instead (the regex callout).
                        capability::g_stop.store(true, std::memory_order_relaxed);
                        s_engine->set_interrupt();
                    }
                }
            }
            s_wd_cv.wait_for(lk, std::chrono::milliseconds(tick), [] { return s_wd_stop; });
        }
        s_wd_alive = false;
    }

    // The cap actually in force, for the error message ("0" = none), read live: the supervisor's
    // grant on the child, or an inherited tighter one.
    static std::string limit_mb() {
        rlimit cur{};
        if (getrlimit(RLIMIT_AS, &cur) == 0 && cur.rlim_cur != RLIM_INFINITY)
            return std::to_string(cur.rlim_cur >> 20);
        return kMaxRssKb == 0 ? std::string("0") : std::to_string(kMaxRssKb >> 10);
    }

    // A bad_alloc across any noexcept boundary lands here as a terminate: translate exactly that
    // one, so a real crash is never laundered as a budget error (doc/issue.md I-4).
    static void on_terminate() {
        if (std::current_exception()) {
            try {
                std::rethrow_exception(std::current_exception());
            } catch (const std::bad_alloc&) {
                const std::string diag =
                    "memory budget exceeded — the run is capped at " + limit_mb() + " MB of address space\n";
                std::fwrite(diag.data(), 1, diag.size(), stderr);
                std::fflush(stderr);
                std::_Exit(static_cast<int>(alx::script::error_type::MemoryError));
            } catch (...) {
            }
        }
        std::abort();
    }

    // Impose the allowance on the whole process -- the CLI on itself, the supervisor on each child.
    // Nothing outside a run is capped, which is why the installer runs before any engine (measured:
    // a JS runtime aborts under this limit).
    static void apply_limit() {
        if (kMaxRssKb == 0) return; // fuzz build: ASan needs the whole address space
        rlimit cur{};
        if (getrlimit(RLIMIT_AS, &cur) != 0) return;
        const rlim_t want = static_cast<rlim_t>(kMaxRssKb) * 1024;
        if (cur.rlim_max != RLIM_INFINITY && want >= cur.rlim_max) return; // already at least as tight
        rlimit rl = cur;
        rl.rlim_cur = want;
        rl.rlim_max = want; // hard: the process cannot lift it back
        setrlimit(RLIMIT_AS, &rl);
    }

    // The first node JSON cannot carry, or nullptr when the whole tree crosses: from_variant's
    // empty value would be indistinguishable from "returned nothing" (doc/issue.md I-5).
    static const char* unserializable(const alx::variant& _v, int _depth = 0) {
        if (_depth > 64) return nullptr; // do not chase what a handle might point back to
        if (_v.is<alx::anyptr>())
            return "an opaque handle (a callable or a host object)";
        if (_v.is<alx::varvec>()) {
            for (const alx::variant& e : _v.to<alx::varvec>())
                if (const char* why = unserializable(e, _depth + 1)) return why;
        } else if (_v.is<alx::varlst>()) {
            for (const alx::variant& e : _v.to<alx::varlst>())
                if (const char* why = unserializable(e, _depth + 1)) return why;
        } else if (_v.is<alx::varmap>()) {
            for (auto it = _v.to<alx::varmap>().cbegin(); it != _v.to<alx::varmap>().cend(); ++it)
                if (const char* why = unserializable(it.value(), _depth + 1)) return why;
        }
        return nullptr;
    }

    // The only door for external resources (design.md §2): a rejection is an uncatchable interrupt,
    // and desc tells the model why.
    static bool gate(alx::script::hook_info& _info) {
        if (_info.type == alx::script::hook_event::import) {
            if (_info.desc)
                *_info.desc = "import is not available here — write one self-contained script; "
                              "data arrives as pre-bound variables";
            return false;
        }
        if (_info.type == alx::script::hook_event::link) {
            if (_info.desc) *_info.desc = "link is not available here — no external libraries";
            return false;
        }
        return true;
    }

    static alx::script::engine* engine_ref() {
        if (s_engine) return s_engine;
        if (s_own_wall) apply_limit(); // the wall first, so the engine and every later allocation live under it
        std::set_terminate(on_terminate);
        // The fill syntax's pre-allocation guard: `[0:1e8]` would take 3 GB in one instruction, and
        // no periodic checkpoint can see an allocation between checkpoints.
        alx::script::engine_config cfg;
        cfg.max_vecfill = kMaxVecfill;
        cfg.overflow_check = ALXRUN_OVERFLOW_CHECK;
        cfg.debug_enable = s_debug;
        s_engine = alx::script::engine::create(cfg);
        registry::install(s_engine);
        s_engine->on_cerr.connect([](const std::string& _s) {
            if (!s_diag) return;
            s_diag->append(_s);
            if (!_s.empty() && _s.back() != '\n') s_diag->push_back('\n');
        });
        s_engine->on_cmpl.connect([](const alx::script::compile_error& _e) {
            if (!s_diag) return;
            // row:col only: loc.path is the home dir we passed, not a file name, and printing a
            // directory as if it were one misleads.
            s_diag->append(std::to_string(_e.loc.row) + ":" + std::to_string(_e.loc.col) + ": " +
                           _e.msg + "\n");
        });
        s_engine->set_pipe(nullptr, on_out, nullptr, nullptr);
        s_engine->set_hook(gate, nullptr, 0); // interval 0: no exec events, the clock is the wall
        // Detached + handshake: s_wd_alive is set here, cleared by the thread on its way out --
        // that pair tells shutdown() the engine is safe to delete.
        s_wd_alive = true;
        std::thread(watchdog_main).detach();
        return s_engine;
    }

    void set_debug(bool _on) {
        s_debug = _on;
    }

    void set_own_wall(bool _on) {
        s_own_wall = _on;
    }

    void set_wall_seconds(uint32_t _seconds) {
        // Out of range leaves the default standing: the caller's face names the range in its
        // refusal, and a window that slipped past must not silently move the guard.
        if (_seconds < kMinSeconds || _seconds > static_cast<uint32_t>(config::kMaxSeconds)) return;
        s_wall_seconds = _seconds;
    }

    size_t memory_limit_bytes() {
        return static_cast<size_t>(kMaxRssKb) * 1024;
    }

    result run(std::string& _code, alx::json_object& _data, const std::string& _home_dir) {
        result res;
        alx::script::engine* e = engine_ref();

        // Wipe, then re-bind: nothing leaks between runs; the binding copy runs outside the cap.
        e->reset();
        std::string out, diag;
        s_out = &out;
        s_diag = &diag;
        for (auto it = _data.begin(); it != _data.end(); ++it)
            *e->load(it.key(), true) = it.value().to_variant();
        _data.clear(); // bound: the caller's copy of the data has reached its end of life

        uint64_t gen = 0;
        {
            std::lock_guard<std::mutex> lk(s_wd_mutex); // arm before exec: the wall is wall clock
            gen = ++s_task.gen;
            s_task.wall = std::chrono::steady_clock::now() + std::chrono::seconds(s_wall_seconds);
            s_task.live = true;
            capability::g_stop.store(false, std::memory_order_relaxed); // this run's window, not the last
        }

        const alx::bytes src(_code); // the engine's own copy of the code
        std::string().swap(_code); // ...so the caller's can go (clear() would keep the capacity)
        bool fired = false;
        {
            alx::script::engine::result rst = e->exec(src, _home_dir); // built in place: no copy, no move
            {
                std::lock_guard<std::mutex> lk(s_wd_mutex);
                fired = s_task.fired_gen == gen; // this run, not an earlier one
                ++s_task.gen;
                s_task.live = false;
            }
            s_out = nullptr;
            s_diag = nullptr;
            res.output = std::move(out);
            res.diag = std::move(diag);
            res.error = rst.error;
            // Convert and release in one breath: the variant can be another wall-sized structure,
            // and an interrupted or over-wall run carries no value -- the branches need diag only.
            if (res.error == alx::script::error_type::NoError) {
                if (const char* why = unserializable(rst.value)) {
                    // The run itself was fine -- say why its value cannot come back.
                    res.error = alx::script::error_type::ConvError;
                    res.diag = std::string(
                                   "the script ran to the end, but its value cannot be returned: it holds ") +
                               why + " — nothing crosses into JSON, so no value came back\n";
                } else {
                    try {
                        res.value = alx::json_value::from_variant(std::move(rst.value));
                        res.has_value = true;
                    } catch (const std::bad_alloc&) {
                        // The conversion hit the wall; same face as every memory refusal -- a
                        // silently null value would read as "the script returned nothing".
                        res.error = alx::script::error_type::MemoryError;
                        res.diag = "memory budget exceeded — the run is capped at " + limit_mb() +
                                   " MB of address space\n";
                    } catch (...) {
                        // Unserializable value (handle in the tree) -- report, keep the process alive.
                        res.diag += "return value is not serializable\n";
                    }
                }
            }
        } // the variant, and the run's whole state with it, are gone by this line
        e->reset();

        // Interrupted is the watchdog's doing (hook refusals carry their own desc): use the budget
        // message the other paths use.
        if (fired && res.error == alx::script::error_type::InterruptedError) {
            // Name the window: the caller's own number, the only handle on "how much longer it
            // needed".
            res.diag = "time budget exceeded — the " + std::to_string(s_wall_seconds) +
                       " s timeout was used up; raise `timeout` on the call, or break the work into "
                       "smaller runs\n";
            res.has_value = false;
            return res;
        }
        // The library names what ran out; this names the ceiling it ran out against (issue.md I-4).
        if (res.error == alx::script::error_type::MemoryError) {
            if (!res.diag.empty() && res.diag.back() != '\n') res.diag.push_back('\n');
            res.diag += "memory budget exceeded — the run is capped at " + limit_mb() +
                        " MB of address space\n";
            return res;
        }
        return res;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(s_wd_mutex); // the watchdog must not outlive the engine
            s_wd_stop = true;
        }
        s_wd_cv.notify_all();
        while (s_wd_alive.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        delete s_engine;
        s_engine = nullptr;
    }

} // namespace kernel
