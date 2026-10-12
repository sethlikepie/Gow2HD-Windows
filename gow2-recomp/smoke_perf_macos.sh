#!/usr/bin/env bash
# smoke_perf_macos.sh -- perfil de 30s + A/B LIFT_OPT -O0 vs -O1
#
# Requisitos do utilizador (2026-07-20):
#   1. Perfilar ~30s: %CPU, tempo em pthread_mutex (via [PERF] wait/hold),
#      vm_write* rate, guest vs host (hold vs wait + process CPU).
#   2. Experiencia -O1 no lift: lines/s e se st620/sticky ainda passam.
#   3. Probes SEMPRE OFF em smokes de perf (PS3_TRACE_*=unset).
#   4. Sem Sleep(1) extra no poll (ja no runtime).
#
# Uso:
#   ./smoke_perf_macos.sh                 # 30s, binario actual (assume -O0)
#   ./smoke_perf_macos.sh 30              # segundos
#   MODE=ab ./smoke_perf_macos.sh 30      # rebuild O0 + O1 e compara
#   BIN=./boot_gow2_O1 ./smoke_perf_macos.sh 30
#
# Env:
#   SECS / $1     duracao do run (default 30)
#   MODE=single|ab   single=so o BIN actual; ab=constroi O0 e O1 e compara
#   BIN           caminho do boot (default ./boot_gow2)
#   SKIP_BUILD=1  no MODE=ab, nao rebuild (usa boot_gow2 e boot_gow2_O1)
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

SECS="${1:-${SECS:-30}}"
MODE="${MODE:-single}"
BIN="${BIN:-$HERE/boot_gow2}"
SKIP_BUILD="${SKIP_BUILD:-0}"
TMP=$(mktemp -d /tmp/gow2_perf.XXXXXX)
FAILED=0

fail() { echo "FAIL: $1"; FAILED=1; }

if [ ! -f EBOOT.ELF ]; then
    echo "FAIL: falta EBOOT.ELF" >&2
    exit 1
fi

# --- helpers ---------------------------------------------------------------

# Strip every PS3_TRACE_* so probes cannot inflate lines/s or steal CPU.
clear_probes() {
    # shellcheck disable=SC2046
    unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
    unset PS3_TRACE_MOVIEOBJ PS3_TRACE_GIANTLOCK PS3_TRACE_FIOSSCHED \
          PS3_TRACE_STG PS3_TRACE_INTROSTATE PS3_TRACE_SNDOPEN \
          PS3_TRACE_COMMITMAP PS3_TRACE_OOBRA PS3_TRACE_HOST_STACK 2>/dev/null || true
}

