#!/usr/bin/env bash
# macOS engine smoke: build the runtime library and run every self-contained
# suite the host can execute (Phase 0 of docs/superpowers/plans/
# 2026-07-20-gow2-full-bringup.md).
#
# This covers the engine only -- no game data required. The GoW2 boot baseline
# lives in gow2-recomp/smoke_boot_mac.sh.
#
# Deps: brew install cmake ninja sdl2 pkg-config, and a Python 3.10+ venv at
# .venv (the tools use PEP 604 unions, which the macOS system Python cannot
# parse).
#
# Usage: ./scripts/smoke_macos.sh [--no-gui]
#   --no-gui   skip the RSX backend smokes, which need a window server
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WANT_GUI=1
[ "${1:-}" = "--no-gui" ] && WANT_GUI=0

# Regression fix (bringup-instrumentacao-fase0, Tasks 1-2 vs. this
# hand-written build path): libs/video/movie_hle.c (sections 3/3b/3c below)
# and runtime/ppu/ppu_fs.cpp (unity-built by test_fs_movie_io.cpp, section 3b)
# now call ps3_checkpoint_fire() -- same shared-variable pattern as
# runtime/spu/tests/run_tests.sh (see the comment there for the full
# rationale). TRACE_SRC is a plain C source list -- fine to pass to $MIO_CC
# directly (sections 3/3c), but NOT to a C++ driver (section 3b uses
# clang++, which mangles .c inputs) -- that section precompiles these to .o
# with $MIO_CC first, same as it already does for movie_hle.c.
TRACE_SRC="$ROOT/runtime/trace/ps3_trace.c"
CHECKPOINT_SRC="$TRACE_SRC $ROOT/runtime/trace/ps3_checkpoint.c"

fail() { echo "FAIL: $1" >&2; exit 1; }

echo "=== 1. configure + build ==="
cmake -B "$ROOT/build-macos" -G Ninja -DCMAKE_BUILD_TYPE=Release "$ROOT" > /dev/null
cmake --build "$ROOT/build-macos" -j "$(sysctl -n hw.ncpu)" > /dev/null

LIB="$ROOT/build-macos/libps3recomp_runtime.a"
[ -f "$LIB" ] || fail "libps3recomp_runtime.a nao foi produzida"
lipo -info "$LIB" | grep -q arm64 || fail "a lib nao e arm64"
echo "  ok: $(basename "$LIB") ($(lipo -info "$LIB" | sed 's/.*: //'))"

echo "=== 2. suite SPU ==="
# run_tests.sh ja escolhe clang no Darwin e resolve o interpretador via .venv.
SPU_OUT=$("$ROOT/runtime/spu/tests/run_tests.sh" 2>&1) || { echo "$SPU_OUT"; fail "suite SPU"; }
echo "$SPU_OUT" | grep -q ', 0 failed' || { echo "$SPU_OUT" | tail -5; fail "suite SPU com falhas"; }
echo "  ok: $(echo "$SPU_OUT" | grep 'Results:')"

echo "=== 2b. suites de trace (Tasks 1-5 Fase 0 + Task 1 Fase 1: CHECKPOINTS/MEMORY/HEAP/THREADS/SPURS/DMA/RSX/MASK) ==="
# Correcao pos-review (Task 4 Minor #4): estas 7 suites nao eram chamadas de
# lado nenhum -- exactamente o modo de falha que ja partiu a suite SPU por
# duas tasks sem ninguem notar (ver secao 2/adenda do relatorio da Task 4).
# A setima (libs/video/tests/run_tests_rsx_trace.sh) e' da Task 5 (RSX); as
# outras seis sao das Tasks 1-4. Mesmo loop, mesmo estilo de asserção da
# secao 2 (", 0 failed" na linha "Results:").
# A oitava (runtime/trace/tests/run_tests_mask_trace.sh) e' da Task 1 da Fase 1
# (bringup-desmascarar-memoria): evento canonico MEMORY/SWALLOWED das mascaras
# sempre-ligadas (uncommitted_guard/tblsize_guard) -- mesmo motivo de nao
# apodrecer em silencio, ligada aqui desde o commit que a introduz.
for TRACE_SUITE in \
    runtime/trace/tests/run_tests.sh \
    runtime/trace/tests/run_tests_checkpoint.sh \
    runtime/trace/tests/run_tests_mem_trace.sh \
    runtime/trace/tests/run_tests_heap.sh \
    runtime/spu/tests/run_tests_dma_trace.sh \
    runtime/syscalls/tests/run_tests_threads_trace.sh \
    libs/video/tests/run_tests_rsx_trace.sh \
    runtime/trace/tests/run_tests_mask_trace.sh \
