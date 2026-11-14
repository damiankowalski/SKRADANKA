#!/bin/sh
# Runs the unit tests and the headless scenes on macOS (the counterpart of Tools\Test.bat).
# Usage: Tools/Test.sh [debug|release]   (default: debug). Extra arguments go to ctest, e.g. -L gpu.
set -e
preset="${1:-debug}"
[ $# -gt 0 ] && shift
cd "$(dirname "$0")/.."
ctest --preset "macos-$preset" "$@"
