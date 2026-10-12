#!/usr/bin/env bash
# Build the Track B capture-replay harness (macOS/Metal, arm64).
# Mac sibling of build_replay.cmd (Windows/D3D12). Run from this directory:
#   ./build_replay_mac.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

clang -fobjc-arc -std=c17 -O2 \
    -I "$HERE/../../../include" \
    "$HERE/replay_mac.m" "$HERE/../rsx_dispatch.c" \
    -framework Metal -framework Foundation -framework CoreGraphics \
    -o "$HERE/replay_mac"

echo "built $HERE/replay_mac"
