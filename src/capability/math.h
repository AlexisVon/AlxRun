// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_MATH_H
#define ALXRUN_CAPABILITY_MATH_H

#include "capability.h"

namespace capability {

    /// $math_* -- 27 numeric functions (design.md §6: all of them; the language has no power
    /// operator, no abs, no min/max, so none of these is redundant). Not shipped: $math_PI /
    /// $math_E -- those are engine *defines*, not functions, and the form table carries functions.
    void math_abs(alx::script::fwrap& _fw);
    void math_sqrt(alx::script::fwrap& _fw);
    void math_cbrt(alx::script::fwrap& _fw);
    void math_exp(alx::script::fwrap& _fw);
    void math_ln(alx::script::fwrap& _fw);
    void math_log2(alx::script::fwrap& _fw);
    void math_log10(alx::script::fwrap& _fw);
    void math_sin(alx::script::fwrap& _fw);
    void math_cos(alx::script::fwrap& _fw);
    void math_tan(alx::script::fwrap& _fw);
    void math_asin(alx::script::fwrap& _fw);
    void math_acos(alx::script::fwrap& _fw);
    void math_atan(alx::script::fwrap& _fw);
    void math_floor(alx::script::fwrap& _fw);
    void math_ceil(alx::script::fwrap& _fw);
    void math_round(alx::script::fwrap& _fw);
    void math_trunc(alx::script::fwrap& _fw);
    void math_sign(alx::script::fwrap& _fw);
    void math_rad(alx::script::fwrap& _fw);
    void math_deg(alx::script::fwrap& _fw);
    void math_min(alx::script::fwrap& _fw);
    void math_max(alx::script::fwrap& _fw);
    void math_pow(alx::script::fwrap& _fw);
    void math_atan2(alx::script::fwrap& _fw);
    void math_hypot(alx::script::fwrap& _fw);
    void math_fmod(alx::script::fwrap& _fw);
    void math_clamp(alx::script::fwrap& _fw);

    const table& math_table();

} // namespace capability

#endif // ALXRUN_CAPABILITY_MATH_H
