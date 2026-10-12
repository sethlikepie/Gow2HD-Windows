#!/usr/bin/env bash
# Build and run the RSX backend smoke test for every backend available on this
# host (phases F3 and F5 of docs/MACOS_PORT_PLAN.md).
#
# Each backend opens a window, takes an NV4097 clear through the RSX command
# processor, and must present exactly the requested colour -- verified by
# reading the dumped frame back, not by eye.
#
# Needs a window server: this will not pass over a bare SSH session.
#
# Usage: ./run_backend_smoke.sh [backend ...]     (default: all available)
# Honours $CC.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"

if [ -n "${CC:-}" ]; then
    COMPILER="$CC"
elif [ "$(uname -s)" = "Darwin" ]; then
    COMPILER="clang"
else
    COMPILER="gcc"
fi

if ! command -v pkg-config > /dev/null 2>&1; then
    echo "pkg-config not found (macOS: brew install pkg-config)" >&2
    exit 1
fi

if ! pkg-config --exists sdl2; then
    echo "SDL2 not found (macOS: brew install sdl2)" >&2
    exit 1
fi

# Regression fix (bringup-instrumentacao-fase0, Tasks 1-2 vs. this
# hand-written build path): rsx_commands.c now calls ps3_checkpoint_fire()
# (FIRST_DRAW) -- same shared-variable pattern as
# runtime/spu/tests/run_tests.sh (see the comment there). rsx_backend_smoke.c
# stubs ps3_timebase_now()/the three SNAPSHOT accessors ps3_checkpoint.c
# needs.
TRACE_CHECKPOINT_SRC=(
    "$REPO/runtime/trace/ps3_trace.c"
    "$REPO/runtime/trace/ps3_checkpoint.c"
)

SOURCES=(
    "$HERE/rsx_backend_smoke.c"
    "$HERE/../rsx_null_backend.c"
    "$HERE/../rsx_commands.c"
    "$HERE/../rsx_frame_dump.c"
    "${TRACE_CHECKPOINT_SRC[@]}"
)
EXTRA_FLAGS=()
BACKENDS=(sdl)

if [ "$(uname -s)" = "Darwin" ]; then
    SOURCES+=("$HERE/../rsx_metal_backend.m")
    EXTRA_FLAGS+=(-DRSX_SMOKE_HAVE_METAL -fobjc-arc
                  -framework Metal -framework QuartzCore -framework Foundation)
    BACKENDS+=(metal)
fi

# Vulkan: native on Linux, MoltenVK on macOS. Optional -- skipped when the
# loader is absent rather than failing the whole run.
if pkg-config --exists vulkan 2>/dev/null; then
    SOURCES+=("$HERE/../rsx_vulkan_backend.c")
    # shellcheck disable=SC2207
    EXTRA_FLAGS+=(-DRSX_SMOKE_HAVE_VULKAN $(pkg-config --cflags --libs vulkan))
    BACKENDS+=(vulkan)
elif [ -f /opt/homebrew/lib/libvulkan.dylib ]; then
    SOURCES+=("$HERE/../rsx_vulkan_backend.c")
    EXTRA_FLAGS+=(-DRSX_SMOKE_HAVE_VULKAN
                  -I/opt/homebrew/include -L/opt/homebrew/lib -lvulkan)
    BACKENDS+=(vulkan)
else
    echo "[smoke] Vulkan loader not found; skipping the vulkan backend"
fi

if [ "$#" -gt 0 ]; then
    BACKENDS=("$@")
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "[smoke] building with $COMPILER"
# shellcheck disable=SC2046
"$COMPILER" -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter \
    -I "$REPO/include" -I "$HERE/.." -I "$REPO/runtime/trace/tests" \
    "${SOURCES[@]}" \
    "${EXTRA_FLAGS[@]}" \
    $(pkg-config --cflags --libs sdl2) \
    -o "$WORK/rsx_backend_smoke"

# Run from the temp dir so frame dumps do not litter the source tree.
cd "$WORK"

failures=0
for backend in "${BACKENDS[@]}"; do
    echo
    echo "--- backend: $backend ---"
    if PS3_RSX_BACKEND="$backend" "$WORK/rsx_backend_smoke"; then
        :
    else
        failures=$((failures + 1))
    fi
done

echo
if [ "$failures" -eq 0 ]; then
    echo "[smoke] all backends passed: ${BACKENDS[*]}"
else
    echo "[smoke] $failures backend(s) failed"
    exit 1
fi
