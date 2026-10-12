#!/usr/bin/env bash
# Build and run the sys_memory/HEAP unit test (Task 3, subsistema HEAP:
# ALLOC/FREE/FREELIST canonicos no allocator LV2 sys_memory).
#
# Standalone: compila runtime/trace/ps3_trace.c (Task 1) + o allocator real
# runtime/syscalls/sys_memory.c + o teste desta pasta. NAO liga o runtime
# inteiro nem ppu_loader.cpp -- sys_memory.c e' game-agnostic (nao inclui
# ppu_recomp.h), so' precisa de ppu_context/lv2_syscall_table/vm.h (todos
# genericos), ver o comentario de cabecalho de test_ps3_heap.c.
#
# ps3_trace_on() resolve getenv() uma vez por subsistema e cacheia para
# sempre, e os globais de sys_memory.c (bump pointer, tabela de allocations)
# sao estado estatico por-processo -- por isso cada cenario corre no SEU
# PROPRIO processo, mesmo principio dos outros runners desta pasta.
#
# Usage: ./run_tests_heap.sh [<cc-path>]
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

exe="$HERE/test_ps3_heap.exe"
build_log="$HERE/build_ps3_heap.log"

if ! "$CC" -std=c11 -Wall -Wextra -O0 \
        -I "$REPO/include" \
        "$REPO/runtime/trace/ps3_trace.c" \
        "$REPO/runtime/syscalls/sys_memory.c" \
        "$HERE/test_ps3_heap.c" \
        -o "$exe" 2> "$build_log"; then
    echo "[FAIL] build error -- see $build_log"
    exit 1
fi

pass=0
fail=0
failed_scenarios=()

# run_scenario <mode> [VAR=val ...] -- corre o binario com PS3_TRACE_HEAP
# explicitamente por-definir e so as VAR=val extra aplicadas.
run_scenario() {
    local mode="$1"; shift
    local out rc
    out=$(env -u PS3_TRACE_HEAP "$@" "$exe" "$mode" 2>&1) && rc=$? || rc=$?
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

run_scenario off                                    # PS3_TRACE_HEAP desligado -> 0 linhas [PS3T]
run_scenario alloc_free    PS3_TRACE_HEAP=1          # ALLOC + FREE canonicos
run_scenario freelist      PS3_TRACE_HEAP=1          # FREELIST (reuso real) + ALLOC
run_scenario free_missing  PS3_TRACE_HEAP=1          # free de endereco nunca alocado -> sem linha FREE
run_scenario alloc_regista_range                    # Fase 1 Task 5: vm_commit bem-sucedido tem de registar a range (ppu_register_committed_range)
run_scenario regista_contencao                      # Fase 1 Task 5 Round 1: contencao (range contida nao consome slot) + aviso-uma-vez quando o array de 8 esgota
run_scenario mmapper_alloc_commit_falha             # Fase 1 Task 5 Round 2: vm_commit falhado -> CELL_ENOMEM, sem escrever addr_out, sem registar

echo
echo "==========================================="
echo "Results: $pass passed, $fail failed"
[ $fail -eq 0 ] || { echo "Failed: ${failed_scenarios[*]}"; exit 1; }
