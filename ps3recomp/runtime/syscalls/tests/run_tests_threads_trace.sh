#!/usr/bin/env bash
# Build and run the THREADS subsystem trace unit test (Task 4 do plano
# "bringup-instrumentacao-fase0"): CREATE + BLOCK(on=/obj=) + WAKE + JOIN,
# usando sys_ppu_thread.c e sys_mutex.c REAIS do runtime (nao uma
# reimplementacao), gate off -> zero linhas [PS3T].
#
# Standalone: compila runtime/trace/ps3_trace.c (Task 1) +
# runtime/trace/ps3_checkpoint.c (Task 2, precisa dele pois vm_init() e
# sys_ppu_thread_create() ja chamam ps3_checkpoint_fire) +
# runtime/syscalls/sys_ppu_thread.c + runtime/syscalls/sys_mutex.c + este
# ficheiro, que fornece os stubs das dependencias de runtime/ppu/ppu_loader.cpp
# (gerado por jogo, nao linkavel aqui) -- ver o cabecalho de
# test_threads_trace_main.c para a lista e o motivo de cada stub.
#
# ps3_trace_on() resolve getenv() uma vez por subsistema e nunca mais re-le
# -- por isso "off"/"on" correm cada uma no SEU PROPRIO processo, mesmo
# principio de runtime/trace/tests/test_ps3_mem_trace.c.
#
# Usage: ./run_tests_threads_trace.sh [<cc-path>]
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

exe="$HERE/test_threads_trace.exe"
build_log="$HERE/build_threads_trace.log"

if ! "$CC" -std=c11 -Wall -Wextra -O0 -pthread \
        -I "$REPO/include" \
        -I "$REPO/runtime/syscalls" \
        -I "$HERE" \
        "$REPO/runtime/trace/ps3_trace.c" \
        "$REPO/runtime/trace/ps3_checkpoint.c" \
        "$REPO/runtime/syscalls/sys_ppu_thread.c" \
        "$REPO/runtime/syscalls/sys_mutex.c" \
        "$HERE/test_threads_trace_main.c" \
        -o "$exe" 2> "$build_log"; then
    echo "[FAIL] build error -- see $build_log"
    exit 1
fi

pass=0
fail=0
failed_scenarios=()

# run_scenario <mode> [VAR=val ...] -- corre o binario com PS3_TRACE_THREADS
# explicitamente por-definir e so as VAR=val extra aplicadas.
run_scenario() {
    local mode="$1"; shift
    local out rc
    out=$(env -u PS3_TRACE_THREADS "$@" "$exe" "$mode" 2>&1) && rc=$? || rc=$?
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

run_scenario off                        # PS3_TRACE_THREADS desligado -> 0 linhas [PS3T]
run_scenario on  PS3_TRACE_THREADS=1     # CREATE + BLOCK(on=/obj=) + WAKE + JOIN

echo
echo "==========================================="
echo "Results: $pass passed, $fail failed"
[ $fail -eq 0 ] || { echo "Failed: ${failed_scenarios[*]}"; exit 1; }
