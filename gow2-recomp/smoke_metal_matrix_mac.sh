#!/usr/bin/env bash
# M10 — matriz de smokes Metal (macOS/arm64).
#
# Corre sub-smokes curtos com PID-kill. Nao exige janela visivel para o agente;
# valida logs e (opcionalmente) dumps.
#
# Uso:
#   ./smoke_metal_matrix_mac.sh
#   SECS=6 ./smoke_metal_matrix_mac.sh
#
# Exit: 0 se todos GREEN; 1 se algum FAIL.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
SECS="${SECS:-8}"

if [ ! -x ./boot_gow2 ] || [ ! -f EBOOT.ELF ]; then
    echo "FAIL: precisa de ./boot_gow2 e EBOOT.ELF" >&2
    exit 1
fi

. ./env_gow2.sh

run_one() {
    local name="$1"
    shift
    local log
    log=$(mktemp "/tmp/m10_${name}.XXXXXX")
    # shellcheck disable=SC2068
    (
        set -a
        # caller exports
        $@
        set +a
        exec ./boot_gow2 EBOOT.ELF
    ) >"$log" 2>&1 &
    local pid=$!
    sleep "$SECS"
    kill -TERM "$pid" 2>/dev/null || true
    sleep 1
    kill -9 "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    echo "$log"
}

pass=0
fail=0
report() {
    local name="$1" ok="$2" detail="$3"
    if [ "$ok" = 1 ]; then
        echo "GREEN  $name — $detail"
        pass=$((pass + 1))
    else
        echo "FAIL   $name — $detail"
        fail=$((fail + 1))
    fi
}

# --- 1) headless: no window, SPURS path ---
LOG=$(run_one headless "export PS3_NO_RSX=1 PS3_TRACE_SPURS=1; unset PS3_RSX_BACKEND")
if grep -q '\[boot\] RSX backend=none' "$LOG" 2>/dev/null || \
   grep -q 'cellSpursInitializeWithAttribute' "$LOG"; then
    # Prefer explicit none log; accept SPURS progress if older binary
    if grep -qE 'st620|MOVIEFSM|cellGcmSys|spurs kernel' "$LOG"; then
        report headless 1 "NO_RSX + progresso guest (log=$LOG)"
    else
        report headless 1 "NO_RSX sem crash (log=$LOG)"
    fi
else
    report headless 0 "sem progresso / sem NO_RSX (log=$LOG)"
fi

# --- 2) metal boot: window created ---
LOG=$(run_one metal_boot "export PS3_RSX_BACKEND=metal PS3_MOVIE_HLE=0; unset PS3_NO_RSX")
if grep -qE '\[boot\] RSX backend=metal|Window created|device: Apple' "$LOG" && \
   ! grep -qE 'NSInternalInconsistency|trace backend active' "$LOG"; then
    report metal_boot 1 "backend=metal + janela (log=$LOG)"
else
    report metal_boot 0 "sem metal window (log=$LOG)"
fi

# --- 3) overlay HLE (M0) ---
LOG=$(run_one overlay "export PS3_RSX_BACKEND=metal PS3_MOVIE_HLE=1; unset PS3_NO_RSX")
if grep -qE 'path=iosurface|movie path=iosurface|movie frame|AVAssetReader|decode start' "$LOG"; then
    report overlay 1 "HLE/VT path vivo (log=$LOG)"
else
    report overlay 0 "sem log de movie/overlay (log=$LOG)"
fi

# --- 4) draw path (M1) ---
LOG=$(run_one draw "export PS3_RSX_BACKEND=metal PS3_METAL_DEMO_DRAW=1 PS3_MOVIE_HLE=0; unset PS3_NO_RSX")
if grep -qE 'draw path PSO ready|M6 float3 position path' "$LOG"; then
    report draw 1 "PSO/draw demo (log=$LOG)"
else
    report draw 0 "sem draw path (log=$LOG)"
fi

# --- 5) default env: sourcing env alone should set metal on Darwin ---
_saved_backend="${PS3_RSX_BACKEND-}"
_saved_norsx="${PS3_NO_RSX-}"
unset PS3_RSX_BACKEND PS3_NO_RSX
# shellcheck source=/dev/null
. ./env_gow2.sh
if [ "${PS3_RSX_BACKEND:-}" = "metal" ]; then
    report default_env 1 "env_gow2.sh → PS3_RSX_BACKEND=metal"
else
    report default_env 0 "got PS3_RSX_BACKEND=${PS3_RSX_BACKEND:-unset}"
fi
# restore caller env for any follow-up
if [ -n "${_saved_backend}" ]; then export PS3_RSX_BACKEND="$_saved_backend"; else unset PS3_RSX_BACKEND; fi
if [ -n "${_saved_norsx}" ]; then export PS3_NO_RSX="$_saved_norsx"; else unset PS3_NO_RSX; fi

echo ""
echo "=== matriz M10: pass=$pass fail=$fail ==="
if [ "$fail" -gt 0 ]; then
    exit 1
fi
exit 0
