# Third-party components and licenses (THIRD-PARTY)

AlxRun itself is Apache-2.0 — `Copyright (c) 2026 AlexisVon`, full text in `LICENSE`. This file
lists the third-party components that **actually enter the deliverables**, and where their full
texts live.

The deliverable is the single static binary, `bin/alxrun`; the texts it must carry ship in this
repo's `licenses/` (five files).

## 1 · Link-time (five)

Measured with `nm bin/alxrun`: every one below is present there as defined symbols (`pcre2_*`,
`ZSTD_*`, `LZ4_*`, `deflate` / `inflate` — re-run the command for counts, which move with the
version):

| Library | Version | License | Full text |
|---|---|---|---|
| AlxLib | 1.0.2 | MIT | `licenses/alxlib.txt` |
| PCRE2 | 10.48 | BSD-3-Clause **WITH** PCRE2-exception | `licenses/pcre2.txt` |
| zstd | 1.5.7 | BSD-3-Clause (GPL-2 offered as an alternative) | `licenses/zstd.txt` |
| LZ4 | 1.10.0 | BSD-2-Clause | `licenses/lz4.txt` |
| zlib | 1.3.2 | zlib | `licenses/zlib.txt` |

All five arrive through the vendored AlxLib archive (`lib/alxlib/`): AlxLib is built on them, and
static linking pulls in only the members this binary calls. No upstream source is patched here, so
the "mark changed files" obligation is not triggered.

**Provenance**: AlxLib is vendored as its release package at tag `v1.0.2`
(`github.com/AlexisVon/AlxLib`); the package name carries the commit sha it was built from.

**Nested, and not in the binary** (measured — zero symbols): zstd's `lib/dictBuilder/divsufsort`
(MIT) — no `divsufsort` symbol in `bin/alxrun`; PCRE2's JIT backend sljit (BSD-2-Clause) — this
AlxLib builds PCRE2 with JIT off and `pcre2_jit_compile` is never called.

## 2 · In the dependency tree, not linked (three)

`nm bin/alxrun` carries zero symbols of these; they exist in the vendored AlxLib package's other
archives, which this binary does not link:

| Library | License | Note |
|---|---|---|
| SQLite3 3.50.4 | public domain | |
| OpenSSL 3.6.3 | Apache-2.0 | enters only through AlxLib's alxcomm (TLS), which this build does not pull; a downstream linking it into a GPLv2-only product can rebuild AlxLib with `ALXCOMM_TLS=OFF` |
| libjpeg-turbo 3.1.90 | IJG + BSD-3-Clause + zlib | nested libspng (BSD-2-Clause) travels with it. With no `jpeg_*` / `png_*` symbols here, the IJG acknowledgment sentence is **not** required for this binary |

## 3 · Shipping

A release is the binary plus `LICENSE`, this file and `licenses/` — nothing else from this repo
travels with it. Everything the repo carries under `lib/alxlib/` (`include/`, `doc/`, `README.md`)
is AlxLib itself, MIT, covered by `licenses/alxlib.txt`. No upstream ships a `NOTICE` file, so no
notice file travels with a release.
