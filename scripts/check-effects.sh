#!/bin/sh
# Validates every effect the same way the binary assembles it: prelude + effect + postlude.
set -e
cd "$(dirname "$0")/.."
status=0
for effect in effects/*.glsl "$@"; do
    if cat src/prelude.glsl "$effect" src/postlude.glsl | glslang --stdin -S frag > /tmp/glsl-check.log 2>&1; then
        echo "ok    $effect"
    else
        echo "FAIL  $effect"; cat /tmp/glsl-check.log; status=1
    fi
done
exit $status
