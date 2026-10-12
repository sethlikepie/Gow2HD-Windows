#!/usr/bin/env bash
# Build and run the ps3_trace unit test (Task 1, casos T1.1-T1.7).
#
# Standalone: compila runtime/trace/ps3_trace.c junto com o test_ps3_trace.c
# desta pasta (que fornece o seu proprio stub de ps3_timebase_now() -- ver o
# comentario de cabecalho em test_ps3_trace.c). Nao ha programa SPU nem
# gen_test_*.py aqui, por isso este e' o SEU PROPRIO runner pequeno -- nao
# esta (e nao deve ser) ligado a runtime/spu/tests/run_tests.sh, que e'
# especifico de SPU e espera pares gen_test_*.py/spu_recomp.h gerados.
#
# ps3_trace_on() resolve getenv() uma vez por subsistema e nunca mais
# re-le -- por isso T1.1 (todas as env vars off), T1.2 (so PS3_TRACE_RSX=1) e
# T1.3 (so PS3_TRACE_HEAP=1) correm cada uma no SEU PROPRIO processo, com o
# env certo, escolhido via argv[1] do binario. Mesmo principio do teste
# spu_fma_precision em runtime/spu/tests/run_tests.sh (compila uma vez, corre
# duas fases, uma por combinacao de env).
#
# Usage: ./run_tests.sh [<cc-path>]
# Honra a variavel de ambiente $CC.

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"

# Compilador: clang e' o compilador de sistema no macOS.
if [ -n "${1:-}" ]; then
    CC="$1"
elif [ -n "${CC:-}" ]; then
    CC="$CC"
elif [ "$(uname -s)" = "Darwin" ]; then
    CC="clang"
else
    CC="gcc"
fi

exe="$HERE/test_ps3_trace.exe"
build_log="$HERE/build_ps3_trace.log"

if ! "$CC" -std=c11 -Wall -Wextra -O0 \
        -I "$REPO/include" \
        "$REPO/runtime/trace/ps3_trace.c" \
        "$HERE/test_ps3_trace.c" \
        -o "$exe" 2> "$build_log"; then
    echo "[FAIL] build error -- see $build_log"
    exit 1
fi

pass=0
fail=0
failed_scenarios=()

# run_scenario <mode> [VAR=val ...] -- corre o binario com as dez
# PS3_TRACE_* explicitamente por-definir e so as VAR=val extra aplicadas.
run_scenario() {
    local mode="$1"; shift
    local out rc
    out=$(env -u PS3_TRACE_CHECKPOINTS -u PS3_TRACE_MEMORY -u PS3_TRACE_HEAP \
              -u PS3_TRACE_THREADS -u PS3_TRACE_SPURS -u PS3_TRACE_DMA \
              -u PS3_TRACE_FIOS -u PS3_TRACE_MEDIA -u PS3_TRACE_RSX \
              -u PS3_TRACE_SNAPSHOT "$@" "$exe" "$mode" 2>&1) && rc=$? || rc=$?
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

run_scenario format                        # T1.4, T1.5, T1.6, T1.7
run_scenario gate_all_off                  # T1.1
run_scenario gate_rsx  PS3_TRACE_RSX=1      # T1.2
run_scenario gate_heap PS3_TRACE_HEAP=1     # T1.3

echo
echo "==========================================="
echo "Results: $pass passed, $fail failed"
[ $fail -eq 0 ] || { echo "Failed: ${failed_scenarios[*]}"; exit 1; }
