#!/bin/bash
# Copyright (c) 2026 AlexisVon
# SPDX-License-Identifier: Apache-2.0

# test/run.sh [alxrun-path] -- capability checks.
# No framework: python does the JSON work, this is just the door.
set -u
cd "$(dirname "$0")/.."
exec python3 test/check.py "${1:-./bin/alxrun}"
