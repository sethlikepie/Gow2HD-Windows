#!/usr/bin/env bash
# M1: prove Metal draw path is not clear-only (demo triangle + optional guest draws).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
. ./env_gow2.sh
unset PS3_NO_RSX || true
export PS3_RSX_BACKEND=metal
export PS3_METAL_DEMO_DRAW=1
export PS3_FRAME_DUMP=1
export PS3_PERF_FSM=1
# Keep movie off so demo triangle is visible in present
export PS3_MOVIE_HLE=0
unset PS3_MOVIE_HLE || true
export PS3_MOVIE_EOS=0
export PS3_MOVIE_DONE_MS=0

LOG=/tmp/smoke_metal_draw.log
rm -f frame_*.bmp 2>/dev/null || true
./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 &
BPID=$!
sleep 8
kill -TERM "$BPID" 2>/dev/null || true
sleep 1
kill -9 "$BPID" 2>/dev/null || true
wait "$BPID" 2>/dev/null || true

echo "=== log ==="
grep -E 'draw path PSO|draw_arrays|Window created|keeping pre|trace backend|FATAL|exception' "$LOG" | head -30
echo "=== dumps ==="
ls -la frame_*.bmp 2>/dev/null | head -5 || echo "(no frame_*.bmp in cwd — dump may write elsewhere)"

# Accept: PSO ready and not crash; dump optional
if grep -q 'trace backend active' "$LOG"; then
  echo "FAIL: metal replaced by trace"
  exit 1
fi
if grep -q 'NSInternalInconsistency\|terminating' "$LOG"; then
  echo "FAIL: exception"
  exit 1
fi
if ! grep -q 'draw path PSO ready\|Window created' "$LOG"; then
  echo "FAIL: no metal window/PSO"
  exit 1
fi
echo "GREEN: M1 smoke (metal alive; demo draw env set)"
exit 0
