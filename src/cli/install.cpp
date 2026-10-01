// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "install.h"

#include "afile.h"
#include "ajson.h"
#include "aplatform.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#if defined(_WIN32)
#    include <windows.h>
#else
#    include <unistd.h>
#endif

namespace cli {

    namespace {

        const char* kServer = "alxrun";
        // The exact tool names, never the wildcard: `mcp__<server>__*` re-prompts per tool in the
        // client, and the native "always allow" covers only the project scope. `capabilities` is
        // here too -- the model needs it before it can write a correct script.
        const char* kAllowRules[] = {"mcp__alxrun__run", "mcp__alxrun__capabilities"};
        constexpr size_t kRuleCount = sizeof(kAllowRules) / sizeof(kAllowRules[0]);
        // The client may health-check the server it registers; 500 ms (exec_sync's default) is not enough.
        constexpr alx::uint_32 kWaitMs = 15000;

        // The three spots that are not portable.

        std::string self_path() {
#if defined(_WIN32)
            char buf[MAX_PATH];
            const DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
            return n > 0 ? std::string(buf, n) : std::string();
#else
            char buf[4096];
            const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
            return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
#endif
        }

        // exec_sync forks and execs the target itself (no shell in between), but it splits our
        // command line honoring POSIX quoting: measured, spaces and quotes survive.
        std::string quote(const std::string& _s) {
#if defined(_WIN32)
            std::string out = "\"";
            for (char c : _s)
                out += (c == '"') ? "\"\"" : std::string(1, c);
            return out + "\"";
#else
            std::string out = "'";
            for (char c : _s)
                out += (c == '\'') ? "'\\''" : std::string(1, c);
            return out + "'";
#endif
        }

        std::string home_dir() {
            const char* home = std::getenv(
#if defined(_WIN32)
                "USERPROFILE"
#else
                "HOME"
#endif
            );
            return home ? std::string(home) : std::string();
        }

        std::string settings_path() {
            const std::string home = home_dir();
            return home.empty() ? std::string() : home + "/.claude/settings.json";
        }

        std::string scope_of(bool _global) {
            return _global ? "user" : "local";
        }

        std::string add_command(const std::string& _self, bool _global) {
            return "claude mcp add " + std::string(kServer) + " -s " + scope_of(_global) + " -- " + quote(_self) + " mcp";
        }

        // One shell line through AlxLib's exec; the child's exit code, or -1.
        int run(const std::string& _cmd, std::string* _out) {
            const alx::exec_result r = alx::exec_sync(_cmd, kWaitMs);
            *_out = r.output;
            if (r.timeout) {
                *_out += "\n(timed out after " + std::to_string(kWaitMs / 1000) + "s)";
                return -1;
            }
            return r.excode;
        }

        bool registered() {
            std::string out;
            return run(std::string("claude mcp get ") + kServer, &out) == 0;
        }

        // Is _name on PATH? POSIX-only: the Windows guard answers no, so the codex leg simply
        // does not exist there.
        bool on_path(const std::string& _name) {
#if defined(_WIN32)
            (void) _name;
            return false;
#else
            const char* path = std::getenv("PATH");
            if (!path) return false;
            const std::string dirs(path);
            size_t at = 0;
            for (;;) {
                const size_t colon = dirs.find(':', at);
                const std::string dir = dirs.substr(at, colon == std::string::npos ? std::string::npos : colon - at);
                if (!dir.empty() && ::access((dir + "/" + _name).c_str(), X_OK) == 0) return true;
                if (colon == std::string::npos) return false;
                at = colon + 1;
            }
#endif
        }

        // The Codex CLI delegates like the other client: `codex mcp add` writes ~/.codex/config.toml
        // itself, and `mcp get` doubles as the registered check.
        std::string codex_add_command(const std::string& _self) {
            return std::string("codex mcp add ") + kServer + " -- " + quote(_self) + " mcp";
        }

        bool codex_registered() {
            std::string out;
            return run(std::string("codex mcp get ") + kServer, &out) == 0;
        }

