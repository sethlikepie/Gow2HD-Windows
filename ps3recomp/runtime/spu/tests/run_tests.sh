#!/usr/bin/env bash
# Build and run every integration test in this directory.
#
# Each test is a pair of files:
#   gen_test_<name>.py   - encodes the SPU program and wraps it in an ELF.
#   test_<name>_main.c   - harness with channel-overriding stubs and asserts.
#
# Usage: ./run_tests.sh  [<gcc-path>]
# Honours $GCC and $PYTHON env vars.

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
TOOLS="$REPO/tools"
RUNTIME_SPU="$REPO/runtime/spu"

# Regression fix (bringup-instrumentacao-fase0, Tasks 1-2 vs. this
# hand-written build path -- CMake's GLOB_RECURSE already picks these up for
# libps3recomp_runtime.a, but this script compiles runtime/libs .c files by
# explicit name, so it never saw the two new modules). Any harness below that
# links production code touched by Task 1 (ps3_trace.h / PS3_TRACE()) or
# Task 2 (ps3_checkpoint.h / ps3_checkpoint_fire()) needs these -- one shared
# list instead of copy-pasting the same three paths into every harness, so a
# future harness that starts linking newly-instrumented code just references
# $TRACE_SRC/$CHECKPOINT_SRC/$TRACE_INC instead of silently breaking again.
#   TRACE_SRC       -- for code that only calls PS3_TRACE(...) directly
#                      (e.g. libs/spurs/cellSpurs.c, spurs_kernel.c).
#   CHECKPOINT_SRC  -- superset: also needed by code that calls
#                      ps3_checkpoint_fire(...) (e.g. spu_workload.c,
#                      cellFs.c) -- ps3_checkpoint.c itself calls
#                      ps3_trace_on()/ps3_trace_emit(), so it always needs
#                      ps3_trace.c linked alongside it too.
TRACE_SRC="$REPO/runtime/trace/ps3_trace.c"
CHECKPOINT_SRC="$TRACE_SRC $REPO/runtime/trace/ps3_checkpoint.c"
# Post-review fix (Task 4 Minor #3): also on the include path so any harness
# that links $CHECKPOINT_SRC can `#include "trace_link_stubs.h"` for the four
# ps3_checkpoint.c stubs instead of pasting them (runtime/trace/tests/
# trace_link_stubs.h). Harmless widening for tests that don't need it.
TRACE_INC="-I $REPO/include -I $REPO/runtime/trace/tests"

# Compiler: clang is the system compiler on macOS, where `gcc` is only a shim.
if [ -n "${1:-}" ]; then
    GCC="$1"
elif [ -n "${GCC:-}" ]; then
    GCC="$GCC"
elif [ "$(uname -s)" = "Darwin" ]; then
    GCC="clang"
else
    GCC="gcc"
fi

# Interpreter: prefer the repo venv, then python3. Plain `python` is absent on
# macOS and on any modern distro that dropped the Python 2 era alias.
if [ -n "${PYTHON:-}" ]; then
    PY="$PYTHON"
elif [ -x "$REPO/.venv/bin/python" ]; then
    PY="$REPO/.venv/bin/python"
elif command -v python3 > /dev/null 2>&1; then
    PY=python3
else
    PY=python
fi

pass=0
fail=0
failed_tests=()

