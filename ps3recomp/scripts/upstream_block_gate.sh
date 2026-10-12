#!/usr/bin/env bash
# upstream_block_gate.sh -- o PORTAO DE ADOPCAO do marco v1.4 (UPST-02).
#
# Mede a intro do GoW2 nas DUAS pontas de um bloco de upstream e escreve as
# duas no MESMO registo. A regra de ouro do marco: um bloco que nao passe o
# smoke NAO ENTRA. Para isso o portao tem de existir ANTES dos blocos de risco.
#
# Modos:
#   --congelar-baseline --ref REF        congela o baseline MEDIDO da intro
#   --bloco N --antes REF [--depois REF] julga um bloco (duas pontas)
#
# ---------------------------------------------------------------------------
# PROTOCOLO G6 (imposto aqui dentro, nao so' prometido no cabecalho)
# ---------------------------------------------------------------------------
#  (1) PS3_NO_RSX=1 **E** PS3_RSX_BACKEND=trace.
#      PS3_NO_RSX=1 sozinho NAO e' headless. O libs/video/cellGcmSys.c tem um
#      SEGUNDO registo de backend, independente do boot host: a ponte
#      rsx_bridge_mode() le' PS3_RSX_BACKEND por sua conta e, com valor
#      desconhecido, cai em `trace`; sem a variavel, fica em modo 0 (a antiga
#      via so'-sync). Fixar `trace` e' o que garante que os dois legs medem o
#      MESMO caminho de RSX, sem janela e sem leituras de memoria guest.
#
#  (2) A ordem do source importa. O env_gow2.sh so' define PS3_RSX_BACKEND
#      quando PS3_NO_RSX esta' VAZIO (no Darwin poe `metal`, com janela). Por
#      isso a recipe exporta PS3_NO_RSX/PS3_RSX_BACKEND **depois** do
#      `. env_gow2.sh`, nunca antes.
#
#  (3) PS3_ENGINE_ROOT explicito por leg. Sem ele o build_macos.sh resolve
#      `$HERE/../ps3recomp` e os DOIS legs linkariam exactamente o mesmo motor
#      -- o portao passaria sempre, por nao ter medido nada.
#
#  (4) Morte do boot SEMPRE por PID: kill -TERM, esperar 1s, kill -9. O matador
#      por PADRAO DE NOME DE PROCESSO e' PROIBIDO neste projecto (custou load 75
#      nesta maquina); a forma exacta proibida esta' escrita no CLAUDE.md e nao
#      e' reproduzida aqui de proposito.
#
#  (5) Os logs vivem em ${TMPDIR:-/tmp}, FORA dos dois repositorios. Nenhum log
#      de boot entra no indice do git (contem caminhos e conteudo do jogo).
#
# ---------------------------------------------------------------------------
# O QUE ESTE PORTAO NAO PROVA
# ---------------------------------------------------------------------------
#  - Prova NAO-REGRESSAO DA INTRO. Nunca prova que o caminho alterado pelo
#    bloco foi exercitado. Sao duas afirmacoes diferentes e ficam separadas.
#  - `exit 124`/137/143 e a morte pelo nosso proprio timeout sao NORMAIS num
#    boot que corre em loop de render. Classificados `timeout-normal`, nunca
#    falha. O `rc` cru fica SEMPRE gravado ao lado da classe: a classe explica,
#    nunca substitui a medicao.
#  - As duas falhas de toolchain do scripts/test_python.sh sao divida DECLARADA
#    (ver known_debt_python no baseline) e nunca sao regressao de um bloco.
#
# Uso:
#   scripts/upstream_block_gate.sh --congelar-baseline --ref HEAD [--runs 6]
#   scripts/upstream_block_gate.sh --bloco LV2S --antes REF [--depois REF]
#
# rc: 0 passou | 1 reprovou | 2 erro de uso / pre-requisito / baseline ausente
set -uo pipefail

# ---------------------------------------------------------------------------
# FUNCOES PURAS DE VEREDICTO
# ---------------------------------------------------------------------------
# Expostas para teste com UPSTREAM_GATE_LIB_ONLY=1 (o Plano 23-03 exercita-as
# com entradas sinteticas, sem gastar uma corrida de boot de 25 s).

# derive_rc_class RC MATAMOS_NOS -> timeout-normal | saida-0 | saida-anormal
#
# MATAMOS_NOS=1 quando o nosso timeout expirou e fomos NOS a mandar o sinal.
# Um boot morto pelo nosso TERM/-9 nunca e' falha: o titulo corre em loop de
# render e so' para porque nos o paramos.
derive_rc_class() {
    local rc="${1:-}" matamos="${2:-0}"
    if [ "$matamos" = "1" ]; then
        echo "timeout-normal"
        return 0
    fi
    case "$rc" in
        124|137|143) echo "timeout-normal"; return 0 ;;
    esac
    if [ "$rc" = "0" ]; then
        echo "saida-0"
    else
        echo "saida-anormal"
    fi
}

