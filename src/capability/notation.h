// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_NOTATION_H
#define ALXRUN_NOTATION_H

#include "capability.h"

namespace capability {

    /// The language's notation -- doc-only (impl = nullptr, origin stays ""). Keys carry no
    /// parens, so the doc and the brief read them as rules rather than as callables.
    const table& notation_table();

} // namespace capability

#endif // ALXRUN_NOTATION_H
