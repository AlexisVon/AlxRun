#!/bin/bash
# Copyright (c) 2026 AlexisVon
# SPDX-License-Identifier: Apache-2.0

# test/fuzz/auto-fuzz.sh <seconds> -- start a campaign the way it has to be started.
#
# run.sh is the device; this is the wrapper that makes a long run safe to leave alone:
#   - the whole campaign sits in a user scope with a memory cap, so going over kills the unit
#     (a clean OOM, libFuzzer's own accounting keeps the workers under it) instead of the machine
#   - everything is tee'd into build-fuzz/logs/fuzz-<MMDDHHMMSS>.log -- the log is the campaign's
#     evidence, and the name carries the time so no two runs can overwrite each other
#
# The only argument is the wall clock in seconds.
set -eu

SECONDS_TO_RUN=${1:-}
if [ -z "$SECONDS_TO_RUN" ]; then
    echo "usage: auto-fuzz.sh <seconds>" >&2
    exit 2
fi

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORKERS=7
# Workers x run.sh's 1024 MB per worker, plus room for the parent and the harness itself.
MEM_MAX=8G
MEM_SWAP_MAX=1G
LOG=$ROOT/build-fuzz/logs/fuzz-$(date +%m%d%H%M%S).log
mkdir -p "$ROOT/build-fuzz/logs"

echo "== scope: MemoryMax=$MEM_MAX MemorySwapMax=$MEM_SWAP_MAX | workers: $WORKERS | log: $LOG"
systemd-run --user --scope -p MemoryMax=$MEM_MAX -p MemorySwapMax=$MEM_SWAP_MAX \
    bash "$ROOT/test/fuzz/run.sh" "$SECONDS_TO_RUN" "$WORKERS" 2>&1 | tee "$LOG"
exit "${PIPESTATUS[0]}"