; do
    TS_OUT=$("$ROOT/$TRACE_SUITE" 2>&1) || { echo "$TS_OUT"; fail "suite $TRACE_SUITE"; }
    echo "$TS_OUT" | grep -q ', 0 failed' || { echo "$TS_OUT" | tail -5; fail "suite $TRACE_SUITE com falhas"; }
    echo "  ok: $TRACE_SUITE -- $(echo "$TS_OUT" | grep 'Results:')"
done

echo "=== 2c. suites de self-check Python (Tasks 6-7: trace_parse.py / baseline_report.py; Task B3: invalid_instructions; Task B2: switch tables TOML) ==="
# Correcao pos-review final de branch (achado Important #5): estas duas
# suites (tools/test_trace_parse.py, tools/test_baseline_report.py) nao
# estavam ligadas a nada -- nem aqui nem em mais lado nenhum -- exactamente o
# modo de apodrecimento que ja deixou a suite SPU partida por duas tasks
# inteiras sem ninguem notar (secao 2 acima). tools/test_invalid_instructions.py
# (Task B3, plano xenonrecomp-lessons) entra na mesma lista pelo mesmo motivo:
# tools/test_recomp_config.py ja tinha ficado ligado so' a scripts/test_python.sh
# e nao a este smoke -- nao repetir esse buraco para o consumidor novo.
# tools/test_switch_tables.py (Task B2, mesmo plano) entra pela mesma razao:
# cobre o merge config/discover_jump_tables e o wiring serial+paralelo do
# lifter (self.jump_tables) -- uma suite que nada executa apodrece em
# silencio, como o brief da B2 recorda explicitamente. Correm
# com o interpretador do
# .venv, nunca o Python de sistema: usam unioes PEP 604 (`X | None`) que o
# Python de sistema do macOS (3.9.x tipicamente) nao parseia -- mesma razao
# pela qual runtime/spu/tests/run_tests.sh (secao 2) ja prefere
# $REPO/.venv/bin/python. Ao contrario daquele fallback (que aceita python3
# do sistema se o venv faltar -- inofensivo la, os geradores desse harness
# nao usam PEP 604), aqui um venv em falta tem de falhar limpo e explicito:
# um fallback silencioso para o Python de sistema rebentaria a meio do parse
# do modulo com um SyntaxError iligivel, nao um "FAIL" claro desta script.
if [ -n "${PYTHON:-}" ]; then
    PY_SMOKE="$PYTHON"
elif [ -x "$ROOT/.venv/bin/python" ]; then
    PY_SMOKE="$ROOT/.venv/bin/python"
else
    fail ".venv nao encontrado em $ROOT/.venv (nem \$PYTHON definido) -- necessario para as suites Python (usam unioes PEP 604); crie o venv (ver cabecalho deste script) antes de correr o smoke"
fi
for PY_SUITE in tools/test_trace_parse.py tools/test_baseline_report.py tools/test_invalid_instructions.py tools/test_switch_tables.py; do
    PY_OUT=$("$PY_SMOKE" "$ROOT/$PY_SUITE" 2>&1) || { echo "$PY_OUT"; fail "suite $PY_SUITE"; }
    echo "$PY_OUT" | grep -q '^ok: ' || { echo "$PY_OUT" | tail -5; fail "suite $PY_SUITE sem linha 'ok:'"; }
    echo "  ok: $PY_SUITE -- $(echo "$PY_OUT" | grep '^ok: ')"
done

echo "=== 3. movie io (caminho de dados do WAD) ==="
# libs/video/movie_hle.c era Win32-only: em POSIX movie_io_open/pread/close nao
# faziam nada, portanto g_ps3_rperma_full nunca abria o gate pos-WAD do ICGLdr.
# O teste exercita as funcoes REAIS (linka o modulo) sobre um cache temporario.
# Nao entra na suite de runtime/spu/tests/run_tests.sh porque nao ha SPU nenhum
# aqui -- e um modulo de libs/video, e esta e a smoke do host macOS.
MIO_CC="${CC:-clang}"
MIO_WORK="$(mktemp -d)"
trap 'rm -rf "$MIO_WORK"' EXIT
"$MIO_CC" -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter \
    -I "$ROOT/include" -I "$ROOT/runtime/trace/tests" \
    "$ROOT/libs/video/tests/test_movie_io.c" "$ROOT/libs/video/movie_hle.c" \
    $CHECKPOINT_SRC \
    -o "$MIO_WORK/test_movie_io" || fail "test_movie_io nao compilou"
MIO_OUT=$("$MIO_WORK/test_movie_io" 2>/dev/null) || { echo "$MIO_OUT"; fail "movie io"; }
echo "  ok: $MIO_OUT"

