# AlxRun

**Extend what your machine can really compute, within limits you control.**

A model will happily write code for a computation, but without somewhere to run it the answer is
guesswork: past a few steps of mental arithmetic the numbers drift, and "looks right" cannot be
checked on the spot. The model is not calculating -- it is estimating, or recalling.

AlxRun is that somewhere, and it is small enough to actually authorize: one static binary that
gives **Claude Code** and **Codex CLI** a `run` tool for **AScript** -- C-like, no shell, no
network, no subprocesses, capabilities closed at compile time. A crashing, hanging or runaway
script dies in its own process, never your session.

## Quickstart

**Grab a release** -- Linux x86-64, static; nothing to install but the binary:

```bash
# from github.com/AlexisVon/AlxRun/releases (verify against the .sha256 beside the tarball)
tar -xzf alxrun-0.1.0-linux-x64.tar.gz && cd alxrun-0.1.0-linux-x64

# Register as an MCP tool. Each client it finds gets its own plan and its own confirmation --
# Claude Code, and Codex CLI when it is on PATH. No Bash rules are written.
./alxrun install -g
#    -g additionally writes exactly two allow rules (run + capabilities, never a wildcard: a
#    wildcard re-prompts per tool, and these two are the one review).

# Run something directly (the debug face).
./alxrun -e 'return rows + "!";' --data '{"rows": "hi"}'
# -> [ret] "hi!"
```

