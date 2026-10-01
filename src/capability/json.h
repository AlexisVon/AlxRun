// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_JSON_H
#define ALXRUN_CAPABILITY_JSON_H

#include "capability.h"

namespace capability {

    /// $json_* -- the three JSON capabilities; renamed from the AScript originals ($io_fmjs /
    /// $io_tojs), which were unguessable (prelude.md §2.6).
    void json_parse(alx::script::fwrap& _fw);
    void json_str(alx::script::fwrap& _fw);
    void json_compact(alx::script::fwrap& _fw);

    const table& json_table();

} // namespace capability

#endif // ALXRUN_CAPABILITY_JSON_H