# perna_a_veredicto ST620_MIN RUNS ST_ANTES ST_DEPOIS CLS_ANTES CLS_DEPOIS
#   listas separadas por virgula, uma entrada por corrida.
# Imprime uma linha de diagnostico; rc 0 = passa, 1 = reprova.
perna_a_veredicto() {
    local st620_min="${1:-0}" runs="${2:-0}"
    local st_antes="${3:-}" st_depois="${4:-}"
    local cls_antes="${5:-}" cls_depois="${6:-}"
    local limiar=$(( (runs * 2 + 2) / 3 ))
    local ok_antes=0 ok_depois=0 anorm_antes=0 anorm_depois=0 v

    for v in ${st_antes//,/ }; do
        [ "${v:-0}" -ge "$st620_min" ] 2>/dev/null && ok_antes=$((ok_antes + 1))
    done
    for v in ${st_depois//,/ }; do
        [ "${v:-0}" -ge "$st620_min" ] 2>/dev/null && ok_depois=$((ok_depois + 1))
    done
    for v in ${cls_antes//,/ }; do
        [ "$v" = "saida-anormal" ] && anorm_antes=$((anorm_antes + 1))
    done
    for v in ${cls_depois//,/ }; do
        [ "$v" = "saida-anormal" ] && anorm_depois=$((anorm_depois + 1))
    done

    local motivo=""
    if [ "$ok_depois" -lt "$limiar" ]; then
        motivo="ok(depois)=$ok_depois < limiar=$limiar"
    elif [ "$ok_depois" -lt "$ok_antes" ]; then
        motivo="ok(depois)=$ok_depois < ok(antes)=$ok_antes"
    elif [ "$anorm_depois" -gt "$anorm_antes" ]; then
        motivo="saidas anormais: depois=$anorm_depois > antes=$anorm_antes"
    fi

    if [ -n "$motivo" ]; then
        printf 'perna_a=FALHA st620_min=%s limiar=%s ok_antes=%s ok_depois=%s anorm_antes=%s anorm_depois=%s motivo=%s\n' \
            "$st620_min" "$limiar" "$ok_antes" "$ok_depois" "$anorm_antes" "$anorm_depois" "$motivo"
        return 1
    fi
    printf 'perna_a=PASSA st620_min=%s limiar=%s ok_antes=%s ok_depois=%s anorm_antes=%s anorm_depois=%s\n' \
        "$st620_min" "$limiar" "$ok_antes" "$ok_depois" "$anorm_antes" "$anorm_depois"
    return 0
}

# perna_b_veredicto FAIL_ANTES FAIL_DEPOIS DIVIDA_CONHECIDA
#   listas separadas por virgula. Passa sse (FAIL_depois - FAIL_antes - divida)
#   for vazio. As duas falhas de toolchain conhecidas NUNCA contam.
perna_b_veredicto() {
    local fail_antes="${1:-}" fail_depois="${2:-}" divida="${3:-}"
    local novos="" f a d visto

    for f in ${fail_depois//,/ }; do
        [ -z "$f" ] && continue
        visto=0
        for a in ${fail_antes//,/ }; do
            [ "$f" = "$a" ] && visto=1 && break
        done
        [ "$visto" = "1" ] && continue
        for d in ${divida//,/ }; do
            [ "$f" = "$d" ] && visto=1 && break
        done
        [ "$visto" = "1" ] && continue
        novos="${novos:+$novos,}$f"
    done

    if [ -n "$novos" ]; then
        printf 'perna_b=FALHA novos=%s\n' "$novos"
        return 1
    fi
    printf 'perna_b=PASSA novos=nenhum\n'
    return 0
}

# perna_c_veredicto FICHEIROS_ALTERADOS SHA_ANTES SHA_DEPOIS
#   FICHEIROS_ALTERADOS: caminhos separados por newline (pode ser vazio).
#
# So' se exige binarios distintos quando o bloco toca codigo COMPILADO
# (^(runtime|libs|include)/). Um bloco vazio, ou so' de documentacao, passa e
# fica declarado no resumo -- e nesses casos NAO se afirma nada sobre os
# sha256: o link de 122 MB nao e' garantidamente reproduzivel byte a byte, e
# afirmar o contrario seria inventar uma propriedade que nao medimos.
perna_c_veredicto() {
    local ficheiros="${1:-}" sha_antes="${2:-}" sha_depois="${3:-}"
    local n_total n_codigo

    n_total=$(printf '%s\n' "$ficheiros" | sed '/^[[:space:]]*$/d' | wc -l | tr -d ' ')
    n_codigo=$(printf '%s\n' "$ficheiros" | sed '/^[[:space:]]*$/d' \
        | grep -cE '^(runtime|libs|include)/' || true)
    n_codigo=${n_codigo:-0}

    if [ "$n_total" -eq 0 ]; then
        printf 'perna_c=PASSA bloco_vazio=sim ficheiros=0\n'
        return 0
    fi
    if [ "$n_codigo" -eq 0 ]; then
        printf 'perna_c=PASSA bloco_sem_codigo=sim ficheiros=%s\n' "$n_total"
        return 0
    fi
    if [ "$sha_antes" = "$sha_depois" ]; then
        printf 'perna_c=FALHA ficheiros=%s compilados=%s motivo=binarios identicos -- o portao nao mediu o bloco (sha256=%s)\n' \
            "$n_total" "$n_codigo" "$sha_antes"
        return 1
    fi
    printf 'perna_c=PASSA ficheiros=%s compilados=%s binarios=distintos\n' "$n_total" "$n_codigo"
    return 0
}

# Modo biblioteca: define as funcoes acima e devolve o controlo sem construir
# nem arrancar nada.
if [ "${UPSTREAM_GATE_LIB_ONLY:-0}" = "1" ]; then
    return 0 2>/dev/null || exit 0
fi

# ---------------------------------------------------------------------------
# RESOLUCAO DE RAIZES E ARGUMENTOS
# ---------------------------------------------------------------------------
ENGINE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
GAME_ROOT="${PS3_GATE_GAME_ROOT:-$ENGINE_ROOT/../gow2-recomp}"
LIFT="${PS3_GATE_LIFT:-$GAME_ROOT/recomp_macos_v2}"
BASELINE_FILE="$ENGINE_ROOT/games/gow2/lift_baseline/INTRO_GATE_BASELINE.tsv"

# A recipe medida, numa linha so'. E' esta string literal que vai para o
# cabecalho de proveniencia do baseline: quem ler o ficheiro daqui a um mes
# tem de conseguir reproduzir a medicao sem adivinhar.
RECIPE_LITERAL='PS3_NO_RSX=1 PS3_RSX_BACKEND=trace PS3_PERF_FSM=1 PS3_MOVIE_EOS=0 (PS3_TRACE_* removidos; PS3_MOVIE_DONE_MS e PS3_VDEC_FORCE_SEQDONE_MS unset; cwd=GAME_ROOT; EBOOT.ELF)'
DIVIDA_PYTHON_DEFAULT='tools/test_ppu_lift.py,runtime/ppu/tests/test_ppu_lifter_emit.py'

MODO=""
REF_BASELINE=""
BLOCO=""
REF_ANTES=""
REF_DEPOIS=""
DIR_DEPOIS=""
BIN_ANTES=""
RUNS=""
TIMEOUT="${PS3_GATE_TIMEOUT:-25}"
OUT_DIR=""

erro_uso() {
    echo "ERRO: $*" >&2
    echo "uso: $0 --congelar-baseline --ref REF [--runs N] [--timeout S] [--out-dir DIR]" >&2
    echo "     $0 --bloco NOME --antes REF [--depois REF | --depois-arvore DIR] [--runs N] [--timeout S] [--out-dir DIR] [--antes-bin BIN]" >&2
    exit 2
}

while [ $# -gt 0 ]; do
    case "$1" in
        --congelar-baseline) MODO="baseline"; shift ;;
        --ref)               REF_BASELINE="${2:-}"; shift 2 || erro_uso "--ref precisa de um valor" ;;
        --bloco)             BLOCO="${2:-}"; shift 2 || erro_uso "--bloco precisa de um valor" ;;
        --antes)             REF_ANTES="${2:-}"; shift 2 || erro_uso "--antes precisa de um valor" ;;
        --depois)            REF_DEPOIS="${2:-}"; shift 2 || erro_uso "--depois precisa de um valor" ;;
        --depois-arvore)     DIR_DEPOIS="${2:-}"; shift 2 || erro_uso "--depois-arvore precisa de um valor" ;;
        --antes-bin)         BIN_ANTES="${2:-}"; shift 2 || erro_uso "--antes-bin precisa de um valor" ;;
        --runs)              RUNS="${2:-}"; shift 2 || erro_uso "--runs precisa de um valor" ;;
        --timeout)           TIMEOUT="${2:-}"; shift 2 || erro_uso "--timeout precisa de um valor" ;;
        --out-dir)           OUT_DIR="${2:-}"; shift 2 || erro_uso "--out-dir precisa de um valor" ;;
        -h|--help)           erro_uso "ajuda pedida" ;;
        *)                   erro_uso "argumento desconhecido: $1" ;;
    esac