# Run one timed boot. Args: label binary
# Writes $TMP/$label.{log,sample,summary}
run_profile() {
    local label=$1
    local binary=$2
    local log="$TMP/${label}.log"
    local samp="$TMP/${label}.sample"
    local sum="$TMP/${label}.summary"

    if [ ! -x "$binary" ]; then
        fail "binario em falta: $binary"
        return 1
    fi

    echo
    echo "== profile $label: $binary (${SECS}s, probes OFF, PS3_PERF=1) =="
    echo "   md5=$(md5 -q "$binary" 2>/dev/null || md5sum "$binary" | awk '{print $1}')"

    (
        set -a
        # env canonico, depois limpa probes e liga so perf
        . "$HERE/env_gow2.sh"
        set +a
        clear_probes
        export PS3_NO_RSX=1
        export PS3_PERF=1
        export PS3_PERF_FSM=1
        # Nao forjar EOS / SEQDONE — medir progresso real
        unset PS3_VDEC_FORCE_SEQDONE_MS
        export PS3_MOVIE_EOS=0
        unset PS3_MOVIE_DONE_MS
        exec "$binary" EBOOT.ELF >"$log" 2>&1
    ) &
    local pid=$!

    # sample %CPU via ps every second (macOS ps %cpu is cumulative-ish; we avg)
    {
        echo "t_s cpu_pct rss_kb"
        for t in $(seq 1 "$SECS"); do
            if ! kill -0 "$pid" 2>/dev/null; then
                echo "$t 0 0"
                break
            fi
            # ps: %cpu and rss (KB)
            line=$(ps -o %cpu=,rss= -p "$pid" 2>/dev/null || echo "0 0")
            # shellcheck disable=SC2086
            set -- $line
            echo "$t ${1:-0} ${2:-0}"
            sleep 1
        done
    } >"$samp" &
    local samp_pid=$!

    sleep "$SECS"
    # Prefer SIGTERM so atexit/[PERF] signal dump runs (kill -9 skips atexit)
    if kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null || true
        # wait up to 2s for clean dump
        for _ in 1 2 3 4; do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.5
        done
        kill -9 "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
    wait "$samp_pid" 2>/dev/null || true

    # --- parse log ---
    local lines lines_s maxst sticky_pub sticky_hit sticky_con
    local perf_last w32_s acq_s wait_ms hold_ms preempt p_sleep
    lines=$(wc -l <"$log" | tr -d ' ')
    lines_s=$(awk -v s="$SECS" -v n="$lines" 'BEGIN{ if(s>0) printf "%.0f", n/s; else print 0 }')

    maxst=$(grep -aE '\[MOVIEFSM\] st620' "$log" 2>/dev/null \
        | sed -n 's/.*-> \([0-9]*\).*/\1/p' | sort -n | tail -1)
    maxst=${maxst:-0}

    sticky_pub=$(grep -ac 'STICKY-PUBLISH\|sticky_publish\|STICKY-RESTORE' "$log" 2>/dev/null || true)
    # Prefer [PERF] sticky counters from last dump
    perf_last=$(grep -a '\[PERF\]' "$log" | tail -1 || true)
    if [ -n "$perf_last" ]; then
        sticky_pub=$(echo "$perf_last" | sed -n 's/.*sticky_pub=\([0-9]*\).*/\1/p')
        sticky_hit=$(echo "$perf_last" | sed -n 's/.*sticky_hit=\([0-9]*\).*/\1/p')
        sticky_con=$(echo "$perf_last" | sed -n 's/.*sticky_con=\([0-9]*\).*/\1/p')
        w32_s=$(echo "$perf_last" | sed -n 's/.*w32\/s=\([0-9.]*\).*/\1/p')
        acq_s=$(echo "$perf_last" | sed -n 's/.*acq\/s=\([0-9.]*\).*/\1/p')
        wait_ms=$(echo "$perf_last" | sed -n 's/.*wait_ms=\([0-9.]*\).*/\1/p')
        hold_ms=$(echo "$perf_last" | sed -n 's/.*hold_ms=\([0-9.]*\).*/\1/p')
        preempt=$(echo "$perf_last" | sed -n 's/.*preempt=\([0-9]*\).*/\1/p')
        p_sleep=$(echo "$perf_last" | sed -n 's/.*p_sleep=\([0-9]*\).*/\1/p')
    fi
    sticky_pub=${sticky_pub:-0}
    sticky_hit=${sticky_hit:-0}
    sticky_con=${sticky_con:-0}
    w32_s=${w32_s:-?}
    acq_s=${acq_s:-?}
    wait_ms=${wait_ms:-?}
    hold_ms=${hold_ms:-?}
    preempt=${preempt:-?}
    p_sleep=${p_sleep:-?}

    local avg_cpu max_cpu
    avg_cpu=$(awk 'NR>1 {s+=$2; n++} END{ if(n) printf "%.1f", s/n; else print 0 }' "$samp")
    max_cpu=$(awk 'NR>1 {if($2>m) m=$2} END{ printf "%.1f", m+0 }' "$samp")

    # Guest vs host proxy: hold_ms = time holding giant lock (guest work under GIL);
    # wait_ms = time blocked on pthread_mutex. Ratio hold/(hold+wait) ~ guest fraction
    # of lock-serialized work.
    local guest_pct="?"
    if [ "$hold_ms" != "?" ] && [ "$wait_ms" != "?" ]; then
        guest_pct=$(awk -v h="$hold_ms" -v w="$wait_ms" 'BEGIN{
            t=h+w; if(t>0.1) printf "%.1f", 100*h/t; else print "n/a" }')
    fi

    {
        echo "label=$label"
        echo "bin=$binary"
        echo "secs=$SECS"
        echo "lines=$lines"
        echo "lines_per_s=$lines_s"
        echo "avg_cpu_pct=$avg_cpu"
        echo "max_cpu_pct=$max_cpu"
        echo "max_st620=$maxst"
        echo "sticky_pub=$sticky_pub sticky_hit=$sticky_hit sticky_con=$sticky_con"
        echo "perf_wait_ms=$wait_ms perf_hold_ms=$hold_ms"
        echo "perf_w32_per_s=$w32_s perf_acq_per_s=$acq_s"
        echo "perf_preempt=$preempt perf_p_sleep=$p_sleep"
        echo "guest_hold_pct=$guest_pct   # hold/(hold+wait) under giant lock"
        echo "perf_last=$perf_last"
        echo "log=$log"
    } | tee "$sum"

    # Soft gates (non-fatal for A/B discovery; hard for sticky regression if we
    # already expect sticky machinery to run — only warn).
    if ! grep -q '\[PERF\]' "$log"; then
        fail "$label: sem linhas [PERF] (PS3_PERF nao ligou?)"
    fi
    # st620>=1 expected once intro opens; sticky_pub>=1 if open completed
    echo "  max_st620=$maxst  sticky_pub=$sticky_pub  lines/s=$lines_s  avg_cpu=${avg_cpu}%"
    if [ "$maxst" -ge 3 ] 2>/dev/null; then
        echo "  PASS progress: st620>=3"
    elif [ "$maxst" -ge 1 ] 2>/dev/null; then
        echo "  WARN progress: st620=$maxst (parked at open/poll; flaky on load)"
    else
        echo "  WARN progress: st620=$maxst (intro nao abriu neste run)"
    fi
}

