#!/bin/bash
# Copyright (c) 2026 AlexisVon
# SPDX-License-Identifier: Apache-2.0

# test/fuzz/run.sh [seconds] [workers] [rss_mb] -- build and run the fuzz harness.
# test/fuzz/run.sh --build-only -- build it and stop (the ASan net, asan_pass.py, comes in here).
#
# Two builds come out of this, neither of which touches bin/alxrun:
#   1. an AlxLib built with clang + ASan/UBSan -- the engine is the target, so it
#      has to be instrumented too; its outputs land in build-fuzz/alxlib-build/bin/
#   2. alxrun_fuzz itself, in build-fuzz/harness/ (the harness adds the fuzzer runtime)
#
# Both go up from scratch on every invocation, and a build output is removed before its build
# starts: a campaign is a long run, and a stale object is a silently wrong verdict. Cached
# configure variables are the trap -- AlxLib's version numbers are `set(... CACHE STRING ...)`, so a
# build dir that outlives a package swap keeps the old ones (measured 2026-09-20: a v3.3.11 tree
# still wrote libalxscpt.so.2.1.9). Minutes per run is the price for never being wrong that way.
#
# A crash lands in build-fuzz/artifacts/crash-*; it reached the real kernel through a script, so
# grade it before calling it a bug.
set -eu

BUILD_ONLY=0
if [ "${1:-}" = "--build-only" ]; then BUILD_ONLY=1; shift; fi

SECONDS_TO_RUN=${1:-3600}
WORKERS=${2:-6}
# Per worker. Keep workers x rss under the memory the campaign is allowed: past it the *cgroup*
# OOM kills the whole unit, and libFuzzer never gets to drop just the growing unit (measured
# 2026-09-19: 7 x 4096 on an 8 GB scope died at 6m33s, oom-kill, no crash).
RSS_MB=${3:-1024}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ALXLIB_SRC=${ALXLIB_SRC:-$HOME/Code/AlxLib}
BUILD=$ROOT/build-fuzz
# Build outputs sit in directories of their own (wiping them is part of every run, so it must not
# be able to reach the campaign's state next to them: corpus/ is its memory, artifacts/ its
# evidence).
ALXLIB_BUILD=$BUILD/alxlib-build
ALXLIB_OUT=$ALXLIB_BUILD/bin
ALXLIB_FUZZ=$ALXLIB_OUT/libalxlib.a
HARNESS_BUILD=$BUILD/harness
HARNESS=$HARNESS_BUILD/alxrun_fuzz

# Everything above stays under build-fuzz/ (gitignored): ALXLIB_BIN_DIR sends every AlxLib output
# -- archives, shared objects, .debug -- here, so none of it is left in the AlxLib tree. A tree
# that predates that knob would ignore the -D (CMake only warns about unused ones) and write into
# its own bin/ instead, so refuse before anything is built.
grep -q "ALXLIB_BIN_DIR" "$ALXLIB_SRC/CMakeLists.txt" || {
    echo "!! $ALXLIB_SRC/CMakeLists.txt has no ALXLIB_BIN_DIR — building would write into its own bin/." >&2
    echo "!! Refusing. See AlxLib CMakeLists.txt (output directories)" >&2
    exit 1
}

echo "== AlxLib: clang + asan/ubsan, from scratch (a few minutes)"
rm -rf "$ALXLIB_BUILD"
cmake -S "$ALXLIB_SRC" -B "$ALXLIB_BUILD" \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DALXLIB_BIN_DIR="$ALXLIB_OUT" \
    -DALXBASE_STATIC=ON -DALXCORE_STATIC=ON -DALXSCPT_STATIC=ON \
    -DALXCOMM_STATIC=ON -DALXLIB_STATIC=ON -DALXCOMM_TLS=ON \
    -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF \
    -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -O1 -g"
cmake --build "$ALXLIB_BUILD" -j7
[ -f "$ALXLIB_FUZZ" ] || {
    echo "!! no archive at $ALXLIB_FUZZ — the build went somewhere else" >&2
    exit 1
}
ENGINE=$(git -C "$ALXLIB_SRC" rev-parse --short HEAD 2>/dev/null || echo '?')
[ -n "$(git -C "$ALXLIB_SRC" status --porcelain -- source include 2>/dev/null)" ] &&
    ENGINE="$ENGINE+dirty" || true
# The vendored side's revision rides in its package name (AlxLib-<ver>-g<sha>), which is what
# makes the two sides comparable at a glance. The tarball is gitignored, so this can come up empty.
VENDORED=$(basename "$(ls "$ROOT"/lib/alxlib/AlxLib-*-linux-x64*.tar.gz 2>/dev/null | grep -v debug | tail -1)")
echo "== engine: $ALXLIB_SRC @ $ENGINE — vendored: ${VENDORED:-?} (AlxRun $(git -C "$ROOT" log -1 --format=%h -- lib/alxlib))"

