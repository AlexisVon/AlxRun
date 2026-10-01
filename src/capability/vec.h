// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_VEC_H
#define ALXRUN_CAPABILITY_VEC_H

#include "capability.h"

namespace capability {

    /// $vec_* -- vec operations (11). In-place ones mutate the caller's vec and return nothing.
    /// $vec_remove is not here: `delete v[i]` already covers it, and returns whether it removed.
    void vec_size(alx::script::fwrap& _fw);
    void vec_push(alx::script::fwrap& _fw);
    void vec_find(alx::script::fwrap& _fw);
    void vec_has(alx::script::fwrap& _fw);
    void vec_insert(alx::script::fwrap& _fw);
    void vec_sort(alx::script::fwrap& _fw);
    void vec_sort_desc(alx::script::fwrap& _fw);
    void vec_reverse(alx::script::fwrap& _fw);
    void vec_merge(alx::script::fwrap& _fw);
    void vec_max(alx::script::fwrap& _fw);
    void vec_min(alx::script::fwrap& _fw);

    const table& vec_table();

} // namespace capability

#endif // ALXRUN_CAPABILITY_VEC_H
