// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_MAP_H
#define ALXRUN_CAPABILITY_MAP_H

#include "capability.h"

namespace capability {

    /// $map_* -- map operations (6). $map_insert / $map_remove are not here: `m[k] = v` and
    /// `delete m[k]` are language-level and already say it once.
    void map_keys(alx::script::fwrap& _fw);
    void map_values(alx::script::fwrap& _fw);
    void map_has(alx::script::fwrap& _fw);
    void map_get(alx::script::fwrap& _fw);
    void map_size(alx::script::fwrap& _fw);
    void map_merge(alx::script::fwrap& _fw);

    const table& map_table();

} // namespace capability

#endif // ALXRUN_CAPABILITY_MAP_H
