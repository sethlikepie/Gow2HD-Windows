#!/usr/bin/env bash
# Viewer de janela Metal (Kratos a rodar, TEXTURADO). macOS/arm64. Requer: brew install sdl2
# Inclui o loader NATIVO (psarc_read + wad_assets): aceita um .psarc directo.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
clang -fobjc-arc -std=c17 -O2 -I"$HERE/.." $(pkg-config --cflags sdl2) \
    "$HERE/mesh_spin_tex.m" "$HERE/psarc_read.c" "$HERE/wad_assets.c" \
    -framework Metal -framework Foundation -framework QuartzCore \
    $(pkg-config --libs sdl2) -lz \
    -o /tmp/mesh_spin_tex
echo "built /tmp/mesh_spin_tex"
