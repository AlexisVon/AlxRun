// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_FS_H
#define ALXRUN_CAPABILITY_FS_H

#include "capability.h"

namespace capability {

    /// $fs_read -- whole-file read, no handle, no range (design.md §6: open->read->close, so there is
    /// no write path to forget). Behind its own switch: with it off, this capability is not in the
    /// binary at all.
    void fs_read(alx::script::fwrap& _fw);

    /// $fs_sha256 -- hashes a file in 64 KB chunks, so a large file never lands in memory at once
    /// (the whole difference from $fs_read + $hash_sha256). No type parameter: AScript's enum of
    /// three algorithms is exactly the unguessable-enum shape we avoid.
    void fs_sha256(alx::script::fwrap& _fw);

    /// $fs_grep -- the lines a pattern matches, streamed: a file bigger than the memory wall is
    /// still searchable. `limit` caps the result (0 or absent = no cap, as with $io_read's len),
    /// `start` skips that many matches first -- together they page over the whole file. No line
    /// numbers; same door as $fs_read.
    void fs_grep(alx::script::fwrap& _fw);

    /// $fs_write -- one whole text file, created or replaced, never appended (design.md §6); returns
    /// the bytes written. Behind its own switch: a read-only assembly ships without it, so the
    /// ability is absent rather than merely refused.
    void fs_write(alx::script::fwrap& _fw);

    /// $fs_istream / $fs_ostream / $fs_append -- the same door as the whole-file pair, then the file
    /// stays open until the script drops the handle (overwrite, `delete h`, or the run ending: the
    /// engine closes it -- measured, design.md §6). Three names rather than one mode string: a
    /// switch in a signature is what the gate refuses, and a string meaning the same is the same
    /// switch. Their two operations ($io_read / $io_write) live in io.h -- the prefix is the module.
    void fs_istream(alx::script::fwrap& _fw);
    void fs_ostream(alx::script::fwrap& _fw);
    void fs_append(alx::script::fwrap& _fw);

    /// The wall as this process resolved it, one resident line. It exists only when the module
    /// does: with both switches off there is no function to call (the caller's `#if` follows).
    std::string fs_rules();

    const table& fs_table();

} // namespace capability

#endif // ALXRUN_CAPABILITY_FS_H