        // Add or drop the allow rule in permissions.allow; writes only when the file would
        // actually change. A missing file counts as empty settings.
        bool settings_rule(bool _add, std::string* _err) {
            const std::string path = settings_path();
            if (path.empty()) {
                *_err = "no home directory in the environment";
                return false;
            }
            const std::string text = alx::file::read_all(path).to_string();
            if (text.empty() && alx::file_info(path).is_exist()) {
                *_err = "cannot read " + path;
                return false;
            }
            bool ok = true; // an empty file is fine: fresh settings
            const alx::json_value parsed = text.empty() ? alx::json_value(alx::json_object()) : alx::json_doc::from_value(text, &ok);
            if (!ok) {
                *_err = path + " is not valid JSON";
                return false;
            }
            alx::json_object root = parsed.to_object();
            alx::json_object perms = root.contain("permissions") ? root.value("permissions").to_object() : alx::json_object();
            alx::json_array keep;
            size_t present = 0;
            if (perms.contain("allow")) {
                const alx::json_array allow = perms.value("allow").to_array();
                for (size_t i = 0; i < allow.size(); ++i) {
                    bool mine = false;
                    if (allow[i].is<std::string>()) {
                        const std::string s = allow[i].to<std::string>();
                        for (const char* r : kAllowRules) mine = mine || s == r;
                    }
                    present += mine ? 1 : 0;
                    if (!(mine && !_add)) keep.append(allow[i]);
                }
            }
            if (_add ? present == kRuleCount : present == 0) return true; // already in the wanted state
            if (_add)
                for (const char* r : kAllowRules) {
                    bool have = false;
                    for (size_t i = 0; i < keep.size(); ++i)
                        if (keep[i].is<std::string>() && keep[i].to<std::string>() == r) have = true;
                    if (!have) keep.append(alx::json_value(r));
                }
            perms["allow"] = alx::json_value(keep); // [] overwrites; insert() is map-insert
            root["permissions"] = alx::json_value(perms);
            // indented: the file is hand-edited, one line is unreadable
            const std::string out = alx::json_doc::to_json(root) + "\n";
            const std::string tmp = path + ".tmp";
            const std::string dir = alx::file_info(path).get_parent().path();
            if (!dir.empty() && !alx::file_info(dir).is_exist() && !alx::file_info::mkdir(dir)) {
                *_err = "cannot create " + dir;
                return false;
            }
            if (!alx::file::write_all(tmp, out.data(), out.size(), false)) {
                *_err = "cannot write " + tmp;
                return false;
            }
            if (!alx::file_info::mvfile(tmp, path)) {
                // a move onto an existing file may be refused (Windows); drop it and retry once
                alx::file_info::rmfile(path);
                if (!alx::file_info::mvfile(tmp, path)) {
                    *_err = "cannot replace " + path;
                    return false;
                }
            }
            return true;
        }

        void print_plan(const std::vector<std::string>& _steps) {
            std::cout << "This will:\n";
            for (const std::string& s : _steps)
                std::cout << "  " << s << "\n";
        }

        // Show the plan, then honor --dry-run / -y / -n / the prompt.
        bool proceed(const setup_options& _o, const std::vector<std::string>& _steps) {
            print_plan(_steps);
            if (_o.dry_run || _o.confirm < 0) return false;
            if (_o.confirm > 0) return true;
            std::cout << "proceed? [y/N] " << std::flush;
            std::string line;
            if (!std::getline(std::cin, line)) return false;
            return !line.empty() && (line[0] == 'y' || line[0] == 'Y');
        }

        int step_failed(const std::string& _cmd, const std::string& _out) {
            std::cerr << "alxrun: " << _cmd << " failed\n"
                      << _out;
            return kExitStepFailed;
        }

    } // namespace

