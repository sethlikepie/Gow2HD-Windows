#!/usr/bin/env bash
# Build and run the ps3_mem_trace unit test (Task 3, subsistema MEMORY:
# formato G1, campos ea=/len=/region=/op=, cap explicito + OOB_TRUNCATED,
# gate off -> zero bytes, vm_region_name(); "flood" acrescentado na review
# pos-Task 3, Important 2: agenda de crescimento de OOB_TRUNCATED alem do
# primeiro corte).
#
# Standalone: compila runtime/trace/ps3_trace.c (Task 1) + ps3_mem_trace.c
# (Task 3) + o teste desta pasta. NAO liga runtime/ppu/ppu_loader.cpp (o
# unico caller real de ps3_mem_oob_report() no boot) -- esse ficheiro inclui
# o ppu_recomp.h GERADO por jogo e nao compila fora de um jogo concreto; ver
# o comentario de cabecalho de ps3_mem_trace.c para o motivo de a logica
# viver num modulo game-agnostic proprio.
#
# ps3_trace_on() resolve getenv() uma vez por subsistema e nunca mais re-le
# -- por isso "off"/"format"/"cap"/"flood" correm cada uma no SEU PROPRIO
# processo, mesmo principio de run_tests.sh (Task 1) e run_tests_checkpoint.sh
# (Task 2). "region" testa vm_region_name(), uma funcao pura sem gate -- corre
# em qualquer processo.
#
# Usage: ./run_tests_mem_trace.sh [<cc-path>]
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

exe="$HERE/test_ps3_mem_trace.exe"
build_log="$HERE/build_ps3_mem_trace.log"

if ! "$CC" -std=c11 -Wall -Wextra -O0 \
        -I "$REPO/include" \
        "$REPO/runtime/trace/ps3_trace.c" \
        "$REPO/runtime/trace/ps3_mem_trace.c" \
        "$HERE/test_ps3_mem_trace.c" \
        -o "$exe" 2> "$build_log"; then
    echo "[FAIL] build error -- see $build_log"
    exit 1
fi

pass=0
fail=0
failed_scenarios=()

# run_scenario <mode> [VAR=val ...] -- corre o binario com PS3_TRACE_MEMORY
# explicitamente por-definir e so as VAR=val extra aplicadas.
run_scenario() {
    local mode="$1"; shift
    local out rc
    out=$(env -u PS3_TRACE_MEMORY "$@" "$exe" "$mode" 2>&1) && rc=$? || rc=$?
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

run_scenario off                                    # PS3_TRACE_MEMORY desligado -> 0 bytes
run_scenario format PS3_TRACE_MEMORY=1               # formato G1 + ea=/len=/region=/op=
run_scenario cap    PS3_TRACE_MEMORY=1               # cap explicito + 1a OOB_TRUNCATED
run_scenario flood  PS3_TRACE_MEMORY=1               # Important 2: agenda de crescimento pos-cap
run_scenario region                                  # vm_region_name(), funcao pura

echo
echo "==========================================="
echo "Results: $pass passed, $fail failed"
[ $fail -eq 0 ] || { echo "Failed: ${failed_scenarios[*]}"; exit 1; }
