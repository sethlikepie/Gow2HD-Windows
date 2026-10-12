#!/usr/bin/env bash
# Build and run the ps3_checkpoint unit test (Task 2, casos T2.off/T2.once/
# T2.snapshot/T2.name).
#
# Standalone: compila ps3_trace.c (Task 1) + ps3_checkpoint.c (Task 2) + este
# ficheiro, que fornece os seus proprios stubs de ps3_timebase_now() e dos
# tres accessors de SNAPSHOT (ps3_ppu_thread_alive_count/ps3_sys_memory_used/
# ps3_gcm_flip_count) -- ver o comentario de cabecalho em
# test_ps3_checkpoint.c. Nao ligado a runtime/trace/tests/run_tests.sh (Task
# 1) nem a runtime/spu/tests/run_tests.sh -- cada tarefa tem o seu proprio
# runner pequeno, mesma convencao das duas.
#
# ps3_checkpoint_fire() tem um once-guard estatico por processo: uma vez
# disparado, um checkpoint nao dispara mais nesse processo. Por isso "gate
# desligado", "gate ligado sem SNAPSHOT" e "gate + SNAPSHOT ligados" correm
# cada um no SEU PROPRIO processo, com o env certo, escolhido via argv[1].
#
# Usage: ./run_tests_checkpoint.sh [<cc-path>]
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

exe="$HERE/test_ps3_checkpoint.exe"
build_log="$HERE/build_ps3_checkpoint.log"

# -pthread: T2.concurrent (abaixo) exercita o once-guard com threads reais.
if ! "$CC" -std=c11 -Wall -Wextra -O0 -pthread \
        -I "$REPO/include" \
        "$REPO/runtime/trace/ps3_trace.c" \
        "$REPO/runtime/trace/ps3_checkpoint.c" \
        "$HERE/test_ps3_checkpoint.c" \
        -o "$exe" 2> "$build_log"; then
    echo "[FAIL] build error -- see $build_log"
    exit 1
fi

pass=0
fail=0
failed_scenarios=()

# run_scenario <mode> [VAR=val ...] -- corre o binario com PS3_TRACE_CHECKPOINTS
# e PS3_TRACE_SNAPSHOT explicitamente por-definir e so as VAR=val extra aplicadas.
run_scenario() {
    local mode="$1"; shift
    local out rc
    out=$(env -u PS3_TRACE_CHECKPOINTS -u PS3_TRACE_SNAPSHOT "$@" "$exe" "$mode" 2>&1) && rc=$? || rc=$?
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

run_scenario off                                                  # T2.off + T2.name
run_scenario once     PS3_TRACE_CHECKPOINTS=1                      # T2.once
run_scenario snapshot  PS3_TRACE_CHECKPOINTS=1 PS3_TRACE_SNAPSHOT=1 # T2.snapshot
run_scenario concurrent PS3_TRACE_CHECKPOINTS=1                     # T2.concurrent

# ---------------------------------------------------------------------------
# T2.lockorder -- regressao do achado do review "self-deadlock quando um
# accessor de SNAPSHOT reentra um lock que o caller ja detem" (ver
# test_ps3_checkpoint_lockorder.c). Duas variantes da MESMA fonte, escolhidas
# em tempo de compilacao por -DLOCKORDER_REPRO_BUG:
#   BUG=1 -- stub do accessor reproduz o padrao PRE-FIX (toma o mesmo lock que
#            o caller ja detem) -- TEM de reproduzir o deadlock (watchdog mata
#            o processo e ele sai com codigo 1); se sair com 0, o padrao nao
#            reproduziu o hazard e isso e' a falha aqui.
#   BUG=0 -- stub lock-free, o padrao do FIX real em sys_ppu_thread.c -- TEM
#            de completar e sair com codigo 0.
# -----------------------------------------------------------------------
lockorder_src="$HERE/test_ps3_checkpoint_lockorder.c"

run_lockorder_variant() {
    local bugflag="$1" expect_rc="$2" label="$3"
    local exe_lo="$HERE/test_ps3_checkpoint_lockorder_${label}.exe"
    local build_log_lo="$HERE/build_ps3_checkpoint_lockorder_${label}.log"
    local out rc

    if ! "$CC" -std=c11 -Wall -Wextra -O0 -pthread \
            -DLOCKORDER_REPRO_BUG="$bugflag" \
            -I "$REPO/include" \
            "$REPO/runtime/trace/ps3_trace.c" \
            "$REPO/runtime/trace/ps3_checkpoint.c" \
            "$lockorder_src" \
            -o "$exe_lo" 2> "$build_log_lo"; then
        echo "[FAIL] lockorder_$label -- build error, see $build_log_lo"
        failed_scenarios+=("lockorder_$label")
        fail=$((fail+1))
        return
    fi

    out=$("$exe_lo" 2>&1) && rc=$? || rc=$?
    if [ "$rc" -eq "$expect_rc" ]; then
        echo "[PASS] lockorder_$label -- $out"
        pass=$((pass+1))
    else
        echo "[FAIL] lockorder_$label (exit $rc, esperava $expect_rc)"
        echo "$out" | sed 's/^/   /'
        failed_scenarios+=("lockorder_$label")
        fail=$((fail+1))
    fi
}

run_lockorder_variant 1 1 buggy   # reproduz o padrao pre-fix -> deve deadlockar (watchdog -> exit 1)
run_lockorder_variant 0 0 fixed   # padrao lock-free do fix real -> deve completar (exit 0)

echo
echo "==========================================="
echo "Results: $pass passed, $fail failed"
[ $fail -eq 0 ] || { echo "Failed: ${failed_scenarios[*]}"; exit 1; }
