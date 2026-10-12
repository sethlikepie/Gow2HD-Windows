#!/bin/bash
# Builds and runs the launcher handoff check. Usage: run.sh [out_dir]
set -euo pipefail
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
ROOT="$(cd "$SRC/../../../.." && pwd)"
OUT="${1:-${TMPDIR:-/tmp}/launch_check}"
mkdir -p "$OUT"
cc -std=c11 -I"$ROOT/libs/video" "$SRC"/tests/launch_check/overlay_env_probe.c \
    "$ROOT"/libs/video/rsx_overlay_settings.c -o "$OUT/overlay_env_probe"
swiftc -swift-version 5 -target arm64-apple-macos14.0 \
    "$SRC"/Backend.swift "$SRC"/Settings.swift "$SRC"/OverlaySettingsFile.swift \
    "$SRC"/tests/launch_check/main.swift -o "$OUT/launch_check"
"$OUT/launch_check" "$OUT/overlay_env_probe"