done

if [ "$MODO" = "baseline" ]; then
    [ -n "$REF_BASELINE" ] || erro_uso "--congelar-baseline exige --ref REF"
    ROTULO="baseline"
    RUNS="${RUNS:-6}"
else
    MODO="julgar"
    [ -n "$BLOCO" ] || erro_uso "faltam argumentos: use --congelar-baseline ou --bloco NOME"
    [ -n "$REF_ANTES" ] || erro_uso "--bloco exige --antes REF"
    ROTULO="$BLOCO"
    RUNS="${RUNS:-3}"
    if [ -n "$REF_DEPOIS" ] && [ -n "$DIR_DEPOIS" ]; then
        erro_uso "--depois e --depois-arvore sao mutuamente exclusivos"
    fi
    # Sem nenhum dos dois, o leg `depois` e' a ARVORE VIVA (o uso pre-commit
    # que o UPST-02 pede).
    if [ -z "$REF_DEPOIS" ] && [ -z "$DIR_DEPOIS" ]; then
        DIR_DEPOIS="$ENGINE_ROOT"
    fi
fi

# T-23-01: o rotulo entra em nomes de ficheiro; validado antes de ser composto.
case "$ROTULO" in
    *[!A-Za-z0-9_.-]*|"") erro_uso "nome invalido (so' A-Za-z0-9_.-): '$ROTULO'" ;;
esac
case "$RUNS" in ''|*[!0-9]*) erro_uso "--runs tem de ser um inteiro: '$RUNS'" ;; esac
case "$TIMEOUT" in ''|*[!0-9]*) erro_uso "--timeout tem de ser um inteiro: '$TIMEOUT'" ;; esac
[ "$RUNS" -ge 1 ] || erro_uso "--runs tem de ser >= 1"

# ---------------------------------------------------------------------------
# PRE-REQUISITOS (rc=2, nomeando o caminho em falta)
# ---------------------------------------------------------------------------
for p in "$GAME_ROOT/EBOOT.ELF" "$GAME_ROOT/env_gow2.sh" "$GAME_ROOT/build_macos.sh" \
         "$GAME_ROOT/lib_boot_chain_metrics.sh"; do
    [ -f "$p" ] || { echo "ERRO: pre-requisito em falta: $p" >&2; exit 2; }
done
[ -d "$LIFT" ] || { echo "ERRO: pre-requisito em falta (dir de lift): $LIFT" >&2; exit 2; }
command -v cmake >/dev/null 2>&1 || { echo "ERRO: cmake nao encontrado no PATH" >&2; exit 2; }
command -v ninja >/dev/null 2>&1 || { echo "ERRO: ninja nao encontrado no PATH" >&2; exit 2; }

# SDKROOT -- MEDIDO nesta maquina (2026-08-12), nao assumido. O clang do host
# (xcode-select -> /Library/Developer/CommandLineTools) NAO traz sysroot por
# default: `clang -c` num ficheiro com `#include <string.h>` da' "fatal error:
# 'string.h' file not found". O cmake nao sofre com isso porque passa
# -isysroot explicito aos seus alvos; o build_macos.sh invoca clang++
# directamente e sofre -- e' exactamente a mesma causa das duas falhas de
# toolchain ja' declaradas em known_debt_python.
#
# Sem isto NENHUM leg conseguia ser construido e o portao nunca poderia medir
# coisa nenhuma. Definido SO' quando o operador nao o definiu: uma escolha
# explicita de quem corre o portao nunca e' sobreposta.
if [ -z "${SDKROOT:-}" ]; then
    _sdk="$(xcrun --show-sdk-path 2>/dev/null || true)"
    if [ -n "$_sdk" ] && [ -d "$_sdk" ]; then
        export SDKROOT="$_sdk"
    fi
fi
SDKROOT_USADO="${SDKROOT:-<nenhum>}"

