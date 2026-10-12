#!/bin/bash
# watch_run.sh -- run a boot script and kill the process as soon as a stop criterion shows in the log.
# Usage: ./watch_run.sh <run_script> <log> [idle_s=8] [deadline_s=120]
# Stop criteria (first wins):
#   - "[ICALL-BAD]" whose host_ra names func_002F0AAC (the E385 GCM-callback crash)
#   - "[ACC30-CYCLE]" dump (registry-walk cycle detector, E387)
#   - idle: no new TYPE15SHELL post#N and no new WADLD-SM tick for idle_s seconds, armed after the first loader tick (needs PS3_TRACE_TYMAP=1)
#   - deadline
# Prints the reason and elapsed seconds. Diagnostic tooling only; touches nothing in the runtime.
RUN="$1"; LOG="$2"; IDLE="${3:-8}"; DL="${4:-120}"
[ -x "$RUN" ] || { echo "run script not executable: $RUN"; exit 2; }
pkill -9 -f boot_gow2_arena 2>/dev/null; sleep 1
( "$RUN" > "$LOG" 2>&1 & echo $! > "$LOG.pid" ); PID=$(cat "$LOG.pid"); t0=$(date +%s)
last_sig=""; last_change=$t0; reason="deadline"
while :; do
  sleep 1; now=$(date +%s); el=$((now-t0))
  if ! kill -0 "$PID" 2>/dev/null; then reason="process exited"; break; fi
  if grep -q "ICALL-BAD" "$LOG" && grep -q "host_ra.*002F0AAC" "$LOG"; then reason="ICALL-BAD in func_002F0AAC"; break; fi
  if grep -q "ACC30-CYCLE" "$LOG"; then sleep 1; reason="ACC30-CYCLE dump"; break; fi
  sig="$(grep -oE 'post#[0-9]+' "$LOG" | tail -1)|$(grep -c 'WADLD-SM' "$LOG")"
  if [ "$sig" != "$last_sig" ]; then last_sig="$sig"; last_change=$now; fi
  if [ "$(grep -c 'WADLD-SM' "$LOG")" -ge 1 ] && [ $((now-last_change)) -ge "$IDLE" ]; then reason="idle ${IDLE}s after $sig"; break; fi
  [ "$el" -ge "$DL" ] && break
done
kill -TERM "$PID" 2>/dev/null; sleep 1; kill -9 "$PID" 2>/dev/null
echo "[watch_run] stop: $reason  elapsed=${el}s  shells=$(grep -oE 'post#[0-9]+' "$LOG" | tail -1) ticks=$(grep -c WADLD-SM "$LOG")"