echo "== harness: alxrun_fuzz (same kernel, same capability set as bin/alxrun)"
rm -rf "$HARNESS_BUILD"
cmake -S "$ROOT" -B "$HARNESS_BUILD" \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DALXRUN_FUZZ=ON -DALXRUN_FUZZ_ALXLIB="$ALXLIB_FUZZ" >/dev/null
cmake --build "$HARNESS_BUILD" -j7 --target alxrun_fuzz

if [ "$BUILD_ONLY" = 1 ]; then
    echo "== --build-only: $HARNESS"
    exit 0
fi

# Seeds come from the repo; the fuzzer's own additions never go back into agent/ (they are
# mutated, not authored).
# Two directories on purpose: libFuzzer writes new units into the first and reads the rest.
# SEEDS is rebuilt from the repo every run (a stale seed would outlive its source); CORPUS
# persists, because it is the campaign's memory of what has been explored across sessions.
CORPUS=$BUILD/corpus
SEEDS=$BUILD/seeds
rm -rf "$SEEDS"
mkdir -p "$CORPUS" "$SEEDS"
cp "$ROOT"/agent/*.axc "$SEEDS"/ 2>/dev/null || true
# Every call the tool has actually served (agent/usage/runnable). These are only *hoped* to be
# clean, and a crasher takes a job down during the initial pass -- so each is vetted, and a
# crasher is named and left out (that name is a finding, not noise).
REAL_SEEDS=0
for f in "$ROOT"/agent/usage/runnable/*.axc; do
    [ -f "$f" ] || continue
    if timeout 25 "$HARNESS" "$f" >/dev/null 2>&1; then
        cp "$f" "$SEEDS"/ && REAL_SEEDS=$((REAL_SEEDS + 1))
    else
        echo "-- real-usage ${f##*/}: CRASHES or STALLS — kept out of the seeds (grade it before calling it a bug)"
    fi
done
echo "== real-usage seeds vetted in: $REAL_SEEDS"
# Authored coverage seeds (applied problems, one per capability cluster) -- agent/ is session
# evidence and must keep meaning that, so hand-written seeds live apart from it.
cp "$ROOT"/test/fuzz/seeds/*.axc "$SEEDS"/ 2>/dev/null || true
for sol in "$ROOT"/cases/*/.judge/solution.axc; do
    [ -f "$sol" ] || continue
    cp "$sol" "$SEEDS/case-$(basename "$(dirname "$(dirname "$sol")")").axc"
done
# Engine bugs this harness already found, one file per issue. They must keep crashing until
# AlxLib fixes them -- a fixed one reports "no longer crashes" here, and that is the signal to
# close the issue and drop the file. They stay OUT of the corpus: a seed that crashes kills a
# job during the initial pass, before any fuzzing happens (measured).
for f in "$ROOT"/test/fuzz/regressions/*.axc; do
    [ -f "$f" ] || continue
    if "$HARNESS" "$f" >/dev/null 2>&1; then
        echo "-- ${f##*/}: NO LONGER CRASHES — close its AlxLib issue, then delete this file"
    else
        echo "-- ${f##*/}: still crashes (a known engine bug -- tracked upstream in AlxLib)"
    fi
done
echo "== seeds: $(ls "$SEEDS" | wc -l) | working corpus: $(ls "$CORPUS" | wc -l)"

ARTIFACTS=$BUILD/artifacts
mkdir -p "$ARTIFACTS"
echo "== fuzzing ${SECONDS_TO_RUN}s on $WORKERS workers, ${RSS_MB} MB each — artifacts in $ARTIFACTS"
# -fork, not -jobs: only fork mode survives a crash in a child (-ignore_crashes is a fork-mode
# flag). Without it the engine's known crash kills every job within seconds of starting.
# cwd is the repo root: a seed that reads its data by the repo-relative path it was written with
# must find it (measured 2026-09-20: from artifacts/, all 20 agent seeds and most case solutions
# died on line 3 with "fs_read … no such file" and covered nothing past it).
cd "$ROOT" && "$HARNESS" "$CORPUS" "$SEEDS" \
    -dict="$ROOT/test/fuzz/alxrun.dict" \
    -fork="$WORKERS" -ignore_crashes=1 -ignore_ooms=1 -ignore_timeouts=1 \
    -max_total_time="$SECONDS_TO_RUN" \
    -max_len=65536 -timeout=20 -rss_limit_mb="$RSS_MB" \
    -artifact_prefix="$ARTIFACTS/" -print_final_stats=1

echo "== crashes: $(find "$ARTIFACTS" -name 'crash-*' -o -name 'timeout-*' -o -name 'leak-*' | wc -l)"
echo "== reproduce one: $HARNESS <artifact>   (or: ./bin/alxrun -e \"\$(cat <artifact>)\")"
