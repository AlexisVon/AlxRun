// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

/// Acceptance for the aplatform process API: kill / status / limits. Everything here is a
/// promise the MCP supervisor leans on, so each check names the promise, not the implementation.
///
/// Built and run by test/platform/run.sh. Children are /bin/sh unless the check is about the
/// memory cap, which needs a real allocator (bin/alxrun is the honest one -- it is what the
/// supervisor will cap).
#include "aplatform.h"

#include <csignal>
#include <unistd.h> // getpid, access (this device drives /bin/sh children: POSIX-only)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

    int g_fails = 0;
    int g_checks = 0;

    void ok(bool _cond, const std::string& _what, const std::string& _detail = std::string()) {
        ++g_checks;
        if (_cond) return;
        ++g_fails;
        std::printf("  FAIL  %s%s%s\n", _what.c_str(), _detail.empty() ? "" : " — ", _detail.c_str());
    }

    struct captured {
        std::string out;
        std::string err;
    };

    // Spawn, feed optional input, wait for exit; streams land in the out params.
    bool spawn_capture(const std::string& _exe, const std::vector<std::string>& _args,
                       const alx::process_ctrl::process_limits& _limits, captured& _cap,
                       alx::process_ctrl::wait_status& _st, const char* _input = nullptr,
                       int _wait_ms = 12000) {
        alx::process_ctrl ctrl(_exe);
        ctrl.sig_output.connect([&](const std::string& _s) { _cap.out += _s; });
        ctrl.sig_errput.connect([&](const std::string& _s) { _cap.err += _s; });
        if (!ctrl.start(_args, _limits)) return false;
        if (_input) ctrl.input(_input);
        const bool exited = ctrl.wait(_wait_ms);
        // Reaping is not "the output is over": wait() returns as soon as the child is reaped,
        // while loop() may still be pumping the pipes (AlxLib doc/core/api.md). Everything
        // measured here must happen after output_done() says so.
        ctrl.output_done(2000);
        _st = ctrl.status();
        return exited;
    }

    // --- a normal exit carries its code -----------------------------------------------------
    void check_exit_code(const std::string& _sh) {
        captured cap;
        alx::process_ctrl::wait_status st;
        const bool exited = spawn_capture(_sh, {"-c", "exit 7"}, {}, cap, st);
        ok(exited && st.exited, "exit: the child is seen to have exited");
        ok(st.exit_code == 7, "exit: exit_code is the child's (7)",
           "got " + std::to_string(st.exit_code));
        ok(!st.signaled, "exit: not reported as signaled");
    }

    // --- a signal death is distinguishable, and its last words survive ----------------------
    void check_signal_death(const std::string& _sh) {
        captured cap;
        alx::process_ctrl::wait_status st;
        const bool exited = spawn_capture(_sh, {"-c", "echo boom 1>&2; kill -ABRT $$"}, {}, cap, st);
        ok(exited && st.exited, "signal: the child is seen to have exited");
        ok(st.signaled, "signal: signaled is true");
        ok(st.term_sig == SIGABRT, "signal: term_sig is SIGABRT",
           "got " + std::to_string(st.term_sig));
        // The supervisor quotes this line in the "engine died" reply, so it must arrive even
        // though the child died by signal.
        ok(cap.err.find("boom") != std::string::npos, "signal: stderr got through before the death",
           "err=" + cap.err.substr(0, 40));
    }

    // --- reaping and "output is over" are two different things ------------------------------
    void check_output_done(const std::string& _sh) {
        alx::process_ctrl ctrl(_sh);
        size_t got = 0;
        ctrl.sig_output.connect([&](const std::string& _s) { got += _s.size(); });
        ok(ctrl.start({"-c", "i=0; while [ $i -lt 10000 ]; do printf x; i=$((i+1)); done; echo"}),
           "output_done: the child starts");
        ctrl.wait(12000);
        ok(ctrl.output_done(2000), "output_done: true once both streams have ended");
        ok(got == 10001, "output_done: nothing is still in flight when it says so",
           "got " + std::to_string(got));
    }

    // --- self path: the supervisor spawns its own clone, so it must find itself --------------
    void check_self_path() {
        const std::vector<alx::process_info> me = alx::process_info::find_process(::getpid());
        ok(me.size() == 1, "self: find_process(getpid()) yields this process");
        if (me.empty()) return;
        ok(!me[0].path.empty() && me[0].path[0] == '/', "self: path is absolute",
           "path=" + me[0].path);
        ok(me[0].path.find("platform_check") != std::string::npos,
           "self: path names this binary", "path=" + me[0].path);
    }

    // --- mmap lifetime: close() detaches, destroy() removes, the creator owns -----------------
    // The supervisor's block rests on exactly these promises, and on Linux the companion named
    // semaphore is a file in /dev/shm -- that is what the last check looks at.
    void check_mmap_lifetime() {
        const std::string name = "alxrun-devtest-" + std::to_string(::getpid());
        const std::string sem = "/dev/shm/sem." + name + "_sem";
        alx::mmap owner(name);
        owner.destroy(); // the session-start idiom: clear a corpse, then create
        ok(owner.open(64u << 20, true), "mmap: the creator opens with create=true");
        ok(owner.owner(), "mmap: the creator knows it created the name");
        owner.write("hello", 0, 5);
        {
            alx::mmap guest(name);
            ok(guest.open(64u << 20, false), "mmap: a second opener attaches");
            ok(!guest.owner(), "mmap: an attacher is not the owner");
            char buf[6] = {0};
            ok(guest.read(buf, 0, 5) && std::string(buf) == "hello", "mmap: the attacher reads it");
            guest.close(); // detach only
        }
        char buf[6] = {0};
        ok(owner.read(buf, 0, 5) && std::string(buf) == "hello",
           "mmap: an attacher's close() leaves the object alone");
        {
            alx::mmap again(name), bigger(name);
            ok(again.open(64u << 20, false), "mmap: a later attacher still finds the name");
            ok(!bigger.open(128u << 20, false), "mmap: attaching larger than the block is refused");
            again.close();
        }
        ok(::access(sem.c_str(), F_OK) == 0, "mmap: the companion semaphore exists while it lives");
        ok(owner.destroy(), "mmap: destroy() removes the names");
        {
            alx::mmap gone(name);
            ok(!gone.open(64u << 20, false), "mmap: after destroy() nothing attaches");
        }
        ok(::access(sem.c_str(), F_OK) != 0, "mmap: destroy() took the companion semaphore with it");
    }

    // --- what the stream signals actually deliver -------------------------------------------
    void check_stream_blocks(const std::string& _sh) {
        captured cap;
        alx::process_ctrl::wait_status st;
        // 10000 bytes + newline: crosses the 4096-byte read size twice.
        spawn_capture(_sh, {"-c", "i=0; while [ $i -lt 10000 ]; do printf x; i=$((i+1)); done; echo"},
                      {}, cap, st);
        ok(cap.out.size() == 10001, "blocks: every byte arrives, none invented",
           "got " + std::to_string(cap.out.size()));
        ok(!cap.out.empty() && cap.out.back() == '\n',
           "blocks: the trailing newline is raw, as documented");
    }

    // --- kill() is hard, reaps, and is idempotent -------------------------------------------
    void check_kill(const std::string& _sh) {
        captured cap;
        alx::process_ctrl ctrl(_sh);
        ctrl.sig_output.connect([&](const std::string& _s) { cap.out += _s; });
        ok(ctrl.start({"-c", "sleep 300"}), "kill: the child starts");
        ok(ctrl.pid() != 0, "kill: pid is known while it runs");
        ok(ctrl.kill(), "kill: returns true for a live child");
        const alx::process_ctrl::wait_status st = ctrl.status();
        ok(st.exited && st.signaled && st.term_sig == SIGKILL,
           "kill: the child died by SIGKILL",
           "exited=" + std::to_string(st.exited) + " sig=" + std::to_string(st.term_sig));
        ok(ctrl.pid() == 0, "kill: the handle is released (pid back to 0)");
        ok(!ctrl.kill(), "kill: idempotent — a dead child returns false");
        ok(!ctrl.input("x"), "kill: input after death returns false, does not hang");
    }

    // --- the polite path, for contrast ------------------------------------------------------
    void check_terminate(const std::string& _sh) {
        captured cap;
        alx::process_ctrl ctrl(_sh);
        ok(ctrl.start({"-c", "trap 'exit 0' TERM; sleep 300"}), "terminate: the child starts");
        ok(ctrl.terminate(), "terminate: the request is sent");
        ok(ctrl.wait(3000), "terminate: a trapping child exits on its own",
           "it did not exit within 3s");
        ctrl.kill(); // idempotent cleanup
    }

    // --- the parent imposes the cap (child = our own binary, the real use case) -------------
    void check_memory_limit(const std::string& _alxrun) {
        // 26 doublings = 64 MiB: comfortable without a cap (the child's own 512 MB wall is far
        // away), impossible under 32 MiB.
        const char* code = "var s = \"x\"; for (var i = 0; i < 26; i = i + 1) { s = s + s; } return $str_size(s);";
        captured free_run, capped;
        alx::process_ctrl::wait_status st_free, st_capped;
        spawn_capture(_alxrun, {"-e", code}, {}, free_run, st_free);
        ok(st_free.exited && st_free.exit_code == 0, "limit: control run without a cap succeeds",
           "rc=" + std::to_string(st_free.exit_code) + " err=" + free_run.err.substr(0, 60));
        spawn_capture(_alxrun, {"-e", code}, alx::process_ctrl::process_limits(32ull << 20), capped,
                      st_capped);
        ok(st_capped.exited && st_capped.exit_code != 0, "limit: the cap kills the allocation",
           "rc=" + std::to_string(st_capped.exit_code));

        // Hard, not advisory: the child must not be able to raise it back.
        captured cap;
        alx::process_ctrl::wait_status st;
        spawn_capture("/bin/sh", {"-c", "ulimit -v unlimited >/dev/null 2>&1 || exit 3; exit 0"},
                      alx::process_ctrl::process_limits(32ull << 20), cap, st);
        ok(st.exited && st.exit_code == 3, "limit: the child cannot raise the cap back",
           "rc=" + std::to_string(st.exit_code));
    }

    // --- the child dies with us --------------------------------------------------------------
    // Roles: "outer" spawns an "inner" (self, --mid) which spawns a sleeper with the flag and
    // prints its pid; the outer then kills the inner and asks whether the sleeper survived.
    int run_mid(const std::string& _sh) {
        alx::process_ctrl ctrl(_sh);
        alx::process_ctrl::process_limits limits;
        limits.kill_on_parent_exit = true;
        if (!ctrl.start({"-c", "sleep 300"}, limits)) return 2;
        std::printf("%u\n", ctrl.pid());
        std::fflush(stdout);
        for (;;) alx::high_precision_sleep(100);
    }

    void check_kill_on_parent_exit(const std::string& _self, const std::string& _sh) {
        alx::process_ctrl mid(_self);
        std::string line;
        mid.sig_output.connect([&](const std::string& _s) { line += _s; });
        if (!mid.start({"--mid", _sh})) {
            ok(false, "parent-exit: the middle process starts");
            return;
        }
        for (int i = 0; i < 50 && line.find('\n') == std::string::npos; ++i)
            alx::high_precision_sleep(20);
        const alx::uint_32 sleeper = static_cast<alx::uint_32>(std::strtoul(line.c_str(), nullptr, 10));
        ok(sleeper != 0, "parent-exit: the sleeper's pid arrives", "line=" + line);
        mid.kill();
        alx::high_precision_sleep(400); // let the kernel reap the orphan
        ok(alx::process_info::find_process(sleeper).empty(),
           "parent-exit: the sleeper is gone with its parent");
    }

} // namespace

int main(int _argc, char** _argv) {
    // The child role for the parent-exit check: spawn a sleeper and idle.
    if (_argc == 3 && std::strcmp(_argv[1], "--mid") == 0) return run_mid(_argv[2]);

    const char* sh = "/bin/sh";
    std::string self = _argv[0];
    std::string alxrun = "bin/alxrun"; // run.sh runs us from the repo root

    std::printf("platform acceptance (bin/alxrun: %s)\n", alxrun.c_str());
    check_self_path();
    check_output_done(sh);
    check_mmap_lifetime();
    check_exit_code(sh);
    check_signal_death(sh);
    check_stream_blocks(sh);
    check_kill(sh);
    check_terminate(sh);
    check_memory_limit(alxrun);
    check_kill_on_parent_exit(self, sh);

    std::printf("%s — %d checks, %d failed\n", g_fails ? "FAIL" : "PASS", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
