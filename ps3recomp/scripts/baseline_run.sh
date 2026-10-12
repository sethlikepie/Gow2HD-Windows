#!/usr/bin/env bash
# scripts/baseline_run.sh -- Task 7 do plano bringup-instrumentacao-fase0.
#
# Automatiza os passos 1-5 da Fase 0 (docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_
# BRINGUP.md, secao "Fase 0"): gera/compila o boot_gow2 se preciso, corre-o
# headless N vezes com timeout controlado, mata cada run por PID (nunca
# pkill -- G6/CLAUDE.md, load 75 ja aconteceu por causa disso), e no fim
# escreve baseline.json (schema ps3recomp.baseline/1) via
# tools/baseline_report.py.
#
# Portabilidade: este script tem de correr no bash 3.2.57 de sistema do
# macOS (verificado -- /usr/bin/env bash --version aqui e' exactamente esse).
# Por isso: SEM mapfile/readarray, SEM ${var,,}/${var^^}, SEM `declare -A`,
# SEM `&>>` -- so' construcoes POSIX-ish (case/while/[ ]/$(( )):).
#
# Uso: scripts/baseline_run.sh [--runs N] [--timeout S] [--out DIR] [--help]
#   N default 3, S default 60, DIR default mktemp fora de ambos os repos.
#
# G7: os logs de boot (podem conter memoria do jogo) NUNCA sao commitados --
# por isso o default de --out fica sempre fora deste repo e do gow2-recomp.
set -euo pipefail

RUNS=3
TIMEOUT=60
OUT=""
# Quantas tentativas totais por indice de run quando o flake conhecido
# "vm commit (sys_memory window) failed" aparece no log (CLAUDE.md: "re-rodar,
# nao perseguir"). 3 = 1 tentativa normal + ate 2 retries; o numero de
# tentativas usadas fica sempre gravado em run_N.meta (attempts=), nunca
# escondido.
RETRY_MAX=3
FLAKE_PATTERN="vm commit (sys_memory window) failed"

usage() {
    cat <<'EOF'
Uso: baseline_run.sh [--runs N] [--timeout S] [--out DIR] [--help]

  --runs N      numero de runs headless a repetir (default: 3)
  --timeout S   segundos ate matar o boot_gow2 por PID (default: 60)
  --out DIR     directorio para logs/meta/baseline.json (default: um
                directorio temporario FORA de ambos os repos -- G7, logs de
                boot nunca sao commitados)
  --help, -h    mostra esta mensagem e sai (nao requer o workspace do jogo)

Corre ../gow2-recomp (workspace irmao, --NAO-- este repo) N vezes, headless
(PS3_NO_RSX=1 + PS3_RSX_BACKEND=trace forcado, sem janela), com os dez
subsistemas PS3_TRACE_* ligados, mata cada run por PID (kill -TERM, depois
kill -9 -- NUNCA pkill), e no fim chama tools/baseline_report.py para
escrever DIR/baseline.json.

Nota (fix pos-review, achado DONE_WITH_CONCERNS): env_gow2.sh define
PS3_RSX_BACKEND=metal no Darwin: PS3_NO_RSX=1 sozinho so impede a JANELA que
boot_macos.cpp criaria, mas cellGcmSys.c tem um SEGUNDO registo de backend
independente (rsx_bridge_mode(), guiado so por PS3_RSX_BACKEND), que abre
uma janela Metal/SDL real assim que o boot alcanca o FIFO/flip -- mesmo
headless. Por isso este script forca PS3_RSX_BACKEND=trace (backend de
logging, sem janela, confirmado in-boot) DEPOIS de fontar env_gow2.sh.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --runs)
            [ $# -ge 2 ] || { echo "baseline_run.sh: --runs precisa de um valor" >&2; exit 1; }
            RUNS="$2"; shift 2 ;;
        --timeout)
            [ $# -ge 2 ] || { echo "baseline_run.sh: --timeout precisa de um valor" >&2; exit 1; }
            TIMEOUT="$2"; shift 2 ;;
        --out)
            [ $# -ge 2 ] || { echo "baseline_run.sh: --out precisa de um valor" >&2; exit 1; }
            OUT="$2"; shift 2 ;;
        --help|-h)
            usage
            exit 0 ;;
        *)
            echo "baseline_run.sh: argumento desconhecido: $1" >&2
            usage >&2
            exit 1 ;;
    esac
