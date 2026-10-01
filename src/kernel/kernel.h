// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_KERNEL_H
#define ALXRUN_KERNEL_H

#include "ajson.h"
#include "ascript.h"
#include "config.h"

#include <string>

namespace kernel {

    /// Lower bound of a caller-supplied window, in whole seconds.
    constexpr uint32_t kMinSeconds = 1;
    /// Window an unasked run gets, min'd with the deployment's ceiling (config::kMaxSeconds).
    constexpr uint32_t kDefaultSeconds = config::kMaxSeconds < 5 ? config::kMaxSeconds : 5;

    /**
     * \brief A caller-supplied wall window, whole seconds: digits only, 1 .. the ceiling
     *
     * One rule for both faces -- the MCP parent peeks it out of the request, the CLI takes it off
     * argv -- so a window means the same thing wherever it is written.
     *
     * \param _text Window as text
     * \param _seconds Out: the parsed window; untouched when _text is refused
     * \return True when _text holds a window in [kMinSeconds, config::kMaxSeconds]
     */
    inline bool parse_wall_seconds(const std::string& _text, uint32_t& _seconds) {
        if (_text.empty() || _text.size() > 9) return false; // nine digits already overrun the ceiling
        uint32_t n = 0;
        for (char c : _text) {
            if (c < '0' || c > '9') return false;
            n = n * 10 + static_cast<uint32_t>(c - '0');
        }
        if (n < kMinSeconds || n > static_cast<uint32_t>(config::kMaxSeconds)) return false;
        _seconds = n;
        return true;
    }

    /// One script run: collected output, diagnostics, return value and error class.
    struct result {
        /// pipe_out -- what the script printed
        std::string output;
        /// on_cerr / on_cmpl -- engine diagnostics
        std::string diag;
        /// exec return value; null when has_value is false
        alx::json_value value;
        bool has_value = false;
        alx::script::error_type error = alx::script::error_type::NoError;
    };

    /**
     * \brief Position tracking (error traces carry file:row:col), on by default
     *
     * Off buys ~33% on compute-bound loops. Takes effect before the first run -- the engine is
     * built lazily.
     */
    void set_debug(bool _on);

    /// Where printed bytes go as they are printed; _size 0 means a pure flush.
    using out_fn = void (*)(void* _ud, const char* _data, size_t _size);

    /**
     * \brief Install the print sink
     *
     * Null (the default) accumulates bytes into result.output; the MCP child points this at the
     * shared block. The callback must not throw.
     */
    void set_output(void* _ud, out_fn _fn);

    /**
     * \brief Whether this process applies the address-space cap to itself
     *
     * The CLI does; the MCP child must not -- the cap is inherited, so applying it there would
     * tighten the parent's grant (measured: 576 MB back to 512).
     */
    void set_own_wall(bool _on);

    /**
     * \brief The watchdog's window for the next run, seconds (1 .. config::kMaxSeconds)
     *
     * The MCP child is told by its parent; the other faces leave the default. Out of range is
     * ignored.
     */
    void set_wall_seconds(uint32_t _seconds);

    /**
     * \brief A run's address-space allowance, bytes (0 = unlimited, fuzz builds)
     *
     * The MCP supervisor grants each child this much plus the block it lends it -- scaffolding,
     * not script memory; the CLI has no parent and applies it to itself.
     */
    size_t memory_limit_bytes();

    /**
     * \brief One script run: bind, exec, collect
     *
     * Binds _data as root variables, execs _code, collects output / diagnostics / value. Both
     * inputs are released as soon as the engine owns its copy. The address-space cap is flat
     * (doc/issue.md I-4): the allocation is refused, so a hungry script ends as a clean
     * MemoryError, and nothing outside a run is capped.
     *
     * \param _code Script text
     * \param _data Root variables to bind
     * \param _home_dir Anchors relative imports ("" = none)
     */
    result run(std::string& _code, alx::json_object& _data,
               const std::string& _home_dir = std::string());

    /// Destroy the engine; call before exit, never while a run is in flight.
    void shutdown();

} // namespace kernel

#endif // ALXRUN_KERNEL_H
