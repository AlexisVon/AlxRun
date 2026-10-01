# Contributing

Thanks for wanting to help. This is a small project with a strict bar: correctness here is
measured, not asserted -- so most of the rules below keep it measurable.

## Reporting bugs

Open an issue with what you ran, what you expected, and what happened. A reproducer reduced to one
`./bin/alxrun -e '...'` line is worth a page of description.

## Building

```bash
# The engine library (AlxLib) is linked statically and its binaries are not in this repository:
# unpack an AlxLib release package into lib/alxlib/ first (github.com/AlexisVon/AlxLib).
mkdir -p build && cd build && cmake .. && make -j7
```

Needs CMake >= 3.19 and a C++17 compiler.

## The gate must be green

```bash
bash test/run.sh
```

runs every capability assertion, the escape probes and the end-to-end cases -- zero tokens, about
a minute. A pull request that leaves it red is not looked at beyond the failure. Two more devices
exist but only run on an AlxLib bump: `bash test/platform/run.sh` and
`python3 test/fuzz/asan_pass.py`.

## Style

- C++17, GCC/Clang.
- Comments in English, and only where the code cannot say it itself -- one crucial line. If it
  needs an essay, the naming or the structure is what wants fixing.
- Match the surrounding style: the tree is `clang-format`-clean under the config shipped in
  `.clang-format`.
- A capability row is a `form` table entry and a runnable `ex` assertion in the same line; add
  both, and raise the assertion floor in `test/check.py` in the same change.

## Sign your commits (DCO)

This project uses the Developer Certificate of Origin (see `DCO`) instead of a CLA. Sign every
commit off:

```bash
git commit -s
```

which appends a line like:

```
Signed-off-by: Your Name <you@example.com>
```

Signing off certifies the statements in `DCO` -- briefly: the contribution is yours to submit,
under this project's license (Apache-2.0, inbound = outbound).