# Um gate sem medicao nunca passa (mesma politica de check_boot_health.py).
if [ "$MODO" = "julgar" ] && [ ! -f "$BASELINE_FILE" ]; then
    echo "ERRO: baseline ausente: $BASELINE_FILE" >&2
    echo "      Um gate sem medicao NUNCA passa. Congele-o primeiro:" >&2
    echo "      $0 --congelar-baseline --ref HEAD" >&2
    exit 2
fi

if [ -n "$DIR_DEPOIS" ] && [ ! -d "$DIR_DEPOIS" ]; then
    echo "ERRO: --depois-arvore aponta para um directorio inexistente: $DIR_DEPOIS" >&2
    exit 2
fi

# ---------------------------------------------------------------------------
# CONGELAR OS REFS -- resolve-se cada ref para um SHA IMUTAVEL antes de
# qualquer trabalho longo.
#
# Razao MEDIDA (2026-08-12): outro plano da mesma wave commita no MESMO ramo
# enquanto o portao corre. `HEAD` e' a ponta do ramo, ou seja um alvo MOVEL:
# entre o `worktree add` do leg `antes` e o do leg `depois` -- minutos de
# build de permeio -- `HEAD` pode ja' ser outro commit, e os dois legs
# deixariam de medir o que o operador pediu sem que nada o denunciasse. O
# rotulo original fica gravado ao lado do SHA para o registo continuar legivel.
# ---------------------------------------------------------------------------
congelar_ref() {
    local ref="$1" nome="$2" sha
    sha="$(git -C "$ENGINE_ROOT" rev-parse --verify "${ref}^{commit}" 2>/dev/null)" || {
        echo "ERRO: $nome nao resolve para um commit: '$ref'" >&2
        exit 2
    }
    echo "$sha"
}

if [ "$MODO" = "baseline" ]; then
    SHA_BASELINE="$(congelar_ref "$REF_BASELINE" "--ref")"
else
    SHA_ANTES="$(congelar_ref "$REF_ANTES" "--antes")"
    if [ -n "$REF_DEPOIS" ]; then
        SHA_DEPOIS="$(congelar_ref "$REF_DEPOIS" "--depois")"
    fi
fi

# ---------------------------------------------------------------------------
# OUT_DIR -- SEMPRE fora dos dois repositorios (G6)
# ---------------------------------------------------------------------------
if [ -z "$OUT_DIR" ]; then
    OUT_DIR="${TMPDIR:-/tmp}/upstream_gate_${ROTULO}_$(date +%Y%m%d_%H%M%S)"
