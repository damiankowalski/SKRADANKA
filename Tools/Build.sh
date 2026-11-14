#!/bin/sh
# Configures and builds Penumbra on macOS (the counterpart of Tools\Build.bat).
# Usage: Tools/Build.sh [debug|release]   (default: debug). Output: build/<config>/bin.
# Needs Xcode's command line tools, CMake, Ninja and the LunarG Vulkan SDK (docs/macOS.md).
set -e
preset="${1:-debug}"
cd "$(dirname "$0")/.."
cmake --preset "macos-$preset"
cmake --build --preset "macos-$preset"
