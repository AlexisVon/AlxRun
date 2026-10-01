// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_MCP_H
#define ALXRUN_MCP_H

#include <cstddef>
#include <string>

namespace mcp {

    /// Past this size the transcript's text is a note instead of the bytes and the payload rides in
    /// structuredContent (design.md §8). Both halves apply it: the child to a successful reply, the
    /// supervisor to the log it hands back when the run died.
    constexpr size_t kTextInline = 64u << 10;

    /**
     * \brief MCP server on stdio: one JSON-RPC message per line; returns on EOF
     *
     * The in-process face, for a session that wants no process boundary (tests, debug).
     */
    int serve();

    /**
     * \brief The product face: serve()'s protocol, every request run in its own child process
     *
     * Owns the channel, the wall clock and the verdict (design.md §4); itself runs no engine code,
     * so a crashing engine cannot take the session down.
     */
    int supervise();

    /**
     * \brief The child face: one request, one reply, exit
     *
     * With a block name the request is read from and the reply written to that shared mapping
     * (supervise fills it before starting us); without one it falls back to stdin/stdout.
     * Internal: `mcp-cli` on the CLI.
     */
    int oneshot(const std::string& _block_name = std::string());

} // namespace mcp

#endif // ALXRUN_MCP_H
