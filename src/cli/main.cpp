// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "ajson.h"
#include "config.h"
#include "install.h"
#include "kernel.h"
#include "mcp.h"
#include "registry.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

    std::string usage() {
        return std::string(
            "alxrun — AScript execution kernel\n"
            "\n"
            "usage:\n"
            "  alxrun mcp                        MCP server on stdio (one child process per request)\n"
            "  alxrun mcp-serve                  the same protocol without the process boundary\n"
            "  alxrun mcp-cli                    internal: one request (spawned by `mcp`)\n"
            "  alxrun -e <code> [--data <json>]  run inline code, optionally with bound data\n"
            "                                    eg  alxrun -e 'return a+b;' --data '{\"a\":1,\"b\":2}'  =>  [ret] 3\n"
            "  alxrun <file.axc>                 run a script file\n"
            "  alxrun --capabilities [q]         capability doc: a clan, or any part of a key\n"
            "                                    (alias --cap; no argument = the clan index, \".\" = everything)\n"
            "  alxrun --brief                    the resident capability listing\n"
            "  alxrun --demo                     the resident language example\n"
            "  alxrun --demo-data                the data the demo expects bound (for tests)\n"
            "  alxrun install [-g]               register this binary with the client (MCP)\n"
            "  alxrun uninstall                  unregister it\n"
            "  alxrun -v | -h                    version | this help\n"
            "\n"
            "  install / uninstall take --dry-run (print the plan, write nothing),\n"
            "  -y / -n (answer the prompt up front) and -g (user scope, not this project).\n"
            "\n"
            "  --no-debug                        drop runtime positions from error traces (~33% faster)\n"
            "  --timeout <seconds>               run window for -e / <file>: whole seconds, " +
            std::to_string(kernel::kMinSeconds) + " to " +
            std::to_string(config::kMaxSeconds) + ", default " +
            std::to_string(kernel::kDefaultSeconds) + "\n");
    }

    std::string read_file(const std::string& _path) {
        std::ifstream f(_path, std::ios::binary);
        if (!f) return {};
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    std::string dir_of(const std::string& _path) {
        const size_t pos = _path.rfind('/');
        return pos == std::string::npos ? std::string() : _path.substr(0, pos);
    }

    // Debug face: value echoed as [ret]; exit code = error_type id. _code and _data are released
    // by kernel::run once the engine owns its copies -- this face is done with them at that point.
    int run_one(std::string& _code, const std::string& _home_dir, alx::json_object& _data) {
        kernel::result r = kernel::run(_code, _data, _home_dir);
        std::cout << r.output;
        if (!r.diag.empty()) std::cerr << r.diag;
        if (r.has_value) std::cout << "[ret] " << alx::json_doc::to_string(std::move(r.value), true) << "\n";
        return static_cast<int>(r.error);
    }

} // namespace

int main(int _argc, char** _argv) {
    // --debug / --no-debug / --data apply to whichever face runs, and may sit anywhere; what is
    // left is the face plus its own arguments, in order.
    alx::json_object data;
    std::vector<std::string> pos;
    for (int i = 1; i < _argc; ++i) {
        if (std::strcmp(_argv[i], "--no-debug") == 0) {
            kernel::set_debug(false);
            continue;
        }
        if (std::strcmp(_argv[i], "--debug") == 0) {
            kernel::set_debug(true);
            continue;
        }
        if (std::strcmp(_argv[i], "--data") == 0) {
            if (i + 1 >= _argc) {
                std::cerr << "alxrun: --data needs a JSON object\n";
                return cli::kExitUsage;
            }
            bool ok = false;
            const alx::json_value v = alx::json_doc::from_value(_argv[i + 1], &ok);
            if (!ok || !v.is<alx::json_object>()) {
                std::cerr << "alxrun: --data must be a JSON object\n";
                return cli::kExitUsage;
            }
            data = v.to_object();
            ++i;
            continue;
        }
        if (std::strcmp(_argv[i], "--timeout") == 0) {
            uint32_t seconds = 0;
            if (i + 1 >= _argc || !kernel::parse_wall_seconds(_argv[i + 1], seconds)) {
                std::cerr << "alxrun: --timeout must be " << kernel::kMinSeconds << " to "
                          << config::kMaxSeconds << " whole seconds\n";
                return cli::kExitUsage;
            }
            kernel::set_wall_seconds(seconds);
            ++i;
            continue;
        }
        pos.push_back(_argv[i]);
    }

    const std::string face = pos.empty() ? std::string() : pos[0];
    if (face.empty() || face == "-h" || face == "--help") {
        std::cout << usage();
        return 0;
    }
    if (face == "-v" || face == "--version") {
        std::cout << "alxrun 0.1.0\n";
        return 0;
    }
    if (face == "--capabilities" || face == "--cap") {
        const alx::json_object doc = pos.size() > 1 ? registry::document(pos[1]) : registry::index();
        std::cout << alx::json_doc::to_string(alx::json_value(doc), false) << "\n";
        return 0;
    }
    if (face == "--brief") {
        std::cout << registry::brief();
        return 0;
    }
    if (face == "--demo") {
        std::cout << registry::demo() << "\n";
        return 0;
    }
    if (face == "--demo-data") {
        std::cout << registry::demo_data() << "\n";
        return 0;
    }
    if (face == "mcp") {
        const int rc = mcp::supervise(); // product face: one child process per request
        kernel::shutdown();
        return rc;
    }
    if (face == "mcp-serve") { // the protocol without the process boundary (tests, debug)
        const int rc = mcp::serve();
        kernel::shutdown();
        return rc;
    }
    if (face == "mcp-cli") { // one request per process: stop the watchdog before the statics go
        const int rc = mcp::oneshot(pos.size() > 1 ? pos[1] : std::string());
        kernel::shutdown();
        return rc;
    }
    if (face == "install" || face == "uninstall") {
        cli::setup_options o;
        for (size_t i = 1; i < pos.size(); ++i) {
            if (pos[i] == "-g" || pos[i] == "--global") o.global = true;
            else if (pos[i] == "--dry-run") o.dry_run = true;
            else if (pos[i] == "-y") o.confirm = 1;
            else if (pos[i] == "-n") o.confirm = -1;
            else {
                std::cerr << "alxrun: unknown option " << pos[i] << "\n";
                return cli::kExitUsage;
            }
        }
        return face == "install" ? cli::install(o) : cli::uninstall(o);
    }

    int rc = 0;
    if (face == "-e") {
        if (pos.size() < 2) {
            std::cerr << "alxrun: -e needs code\n";
            rc = 2;
        } else {
            rc = run_one(pos[1], "", data);
        }
    } else {
        const std::string path = face;
        std::string src = read_file(path);
        if (src.empty()) {
            std::cerr << "alxrun: cannot read " << path << "\n";
            rc = 2;
        } else {
            rc = run_one(src, dir_of(path), data);
        }
    }
    kernel::shutdown();
    return rc;
}
