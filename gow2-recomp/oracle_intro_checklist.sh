#!/usr/bin/env bash
# Oracle session helper — GoW2 intro / media open wall
#
# Runs boot_gow2 with FS-relevant traces, parses the log into the 10-row
# checklist (RPCS3 column left blank for you to fill), and prints next steps.
#
# Full theory: ../ps3recomp/docs/RPCS3_AS_REFERENCE.md
# Plan:        ../ps3recomp/docs/superpowers/plans/2026-07-20-macos-intro-audio-open-wall.md
#
# Usage:
#   ./oracle_intro_checklist.sh              # 25s default
#   ./oracle_intro_checklist.sh 40           # longer
#   ./oracle_intro_checklist.sh 25 /path/to/RPCS3.log   # attach RPCS3 log hints
#
# Output:
#   oracle_out/recomp_<ts>.log
#   oracle_out/checklist_<ts>.md
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

SECS="${1:-25}"
RPCS3_LOG="${2:-}"

PS3="${PS3:-$HERE/../ps3recomp}"
PARSER="$PS3/tools/oracle_parse_recomp_log.py"
OUT_DIR="$HERE/oracle_out"
mkdir -p "$OUT_DIR"
TS="$(date +%Y%m%d_%H%M%S)"
LOG="$OUT_DIR/recomp_${TS}.log"
MD="$OUT_DIR/checklist_${TS}.md"

if [ ! -x ./boot_gow2 ]; then
    echo "FAIL: ./boot_gow2 missing — run ./build_macos.sh first" >&2
    exit 1
fi
if [ ! -f EBOOT.ELF ]; then
    echo "FAIL: EBOOT.ELF missing" >&2
    exit 1
fi
if [ ! -f "$PARSER" ]; then
    echo "FAIL: parser not found at $PARSER" >&2
    exit 1
fi

# Env: intro / movie path (do not force NOMOVIES)
# shellcheck source=/dev/null
. "$HERE/env_gow2.sh"
export PS3_NO_RSX="${PS3_NO_RSX:-1}"          # CPU/I/O focus; set 0 for window
export PS3_TRACE_SPURS="${PS3_TRACE_SPURS:-1}"
export PS3_MOVIE_IO="${PS3_MOVIE_IO:-1}"
export PS3_MOVIE_HLE="${PS3_MOVIE_HLE:-1}"
export PS3_NOMOVIES="${PS3_NOMOVIES:-0}"
# Intro probes (patches already on recomp_macos_v2; gated no-ops if missing):
export PS3_TRACE_MOVIEOBJ="${PS3_TRACE_MOVIEOBJ:-1}"
export PS3_TRACE_ASSET="${PS3_TRACE_ASSET:-1}"
export PS3_TRACE_FIOSOPEN="${PS3_TRACE_FIOSOPEN:-1}"
export PS3_TRACE_SNDOPEN="${PS3_TRACE_SNDOPEN:-1}"
# Optional extra probes:
if [ -n "${PS3_ORACLE_FULL_PROBES:-}" ]; then
    export PS3_TRACE_INTROSEQ=1
    export PS3_TRACE_FIOSSCHED=1
fi

echo "[oracle] secs=$SECS log=$LOG"
echo "[oracle] VFS=$PS3_VFS_ROOT"
echo "[oracle] MOVIE_CACHE=$PS3_MOVIE_CACHE"
echo "[oracle] running boot_gow2 …"

./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 &
PID=$!
sleep "$SECS"
if kill -0 "$PID" 2>/dev/null; then
    kill -9 "$PID" 2>/dev/null || true
fi
wait "$PID" 2>/dev/null || true

PYTHON="${PYTHON:-python3}"
if [ -x "$PS3/.venv/bin/python" ]; then
    PYTHON="$PS3/.venv/bin/python"
fi

"$PYTHON" "$PARSER" "$LOG" --markdown -o "$MD"

# Append session header + RPCS3 instructions
{
    echo ""
    echo "---"
    echo ""
    echo "## Session meta"
    echo ""
    echo "| field | value |"
    echo "|-------|-------|"
    echo "| date | $(date -Iseconds) |"
    echo "| secs | $SECS |"
    echo "| host | $(uname -m) $(uname -s) |"
    echo "| boot | \`boot_gow2\` |"
    echo "| recomp log | \`$LOG\` |"
    echo "| movie_cache exists | $([ -d "$PS3_MOVIE_CACHE" ] && echo yes || echo NO) |"
    if [ -d "$PS3_MOVIE_CACHE" ]; then
        echo "| SmLogo wav in cache | $(ls "$PS3_MOVIE_CACHE" 2>/dev/null | grep -qi smlogo && echo yes || echo no) |"
    fi
    echo ""
    echo "## RPCS3 column — how to fill"
    echo ""
    echo "1. Install [RPCS3](https://github.com/RPCS3/rpcs3) (same game dump / EBOOT)."
    echo "2. Log filters high for: \`sys_fs\`, \`cellFs\`, \`cellAudio\`, \`cellVdec\`, \`cellSpurs\`."
    echo "3. Boot only through the intro logo hang; save \`RPCS3.log\`."
    echo "4. Extract lines with \`open\`, \`SmLogo\`, \`smlogo\`, \`.wav\`, \`.m2v\`."
    echo "5. Paste summaries into the **RPCS3 (fill)** column of the table above."
    echo "6. Pick **one** gap → one fix → re-run this script."
    echo ""
    echo "Sparse source map (read-only):"
    echo '```'
    echo "rpcs3/Emu/Cell/lv2/sys_fs.cpp"
    echo "rpcs3/Emu/Cell/Modules/cellFs.cpp"
    echo "rpcs3/Emu/Cell/Modules/cellAudio*.cpp"
    echo "rpcs3/Emu/Cell/Modules/cellVdec.cpp"
    echo "rpcs3/Emu/Cell/Modules/cellSpurs.cpp"
    echo '```'
    echo ""
    echo "**Forbidden:** host write of st620 / obj+0x744 / fake EOS without a real done producer."
    echo "See: \`../ps3recomp/docs/RPCS3_AS_REFERENCE.md\`"
    echo ""
    if [ -n "$RPCS3_LOG" ] && [ -f "$RPCS3_LOG" ]; then
        echo "## RPCS3 log snippet (auto-grep)"
        echo ""
        echo "Source: \`$RPCS3_LOG\`"
        echo ""
        echo '```'
        grep -iE 'open|smlogo|sm_logo|\.wav|\.m2v|cellFs|sys_fs|snd_stream|vdec|spurs' \
            "$RPCS3_LOG" 2>/dev/null | head -80 || true
        echo '```'
        echo ""
    else
        echo "## RPCS3 log"
        echo ""
        echo "Optional: pass path as 2nd arg to auto-grep:"
        echo "  ./oracle_intro_checklist.sh 25 /path/to/RPCS3.log"
        echo ""
    fi
} >>"$MD"

echo ""
echo "[oracle] wrote $MD"
echo "[oracle] recomp log: $LOG"
echo ""
# Short human summary on stdout
"$PYTHON" "$PARSER" "$LOG" --markdown 2>/dev/null | head -40
echo "..."
echo "Full checklist: $MD"
echo ""
echo "Next: fill RPCS3 column → one gap → fix → re-run."
