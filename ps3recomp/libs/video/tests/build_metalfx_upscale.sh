#!/bin/sh
# Builda e roda o teste do upscaler MetalFX (dims + preservacao de conteudo).
# macOS/arm64, requer SDK com MetalFX (macOS 13+).
set -e
cd "$(dirname "$0")"
clang -fobjc-arc -std=c17 -O2 -mmacosx-version-min=13.0 -I.. -I../.. \
    test_metalfx_upscale.m \
    -framework Metal -framework MetalFX -framework Foundation \
    -o /tmp/test_metalfx_upscale
/tmp/test_metalfx_upscale