echo "=== 3b. routing cellFs* -> movie io ==="
# O movie_io_* acima existia mas NAO tinha chamador nenhum: PS3_MOVIE_IO=1 era
# inerte porque nada oferecia um path do guest ao cache. Este teste exercita os
# handlers REAIS de runtime/ppu/ppu_fs.cpp (inclui a TU) e cobre tambem o
# mapeamento /dev_hdd0/game/<TITLEID>/USRDIR/... -> PS3_VFS_ROOT, que fazia o
# gow2.psarc resolver para um path duplicado e nunca abrir.
#
# movie_hle.c e' C: compilado a parte com o driver C, senao o clang++ trata o
# .c como C++ e os simbolos saem manglados.
"$MIO_CC" -std=c11 -O1 -I "$ROOT/include" -c "$ROOT/libs/video/movie_hle.c" \
    -o "$MIO_WORK/movie_hle.o" || fail "movie_hle.c nao compilou"
# ps3_trace.c/ps3_checkpoint.c precisam do driver C (ver nota no topo do
# ficheiro): clang++ trata .c como C++ e manglava os simbolos, mesmo motivo
# pelo qual movie_hle.c acima ja' e' compilado a parte.
"$MIO_CC" -std=c11 -O1 -I "$ROOT/include" -c "$ROOT/runtime/trace/ps3_trace.c" \
    -o "$MIO_WORK/ps3_trace.o" || fail "ps3_trace.c nao compilou"
"$MIO_CC" -std=c11 -O1 -I "$ROOT/include" -c "$ROOT/runtime/trace/ps3_checkpoint.c" \
    -o "$MIO_WORK/ps3_checkpoint.o" || fail "ps3_checkpoint.c nao compilou"
"${CXX:-clang++}" -std=c++20 -O1 -Wall -Wno-unused-function \
    -I "$ROOT/runtime/ppu/tests/fixture" -I "$ROOT/include" -I "$ROOT" \
    -I "$ROOT/runtime/trace/tests" \
    "$ROOT/runtime/ppu/tests/test_fs_movie_io.cpp" "$MIO_WORK/movie_hle.o" \
    "$MIO_WORK/ps3_trace.o" "$MIO_WORK/ps3_checkpoint.o" \
    -o "$MIO_WORK/test_fs_movie_io" || fail "test_fs_movie_io nao compilou"
FSIO_OUT=$("$MIO_WORK/test_fs_movie_io" 2>/dev/null) || { echo "$FSIO_OUT"; fail "routing cellFs/movie io"; }
echo "  ok: $(echo "$FSIO_OUT" | tail -1)"

echo "=== 3c. HLE de async-read do FIOS (op -> movie_io_pread) ==="
# O trafego em bloco do WAD NAO passa por cellFsRead: e' uma op assincrona do
# FIOS (func_002B3D1C), portanto o movie_io das seccoes 3/3b nunca chegava a
# ser consultado para o grosso dos 20 MB. ps3_fios_aread_hle e' o lado host
# dessa op -- le pelo movie_io_pread REAL, escreve no ring do guest e completa
# a op com as mesmas transicoes que o poll do jogo (func_002B45D0) ja entende.
#
# Alem do caminho feliz o teste prende as duas propriedades que tornam o hook
# seguro de deixar compilado: recusar SEM tocar na memoria do guest (o caller
# cai no corpo natural), e nunca marcar a op como concluida em leitura curta ou
# falhada -- um waiter sincrono libertado sobre um ring meio escrito le lixo
# que nao consegue detectar (mesma classe que o teste do lfq_host_inflate).
#
# Reutiliza $MIO_WORK da seccao 3 (ja tem trap de limpeza): nada fica na arvore.
"$MIO_CC" -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter \
    -I "$ROOT/include" -I "$ROOT/runtime/trace/tests" \
    "$ROOT/libs/video/tests/test_fios_aread_hle.c" \
    "$ROOT/libs/video/movie_hle.c" "$ROOT/libs/video/fios_aread_hle.c" \
    $CHECKPOINT_SRC \
    -o "$MIO_WORK/test_fios_aread_hle" || fail "test_fios_aread_hle nao compilou"
AREAD_OUT=$("$MIO_WORK/test_fios_aread_hle" 2>/dev/null) || { echo "$AREAD_OUT"; fail "HLE de async-read do FIOS"; }
echo "  ok: $AREAD_OUT"

