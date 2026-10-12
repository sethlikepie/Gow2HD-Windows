#!/usr/bin/env bash
# Viewer de janela Metal (Kratos a rodar). macOS/arm64. Requer: brew install sdl2
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
clang -fobjc-arc -std=c17 -O2 $(pkg-config --cflags sdl2) \
    "$HERE/mesh_spin.m" \
    -framework Metal -framework Foundation -framework QuartzCore \
    $(pkg-config --libs sdl2) \
    -o /tmp/mesh_spin
echo "built /tmp/mesh_spin"