**Or build from source** -- CMake >= 3.19 and a C++17 compiler; the engine library (AlxLib) is
linked statically and its binaries are not in this repository, so unpack its
[release package](https://github.com/AlexisVon/AlxLib/releases/tag/v1.0.2) into `lib/alxlib/` first:

```bash
mkdir -p build && cd build && cmake .. && make -j7 && cd ..
# the binary is bin/alxrun -- use it in place of ./alxrun below
```

The installer auto-detects and wires up **Claude Code** and **Codex CLI** -- each with its own
plan and its own confirmation. The MCP face itself is plain stdio MCP: protocol `2025-11-25`,
with the older handshake revisions back to `2024-11-05` accepted, so any MCP client can drive
the binary.

After `install`, a computation is one ordinary tool call:

```
mcp__alxrun__run({"code": "...", "data": {...}})
```

The model looks up what it can use instead of memorizing it:

```bash
./alxrun --brief                  # the capability listing (resident in run's description)
./alxrun --demo                   # the language example (resident too)
./alxrun --capabilities 'str_'    # doc by family or key substring, with runnable examples
```

## Why not "one Bash rule"

Intuition says one narrow Bash rule would do the same job. `Bash(python:*)` cannot be added
(Python can `os.system`); `Bash(alxrun:*)` looks safe, because the capability set is closed at
compile time.

**On the current client that argument does not hold. Measured (Claude Code v2.1.274):**

```
rule:    --allowedTools "Bash(./axc-cli:*)"
command: ./axc-cli -e '...' ; echo "hi!"
result:  the whole line runs with no approval -- echo included
```

=> **`Bash(X:*)` authorizes not "program X" but "any command line that starts with the string X".**
That rule actually grants an unrestricted shell. (The client's docs say it splits on `&&` `||` `;`
`|` and matches each subcommand -- measured on issue #94314 to not split; deny rules are bypassed
the same way, #36637 / PR #36645.)

| | shell layer? | does the promise hold? |
|---|---|---|
| `Bash(alxrun:*)` | yes | **no** -- the rule covers more than the user thinks |
| `mcp__alxrun__run({"code": ...})` | **no** | yes -- **there is no seam** |

On the MCP path `code` is a JSON argument -- **the notion of a "command line" does not exist**;
any number of `;` in it are just AScript statements. This is not a different route, it is the same
principle one layer up: where the capability layer uses "does not exist" instead of "validated",
so does the calling layer.

=> **`install` registers MCP only; it writes no Bash rule.** The pitch is not "no review needed"
but "**reviewed once, then never looked at again**" -- and that "once" is one MCP tool
authorization, not a Bash rule.

## How a run is isolated

- **The MCP face is a parent and a child.** Every request runs in its own child process, with a
  64 MB shared block for the request, the log and the reply, destroyed once answered. A crashing,
  hanging or memory-hungry engine kills only the child; the parent turns that into one `isError`
  result, carrying the log the child left. The parent parses no JSON and touches no variant -- it
  is the half that must not die.
- **One memory wall.** 512 MB of address space per run, imposed by the parent. There is no
  per-value accounting: the way in (64 MB per request line) and the way out (the reply block's
  reservation) are each judged once, at the door.
- **One time wall.** A watchdog thread watches the wall clock (a hook cannot see the time spent
  inside a capability call), with the parent's kill as the backstop. The window is per call:
  `timeout` in whole seconds, 1 s to the configured ceiling, 5 s when nothing is asked for.

## Security: three corollaries

Security is not the pitch, it is a corollary. The three are independent, each with structure or
measurement behind it:

| corollary | in one line |
|---|---|
| **Reviewed once** | what is authorized is one server's two tools (the executor -- arguments: code, data, a time window -- and a read-only query); no capability or policy switches |
| **Closed capabilities** | you have what the host registered: a dangerous operation is not blocked, it **does not exist** |
| **On-device** | a single static file, no network, no subprocesses, a memory cap -- it fits beside an app, no server needed |

Four red lines hold this in place:

- **The capability set is closed at compile time** -- nothing at run time can add one
- **Extension points can only narrow it** -- policies plug in, but no policy can make it more
  capable
- **The command line picks "what to do", never "how much can be done"** -- any flag that widens
  the capability set would void that one authorization
- **Host data never arrives by path** -- it is pre-bound (the `data` channel), so the data channel
  needs no path validation and has no TOCTOU; file access has its own wall

## The language and its capabilities

AScript is C-like: no `import`, no shell, no network, no subprocesses; file access passes one
compiled-in wall (below). Capabilities are host functions under `$` (`$str_split`, `$math_ln`,
...), plus a layer of language rules (slicing, `map{}` literals, conventions like `x[null]`).

**The capability set is closed at compile time**: the host's `set_extend` is injected at build
time and an unregistered `$xxx` is a **compile error**; a module left out of `CMakeLists.txt` is
not in the binary. Nothing at run time can add a capability.

Shipped: **87 host capabilities across 10 modules** (`str` `vec` `map` `regex` `math` `json` `io`
`fs` `hash` `util`), plus **6 engine builtins** and the language rules -- 93 capability names in
all -- documented through three table layers (`type` / `func` / `notation`).

**Docs are the material**: registration and documentation sit on the same line -- a
self-describing signature plus one runnable example, queryable with `--capabilities`, so the
model looks things up instead of memorizing an API.

## What is not here

**No network, no subprocesses, no shell, no environment variables.** They "do not exist"; they are
not "blocked".

**File access passes one wall.** Reads and writes are judged against rule sets compiled into the
binary (`src/config.h`): a root allows by prefix, a deny pattern forbids by containment anywhere
in the path -- deny wins, and a pattern that will not compile closes its direction. The default
root is the process's own directory; `{home}` and `{tmp}` are opt-in. Only regular files; a
symlink at a write target is refused; a FIFO is a named refusal, not a silent attempt to read it.

**Large artifacts have a way out.** Stream handles (`$fs_ostream` / `$fs_append` + `$io_write`,
`$fs_istream` + `$io_read`) write and read in pieces -- measured: a 640 MiB product landed in
0.17 s with no copy ever entering memory.

**Spreadsheet files (xlsx) are not supported**; a companion module (`alxcore`) may add them when
a real task calls for one.

## Build, configure, test

What a build *is* -- the resource dimensions (memory, time, block, concurrency), the module
switches and the fs rule sets -- lives in `src/config.h`; edit it and rebuild. No dimension is
passed with `-D`: a second place that can disagree is worse than no knob.

AlxLib is linked statically (a dynamic build would need `LD_LIBRARY_PATH` or an rpath, and an
environment prefix breaks the client's rule matching), so the build expects the AlxLib package
under `lib/alxlib/` -- see Quickstart. Releases cover Linux x86-64; a Windows build from source
(MSVC) is untested.

```bash
bash test/run.sh                # the gate: capability assertions + escape probes + end-to-end cases
bash test/platform/run.sh       # the process API the supervisor stands on (after an AlxLib bump)
python3 test/fuzz/asan_pass.py  # assertions + seeds + corpus through an ASan/UBSan harness
```

The gate also guards the resident material: the capability tables have an assertion floor (a row
silently dropped keeps every other assertion green -- measured: an edit replaced six rows instead
of adding to them; only a floor catches material thinning out), a family name must be a word no
key carries (or the substring pass dilutes it), and doc queries are literal, never regex.

## License

[Apache-2.0](LICENSE), Copyright (c) 2026 AlexisVon.
Third parties linked into the binary (AlxLib, PCRE2, zstd, LZ4, zlib) and their full texts are in
[THIRD-PARTY.md](THIRD-PARTY.md) and `licenses/`.

**Input responsibility**: scripts and data are supplied by the user, at the user's own
responsibility; this tool does not judge the legality of their origin, nor does it endorse any use
of the artifacts produced.
