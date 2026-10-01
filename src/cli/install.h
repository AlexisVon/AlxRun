// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_INSTALL_H
#define ALXRUN_INSTALL_H

namespace cli {

    /// The installer's own exit codes live in 100-125 -- the band the CLI reserved for itself,
    /// so they can never be read as an error_type id (design.md §8).
    constexpr int kExitUsage = 100;
    constexpr int kExitDeclined = 101;
    constexpr int kExitStepFailed = 102;
    constexpr int kExitIo = 103;

    /// What install / uninstall may touch: this binary's MCP registration with the client
    /// (design.md §1) and, with -g, the user-level allow rule -- nothing else.
    struct setup_options {
        bool global = false;
        bool dry_run = false;
        /// 1 = -y, -1 = -n, 0 = ask on stdin
        int confirm = 0;
    };

    /// Register this binary with the client / undo it (design.md §1).
    int install(const setup_options& _o);
    int uninstall(const setup_options& _o);

} // namespace cli

#endif // ALXRUN_INSTALL_H
