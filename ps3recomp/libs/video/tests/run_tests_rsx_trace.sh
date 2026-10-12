#!/usr/bin/env bash
# Build and run the RSX subsystem trace unit test (Task 5 review fix, Minor
# #3, "bringup-instrumentacao-fase0"): FIFO_KICK/FENCE/LABEL/DRAW(x2)/
# FLIP_REQ/FLIP_DONE, gate off -> zero "[PS3T]" lines.
#
# Standalone: links the REAL libs/video/cellGcmSys.c and
# libs/video/rsx_commands.c (not a copy of their logic), driven through their
# public guest-facing entry points (cellGcmInit/cellGcmGetControlRegister/
# cellGcmSetFlipCommand) with a hand-built FIFO command buffer -- see
# test_rsx_trace.c's header comment for the full design, including exactly
# which RSX event (PRESENT) cannot be reached without a GPU/window server and
# stays in-boot-only.
#
# ps3_trace_on() resolves getenv() once per subsystem and never re-reads --
# same reason "off"/"on" run in their OWN process, same idiom as
# runtime/spu/tests/run_tests_dma_trace.sh (Task 4).
#
# Usage: ./run_tests_rsx_trace.sh [<cc-path>]
# Honra a variavel de ambiente $CC.

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"

if [ -n "${1:-}" ]; then
    CC="$1"
elif [ -n "${CC:-}" ]; then
    CC="$CC"
elif [ "$(uname -s)" = "Darwin" ]; then
    CC="clang"
else
    CC="gcc"
fi

exe="$HERE/test_rsx_trace.exe"
build_log="$HERE/build_rsx_trace.log"

if ! "$CC" -std=c11 -Wall -Wextra -Wno-unused-parameter -O1 \
        -I "$REPO/include" \
        -I "$REPO/libs/video" \
        -I "$REPO/runtime/trace/tests" \
        "$HERE/test_rsx_trace.c" \
        "$REPO/libs/video/cellGcmSys.c" \
        "$REPO/libs/video/rsx_commands.c" \
        "$REPO/runtime/trace/ps3_trace.c" \
        "$REPO/runtime/trace/ps3_checkpoint.c" \
        -o "$exe" 2> "$build_log"; then
    echo "[FAIL] build error -- see $build_log"
    exit 1
fi

pass=0
fail=0
failed_scenarios=()

# Todos os dez tokens PS3_TRACE_* de include/ps3_trace.h -- nao so
# PS3_TRACE_RSX. O cenario "off" prova que o gate RSX especificamente
# silencia as linhas RSX; se so PS3_TRACE_RSX fosse limpo aqui, um
# PS3_TRACE_CHECKPOINTS=1 sobrevivente no ambiente do caller faria
# ps3_checkpoint_fire() (FIRST_DRAW/FIRST_FLIP, chamado pelo proprio
# cellGcmSys.c/rsx_commands.c exercitados por este teste) emitir uma linha
# "[PS3T] ... | CHECKPOINTS | ..." e o cenario "off" falhar por um motivo
# que nada tem a ver com a instrumentacao RSX sob teste (review Minor #1;
# mesmo idioma multi- -u de runtime/trace/tests/run_tests_checkpoint.sh:59,
# aqui alargado aos dez).
ALL_TRACE_VARS=(
    PS3_TRACE_CHECKPOINTS PS3_TRACE_MEMORY PS3_TRACE_HEAP PS3_TRACE_THREADS
    PS3_TRACE_SPURS PS3_TRACE_DMA PS3_TRACE_FIOS PS3_TRACE_MEDIA
    PS3_TRACE_RSX PS3_TRACE_SNAPSHOT
)

# run_scenario <mode> [VAR=val ...] -- corre o binario com TODAS as
# PS3_TRACE_* explicitamente por-definir e so as VAR=val extra aplicadas.
run_scenario() {
    local mode="$1"; shift
    local out rc
    out=$(env "${ALL_TRACE_VARS[@]/#/-u}" "$@" "$exe" "$mode" 2>&1) && rc=$? || rc=$?
    if [ "$rc" -eq 0 ]; then
        echo "[PASS] $mode -- $out"
        pass=$((pass+1))
    else
        echo "[FAIL] $mode (exit $rc)"
        echo "$out" | sed 's/^/   /'
        failed_scenarios+=("$mode")
        fail=$((fail+1))
    fi
}

run_scenario off                  # PS3_TRACE_RSX desligado -> 0 linhas [PS3T]
run_scenario on  PS3_TRACE_RSX=1  # FIFO_KICK/FENCE/LABEL/DRAW(x2)/FLIP_REQ/FLIP_DONE

echo
echo "==========================================="
echo "Results: $pass passed, $fail failed"
[ $fail -eq 0 ] || { echo "Failed: ${failed_scenarios[*]}"; exit 1; }
