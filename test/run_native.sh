#!/usr/bin/env bash
# Host-compile and run the freestanding SDK tests. No PlatformIO / Arduino needed.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-c++}"
OUT="$(mktemp -d)/spd_tests"
$CXX -std=c++17 -Wall -Wextra -Wpedantic -O2 test/test_native/test_main.cpp -o "$OUT"
"$OUT"
