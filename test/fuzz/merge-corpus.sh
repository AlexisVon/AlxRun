#!/bin/bash
# Copyright (c) 2026 AlexisVon
# SPDX-License-Identifier: Apache-2.0

# test/fuzz/merge-corpus.sh -- shrink the campaign corpus to a coverage-equivalent subset, then
# archive what it replaces. Nothing is ever deleted: the corpus is the campaign's memory.
#
# A campaign's corpus is a union across forked workers, so it keeps units the fuzzer's own working
# set has already deduped. Merging runs every unit through one process, so it is a
# run-after-a-long-campaign chore (an hour for ~19k units), never part of a run.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$ROOT/build-fuzz
CORPUS=$BUILD/corpus
MERGED=$BUILD/corpus-merged
ARCHIVE=$BUILD/corpus-archive
HARNESS=$BUILD/harness/alxrun_fuzz

[ -x "$HARNESS" ] || {
    echo "!! no harness at $HARNESS — test/fuzz/run.sh --build-only builds it" >&2
    exit 1
}

BEFORE=$(ls "$CORPUS" | wc -l)
echo "== merging $BEFORE units (single-threaded: minutes to an hour)"
rm -rf "$MERGED"
mkdir -p "$MERGED"
# A merge that dies (a big allocation, a kill) leaves a partial set, and swapping that in would
# lose coverage -- so the swap only happens if this exits 0.
"$HARNESS" -merge=1 -rss_limit_mb=8192 -timeout=20 "$MERGED" "$CORPUS"
AFTER=$(ls "$MERGED" | wc -l)

STAMP=$(date +%Y-%m-%d-%H%M)
mkdir -p "$ARCHIVE/$STAMP-$BEFORE"
mv "$CORPUS" "$ARCHIVE/$STAMP-$BEFORE/corpus"
mv "$MERGED" "$CORPUS"
echo "== $BEFORE → $AFTER units; the old set is archived at $ARCHIVE/$STAMP-$BEFORE/"