build_variant() {
    local opt=$1
    local out=$2
    echo
    echo "== build LIFT_OPT=$opt -> $out =="
    LIFT_OPT="$opt" HOST_OPT=-O0 OUT="$out" FORCE_REBUILD_LIFT=0 \
        ./build_macos.sh 2>&1 | tail -20
}

# --- main ------------------------------------------------------------------

case "$MODE" in
    single)
        run_profile single "$BIN"
        ;;
    ab|AB)
        O0="$HERE/boot_gow2"
        O1="$HERE/boot_gow2_O1"
        if [ "$SKIP_BUILD" != "1" ]; then
            # Ensure runtime .a is current (ppu_loader PS3_PERF etc.)
            if [ -d "$HERE/../ps3recomp/build-macos" ]; then
                echo "== cmake --build runtime (ps3recomp) =="
                cmake --build "$HERE/../ps3recomp/build-macos" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" 2>&1 | tail -15
            fi
            build_variant -O0 "$O0"
            # -O1 needs dedicated object names (.1.o); first time compiles all chunks
            build_variant -O1 "$O1"
        fi
        run_profile O0 "$O0"
        run_profile O1 "$O1"
        echo
        echo "======== A/B SUMMARY ========"
        if [ -f "$TMP/O0.summary" ] && [ -f "$TMP/O1.summary" ]; then
            echo "--- O0 ---"; cat "$TMP/O0.summary"
            echo "--- O1 ---"; cat "$TMP/O1.summary"
            l0=$(grep lines_per_s= "$TMP/O0.summary" | cut -d= -f2)
            l1=$(grep lines_per_s= "$TMP/O1.summary" | cut -d= -f2)
            s0=$(grep max_st620= "$TMP/O0.summary" | cut -d= -f2)
            s1=$(grep max_st620= "$TMP/O1.summary" | cut -d= -f2)
            sp0=$(grep sticky_pub= "$TMP/O0.summary" | awk '{print $1}' | cut -d= -f2)
            sp1=$(grep sticky_pub= "$TMP/O1.summary" | awk '{print $1}' | cut -d= -f2)
            echo
            echo "lines/s:  O0=$l0  O1=$l1  ratio=$(awk -v a="$l0" -v b="$l1" 'BEGIN{ if(a+0>0) printf "%.2fx", b/a; else print "?" }')"
            echo "st620:    O0=$s0  O1=$s1"
            echo "sticky:   O0 pub=$sp0  O1 pub=$sp1"
            if [ "${s1:-0}" -lt 1 ] && [ "${s0:-0}" -ge 1 ]; then
                fail "O1 perdeu st620 progress (O0 tinha st>=1)"
            fi
        fi
        ;;
    *)
        echo "MODE must be single|ab (got $MODE)" >&2
        exit 2
        ;;
esac

echo
echo "logs: $TMP"
if [ "$FAILED" -eq 0 ]; then
    echo "PASS: smoke_perf_macos ($MODE ${SECS}s)"
else
    echo "DONE with FAIL flags (ver acima); logs em $TMP"
fi
exit $FAILED
