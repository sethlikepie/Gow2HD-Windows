#!/usr/bin/env bash
# Build and run the DMA subsystem trace unit test (Task 4 do plano
# "bringup-instrumentacao-fase0"): GET/PUT/LIST/TAG_WAIT/DONE do motor MFC,
# campos ea=/lsa=/size=/tag=, gate off -> zero bytes.
#
# Standalone: compila runtime/trace/ps3_trace.c (Task 1) + este ficheiro, que
# fornece o proprio stub de ps3_timebase_now() (mesmo motivo de
# runtime/trace/tests/test_ps3_mem_trace.c -- ver o cabecalho de
# test_dma_trace_main.c). Define SPU_DMA_TRACE_HOOKS=1 na linha de compilacao
# para activar os PS3_TRACE(...) atras do gate de compilacao em
# runtime/spu/spu_dma.h (ver o comentario grande la para o motivo do gate: os
# outros ~8 testes SPU que incluem esse header directamente NAO definem essa
# macro, por isso continuam a compilar sem ligar ps3_trace.c -- este runner
# NAO faz parte de runtime/spu/tests/run_tests.sh por isso mesmo).
#
# ps3_trace_on() resolve getenv() uma vez por subsistema e nunca mais re-le
# -- por isso "off"/"on" correm cada uma no SEU PROPRIO processo, mesmo
# principio de run_tests_mem_trace.sh (Task 3).
#
# Usage: ./run_tests_dma_trace.sh [<cc-path>]
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

exe="$HERE/test_dma_trace.exe"
build_log="$HERE/build_dma_trace.log"

if ! "$CC" -std=c11 -Wall -Wextra -O0 \
        -DSPU_DMA_TRACE_HOOKS=1 \
        -I "$REPO/include" \
        -I "$REPO/runtime/spu" \
        -I "$HERE" \
        "$REPO/runtime/trace/ps3_trace.c" \
        "$HERE/test_dma_trace_main.c" \
        -o "$exe" 2> "$build_log"; then
    echo "[FAIL] build error -- see $build_log"
    exit 1
fi

pass=0
fail=0
failed_scenarios=()

# run_scenario <mode> [VAR=val ...] -- corre o binario com PS3_TRACE_DMA
# explicitamente por-definir e so as VAR=val extra aplicadas.
run_scenario() {
    local mode="$1"; shift
    local out rc
    out=$(env -u PS3_TRACE_DMA "$@" "$exe" "$mode" 2>&1) && rc=$? || rc=$?
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

run_scenario off                  # PS3_TRACE_DMA desligado -> 0 bytes
run_scenario on  PS3_TRACE_DMA=1   # GET/PUT/LIST/TAG_WAIT/DONE com campos corretos

echo
echo "==========================================="
echo "Results: $pass passed, $fail failed"
[ $fail -eq 0 ] || { echo "Failed: ${failed_scenarios[*]}"; exit 1; }