fi
mkdir -p "$OUT_DIR" || { echo "ERRO: nao consigo criar OUT_DIR: $OUT_DIR" >&2; exit 2; }
case "$(cd "$OUT_DIR" && pwd)" in
    "$ENGINE_ROOT"/*|"$GAME_ROOT"/*)
        echo "ERRO: OUT_DIR ($OUT_DIR) fica dentro de um repositorio -- G6 exige ${TMPDIR:-/tmp}" >&2
        exit 2 ;;
esac

TSV="$OUT_DIR/${ROTULO}-gate.tsv"
RESUMO="$OUT_DIR/${ROTULO}-gate-resumo.txt"
printf 'bloco\tleg\tref\tbinario_sha256\trun\tst620\trc\trc_class\tlog\n' > "$TSV"

# ---------------------------------------------------------------------------
# LIMPEZA -- nem worktrees pendurados nem boots orfaos, mesmo se estoirar
# ---------------------------------------------------------------------------
WORKTREES=()
BPID_ACTUAL=""

limpar() {
    local rc_saida=$?
    if [ -n "${BPID_ACTUAL:-}" ] && kill -0 "$BPID_ACTUAL" 2>/dev/null; then
        kill -TERM "$BPID_ACTUAL" 2>/dev/null
        sleep 1
        kill -0 "$BPID_ACTUAL" 2>/dev/null && kill -9 "$BPID_ACTUAL" 2>/dev/null
    fi
    local wt
    for wt in ${WORKTREES[@]+"${WORKTREES[@]}"}; do
        git -C "$ENGINE_ROOT" worktree remove --force "$wt" >/dev/null 2>&1
        [ -d "$wt" ] && rm -rf "$wt"
    done
    if [ "${#WORKTREES[@]}" -gt 0 ]; then
        git -C "$ENGINE_ROOT" worktree prune >/dev/null 2>&1
    fi
    return "$rc_saida"
}
trap limpar EXIT

# O grep do st620 vive na lib partilhada e NUNCA e' reescrito aqui.
# shellcheck source=/dev/null
source "$GAME_ROOT/lib_boot_chain_metrics.sh"

# ---------------------------------------------------------------------------
# materializar_motor LEG REF -> define WT_PATH
#
# NAO devolve o caminho por stdout de proposito: `wt=$(materializar_motor ...)`
# correria a funcao num SUBSHELL e o `WORKTREES+=(...)` morreria com ele -- o
# trap de limpeza nunca veria o worktree e ele ficaria pendurado. O resultado
# sai por variavel global.
# ---------------------------------------------------------------------------
materializar_motor() {
    local leg="$1" ref="$2"
    local wt="$OUT_DIR/wt_$leg"
    WT_PATH=""
    echo "=== [$leg] worktree destacado em '$ref' -> $wt" >&2
    git -C "$ENGINE_ROOT" worktree add --detach "$wt" "$ref" >&2 || return 1
    WORKTREES+=("$wt")
    # O build_macos.sh invoca "$PS3/.venv/bin/python" (gen_hle_nids.py). Um
    # worktree acabado de criar nao tem .venv proprio -- e' ambiente do host,
    # nao codigo do motor, por isso e' ligado por symlink em vez de recriado.
    if [ ! -e "$wt/.venv" ] && [ -d "$ENGINE_ROOT/.venv" ]; then
        ln -s "$ENGINE_ROOT/.venv" "$wt/.venv"
    fi
    WT_PATH="$wt"
    return 0
}

# ---------------------------------------------------------------------------
# construir_motor WT LEG
# ---------------------------------------------------------------------------
construir_motor() {
    local wt="$1" leg="$2"
    local jobs
    jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    echo "=== [$leg] cmake configure + build do motor ($wt/build-macos)" >&2
    cmake -B "$wt/build-macos" -G Ninja -DCMAKE_BUILD_TYPE=Release "$wt" \
        > "$OUT_DIR/${leg}_cmake_configure.log" 2>&1 || {
        echo "ERRO: [$leg] cmake configure falhou -- ver $OUT_DIR/${leg}_cmake_configure.log" >&2
        return 1
    }
    cmake --build "$wt/build-macos" -j "$jobs" \
        > "$OUT_DIR/${leg}_cmake_build.log" 2>&1 || {
        echo "ERRO: [$leg] cmake build falhou -- ver $OUT_DIR/${leg}_cmake_build.log" >&2
        return 1
    }
    if [ ! -f "$wt/build-macos/libps3recomp_runtime.a" ]; then
        echo "ERRO: [$leg] libps3recomp_runtime.a nao foi produzida em $wt/build-macos" >&2
        return 1
    fi
    return 0
}

# ---------------------------------------------------------------------------
# linkar_binario WT LEG -> define BIN_PATH (mesma razao de nao usar stdout)
# ---------------------------------------------------------------------------
linkar_binario() {
    local wt="$1" leg="$2"
    local out="$OUT_DIR/boot_gow2_$leg"
    BIN_PATH=""
    echo "=== [$leg] link do boot_gow2 (PS3_ENGINE_ROOT=$wt, FORCE_REBUILD_LIFT=${FORCE_REBUILD_LIFT:-0})" >&2
    (
        cd "$GAME_ROOT" || exit 127
        PS3_ENGINE_ROOT="$wt" \
        OUT="$out" \
        FORCE_REBUILD_LIFT="${FORCE_REBUILD_LIFT:-0}" \
        "$GAME_ROOT/build_macos.sh" "$LIFT"
    ) > "$OUT_DIR/${leg}_link.log" 2>&1 || {
        echo "ERRO: [$leg] build_macos.sh falhou -- ver $OUT_DIR/${leg}_link.log" >&2
        return 1
    }
    [ -x "$out" ] || { echo "ERRO: [$leg] binario nao produzido: $out" >&2; return 1; }
    BIN_PATH="$out"
    return 0
}

# ---------------------------------------------------------------------------
# correr_intro BIN LOG TIMEOUT  -> define RUN_RC e RUN_MATAMOS
# ---------------------------------------------------------------------------
correr_intro() {
    local bin="$1" log="$2" tmo="$3"
    (
        cd "$GAME_ROOT" || exit 127
        # A ordem e' G6: source primeiro, recipe DEPOIS (o env_gow2.sh so'
        # escolhe backend quando PS3_NO_RSX ainda esta' vazio).
        set -a; . "$GAME_ROOT/env_gow2.sh"; set +a
        unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
        export PS3_NO_RSX=1 PS3_RSX_BACKEND=trace PS3_PERF_FSM=1 PS3_MOVIE_EOS=0
        unset PS3_MOVIE_DONE_MS PS3_VDEC_FORCE_SEQDONE_MS
        # `exec` obrigatorio: sem ele $! e' o PID do subshell e o boot fica
        # orfao a 100% de CPU (ja' aconteceu nesta base de codigo).
        exec "$bin" EBOOT.ELF
    ) > "$log" 2>&1 &
    local bpid=$!
    BPID_ACTUAL="$bpid"

    local i
    for (( i = 0; i < tmo; i++ )); do
        kill -0 "$bpid" 2>/dev/null || break
        sleep 1
    done

    RUN_MATAMOS=0
    if kill -0 "$bpid" 2>/dev/null; then
        RUN_MATAMOS=1
        kill -TERM "$bpid" 2>/dev/null
        sleep 1
        kill -0 "$bpid" 2>/dev/null && kill -9 "$bpid" 2>/dev/null
    fi
    wait "$bpid" 2>/dev/null
    RUN_RC=$?
    BPID_ACTUAL=""
    return 0
}

# ---------------------------------------------------------------------------
# medir_leg LEG BIN REF SHA -> preenche LEG_ST620 / LEG_RC / LEG_CLS
# ---------------------------------------------------------------------------
medir_leg() {
    local leg="$1" bin="$2" ref="$3" sha="$4"
    local r log st cls
    LEG_ST620=""
    LEG_RC=""
    LEG_CLS=""
    for r in $(seq 1 "$RUNS"); do
        log="$OUT_DIR/${leg}_run${r}.log"
        correr_intro "$bin" "$log" "$TIMEOUT"
        st="$(extract_st620 "$log")"
        cls="$(derive_rc_class "$RUN_RC" "$RUN_MATAMOS")"
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$ROTULO" "$leg" "$ref" "$sha" "$r" "$st" "$RUN_RC" "$cls" "$log" >> "$TSV"
        printf '  [%s] run%s st620=%s rc=%s (%s)\n' "$leg" "$r" "$st" "$RUN_RC" "$cls" >&2
        LEG_ST620="${LEG_ST620:+$LEG_ST620,}$st"
        LEG_RC="${LEG_RC:+$LEG_RC,}$RUN_RC"
        LEG_CLS="${LEG_CLS:+$LEG_CLS,}$cls"
    done
}

# ---------------------------------------------------------------------------
# MODO --congelar-baseline
# ---------------------------------------------------------------------------
if [ "$MODO" = "baseline" ]; then
    materializar_motor "baseline" "$SHA_BASELINE" || exit 1
    WT="$WT_PATH"
    SHA_REF="$(git -C "$WT" rev-parse HEAD)"
    construir_motor "$WT" "baseline" || exit 1
    linkar_binario "$WT" "baseline" || exit 1
    BIN="$BIN_PATH"
    SHA_BIN="$(shasum -a 256 "$BIN" | awk '{print $1}')"

    echo "=== [baseline] ref=$REF_BASELINE ($SHA_REF) binario_sha256=$SHA_BIN" >&2
    medir_leg "baseline" "$BIN" "$SHA_REF" "$SHA_BIN"

    ST_MIN="$(printf '%s\n' "${LEG_ST620//,/$'\n'}" | sort -n | head -1)"
    ST_MED="$(printf '%s\n' "${LEG_ST620//,/$'\n'}" | sort -n | awk '{a[NR]=$1} END{print a[int((NR+1)/2)]}')"
    OK_MIN=0
    for v in ${LEG_ST620//,/ }; do
        [ "${v:-0}" -ge "$ST_MIN" ] 2>/dev/null && OK_MIN=$((OK_MIN + 1))
    done

    # Um baseline com st620_min=0 seria um limiar que NENHUMA corrida pode
    # falhar -- um gate que nasce verde para tudo nao e' um gate. Recusa-se a
    # congelar e diz-se porque: ha' pelo menos uma corrida sem um unico
    # [MOVIEFSM] st620 no log, e isso e' uma falha estrutural do boot, nao um
    # baseline.
    if [ "${ST_MIN:-0}" -le 0 ] 2>/dev/null; then
        echo "ERRO: st620_min=$ST_MIN -- pelo menos uma corrida nao produziu st620 nenhum." >&2
        echo "      Congelar isto daria um limiar que nada pode falhar. Baseline NAO escrito." >&2
        echo "      st620 por corrida: $LEG_ST620 | rc: $LEG_RC | logs em $OUT_DIR" >&2
        exit 1
    fi

    mkdir -p "$(dirname "$BASELINE_FILE")"
    {
        echo "# INTRO_GATE_BASELINE.tsv -- baseline MEDIDO da intro para o portao de adopcao"
        echo "# (scripts/upstream_block_gate.sh, Fase 23 / UPST-02, marco v1.4)."
        echo "#"
        echo "# Estes numeros sao MEDIDOS por uma corrida real, nunca citados de memoria."
        echo "# Os valores historicos conhecidos -- st620=11 na recipe de PRODUCAO com"
        echo "# Metal, e o limiar >=3 do gate antigo -- sao de OUTRAS recipes e NAO servem"
        echo "# de baseline para esta. A recipe desta medicao esta' na linha 'recipe='."
        echo "#"
        echo "# Regenerar:"
        echo "#   scripts/upstream_block_gate.sh --congelar-baseline --ref HEAD --runs 6"
        echo "#"
        echo "# PROVENIENCIA (medida nesta corrida):"
        echo "# medido_em=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "# ref=$REF_BASELINE ($SHA_REF)"
        echo "# binario_sha256=$SHA_BIN"
        echo "# runs=$RUNS"
        echo "# timeout_s=$TIMEOUT"
        echo "# recipe=$RECIPE_LITERAL"
        echo "#"
        echo "# ambiente do host: SDKROOT=$SDKROOT_USADO"
        echo "#"
        echo "# st620 por corrida desta medicao: $LEG_ST620"
        echo "# rc por corrida desta medicao:    $LEG_RC"
        echo "# classe por corrida:              $LEG_CLS"
        echo "#"
        echo "# known_debt_python: as duas falhas de toolchain do scripts/test_python.sh"
        echo "# ('string.h' file not found / COMPILE FAILED do harness). Sao divida"
        echo "# DECLARADA desde antes desta fase -- registadas em .planning/phases/"
        echo "# 22-a-que-mede-o-que-o-upstream-avancou-e-nos-nao-temos/deferred-items.md,"
        echo "# provadas pre-existentes num worktree destacado. O portao NUNCA as pode"
        echo "# ler como regressao causada por um bloco."
        printf 'chave\tvalor\n'
        printf 'st620_min\t%s\n' "$ST_MIN"
        printf 'st620_mediana\t%s\n' "$ST_MED"
        printf 'ok_runs_min\t%s\n' "$OK_MIN"
        printf 'runs\t%s\n' "$RUNS"
        printf 'timeout_s\t%s\n' "$TIMEOUT"
        printf 'known_debt_python\t%s\n' "$DIVIDA_PYTHON_DEFAULT"
    } > "$BASELINE_FILE"

    {
        echo "PORTAO DE ADOPCAO -- baseline congelado"
        echo "  ref              : $REF_BASELINE ($SHA_REF)"
        echo "  binario_sha256   : $SHA_BIN"
        echo "  runs / timeout_s : $RUNS / $TIMEOUT"
        echo "  recipe           : $RECIPE_LITERAL"
        echo "  st620 por corrida: $LEG_ST620"
        echo "  rc por corrida   : $LEG_RC"
        echo "  classes          : $LEG_CLS"
        echo "  st620_min        : $ST_MIN"
        echo "  st620_mediana    : $ST_MED"
        echo "  ok_runs_min      : $OK_MIN"
        echo "  ficheiro         : $BASELINE_FILE"
    } | tee "$RESUMO"

    echo "tsv: $TSV"
    echo "BASELINE_CONGELADO st620_min=$ST_MIN runs=$RUNS"
    exit 0
fi

# ---------------------------------------------------------------------------
# MODO --bloco -- as DUAS pontas no MESMO registo e o veredicto de tres pernas
# ---------------------------------------------------------------------------

# baseline_val CHAVE -> valor da coluna 2 do baseline congelado
baseline_val() {
    awk -F'\t' -v k="$1" '$1==k {print $2; exit}' "$BASELINE_FILE"
}

ST620_MIN="$(baseline_val st620_min)"
DIVIDA_PYTHON="$(baseline_val known_debt_python)"
case "${ST620_MIN:-}" in
    ''|*[!0-9]*)
        echo "ERRO: baseline sem st620_min utilizavel ('$ST620_MIN'): $BASELINE_FILE" >&2
        echo "      Um gate sem medicao NUNCA passa. Recongele com --congelar-baseline." >&2
        exit 2 ;;
esac

# ---------------------------------------------------------------------------
# CONJUNTO DE FICHEIROS ALTERADOS ENTRE OS LEGS
#
# Alimenta duas decisoes: (1) se a cache dos chunks liftados ainda e' fiavel,
# (2) a PERNA C (o portao mediu mesmo alguma coisa).
# ---------------------------------------------------------------------------
ARVORE_SUJA="nao"
SUJOS=""
if [ -n "$DIR_DEPOIS" ]; then
    SHA_DEPOIS="$(git -C "$DIR_DEPOIS" rev-parse HEAD 2>/dev/null)" || {
        echo "ERRO: --depois-arvore nao e' um repositorio git: $DIR_DEPOIS" >&2; exit 2; }
    SUJOS="$(git -C "$DIR_DEPOIS" status --porcelain 2>/dev/null)"
    if [ -n "$SUJOS" ]; then
        ARVORE_SUJA="sim"
    fi
    ALTERADOS="$( { git -C "$ENGINE_ROOT" diff --name-only "$SHA_ANTES".."$SHA_DEPOIS" 2>/dev/null
                    printf '%s\n' "$SUJOS" | awk 'NF {print $NF}'
                  } | sed '/^[[:space:]]*$/d' | sort -u )"
    REF_DEPOIS_ROTULO="arvore viva $DIR_DEPOIS"
else
    SHA_DEPOIS="${SHA_DEPOIS:-$SHA_ANTES}"
    ALTERADOS="$(git -C "$ENGINE_ROOT" diff --name-only "$SHA_ANTES".."$SHA_DEPOIS" 2>/dev/null \
                 | sed '/^[[:space:]]*$/d' | sort -u)"
    REF_DEPOIS_ROTULO="${REF_DEPOIS:-$REF_ANTES}"
fi

# LIMITACAO DECLARADA E IMPOSTA, nao escondida.
#
# O build_macos.sh so' recompila um chunk liftado (ppu_recomp_*.cpp.o) quando o
# proprio .cpp e' mais novo que o .o -- mas compila-os com
# -I "$PS3/include" -I "$PS3/runtime/ppu". Se o bloco alterar um CABECALHO sob
# esses dois caminhos, os objectos em cache ficam desactualizados sem que
# nenhum mtime o denuncie, e a medicao seria FALSA (o leg `depois` correria
# codigo do leg `antes`). Nesse caso forca-se a recompilacao nos DOIS legs.
#
# Um .c/.cpp alterado sob runtime/ppu/ NAO dispara a regra: esses ficheiros sao
# recompilados incondicionalmente a cada leg e nao entram na compilacao dos
# chunks liftados.
FORCE_REBUILD_LIFT=0
CABECALHOS_TOCADOS="$(printf '%s\n' "$ALTERADOS" | grep -E '^(include|runtime/ppu)/.*\.(h|hpp)$' || true)"
if [ -n "$CABECALHOS_TOCADOS" ]; then
    FORCE_REBUILD_LIFT=1
    AVISO_CACHE="AVISO: o bloco altera cabecalhos sob include/ ou runtime/ppu/ -- FORCE_REBUILD_LIFT=1 nos dois legs (custo acrescido; sem isto a medicao seria falsa). Cabecalhos: $(printf '%s' "$CABECALHOS_TOCADOS" | tr '\n' ' ')"
    echo "$AVISO_CACHE" >&2
else
    AVISO_CACHE="cache dos chunks liftados mantida (nenhum cabecalho de include/ ou runtime/ppu/ alterado)"
fi
export FORCE_REBUILD_LIFT

# ---------------------------------------------------------------------------
# correr_suite_python RAIZ LEG -> imprime a lista FAIL separada por virgulas
#
# PYTHON e' fixado ao interprete do ENGINE_ROOT nos DOIS legs: o worktree do
# leg `antes` nao tem .venv proprio e, sem fixar o interprete, a comparacao
# nao seria entre iguais.
# ---------------------------------------------------------------------------
correr_suite_python() {
    local raiz="$1" leg="$2"
    local log="$OUT_DIR/${leg}_test_python.log"
    (
        cd "$raiz" || exit 127
        PYTHON="$ENGINE_ROOT/.venv/bin/python3" "$raiz/scripts/test_python.sh"
    ) > "$log" 2>&1
    awk '/^Testes FAIL:/ {f=1; next}
         f { if ($0 ~ /^  - /) { sub(/^  - /, ""); sub(/ \(.*$/, ""); print } else { f=0 } }' \
        "$log" | sort -u | paste -sd, -
}

# ---------------------------------------------------------------------------
# LEG `antes`
# ---------------------------------------------------------------------------
if [ -n "$BIN_ANTES" ]; then
    # Um leg reutilizado SEM proveniencia deixaria de ser refutavel -- por isso
    # o sha256 e o ref de origem vao para o registo na mesma.
    [ -x "$BIN_ANTES" ] || { echo "ERRO: --antes-bin nao e' executavel: $BIN_ANTES" >&2; exit 2; }
    BIN_A="$BIN_ANTES"
    ORIGEM_A="binario reutilizado ($BIN_ANTES)"
    echo "=== [antes] binario reutilizado: $BIN_ANTES" >&2
else
    materializar_motor "antes" "$SHA_ANTES" || exit 1
    WT_A="$WT_PATH"
    construir_motor "$WT_A" "antes" || exit 1
    linkar_binario "$WT_A" "antes" || exit 1
    BIN_A="$BIN_PATH"
    ORIGEM_A="construido do worktree"
fi
SHA_BIN_A="$(shasum -a 256 "$BIN_A" | awk '{print $1}')"
medir_leg "antes" "$BIN_A" "$SHA_ANTES" "$SHA_BIN_A"
ST_ANTES="$LEG_ST620"; RC_ANTES="$LEG_RC"; CLS_ANTES="$LEG_CLS"

echo "=== [antes] suite Python" >&2
if [ -n "$BIN_ANTES" ]; then
    RAIZ_A="$ENGINE_ROOT"
else
    RAIZ_A="$WT_A"
fi
FAIL_ANTES="$(correr_suite_python "$RAIZ_A" "antes")"

# ---------------------------------------------------------------------------
# LEG `depois` -- SEQUENCIAL, nunca em paralelo com o `antes`.
#
# Razao MEDIDA: o build_macos.sh escreve os objectos de host (ppu_loader.o,
# ppu_imports.o, ppu_hle.o, ppu_sysprx.o, ppu_fs.o) dentro do proprio $LIFT,
# que os dois legs PARTILHAM. Em paralelo, um leg linkaria objectos compilados
# a partir do motor do outro. Sequencial, cada leg recompila-os do seu proprio
# PS3_ENGINE_ROOT antes de linkar, o que e' correcto.
# ---------------------------------------------------------------------------
if [ -n "$DIR_DEPOIS" ]; then
    WT_B="$(cd "$DIR_DEPOIS" && pwd)"
    echo "=== [depois] arvore viva: $WT_B (suja=$ARVORE_SUJA)" >&2
else
    materializar_motor "depois" "$SHA_DEPOIS" || exit 1
    WT_B="$WT_PATH"
fi
construir_motor "$WT_B" "depois" || exit 1
linkar_binario "$WT_B" "depois" || exit 1
BIN_B="$BIN_PATH"
SHA_BIN_B="$(shasum -a 256 "$BIN_B" | awk '{print $1}')"
medir_leg "depois" "$BIN_B" "$SHA_DEPOIS" "$SHA_BIN_B"
ST_DEPOIS="$LEG_ST620"; RC_DEPOIS="$LEG_RC"; CLS_DEPOIS="$LEG_CLS"

echo "=== [depois] suite Python" >&2
FAIL_DEPOIS="$(correr_suite_python "$WT_B" "depois")"

# ---------------------------------------------------------------------------
# VEREDICTO DE TRES PERNAS -- todas correm SEMPRE, o relatorio mostra as tres
# ---------------------------------------------------------------------------
LINHA_A="$(perna_a_veredicto "$ST620_MIN" "$RUNS" "$ST_ANTES" "$ST_DEPOIS" "$CLS_ANTES" "$CLS_DEPOIS")"; RC_A=$?
LINHA_B="$(perna_b_veredicto "$FAIL_ANTES" "$FAIL_DEPOIS" "$DIVIDA_PYTHON")"; RC_B=$?
LINHA_C="$(perna_c_veredicto "$ALTERADOS" "$SHA_BIN_A" "$SHA_BIN_B")"; RC_C=$?

ELO_CAIDO="nenhum"
if [ "$RC_A" -ne 0 ]; then
    ELO_CAIDO="intro (st620)"
elif [ "$RC_B" -ne 0 ]; then
    ELO_CAIDO="suite python"
elif [ "$RC_C" -ne 0 ]; then
    ELO_CAIDO="binarios identicos -- o portao nao mediu o bloco"
fi

N_ALTERADOS="$(printf '%s\n' "$ALTERADOS" | sed '/^[[:space:]]*$/d' | wc -l | tr -d ' ')"

{
    echo "=============================================================="
    echo " PORTAO DE ADOPCAO -- bloco $BLOCO"
    echo "=============================================================="
    echo "  baseline         : $BASELINE_FILE (st620_min=$ST620_MIN)"
    echo "  runs / timeout_s : $RUNS / $TIMEOUT"
    echo "  recipe           : $RECIPE_LITERAL"
    echo "  SDKROOT          : $SDKROOT_USADO"
    echo "  ficheiros alterados entre os legs: $N_ALTERADOS"
    echo "  $AVISO_CACHE"
    echo "--------------------------------------------------------------"
    echo "  LEG antes"
    echo "    ref            : $REF_ANTES ($SHA_ANTES)"
    echo "    origem         : $ORIGEM_A"
    echo "    binario_sha256 : $SHA_BIN_A"
    echo "    arvore_suja    : nao"
    echo "    st620          : $ST_ANTES"
    echo "    rc             : $RC_ANTES"
    echo "    rc_class       : $CLS_ANTES"
    echo "    suite FAIL     : ${FAIL_ANTES:-nenhum}"
    echo "  LEG depois"
    echo "    ref            : $REF_DEPOIS_ROTULO ($SHA_DEPOIS)"
    echo "    binario_sha256 : $SHA_BIN_B"
    echo "    arvore_suja    : $ARVORE_SUJA"
    if [ "$ARVORE_SUJA" = "sim" ]; then
        echo "    (medicao valida como uso PRE-COMMIT; NUNCA pode ser apresentada"
        echo "     como medicao de um commit limpo. Por commitar:)"
        printf '%s\n' "$SUJOS" | sed 's/^/      /'
    fi
    echo "    st620          : $ST_DEPOIS"
    echo "    rc             : $RC_DEPOIS"
    echo "    rc_class       : $CLS_DEPOIS"
    echo "    suite FAIL     : ${FAIL_DEPOIS:-nenhum}"
    echo "--------------------------------------------------------------"
    echo "  PERNA A (intro)        $([ "$RC_A" -eq 0 ] && echo PASSA || echo FALHA)   $LINHA_A"
    echo "  PERNA B (suite python) $([ "$RC_B" -eq 0 ] && echo PASSA || echo FALHA)   $LINHA_B"
    echo "     divida conhecida (nunca conta como regressao): $DIVIDA_PYTHON"
    echo "  PERNA C (mediu mesmo)  $([ "$RC_C" -eq 0 ] && echo PASSA || echo FALHA)   $LINHA_C"
    echo "--------------------------------------------------------------"
    echo "  O portao prova NAO-REGRESSAO DA INTRO. Nunca prova que o caminho"
    echo "  alterado pelo bloco foi exercitado."
    echo "=============================================================="
} | tee "$RESUMO"

echo "tsv: $TSV"
echo "resumo: $RESUMO"
if [ "$ELO_CAIDO" != "nenhum" ]; then
    echo "ELO_CAIDO=$ELO_CAIDO"
fi

if [ "$RC_A" -eq 0 ] && [ "$RC_B" -eq 0 ] && [ "$RC_C" -eq 0 ]; then
    echo "VEREDICTO=PASSOU ELO_CAIDO=nenhum"
    exit 0
fi
echo "VEREDICTO=REJEITADO ELO_CAIDO=$ELO_CAIDO"
exit 1
