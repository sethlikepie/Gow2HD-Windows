#!/bin/sh
# Builda e roda o teste do deswizzle na GPU (byte-exato vs CPU). macOS/arm64.
set -e
cd "$(dirname "$0")"
clang -fobjc-arc -std=c17 -O2 -I.. -I../.. \
    test_gpu_deswizzle.m -framework Metal -framework Foundation \
    -o /tmp/test_gpu_deswizzle
/tmp/test_gpu_deswizzle
