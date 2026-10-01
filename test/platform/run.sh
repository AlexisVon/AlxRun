#!/bin/bash
# Copyright (c) 2026 AlexisVon
# SPDX-License-Identifier: Apache-2.0

# test/platform/run.sh -- build and run the aplatform acceptance (test/platform/check.cpp).
#
# Run it after an AlxLib bump, next to test/fuzz/asan_pass.py: the process API is the floor the
# MCP supervisor stands on, and every promise it makes is cheaper to re-check here than to
# rediscover through a crashed session.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$ROOT/build-platform
mkdir -p "$OUT"

# Same compile shape as the product build: C++17, the module dirs on the include path (that is
# how CMakeLists lists them), the vendored archive, no extensions.
INC=""
for m in alxbase alxcore alxscpt alxcomm; do
    INC="$INC -I $ROOT/lib/alxlib/include/$m"
done

g++ -std=c++17 -O1 -Wall -Wextra $INC \
    "$ROOT/test/platform/check.cpp" \
    "$ROOT/lib/alxlib/lib/libalxlib.a" \
    -lpthread -ldl -lm \
    -o "$OUT/platform_check"

cd "$ROOT" && "$OUT/platform_check"
