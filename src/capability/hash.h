// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_HASH_H
#define ALXRUN_CAPABILITY_HASH_H

#include "capability.h"

namespace capability {

    /// $hash_sha256 -- one algorithm, no type parameter. AScript's `hash_file(path, type)` took a
    /// third unguessable enum; the file variant lives in the fs module instead.
    void hash_sha256(alx::script::fwrap& _fw);

    const table& hash_table();

} // namespace capability

#endif // ALXRUN_CAPABILITY_HASH_H
