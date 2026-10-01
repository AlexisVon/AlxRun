// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_STR_H
#define ALXRUN_CAPABILITY_STR_H

#include "capability.h"

namespace capability {

    /// $str_* -- string operations (15).
    void str_size(alx::script::fwrap& _fw);
    void str_find(alx::script::fwrap& _fw);
    void str_rfind(alx::script::fwrap& _fw);
    void str_left(alx::script::fwrap& _fw);
    void str_right(alx::script::fwrap& _fw);
    void str_mid(alx::script::fwrap& _fw);
    void str_split(alx::script::fwrap& _fw);
    void str_join(alx::script::fwrap& _fw);
    void str_replace(alx::script::fwrap& _fw);
    void str_upper(alx::script::fwrap& _fw);
    void str_lower(alx::script::fwrap& _fw);
    void str_trim(alx::script::fwrap& _fw);
    void str_starts(alx::script::fwrap& _fw);
    void str_ends(alx::script::fwrap& _fw);
    void str_contains(alx::script::fwrap& _fw);

    const table& str_table();

} // namespace capability

#endif // ALXRUN_CAPABILITY_STR_H
