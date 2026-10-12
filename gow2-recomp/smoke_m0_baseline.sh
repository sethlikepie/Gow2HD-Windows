#!/usr/bin/env bash
# Smoke M0 do RDY-0: 6 runs de 25 s, recipe do braco M0 do counters_pre.tsv.
# Uso: smoke_m0.sh <BINARIO>            (ex.: ./boot_gow2_v3)
# Kill SEMPRE por PID (TERM depois -9). NUNCA pkill -f boot_gow2.
set -uo pipefail
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp || exit 1
BIN="${1:-./boot_gow2_v3}"
[ -x "$BIN" ] || { echo "binario inexistente: $BIN"; exit 2; }
TAG="$(basename "$BIN")"
OK=0

for r in 1 2 3 4 5 6; do
  LOG="/tmp/smoke_${TAG}_run$r.log"
  (
    set -a; . ./env_gow2.sh; set +a
    unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
    export PS3_NO_RSX=1 PS3_PERF_FSM=1 PS3_MOVIE_EOS=0
    unset PS3_MOVIE_DONE_MS PS3_VDEC_FORCE_SEQDONE_MS
    exec "$BIN" EBOOT.ELF
  ) > "$LOG" 2>&1 &
  BPID=$!
  sleep 25
  kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null

  # Formato real: "[MOVIEFSM] st620 <de> -> <para>". O estado e' o lado DIREITO.
  # 4294967295 (0xFFFFFFFF) e' o "sem estado" inicial do sampler, nao um valor.
  ST=$(grep -oE '\[MOVIEFSM\] st620 [0-9]+ -> [0-9]+' "$LOG" 2>/dev/null \
        | awk '{print $NF}' | grep -v '^4294967295$' | sort -n | tail -1)
  ST=${ST:-0}
  FN=$(grep -oE '\[boot\] [0-9]+ lifted functions' "$LOG" 2>/dev/null | grep -oE '[0-9]+' | head -1)
  IM=$(grep -oE '\[imp\].*' "$LOG" 2>/dev/null | head -1)
  [ "$ST" -ge 3 ] 2>/dev/null && OK=$((OK+1))
  printf "run%d  st620_max=%-4s lifted=%-7s %s\n" "$r" "$ST" "${FN:-?}" "${IM:-}"
done

echo "-----"
echo "st620>=3 em $OK de 6   (gate: >=4; baseline2: 6)"
echo "orfaos pos-run: $(pgrep -f "$TAG" | wc -l | tr -d ' ')"
