#!/bin/sh
# Builda e roda a suite do loader nativo (fixtures sinteticas, zero dados de jogo).
# Uso: ./build_native_loader.sh [fixture_dir]   (default /tmp/native_fixtures)
set -e
cd "$(dirname "$0")"
FIXDIR="${1:-/tmp/native_fixtures}"

python3 gen_native_fixtures.py "$FIXDIR"

clang -std=c17 -O2 -Wall -Wextra -I.. \
    psarc_read.c wad_assets.c test_native_loader.c \
    -lz -o /tmp/test_native_loader

/tmp/test_native_loader "$FIXDIR"