    int install(const setup_options& _o) {
        const std::string self = self_path();
        if (self.empty()) {
            std::cerr << "alxrun install: cannot resolve this binary's path — register it by hand\n";
            return kExitIo;
        }

        // One client, one plan, one yes: a registration for one client is never covered by
        // another's authorization. Declining a leg skips that client, not the install.
        const bool already = registered();
        const std::string add = add_command(self, _o.global);
        const bool have_codex = on_path("codex");
        const bool codex_already = have_codex && codex_registered();
        const std::string codex_add = codex_add_command(self);
        const std::string codex_drop = std::string("codex mcp remove ") + kServer;

        std::vector<std::string> steps;
        if (already) steps.push_back(std::string("claude mcp remove ") + kServer + "   # re-point an existing entry");
        steps.push_back(add);
        if (_o.global) {
            std::string rule_list;
            for (size_t i = 0; i < kRuleCount; ++i) rule_list += (i ? ", " : "") + std::string(kAllowRules[i]);
            steps.push_back("write " + rule_list + " into " + settings_path() + " (permissions.allow)");
        }
        const bool claude_ok = proceed(_o, steps);

        bool codex_ok = false;
        if (have_codex) {
            std::vector<std::string> codex_steps;
            if (codex_already) codex_steps.push_back(codex_drop + "   # re-point an existing entry");
            codex_steps.push_back(codex_add);
            codex_ok = proceed(_o, codex_steps);
        }
        if (_o.dry_run) return 0; // plans printed, nothing written

        bool wrote = false;
        if (claude_ok) {
            std::string out;
            if (already) {
                const std::string drop = std::string("claude mcp remove ") + kServer;
                if (run(drop, &out) != 0) return step_failed(drop, out);
            }
            out.clear();
            if (run(add, &out) != 0) return step_failed(add, out);
            std::cout << out;
            // Measured: the client starts the server at session start and caches its tool
            // descriptions, so a config change alone does not put the new server to work.
            std::cout << "next: restart Claude Code — the server is started at session start\n";
            if (_o.global) {
                std::string err;
                if (!settings_rule(true, &err)) {
                    std::cerr << "alxrun install: " << err << "\n";
                    return kExitIo;
                }
                std::cout << "allowed";
                for (const char* r : kAllowRules) std::cout << " " << r;
                std::cout << " in " << settings_path() << "\n";
            }
            wrote = true;
        } else {
            std::cerr << "alxrun install: claude — declined, skipped\n";
        }

        if (have_codex) {
            if (codex_ok) {
                std::string out;
                if (codex_already && run(codex_drop, &out) != 0) return step_failed(codex_drop, out);
                out.clear();
                if (run(codex_add, &out) != 0) return step_failed(codex_add, out);
                std::cout << out;
                std::cout << "codex: registered; a new Codex session reads the config\n";
                wrote = true;
            } else {
                std::cerr << "alxrun install: codex — declined, skipped\n";
            }
        }
        if (!wrote) {
            std::cerr << "alxrun install: declined — nothing written\n";
            return kExitDeclined;
        }
        return 0;
    }

    int uninstall(const setup_options& _o) {
        const bool codex_already = on_path("codex") && codex_registered();
        const std::string drop = std::string("claude mcp remove ") + kServer + "   # from whichever scope holds it";
        std::vector<std::string> steps{drop};
        if (codex_already) steps.push_back(std::string("codex mcp remove ") + kServer);
        steps.push_back("drop our allow rules from " + settings_path() + " (only if present)");

        if (!proceed(_o, steps)) {
            if (_o.dry_run) return 0;
            std::cerr << "alxrun uninstall: declined — nothing written\n";
            return kExitDeclined;
        }
        std::string out;
        if (run(std::string("claude mcp remove ") + kServer, &out) != 0) {
            // Not registered is a fine state to uninstall from; anything else is not.
            if (out.find("No MCP server") == std::string::npos) return step_failed(drop, out);
        }
        if (codex_already) {
            out.clear();
            const std::string codex_drop = std::string("codex mcp remove ") + kServer;
            if (run(codex_drop, &out) != 0) return step_failed(codex_drop, out);
        }
        std::string err;
        if (!settings_rule(false, &err)) {
            std::cerr << "alxrun uninstall: " << err << "\n";
            return kExitIo;
        }
        std::cout << "uninstalled: " << kServer << " (restart Claude Code to drop the tools)\n";
        return 0;
    }

} // namespace cli
