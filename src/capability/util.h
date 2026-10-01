// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_UTIL_H
#define ALXRUN_CAPABILITY_UTIL_H

#include "capability.h"

namespace capability {

    /// The drawer (design.md §6): names that carry their own root instead of a module prefix, so
    /// this is a facet (`--capabilities util`) rather than a family. Nothing converts between a
    /// number and a time: a lone int does not say whether it is seconds or µs, UTC or local, and
    /// guessing that is the silent kind of wrong.

    /// $timestamp -- one clock read, int64 µs since the epoch. µs and not ns: the value travels
    /// through JSON, and a client that reads it as a double is exact only below 2^53.
    void timestamp(alx::script::fwrap& _fw);

    /// $datetime -- the whole local map of one instant: {year, month, day, week, h, m, s, ms, us, stamp_us}.
    /// One call, one clock read -- taking the date and the time separately can straddle midnight.
    void datetime(alx::script::fwrap& _fw);

    /// $date / $time -- its projections, so a reader pays only for the keys it reads.
    void date(alx::script::fwrap& _fw);
    void time(alx::script::fwrap& _fw);

    /// $rand -- (a, b): two ints give an int in [a, b], any float gives a float in [a, b): the type
    /// is the bracket. a > b is a named error, never a silent swap. No seed -- the sequence is not
    /// meant to be reproduced, so an assertion pins the properties and not the numbers.
    void rand(alx::script::fwrap& _fw);

    const table& util_table();

} // namespace capability

#endif // ALXRUN_CAPABILITY_UTIL_H
