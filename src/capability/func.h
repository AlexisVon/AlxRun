// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_FUNC_H
#define ALXRUN_CAPABILITY_FUNC_H

#include "capability.h"

namespace capability {

    /// The function family -- doc-only forms (impl = nullptr): how a function is defined, and what
    /// the C/JS priors get wrong (no hoisting, strict arity, a name that collides with a variable).
    const table& func_table();

} // namespace capability

#endif // ALXRUN_CAPABILITY_FUNC_H
