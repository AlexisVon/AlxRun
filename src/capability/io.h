// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_IO_H
#define ALXRUN_CAPABILITY_IO_H

#include "capability.h"

namespace capability {

    /// $io_print -- everything else the engine exposes under `io` is stdin (would eat protocol
    /// messages) or JSON (its own module).
    void io_print(alx::script::fwrap& _fw);

    /// $io_read / $io_write -- the two operations of a stream handle. No path here: the wall
    /// judged it when the handle was opened ($fs_istream / $fs_ostream / $fs_append).
    void io_read(alx::script::fwrap& _fw);
    void io_write(alx::script::fwrap& _fw);

    const table& io_table();

} // namespace capability

#endif // ALXRUN_CAPABILITY_IO_H