for gen in "$HERE"/gen_test_*.py; do
    name=$(basename "$gen" .py)
    name=${name#gen_test_}
    main="$HERE/test_${name}_main.c"
    [ -f "$main" ] || { echo "[skip] $name: no test_${name}_main.c"; continue; }

    # gen_test_spu_fma_precision.py does NOT emit an SPU ELF (it emits a C
    # header of exact-arithmetic precision vectors) and the test is compiled +
    # run twice (gate on/off) in the dedicated section below -- so it must not
    # go through the lifter loop here.
    [ "$name" = spu_fma_precision ] && continue

    elf="$HERE/test_${name}.elf"
    # Use an `out_` prefix for generated dirs so `gen_test_*.py` and
    # `out_*` never collide under a `rm -rf gen_*` glob.
    gen_out="$HERE/out_${name}"
    exe="$HERE/test_${name}.exe"

    rm -rf "$gen_out"
    "$PY" "$gen" > /dev/null
    "$PY" "$TOOLS/spu_lifter.py" --auto-functions "$elf" \
        --output "$gen_out" > /dev/null

    # Optional per-test extra build args (extra sources / -I dirs). A
    # test_<name>.deps file is sourced with $REPO/$RUNTIME_SPU in scope and may
    # set EXTRA="..." (e.g. to link a libs/ HLE module like cellSync.c).
    EXTRA=""
    [ -f "$HERE/test_${name}.deps" ] && . "$HERE/test_${name}.deps"

    # spu_stop.c: canonical halt handler that the lifted `stop` path calls
    # (declared in every generated spu_recomp.h). Linked into every image.
    # $TRACE_INC (-I "$REPO/include") is harmless for tests that don't need
    # ps3_trace.h/ps3_checkpoint.h -- it only widens the include search path;
    # only a test's own $EXTRA (via its .deps file) pulls in $TRACE_SRC/
    # $CHECKPOINT_SRC as actual sources, so nothing else gains a new link
    # requirement.
    if "$GCC" -std=c11 -O2 -I "$gen_out" -I "$RUNTIME_SPU" $TRACE_INC \
        "$gen_out/spu_recomp.c" "$RUNTIME_SPU/spu_stop.c" "$main" $EXTRA -o "$exe" \
        2> "$HERE/build_${name}.log"; then
        out=$("$exe" 2>&1) && rc=$? || rc=$?
        if [ "$rc" -eq 0 ]; then
            echo "[PASS] $name -- $out"
            pass=$((pass+1))
        else
            echo "[FAIL] $name (exit $rc)"
            echo "$out" | sed 's/^/   /'
            failed_tests+=("$name")
            fail=$((fail+1))
        fi
    else
        echo "[FAIL] $name (build error -- see build_${name}.log)"
        failed_tests+=("$name")
        fail=$((fail+1))
    fi
done

# --- testes sem par gerador ------------------------------------------------
# Alguns testes nao encodam um programa SPU proprio: ou nao usam SPU nenhum (o
# do kernel SPURS, cujo dispatch e um callback C; ou os de spu_helpers.h como o
# FMA/precisao, que sao chamadas C diretas sem bytecode SPU), ou reaproveitam o
# job liftado de outro teste (o e2e usa o do workload). O loop acima so ve pares
# gen_test_<n>.py + test_<n>_main.c, portanto estes ficavam de fora -- estavam
# no repo sem nunca correr.
run_extra() {   # run_extra <nome> <fontes...>
    local name="$1"; shift
    local exe="$HERE/test_${name}.exe"
    if "$GCC" -std=c11 -O2 -I "$HERE" -I "$RUNTIME_SPU" -I "$REPO/libs/spurs" \
        -I "$REPO/include" -I "$HERE/out_workload" -I "$REPO/runtime/trace/tests" \
        "$@" -o "$exe" 2> "$HERE/build_${name}.log"; then
        out=$("$exe" 2>&1) && rc=$? || rc=$?
        if [ "$rc" -eq 0 ]; then
            echo "[PASS] $name -- $out"; pass=$((pass+1))
        else
            echo "[FAIL] $name (exit $rc)"; echo "$out" | sed 's/^/   /'
            failed_tests+=("$name"); fail=$((fail+1))
        fi
    else
        echo "[FAIL] $name (build error -- see build_${name}.log)"
        failed_tests+=("$name"); fail=$((fail+1))
    fi
}

# Task 4 (bringup-instrumentacao-fase0): cellSpurs.c/spurs_kernel.c agora
# chamam PS3_TRACE(PS3_TS_SPURS, ...) (WORKLOAD_ADD/TASK_CREATE/TASK_START/
# TASK_END/WORKER_IDLE/QUEUE_PUSH/QUEUE_POP) -- $TRACE_SRC (ver topo do
# ficheiro) tem de entrar na ligacao destes dois testes (-I "$REPO/include"
# ja vem do run_extra() partilhado acima). ps3_timebase_now() e' stubado no
# proprio test_*_main.c (mesmo padrao de runtime/trace/tests/test_ps3_mem_trace.c).
# cellSpurs.c/spurs_kernel.c nao chamam ps3_checkpoint_fire(), so' $TRACE_SRC
# (nao o superset $CHECKPOINT_SRC) -- linkar ps3_checkpoint.c aqui obrigaria
# a stubar tambem os 3 accessors de SNAPSHOT sem necessidade nenhuma.
run_extra spurs_kernel "$REPO/libs/spurs/tests/test_spurs_kernel_main.c" \
                       "$REPO/libs/spurs/spurs_kernel.c" \
                       $TRACE_SRC

# kick de task assincrono: o ponto de observacao E' o spu_workload_dispatch_task
# (carimba a thread em que correu), portanto o teste define-o e NAO linka o
# runtime/spu/spu_workload.c -- nao ha programa SPU nenhum, so a politica de
# despacho do cellSpurs sob um kernel saturado.
run_extra spurs_kick_async "$REPO/libs/spurs/tests/test_spurs_kick_async_main.c" \
                           "$REPO/libs/spurs/cellSpurs.c" \
                           "$REPO/libs/spurs/spurs_kernel.c" \
                           $TRACE_SRC

# isolacao de crash: os "jobs" sao funcoes C que faltam de proposito, portanto
# nao ha programa SPU nenhum para gerar -- mas o teste linka o spu_workload.c a
# serio e passa pelo dispatch real. Regressao Task 1-2 corrigida aqui:
# spu_workload.c chama ps3_checkpoint_fire() (FIRST_SPU_JOB), por isso precisa
# do superset $CHECKPOINT_SRC (ps3_trace.c + ps3_checkpoint.c); o proprio
# test_seh_isolation_main.c ganhou os stubs de ps3_timebase_now()/accessors
# de SNAPSHOT (mesmo padrao de test_ps3_checkpoint.c).
run_extra seh_isolation "$HERE/test_seh_isolation_main.c" \
                        "$RUNTIME_SPU/spu_workload.c" \
                        $CHECKPOINT_SRC

# fulfiller host de inflate: puro C sobre um elemento de LFQueue montado a mao,
# portanto tambem nao ha programa SPU nenhum. cellPngDec.c e' quem define
# STB_IMAGE_IMPLEMENTATION no repo, logo e' de la que vem o
# stbi_zlib_decode_noheader_buffer (lfq_host_inflate.c so inclui as declaracoes
# -- definir a implementacao nas duas TUs duplicava todos os simbolos stbi_*).
# cellFs.c entra so para resolver o cellfs_translate_path do cellPngDec.
# Os -I extra vao aqui em vez da lista fixa do run_extra para nao mexer no
# ambiente de compilacao dos outros testes. Regressao Task 1-2 corrigida
# aqui: cellFs.c chama ps3_checkpoint_fire() (FIRST_FS_OPEN), por isso precisa
# do superset $CHECKPOINT_SRC; test_lfq_inflate_main.c ganhou os mesmos stubs.
run_extra lfq_inflate "-I$REPO/libs/filesystem" \
                      "$REPO/libs/spurs/tests/test_lfq_inflate_main.c" \
                      "$REPO/libs/spurs/lfq_host_inflate.c" \
                      "$REPO/libs/codec/cellPngDec.c" \
                      "$REPO/libs/filesystem/cellFs.c" \
                      $CHECKPOINT_SRC

# spu_fma_double: familia DP fundida (dfma/dfms/dfnms/dfnma via C99 fma()) +
# mpyhha/mpyhhau. Chamadas C diretas a spu_helpers.h, sem programa SPU -- por
# isso corre aqui e nao no loop do lifter acima.
run_extra spu_fma_double "$HERE/test_spu_fma_double_main.c"

# e2e: reaproveita o job liftado do teste workload, logo tem de correr DEPOIS
# dele (out_workload/ ja existe a esta altura). Regressao Task 1-2 corrigida
# aqui: spu_workload.c chama ps3_checkpoint_fire() E cellSpurs.c/
# spurs_kernel.c chamam PS3_TRACE(...) -- o superset $CHECKPOINT_SRC cobre as
# duas necessidades numa lista so'; test_spurs_e2e_main.c ganhou os stubs.
if [ -f "$HERE/out_workload/spu_recomp.c" ]; then
    run_extra spurs_e2e "$HERE/test_spurs_e2e_main.c" \
                        "$HERE/out_workload/spu_recomp.c" \
                        "$RUNTIME_SPU/spu_stop.c" \
                        "$RUNTIME_SPU/spu_workload.c" \
                        "$REPO/libs/spurs/spurs_kernel.c" \
                        "$REPO/libs/spurs/cellSpurs.c" \
                        $CHECKPOINT_SRC
else
    echo "[skip] spurs_e2e: out_workload/ ausente (o teste workload nao correu)"
fi

# spu_fma_precision compila UMA vez mas corre-se DUAS: sem PS3_SPU_FMA_ISA
# (guarda de regressao -- confirma que o gate desligado continua
# byte-identico ao comportamento antigo) e com PS3_SPU_FMA_ISA=1 (vetores de
# fronteira do arredondamento truncado + flush-to-zero + saturacao de
# overflow). O main() do teste le a mesma env var para escolher a bateria,
# entao os dois processos exercitam exatamente o par (env, comportamento)
# que o gate real usa em producao -- sem nenhum hook de override so' para
# teste. Nao passa pelo run_extra porque precisa das duas fases.
name="spu_fma_precision"
exe="$HERE/test_${name}.exe"
# (Re)generate the exact-arithmetic precision-vector header consumed by the
# test. Deterministic output -> re-running leaves the tree clean.
"$PY" "$HERE/gen_test_${name}.py" > /dev/null
if "$GCC" -std=c11 -O2 -I "$RUNTIME_SPU" -I "$HERE" "$HERE/test_${name}_main.c" -o "$exe" \
    2> "$HERE/build_${name}.log"; then
    for phase in off on; do
        pname="${name}_${phase}"
        if [ "$phase" = on ]; then
            out=$(PS3_SPU_FMA_ISA=1 "$exe" 2>&1) && rc=$? || rc=$?
        else
            out=$(env -u PS3_SPU_FMA_ISA "$exe" 2>&1) && rc=$? || rc=$?
        fi
        if [ "$rc" -eq 0 ]; then
            echo "[PASS] $pname -- $out"
            pass=$((pass+1))
        else
            echo "[FAIL] $pname (exit $rc)"
            echo "$out" | sed 's/^/   /'
            failed_tests+=("$pname")
            fail=$((fail+1))
        fi
    done
else
    echo "[FAIL] $name (build error -- see build_${name}.log)"
    failed_tests+=("$name")
    fail=$((fail+1))
fi

# --- discriminador de corrida cross-thread (ThreadSanitizer) ---------------
# O case5 do test_spu_channel_blocking faz o ping-pong funcional, mas num core
# de copia-unica (Apple Silicon) um word de 32 bits nunca "tosa" e o ping-pong
# de 1ms fecha a janela de visibilidade em tempo de relogio -- logo passaria
# mesmo com a sincronizacao PARTIDA (store simples + volatile read, o codigo
# antigo). Quem separa o certo do errado e' o ThreadSanitizer: ele constroi o
# grafo happens-before a partir dos atomicos, independente de a leitura obsoleta
# se manifestar ou nao. Com o par acquire/release fica limpo; se alguem regredir
# para store/volatile, acusa a corrida em ch->value e aborta (verificado nos dois
# sentidos em arm64: fixed -> exit 0; broken -> "data race ... in consumer").
# O probe e' self-contained (so inclui spu_context.h), NAO passa pelo lifter, por
# isso builda mesmo neste tree onde o gerador SPU nao corre.
tsan_probe="$HERE/tsan_spu_channel_probe.c"
if [ -f "$tsan_probe" ]; then
    name=spu_channel_tsan
    exe="$HERE/test_${name}.exe"
    tsan_check="$HERE/.tsan_supportcheck"
    if echo 'int main(void){return 0;}' | \
        "$GCC" -fsanitize=thread -x c - -o "$tsan_check" 2>/dev/null; then
        rm -f "$tsan_check"
        if "$GCC" -std=c11 -O1 -fsanitize=thread -I "$RUNTIME_SPU" \
            "$tsan_probe" -o "$exe" 2> "$HERE/build_${name}.log"; then
            out=$(TSAN_OPTIONS="halt_on_error=1" "$exe" 2>&1) && rc=$? || rc=$?
            if [ "$rc" -eq 0 ]; then
                echo "[PASS] $name -- $out"; pass=$((pass+1))
            else
                echo "[FAIL] $name (exit $rc -- TSan flagged a race or the handoff broke)"
                echo "$out" | sed 's/^/   /'
                failed_tests+=("$name"); fail=$((fail+1))
            fi
        else
            echo "[FAIL] $name (build error -- see build_${name}.log)"
            failed_tests+=("$name"); fail=$((fail+1))
        fi
    else
        rm -f "$tsan_check"
        echo "[skip] $name: compiler sem suporte a -fsanitize=thread"
    fi
fi

echo
echo "==========================================="
echo "Results: $pass passed, $fail failed"
[ $fail -eq 0 ] || { echo "Failed: ${failed_tests[*]}"; exit 1; }