echo "=== 3d. cellVdec: portao do FORCE SEQDONE ==="
# libs/codec/cellVdec.c era Win32-only da linha 36 ate ao fim: em POSIX TODAS as
# entradas eram no-ops, portanto PS3_VDEC_FORCE_SEQDONE_MS nao fazia nada e o
# cellVdecOpen devolvia CELL_OK sem sequer entregar um handle ao jogo. A FSM do
# logo do GoW2 nao sai do estado "a tocar" ate receber SEQDONE no callback que
# registou, logo este era o unico canal fechado no macOS.
#
# O teste linka o cellVdec.c REAL e prende os dois lados do portao: com o env
# ligado o SEQDONE chega ao OPD do proprio guest (replay), e com o env ausente
# ou "0" nao chega nada (a falsificacao -- sinalizar fim sem produtor seria
# forjar). Prende tambem o disparo unico partilhado entre o watchdog e o fim de
# stream natural: dois produtores, uma sequencia, um evento.
#
# Hermetico: movie_hle_cache_path() e' local ao teste, nao ha dados de jogo nem
# ffmpeg envolvidos (o caso do EOF usa um caminho inexistente de proposito).
# Reutiliza $MIO_WORK (ja tem trap de limpeza): nada fica na arvore.
#
# Task 5 (bringup-instrumentacao-fase0): $TRACE_SRC (ps3_trace.c) entra porque
# cellVdec.c agora emite PS3_TRACE(PS3_TS_MEDIA, ...) (AU_IN/AUDONE/PICOUT/
# SEQDONE); o proprio teste ganhou a asserçao src=natural|forced e um stub
# local de ps3_timebase_now() (ver o ficheiro), mesmo padrao das outras
# secções desta script.
"$MIO_CC" -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter \
    -I "$ROOT/include" -I "$ROOT/libs/codec" -I "$ROOT/runtime/trace/tests" \
    "$ROOT/libs/codec/tests/test_cellvdec_force_gate.c" "$ROOT/libs/codec/cellVdec.c" \
    $TRACE_SRC \
    -o "$MIO_WORK/test_cellvdec_force_gate" || fail "test_cellvdec_force_gate nao compilou"
VDEC_OUT=$("$MIO_WORK/test_cellvdec_force_gate" 2>/dev/null) || { echo "$VDEC_OUT"; fail "portao do FORCE SEQDONE"; }
echo "  ok: $VDEC_OUT"

echo "=== 3e. MEDIA EOS: src=unknown honesto (review Task 5, Important #1) ==="
# vm_read8's EOS read-hook (runtime/ppu/ppu_loader.cpp) used to hardcode
# src=natural on every MEDIA EOS trace line -- a claim the hook cannot back
# up (it cannot see WHY g_movie_eos_ea got armed; the real armer is the
# sibling repo ../gow2-recomp/movie_eos_arm.c, out of this engine's view).
# The fix factors the trace decision into its own tiny TU
# (runtime/ppu/ppu_movie_eos_trace.c, #include'd by ppu_loader.cpp), which
# reports the honest src=unknown unconditionally instead of asserting a
# natural/forced split this repo cannot verify end-to-end -- see that file's
# header comment. The genuine natural|forced discriminator for MEDIA already
# lives on the SEQDONE path (section 3d above), unaffected by this change.
#
# Two invocations because ps3_trace_on() caches its env-var gate on first use
# (see test_movie_eos_trace.c's header comment): default mode exercises the
# gate-ON cases, --baseline exercises the gate-OFF/no-op baseline in a fresh
# process/env.
"$MIO_CC" -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter \
    -I "$ROOT/include" -I "$ROOT/runtime/trace/tests" \
    "$ROOT/runtime/ppu/tests/test_movie_eos_trace.c" "$ROOT/runtime/ppu/ppu_movie_eos_trace.c" \
    $TRACE_SRC \
    -o "$MIO_WORK/test_movie_eos_trace" || fail "test_movie_eos_trace nao compilou"
EOST_OUT=$("$MIO_WORK/test_movie_eos_trace" 2>/dev/null) || { echo "$EOST_OUT"; fail "MEDIA EOS src=unknown (gate ON)"; }
echo "  ok: $(echo "$EOST_OUT" | tail -1)"
EOST_BASE_OUT=$(env -i PATH="$PATH" TMPDIR="${TMPDIR:-/tmp}" "$MIO_WORK/test_movie_eos_trace" --baseline 2>/dev/null) \
    || { echo "$EOST_BASE_OUT"; fail "MEDIA EOS src=unknown (baseline)"; }
echo "  ok: $(echo "$EOST_BASE_OUT" | tail -1)"

if [ "$WANT_GUI" -eq 1 ]; then
    echo "=== 4. backends RSX (precisa de window server) ==="
    BE_OUT=$("$ROOT/libs/video/tests/run_backend_smoke.sh" 2>&1) || { echo "$BE_OUT" | tail -20; fail "backends RSX"; }
    echo "$BE_OUT" | grep -q 'all backends passed' || { echo "$BE_OUT" | tail -20; fail "backends RSX"; }
    echo "  ok: $(echo "$BE_OUT" | grep 'all backends passed')"
else
    echo "=== 4. backends RSX: saltado (--no-gui) ==="
fi

echo
echo "PASS: runtime + SPU suite + suites de trace + suites Python (trace_parse/baseline_report) + movie io + HLE de async-read + portao do cellVdec + MEDIA EOS src=unknown"
