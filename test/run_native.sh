#!/usr/bin/env bash
# Run the host unit tests (GoogleTest via PlatformIO).
#   ./test/run_native.sh                     # env "native"
#   ./test/run_native.sh -e native-san       # any pio test args
# Needs PlatformIO (`pip install platformio` or `pipx install platformio`).
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "$#" -eq 0 ]; then
  exec pio test -e native
fi
exec pio test "$@"
