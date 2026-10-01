// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_TYPE_H
#define ALXRUN_CAPABILITY_TYPE_H

#include "capability.h"

namespace capability {

    /// The type family -- engine builtins, doc-only forms (impl = nullptr). They exist in the
    /// engine whether or not we register anything, and the engine cannot enumerate them, so
    /// writing them down is the only way the model learns they exist.
    const table& type_table();

    /// The resident language demo -- shipped in `run`'s description, and asserted to run
    /// (prelude.md §2.7: material buys round trips back).
    const char* demo();

    /// The data the demo expects the host to bind (design.md §7: pre-binding first).
    const char* demo_data();

} // namespace capability

#endif // ALXRUN_CAPABILITY_TYPE_H
