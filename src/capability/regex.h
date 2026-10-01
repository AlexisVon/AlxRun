// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_REGEX_H
#define ALXRUN_CAPABILITY_REGEX_H

#include "capability.h"

namespace capability {

    /// $regex_* -- regex operations (9). Renamed from AScript's `regx_` (design.md §6: `regex` is
    /// the spelling the corpus uses, `regx` is not).
    void regex_valid(alx::script::fwrap& _fw);
    void regex_match(alx::script::fwrap& _fw);
    void regex_find(alx::script::fwrap& _fw);
    void regex_find_all(alx::script::fwrap& _fw);
    void regex_group(alx::script::fwrap& _fw);
    void regex_group_all(alx::script::fwrap& _fw);
    void regex_replace(alx::script::fwrap& _fw);
    void regex_replace_all(alx::script::fwrap& _fw);
    void regex_split(alx::script::fwrap& _fw);

    const table& regex_table();

} // namespace capability

#endif // ALXRUN_CAPABILITY_REGEX_H
