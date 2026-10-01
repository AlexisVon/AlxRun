// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CONFIG_H
#define ALXRUN_CONFIG_H

/// What this binary is (design.md §6): the one config point. A switch is a macro -- it gates code
/// out of the build -- a value is a constexpr, and the build system overrides none of them.

#define ALXRUN_STR_ENABLE 1
#define ALXRUN_VEC_ENABLE 1
#define ALXRUN_MAP_ENABLE 1
#define ALXRUN_IO_ENABLE 1
#define ALXRUN_MATH_ENABLE 1
#define ALXRUN_HASH_ENABLE 1
#define ALXRUN_REGEX_ENABLE 1
#define ALXRUN_JSON_ENABLE 1
#define ALXRUN_FS_READ_ENABLE 1 // $fs_read, $fs_sha256, $fs_grep, $fs_istream, $io_read
#define ALXRUN_FS_WRITE_ENABLE 1 // $fs_write, $fs_ostream, $fs_append, $io_write
// Both off = the file facet is not in this binary at all; only one off = that direction only.
#define ALXRUN_UTIL_ENABLE 1 // $timestamp, $datetime, $date, $time, $rand
// A macro and not a value: it gates the `notation` facts that state it, so they leave with it.
#define ALXRUN_OVERFLOW_CHECK 1 // int64 arithmetic throws instead of wrapping

namespace config {

#if defined(ALXRUN_FUZZ_BUILD) // ASan needs terabytes of address space, and a fuzz run must still end
    inline constexpr int kMemMb = 0;
    inline constexpr int kMaxSeconds = 1;
#else
    inline constexpr int kMemMb = 512; // address-space wall per run, MB
    inline constexpr int kMaxSeconds = 30; // the ceiling a request may ask for (unasked gets 5)
#endif
    inline constexpr int kBlockMb = 64; // supervisor<->child block, MB (request + reply)
    inline constexpr int kMaxChildren = 2; // requests the supervisor runs at once

    /// fs rules (design.md §6): canonicalize the path, then a root allows it by prefix and a deny
    /// pattern forbids it by containment anywhere in the path; deny wins, and a pattern that will
    /// not compile closes its direction. Patterns are fragments, not globs.
    /// A root may carry the {cwd} (start dir), {home} (user home) or {tmp} ($TMPDIR, else /tmp)
    /// prefix; only {cwd} is a default -- {home} is where credentials live, {tmp} is shared with
    /// every other process. A root that does not resolve is dropped, with a line on stderr.
    inline constexpr const char* const kFsReadRoots[] = {"{cwd}"};
    inline constexpr const char* const kFsReadDeny[] = {
        R"(/\.git/)", R"(/\.env)", R"(/id_rsa)", R"(\.pem$)"};
    inline constexpr const char* const kFsWriteRoots[] = {"{cwd}"};
    inline constexpr const char* const kFsWriteDeny[] = {
        R"(/\.git/)", R"(/\.env)", R"(/id_rsa)", R"(\.pem$)"};

} // namespace config

#endif // ALXRUN_CONFIG_H
