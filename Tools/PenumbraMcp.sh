#!/bin/sh
# Starts the Penumbra MCP server on stdin/stdout (the counterpart of Tools\PenumbraMcp.bat).
# Uses build/release if it exists, otherwise build/debug; set PENUMBRA_MCP_BUILD=debug|release to choose.
# Extra arguments are passed on, e.g. --backend headless, --port 7778, --project DemoProject.
# Nothing but the server may write to stdout here: it carries the protocol.
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$PENUMBRA_MCP_BUILD"
if [ -z "$build" ]; then
    if [ -x "$root/build/release/bin/PenumbraEditor" ]; then build=release; else build=debug; fi
fi
editor="$root/build/$build/bin/PenumbraEditor"
if [ ! -x "$editor" ]; then
    echo "[Penumbra] $editor not found: run Tools/Build.sh $build first." >&2
    exit 1
fi
exec "$editor" --mcp "$@"