done

case "$RUNS" in
    ''|*[!0-9]*)
        echo "baseline_run.sh: --runs tem de ser um inteiro positivo (recebi '$RUNS')" >&2
        exit 1 ;;
esac
[ "$RUNS" -ge 1 ] || { echo "baseline_run.sh: --runs tem de ser >= 1" >&2; exit 1; }

case "$TIMEOUT" in
    ''|*[!0-9]*)
        echo "baseline_run.sh: --timeout tem de ser um inteiro positivo (recebi '$TIMEOUT')" >&2
        exit 1 ;;
esac
[ "$TIMEOUT" -ge 1 ] || { echo "baseline_run.sh: --timeout tem de ser >= 1" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Resolucao de caminhos. GAME_DIR e' SEMPRE ../gow2-recomp relativo a este
# ficheiro (o motor), nunca ao cwd de quem chama -- para --help funcionar de
# qualquer directorio e para a falha "workspace ausente" ser clara e
# reprodutivel (requisito do brief: falha limpa, nao stack trace).
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENGINE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GAME_DIR="$(cd "$ENGINE_ROOT/.." && pwd)/gow2-recomp"

if [ ! -d "$GAME_DIR" ]; then
    echo "baseline_run.sh: workspace do jogo nao encontrado em: $GAME_DIR" >&2
    echo "  (esperado um directorio irmao 'gow2-recomp' ao lado de $ENGINE_ROOT)" >&2
    exit 1
fi

if [ -z "$OUT" ]; then
    OUT="$(mktemp -d "${TMPDIR:-/tmp}/ps3recomp_baseline.XXXXXXXX")"
else
    mkdir -p "$OUT"
    OUT="$(cd "$OUT" && pwd)"
fi
echo "baseline_run.sh: engine=$ENGINE_ROOT jogo=$GAME_DIR out=$OUT runs=$RUNS timeout=${TIMEOUT}s"

if [ ! -f "$GAME_DIR/EBOOT.ELF" ]; then
    echo "baseline_run.sh: EBOOT.ELF nao encontrado em $GAME_DIR" >&2
    exit 1
fi
if [ ! -f "$GAME_DIR/env_gow2.sh" ]; then
    echo "baseline_run.sh: env_gow2.sh nao encontrado em $GAME_DIR" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Passo 1-2: gerar/compilar. O motor (libps3recomp_runtime.a) e' sempre
# reconfigurado/recompilado a partir DESTE worktree (PS3_ENGINE_ROOT) -- ninja
# e' incremental, custo ~0 se nada mudou desde o ultimo build. O binario do
# jogo (boot_gow2) so' e' recompilado (build_macos.sh) se estiver mais antigo
# que o motor ou que o lift -- ver a nota de "regeneracao completa" abaixo.
# ---------------------------------------------------------------------------
BOOT_BIN="$GAME_DIR/boot_gow2"
LIFT_DIR="$GAME_DIR/recomp_macos_v2"
RUNTIME_LIB="$ENGINE_ROOT/build-macos/libps3recomp_runtime.a"
BUILD_LOG="$OUT/build.log"
BUILD_RAN=0
BUILD_DURATION_S=0
BUILD_SKIP_REASON=""

: > "$BUILD_LOG"
echo "baseline_run.sh: a (re)configurar/compilar o motor (cmake+ninja, $ENGINE_ROOT)..."
engine_build_start=$(date +%s)
{
    cmake -B "$ENGINE_ROOT/build-macos" -G Ninja -DCMAKE_BUILD_TYPE=Release "$ENGINE_ROOT"
    cmake --build "$ENGINE_ROOT/build-macos" -j "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
} >>"$BUILD_LOG" 2>&1
engine_build_end=$(date +%s)
BUILD_DURATION_S=$((engine_build_end - engine_build_start))

NEED_GAME_BUILD=0
if [ ! -x "$BOOT_BIN" ]; then
    NEED_GAME_BUILD=1
elif [ -f "$RUNTIME_LIB" ] && [ "$RUNTIME_LIB" -nt "$BOOT_BIN" ]; then
    NEED_GAME_BUILD=1
elif [ -f "$LIFT_DIR/ppu_recomp.h" ] && [ "$LIFT_DIR/ppu_recomp.h" -nt "$BOOT_BIN" ]; then
    NEED_GAME_BUILD=1
fi

if [ "$NEED_GAME_BUILD" -eq 1 ] && [ ! -f "$LIFT_DIR/ppu_recomp.h" ]; then
    # Sem lift nenhum (ppu_recomp.h ausente): build_macos.sh nao gera o lift,
    # so' compila um lift ja existente (ver o cabecalho desse script). Uma
    # regeneracao completa exigiria re-liftar TODOS os chunks via
    # ppu_lifter.py -- operacao muito mais pesada que compilar, com custo
    # estimado (nao medido aqui) bem acima dos 45min do requisito do brief.
    # Por isso NAO a corremos: declaramos a divergencia e seguimos com o
    # binario existente, se houver.
    BUILD_SKIP_REASON="lift ausente em $LIFT_DIR (ppu_recomp.h nao existe) -- regeneracao completa (re-lift via ppu_lifter.py) seria necessaria antes de compilar; custo estimado bem acima de 45min, NAO executado por esta task (Task 7 e' so o harness). A prosseguir com o binario boot_gow2 existente, se houver."
    echo "AVISO: $BUILD_SKIP_REASON" >&2
    NEED_GAME_BUILD=0
fi

if [ "$NEED_GAME_BUILD" -eq 1 ]; then
    echo "baseline_run.sh: boot_gow2 desactualizado -- a compilar via build_macos.sh (PS3_ENGINE_ROOT=$ENGINE_ROOT)..."
    game_build_start=$(date +%s)
    ( cd "$GAME_DIR" && PS3_ENGINE_ROOT="$ENGINE_ROOT" ./build_macos.sh ) >>"$BUILD_LOG" 2>&1
    game_build_end=$(date +%s)
    BUILD_RAN=1
    BUILD_DURATION_S=$((BUILD_DURATION_S + game_build_end - game_build_start))
else
    echo "baseline_run.sh: boot_gow2 ja esta actualizado -- a saltar build_macos.sh."
fi

if [ ! -x "$BOOT_BIN" ]; then
    echo "baseline_run.sh: $BOOT_BIN nao existe e nao foi possivel compila-lo (ver $BUILD_LOG)" >&2
    exit 1
fi
echo "baseline_run.sh: build_duration_s=$BUILD_DURATION_S build_ran=$BUILD_RAN (log: $BUILD_LOG)"

# Proveniencia do binario (path/sha256/mtime) + git rev-parse HEAD dos dois
# repos -- sempre registados, mesmo quando o build foi saltado.
BOOT_SHA256="$(shasum -a 256 "$BOOT_BIN" | awk '{print $1}')"
BOOT_MTIME_EPOCH="$(stat -f %m "$BOOT_BIN")"
BOOT_MTIME_ISO="$(date -r "$BOOT_MTIME_EPOCH" -u +"%Y-%m-%dT%H:%M:%SZ")"
COMMIT_ENGINE="$(git -C "$ENGINE_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
COMMIT_JOGO="$(git -C "$GAME_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"

{
    echo "boot_bin=$BOOT_BIN"
    echo "boot_sha256=$BOOT_SHA256"
    echo "boot_mtime_iso=$BOOT_MTIME_ISO"
    echo "commit_engine=$COMMIT_ENGINE"
    echo "commit_jogo=$COMMIT_JOGO"
    echo "build_ran=$BUILD_RAN"
    echo "build_duration_s=$BUILD_DURATION_S"
    echo "build_skip_reason=$BUILD_SKIP_REASON"
} > "$OUT/provenance.meta"

# ---------------------------------------------------------------------------
# Ambiente: fonte env_gow2.sh e SO' DEPOIS forca PS3_NO_RSX=1 + PS3_RSX_
# BACKEND=trace -- a ORDEM importa. env_gow2.sh, sourced primeiro, define
# PS3_RSX_BACKEND=metal (default Darwin, so' porque PS3_NO_RSX ainda nao
# estava definido nesse ponto).
#
# FIX (achado DONE_WITH_CONCERNS da review pos-Task-7, confirmado in-boot):
# PS3_NO_RSX=1 sozinho SO' impede boot_macos.cpp de criar A SUA PROPRIA
# janela (pick_backend() -> Backend::None, banner "[boot] RSX backend=none
# (PS3_NO_RSX)"). Mas libs/video/cellGcmSys.c tem um SEGUNDO registo de
# backend, INDEPENDENTE de PS3_NO_RSX (rsx_bridge_mode(), lido preguicosamente
# so' de PS3_RSX_BACKEND na primeira vez que o boot chega a
# cellGcmSetFlipCommand/rsx_process_method) -- com PS3_RSX_BACKEND=metal
# herdado de env_gow2.sh, esse segundo registo abre uma janela Metal/SDL REAL
# (SDL_WINDOW_SHOWN) assim que o boot alcanca o FIFO/flip, mesmo com
# PS3_NO_RSX=1. Um run curto que nunca chega la fica windowless por acaso; um
# run mais longo (o smoke real desta task, 30s) abre a janela.
#
# Por isso forcamos aqui PS3_RSX_BACKEND=trace, OVERRIDING o "metal" herdado
# de env_gow2.sh -- rsx_bridge_mode() entao regista s_trace_backend (modo 1:
# logging leve, NAO abre janela nenhuma, NAO le memoria guest), confirmado
# in-boot pelo banner "[rsx-bridge] trace backend active (no window)". O
# trade-off, tambem confirmado in-boot e documentado em tools/
# baseline_report.py: o backend trace nunca chama present(), por isso
# RSX/PRESENT (frames_apresentados) fica sempre 0 neste harness -- e' uma
# medicao real (zero presents no host), nao um valor fabricado; FLIP_REQ/
# FLIP_DONE continuam a disparar normalmente e sao o sinal de conclusao de
# flip headless (ver o cabecalho de tools/baseline_report.py).
set +u
. "$GAME_DIR/env_gow2.sh"
set -u

export PS3_NO_RSX=1
export PS3_RSX_BACKEND=trace

export PS3_TRACE_CHECKPOINTS=1
export PS3_TRACE_MEMORY=1
export PS3_TRACE_HEAP=1
export PS3_TRACE_THREADS=1
export PS3_TRACE_SPURS=1
export PS3_TRACE_DMA=1
export PS3_TRACE_FIOS=1
export PS3_TRACE_MEDIA=1
export PS3_TRACE_RSX=1
export PS3_TRACE_SNAPSHOT=1

# G8: registar EXACTAMENTE que variaveis PS3_* ficaram activas (inclui os
# forcadores herdados de env_gow2.sh, ex. PS3_MOVIE_EOS=1, e quaisquer
# PS3_VDEC_FORCE_SEQDONE_MS/PS3_MOVIE_DONE_MS que o CHAMADOR desta script ja
# tivesse no ambiente -- este script nao os define nem os apaga).
env | grep '^PS3_' | sort > "$OUT/env.txt" || true
echo "baseline_run.sh: env PS3_* activo gravado em $OUT/env.txt ($(wc -l < "$OUT/env.txt" | tr -d ' ') vars)"

# ---------------------------------------------------------------------------
# Passo 3-4: N runs headless, timeout por polling de 1s, morte SEMPRE por PID
# (kill -TERM, esperar, kill -9 -- NUNCA pkill -f, G6/CLAUDE.md).
# ---------------------------------------------------------------------------
run_idx=1
while [ "$run_idx" -le "$RUNS" ]; do
    attempt=1
    while :; do
        log="$OUT/run_${run_idx}.log"
        : > "$log"
        echo "baseline_run.sh: run $run_idx/$RUNS (tentativa $attempt/$RETRY_MAX) -> $log"

        start_ts=$(date +%s)
        ( cd "$GAME_DIR" && exec ./boot_gow2 EBOOT.ELF ) >>"$log" 2>&1 &
        pid=$!

        waited=0
        while kill -0 "$pid" 2>/dev/null; do
            if [ "$waited" -ge "$TIMEOUT" ]; then
                echo "baseline_run.sh: run $run_idx: timeout de ${TIMEOUT}s -- kill -TERM $pid"
                kill -TERM "$pid" 2>/dev/null || true
                sleep 1
                kill -9 "$pid" 2>/dev/null || true
                break
            fi
            sleep 1
            waited=$((waited + 1))
        done

        rc=0
        wait "$pid" 2>/dev/null || rc=$?
        end_ts=$(date +%s)
        duration=$((end_ts - start_ts))
        # exit 124 / morte por sinal (rc > 128) e' NORMAL aqui -- o boot corre
        # em loop de render ate ser morto pelo timeout (G6). Nao e' tratado
        # como falha do harness.

        flaked=0
        if grep -qF "$FLAKE_PATTERN" "$log" 2>/dev/null; then
            flaked=1
        fi

        if [ "$flaked" -eq 1 ] && [ "$attempt" -lt "$RETRY_MAX" ]; then
            echo "AVISO: run $run_idx: flake conhecido detectado ('$FLAKE_PATTERN') -- repetindo (tentativa $((attempt + 1))/$RETRY_MAX)" >&2
            attempt=$((attempt + 1))
            continue
        fi

        # ---------------------------------------------------------------
        # Guarda de headlessness (fix pos-review, achado DONE_WITH_CONCERNS):
        # com PS3_RSX_BACKEND=trace forcado acima, TODO run que alcance o
        # FIFO/flip deve mostrar so' o banner "[rsx-bridge] trace backend
        # active (no window)". Se, apesar disso, aparecer um banner de
        # backend COM JANELA (boot_macos.cpp OU o registo independente de
        # cellGcmSys.c), isso e' uma violacao real de G6 -- NAO aceite
        # silenciosamente: avisamos alto (stderr) E gravamos em
        # run_N.meta + $OUT/headless_violations.txt (persistido, nao so'
        # um AVISO efemero que desaparece do scrollback).
        # ---------------------------------------------------------------
        boot_none_banner=0
        if grep -q '\[boot\] RSX backend=none (PS3_NO_RSX)' "$log"; then
            boot_none_banner=1
        fi
        trace_banner=0
        if grep -q '\[rsx-bridge\] trace backend active (no window)' "$log"; then
            trace_banner=1
        fi
        window_banner=0
        window_evidence=""
        if grep -qE '\[boot\] RSX backend=(metal|vulkan|sdl)$' "$log"; then
            window_banner=1
            window_evidence="${window_evidence}[boot-backend] "
        fi
        if grep -qE '\[RSX metal\] Window created|\[rsx-bridge\] metal backend active|\[rsx-bridge\] mode=.* keeping pre-registered backend' "$log"; then
            window_banner=1
            window_evidence="${window_evidence}[rsx-bridge-window] "
        fi
        headless_verified=1
        [ "$window_banner" -eq 1 ] && headless_verified=0

        {
            echo "duration_s=$duration"
            echo "timeout_s=$TIMEOUT"
            echo "pid=$pid"
            echo "attempts=$attempt"
            echo "flaked_final=$flaked"
            echo "exit_status=$rc"
            echo "headless_verified=$headless_verified"
            echo "trace_backend_banner_seen=$trace_banner"
            echo "boot_none_banner_seen=$boot_none_banner"
            echo "window_backend_banner_seen=$window_banner"
        } > "$OUT/run_${run_idx}.meta"

        if [ "$window_banner" -eq 1 ]; then
            echo "AVISO CRITICO: run $run_idx NAO ficou headless -- banner de backend COM JANELA detectado (${window_evidence}ver $log) -- G6 violado, nao e' um AVISO cosmetico" | tee -a "$OUT/headless_violations.txt" >&2
        fi
        if [ "$boot_none_banner" -eq 0 ]; then
            echo "AVISO: run $run_idx: banner '[boot] RSX backend=none (PS3_NO_RSX)' ausente em $log -- pode so significar que o run terminou antes desse ponto do boot; confirmar manualmente se necessario" >&2
        fi

        if [ "$flaked" -eq 1 ]; then
            echo "AVISO: run $run_idx: flake persistiu apos $attempt tentativas -- log mantido para inspeccao ($log)" >&2
        elif [ "$attempt" -gt 1 ]; then
            echo "baseline_run.sh: run $run_idx: recuperou do flake apos $attempt tentativas"
        fi

        break
    done
    run_idx=$((run_idx + 1))
done

# ---------------------------------------------------------------------------
# Sumario final de headlessness -- agrega o headless_verified= gravado em
# CADA run_N.meta (acima) num unico ficheiro persistido, para nao depender
# so' de aviso efemero em stderr (requisito do fix: "warn loudly and record
# it, do not silently accept it").
# ---------------------------------------------------------------------------
: > "$OUT/headless_summary.txt"
violations=0
for f in "$OUT"/run_*.meta; do
    [ -f "$f" ] || continue
    hv=$(grep '^headless_verified=' "$f" | cut -d= -f2)
    wb=$(grep '^window_backend_banner_seen=' "$f" | cut -d= -f2)
    tb=$(grep '^trace_backend_banner_seen=' "$f" | cut -d= -f2)
    echo "$(basename "$f"): headless_verified=${hv:-?} window_backend_banner_seen=${wb:-?} trace_backend_banner_seen=${tb:-?}" >> "$OUT/headless_summary.txt"
    if [ "${wb:-0}" = "1" ]; then
        violations=$((violations + 1))
    fi
done
if [ "$violations" -gt 0 ]; then
    echo "AVISO CRITICO: $violations de $RUNS run(s) NAO ficaram headless -- ver $OUT/headless_summary.txt e $OUT/headless_violations.txt (G6 violado)" >&2
else
    echo "baseline_run.sh: headless confirmado (nenhum banner de janela) em todos os $RUNS run(s) -- ver $OUT/headless_summary.txt"
fi

# ---------------------------------------------------------------------------
# Passo 5: registar as dez metricas -> baseline.json, via tools/
# baseline_report.py (importa tools/trace_parse.py como modulo, Task 6).
# ---------------------------------------------------------------------------
PYTHON_BIN="$ENGINE_ROOT/.venv/bin/python"
if [ ! -x "$PYTHON_BIN" ]; then
    echo "baseline_run.sh: python do .venv nao encontrado em $PYTHON_BIN -- os runs e o meta ficaram em $OUT, mas baseline.json NAO foi gerado. Corra manualmente:" >&2
    echo "  <python3.11+> $ENGINE_ROOT/tools/baseline_report.py --out $OUT" >&2
    exit 1
fi

echo "baseline_run.sh: a gerar baseline.json..."
if "$PYTHON_BIN" "$ENGINE_ROOT/tools/baseline_report.py" --out "$OUT"; then
    echo "baseline_run.sh: OK -- $OUT/baseline.json"
else
    rc=$?
    echo "baseline_run.sh: baseline_report.py falhou (rc=$rc) -- os artefactos brutos continuam em $OUT." >&2
    echo "  Se for por barreira.igual_nos_runs=false, re-corra com --variacao-explicada:" >&2
    echo "  $PYTHON_BIN $ENGINE_ROOT/tools/baseline_report.py --out $OUT --variacao-explicada \"...\"" >&2
    exit "$rc"
fi
