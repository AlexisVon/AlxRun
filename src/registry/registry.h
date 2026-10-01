// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_REGISTRY_H
#define ALXRUN_REGISTRY_H

#include "ajson.h"
#include "ascript.h"

#include <string>

/// The enabled capability modules are the single source of truth (design.md §6): every surface --
/// the CLI's --capabilities, the MCP tool, the resident list in `run`'s description -- derives from
/// their form tables.
namespace registry {

    /// Register every enabled capability (doc-only forms are engine builtins: skipped).
    void install(alx::script::engine* _e);

    /// The document: {decl: [assertions], ...}, decl being "name(p: type): ret"; a key starting
    /// with "_" is metadata.
    const alx::json_object& document();

    /// The same document, filtered by _q (a clan, a name or a substring).
    alx::json_object document(const std::string& _q);

    /// What an argument-less query answers with: the clan names and how to ask, instead of the
    /// whole doc (~10 KB). A caller that omits the name wants a slice; this is the map.
    alx::json_object index();

    /// The clan names joined, for the capabilities tool description: the model reads the handles
    /// before spending a call, and they come from the table the query uses.
    std::string clans_line();

    /// The resident listing: one line per module, types and returns stripped (design.md §6).
    std::string brief();

    /// The resident language example that rides along with brief().
    std::string demo();

    /// The JSON the demo expects the host to bind (design.md §7); the test uses it.
    std::string demo_data();

} // namespace registry

#endif // ALXRUN_REGISTRY_H
