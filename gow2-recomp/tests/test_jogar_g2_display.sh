#!/bin/bash
# jogar_g2.sh + env_gow2.sh -> fullscreen/VSync the runtime would choose.
# Runs the real scripts in a scratch game dir whose `g2play` is the settings
# probe (rsx_overlay_settings_load + rsx_overlay_resolve_flag, the same
# precedence rsx_metal_backend_init uses). Usage: test_jogar_g2_display.sh [out_dir]
# G2_SCRIPTS_DIR overrides where jogar_g2.sh/env_gow2.sh are taken from.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SCRIPTS="${G2_SCRIPTS_DIR:-$HERE/..}"
OUT="${1:-${TMPDIR:-/tmp}/jogar_g2_display}"
mkdir -p "$OUT"
cc -std=c11 -I"$ROOT/libs/video" \
    "$ROOT/games/gow2/launcher/macos/tests/launch_check/overlay_env_probe.c" \
    "$ROOT/libs/video/rsx_overlay_settings.c" -o "$OUT/overlay_env_probe"

GAME="$(mktemp -d "$OUT/game.XXXXXX")"
trap 'rm -rf "$GAME"' EXIT
cp "$SCRIPTS/jogar_g2.sh" "$SCRIPTS/env_gow2.sh" "$GAME/"
cp "$OUT/overlay_env_probe" "$GAME/g2play"
FAILS=0

settings_file() { # home
    if [ "$(uname -s)" = Darwin ]; then
        printf '%s' "$1/Library/Application Support/ps3recomp/gow2/runtime-overlay.settings"
    else
        printf '%s' "$1/.config/ps3recomp/gow2/runtime-overlay.settings"
    fi
}

# run <name> <expected "fullscreen=N vsync=N"> <home> [VAR=value...]
run() {
    local name="$1" want="$2" home="$3"
    shift 3
    local log="$GAME/$name.log" got
    env -i HOME="$home" PATH=/usr/bin:/bin TMPDIR="${TMPDIR:-/tmp}" JOGAR_LOG="$log" "$@" \
        bash "$GAME/jogar_g2.sh" > /dev/null
    got="$(cat "$log")"
    if [ "$got" = "$want" ]; then
        echo "[ OK ] $name: $got"
    else
        echo "[FAIL] $name: got '$got', want '$want'"
        FAILS=$((FAILS + 1))
    fi
}

write_file() { # home contents
    local f
    f="$(settings_file "$1")"
    mkdir -p "$(dirname "$f")"
    printf '%b' "$2" > "$f"
}

# 1. No settings file, no env: fullscreen by default (seeded), VSync default on.
H="$GAME/home1"; mkdir -p "$H"
run no_file_default "fullscreen=1 vsync=1" "$H"
if ! grep -q "^fullscreen=1$" "$(settings_file "$H")" 2>/dev/null; then
    echo "[FAIL] no_file_default: settings file not seeded"; FAILS=$((FAILS + 1))
fi

# 2. File saved by the shell (window, VSync off), no env: the file wins.
H="$GAME/home2"
write_file "$H" 'version=2\nfullscreen=0\nvsync=0\n'
run file_wins "fullscreen=0 vsync=0" "$H"
if [ "$(cat "$(settings_file "$H")")" != "$(printf 'version=2\nfullscreen=0\nvsync=0')" ]; then
    echo "[FAIL] file_wins: settings file was modified"; FAILS=$((FAILS + 1))
fi

# 3. Same file, caller exports both: the caller wins.
run caller_wins "fullscreen=1 vsync=1" "$H" PS3_FULLSCREEN=1 PS3_METAL_VSYNC=1
H3="$GAME/home3"
write_file "$H3" 'version=2\nfullscreen=1\nvsync=1\n'
run caller_wins_off "fullscreen=0 vsync=0" "$H3" PS3_FULLSCREEN=0 PS3_METAL_VSYNC=0

# 4. File without a `fullscreen` key (no trailing newline): seeded to 1, the
#    rest of the file (vsync=0) still applies.
H="$GAME/home4"
write_file "$H" 'version=2\nvsync=0'
run missing_key_seeded "fullscreen=1 vsync=0" "$H"
if [ "$(grep -c '^fullscreen=1$' "$(settings_file "$H")")" != 1 ] ||
   ! grep -q '^vsync=0$' "$(settings_file "$H")"; then
    echo "[FAIL] missing_key_seeded: bad seeded file"; FAILS=$((FAILS + 1))
fi

# 5. PS3_OVERLAY_SETTINGS names the file: that one is seeded and read.
H="$GAME/home5"; mkdir -p "$H"
CUSTOM="$GAME/custom/overlay.settings"
run custom_path "fullscreen=1 vsync=1" "$H" PS3_OVERLAY_SETTINGS="$CUSTOM"
if ! grep -q "^fullscreen=1$" "$CUSTOM" 2>/dev/null || [ -e "$(settings_file "$H")" ]; then
    echo "[FAIL] custom_path: wrong file seeded"; FAILS=$((FAILS + 1))
fi

# 6. env_gow2.sh alone never forces VSync, and keeps a caller's value.
got="$(env -i PATH=/usr/bin:/bin bash -c 'set -euo pipefail; . "$1"; echo "${PS3_METAL_VSYNC-unset}"' _ "$GAME/env_gow2.sh")"
[ "$got" = unset ] && echo "[ OK ] env_no_vsync: unset" || { echo "[FAIL] env_no_vsync: $got"; FAILS=$((FAILS + 1)); }
got="$(env -i PATH=/usr/bin:/bin PS3_METAL_VSYNC=0 bash -c 'set -euo pipefail; . "$1"; echo "$PS3_METAL_VSYNC"' _ "$GAME/env_gow2.sh")"
[ "$got" = 0 ] && echo "[ OK ] env_keeps_vsync: 0" || { echo "[FAIL] env_keeps_vsync: $got"; FAILS=$((FAILS + 1)); }

if [ "$FAILS" -ne 0 ]; then
    echo "test_jogar_g2_display: $FAILS failure(s)"
    exit 1
fi
echo "test_jogar_g2_display: all passed"
