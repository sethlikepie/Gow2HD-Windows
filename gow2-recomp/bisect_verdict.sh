#!/usr/bin/env bash
# bisect_verdict.sh -- oraculo do `git bisect run` para a Fase 9 (marco v1.1):
# classifica UM commit do ps3recomp (HERDA / RELINK / RECONSTRUCAO-COMPLETA),
# constroi o binario pelo caminho mais barato que seja VALIDO para esse
# commit, mede-o com o smoke_chain_gate.sh e devolve o veredicto no contrato
# que o `git bisect run` exige: exit 0=good, 1=bad, 125=skip.
#
# Achados da Tarefa 1 (notes/2026-07-31-fase9-fronteiras-oraculo.md) que este
# script tem de respeitar -- NAO redescobrir:
#
#   1. RUNS=1 nao chega -- ha' nao-determinismo raro (~9%) e concentrado num
#      "corrida contra o relogio" no FIM da cadeia (thr_auto_load dispara ~12
#      linhas antes do fim do log, nas corridas boas). O 09-CONTEXT.md
#      corrigiu isto: MINIMO 3 corridas por passo, veredicto por MAIORIA.
#      Nunca RUNS=1 aqui (ao contrario do que o Plano 09-01 tinha escrito
#      antes da correcao do contexto -- o contexto manda).
#   2. RELINK (variar so' o runtime, fixar um lift ja' compilado) so' e'
#      valido quando o tools/ppu_lifter.py do commit sob teste e' BYTE-A-BYTE
#      identico ao lifter que gerou o lift cacheado -- caso contrario o
#      resultado fica confundido (medido: relink do runtime de 8648805 contra
#      o lift de HOJE deu REGRESSAO 3/3, mesmo 8648805 sendo o lado GOOD --
#      o lift de hoje ja' incorpora TODAS as correcoes do lifter da janela).
#      Por isso este oraculo SO' usa RELINK quando essa igualdade byte-a-byte
#      e' confirmada; senao escala para RECONSTRUCAO-COMPLETA.
#   3. Um lift gerado por um tools/ppu_lifter.py anterior a' correcao
#      "output volta a compilar em clang" (145fe58) emite
#      `__declspec(thread)` incondicional para g_trampoline_fn -- isto NAO
#      linka em macOS/clang (incompatibilidade real de modelo TLS, nao um
#      problema de flags). Deteccao: se o link falhar especificamente com
#      "_g_trampoline_fn" undefined APOS a compilacao ter aceite
#      -fms-extensions -fdeclspec, classifica-se `exit 125` (skip) -- nunca
#      "bad".
#
# Uso:
#   ./bisect_verdict.sh <sha>                 -- classifica+constroi+mede,
#                                                 devolve 0/1/125 (contrato
#                                                 do git bisect run)
#   ./bisect_verdict.sh                        -- SEM argumento: e' o modo
#                                                 que o `git bisect run`
#                                                 realmente usa. `git bisect`
#                                                 ja fez `git checkout
#                                                 <candidato>` no worktree
#                                                 onde foi invocado ANTES de
#                                                 chamar este script -- o sha
#                                                 sob teste e' resolvido por
#                                                 `git rev-parse HEAD` NESSE
#                                                 worktree (capturado como cwd
#                                                 ANTES deste script mudar de
#                                                 directorio para $HERE).
#                                                 NUNCA passar um sha nem
#                                                 confiar em $1 quando chamado
#                                                 assim -- git bisect run nao
#                                                 injecta argumentos.
#   ./bisect_verdict.sh --classify-only <sha>  -- so' imprime o nivel
#                                                 (HERDA/RELINK/
#                                                 RECONSTRUCAO-COMPLETA), sem
#                                                 construir nada (Tarefa 3)
#   PS3_REPO=/path/to/ps3recomp                -- checkout principal (para
#                                                 `git show`/`git diff`; o
#                                                 build corre sempre num
#                                                 worktree isolado, nunca
#                                                 aqui)
#   BISECT_RUNS=3                              -- corridas por passo
#                                                 (minimo 3, nunca 1)
#   BISECT_CACHE=/tmp/bisect09_cache            -- cache de veredictos HERDA
#
# Guarda anti-producao (T-09-01): todo o build/relift corre dentro de
# /tmp/ps3recomp_bisect09_* -- o script aborta se resolver caminho fora dai.
set -uo pipefail

# CAPTURAR o cwd de invocacao ANTES de qualquer `cd` -- e' o worktree onde o
# `git bisect run` fez checkout do candidato (achado desta execucao: um bug
# anterior desta mesma tarefa fazia TODO passo do bisect devolver "ERRO:
# precisa de um sha" -- exit 2, que o git bisect trata como "bad" -- porque o
# script exigia um argumento que o `git bisect run` nunca fornece).
INVOKED_CWD="$(pwd)"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE" || exit 1

PS3_REPO="${PS3_REPO:-$HOME/Documents/PESSOAL/ps3recomp}"
[ -d "$PS3_REPO/.git" ] || PS3_REPO="$(cd "$HERE/../ps3recomp" && pwd)"

BISECT_RUNS="${BISECT_RUNS:-3}"
if [ "$BISECT_RUNS" -lt 3 ]; then
  echo "ERRO: BISECT_RUNS=$BISECT_RUNS < 3 -- o 09-CONTEXT.md corrigiu isto: minimo 3 corridas, nunca 1 (uma corrida sozinha ja' mentiu nesta sessao: pre_v3 byte-identico deu OK/OK/REGRESSAO em 3 corridas)." >&2
  exit 2
fi
BISECT_CACHE="${BISECT_CACHE:-/tmp/bisect09_cache}"
mkdir -p "$BISECT_CACHE"

WORKTREE_PREFIX="/tmp/ps3recomp_bisect09_"

# ---- referencia estavel: o lift de HOJE (recomp_macos_v3) e o lifter que o gerou ----
REF_LIFT="$HERE/recomp_macos_v3"
REF_LIFTER="$PS3_REPO/tools/ppu_lifter.py"
EBOOT="${PS3_EBOOT:-$HERE/EBOOT.ELF}"
FUNCS="${PS3_FUNCTIONS_JSON:-$HERE/functions.json}"

# =============================================================================
# classify_commit SHA -> imprime HERDA|RELINK|RECONSTRUCAO-COMPLETA
# =============================================================================
classify_commit() {
  local sha="$1"
  local parent
  parent="$(cd "$PS3_REPO" && git rev-parse "${sha}^1" 2>/dev/null)" || {
    # commit-raiz (sem pai) -- trata-se como RECONSTRUCAO-COMPLETA (nunca
    # deveria acontecer dentro da janela, mas nunca assumir HERDA por falta
    # de dados).
    echo "RECONSTRUCAO-COMPLETA"
    return
  }

  local files
  files="$(cd "$PS3_REPO" && git diff --name-only "${sha}^1..${sha}" 2>/dev/null)"

  if [ -z "$files" ]; then
    echo "HERDA"
    return
  fi

  # ---- primeiro, tirar do conjunto os ficheiros que NUNCA afectam o binario
  # (docs/notas/planning/tools nao-lifter) -- um commit misto (ex.: toca
  # runtime/ppu/ppu_loader.cpp E docs/superpowers/plans/*.md) continua RELINK,
  # nao escala para RECONSTRUCAO-COMPLETA so' por trazer prosa junto.
  local relevant
  relevant="$(printf '%s\n' "$files" | grep -vE '^(\.planning/|notes/|docs/|\.superpowers/|\.github/|\.gitignore$|games/gow2/notes/|[A-Za-z0-9_.-]+\.md$|tools/(test_|gen_|audit_)[A-Za-z0-9_]*\.py$)')"

  if [ -z "$relevant" ]; then
    echo "HERDA"
    return
  fi

  # ---- nivel 3: qualquer ficheiro relevante do pipeline de lift/patch/build
  # do GoW2, ou o proprio lifter, forca RECONSTRUCAO-COMPLETA. Isto inclui a
  # subtree(gow2) inteira (d039469 classifica aqui: traz TODO o games/gow2/
  # de uma vez, unidade atomica gracas a --first-parent).
  if printf '%s\n' "$relevant" | grep -qE '(^|/)tools/ppu_lifter\.py$'; then
    echo "RECONSTRUCAO-COMPLETA"
    return
  fi
  if printf '%s\n' "$relevant" | grep -qE '^games/gow2/'; then
    echo "RECONSTRUCAO-COMPLETA"
    return
  fi

  # ---- nivel 2: o que sobra so' pode ser runtime/libs/include/CMakeLists.txt
  if ! printf '%s\n' "$relevant" | grep -qvE '^(runtime/|libs/|include/|CMakeLists\.txt$)'; then
    echo "RELINK"
    return
  fi

  # sobrou algo que nao e' claramente RELINK (ex.: ficheiro de topo
  # desconhecido, runtime/spu/tests/out_* gerado, etc.) -- por seguranca
  # (T-09-03), tratar como RECONSTRUCAO-COMPLETA em vez de assumir barato.
  echo "RECONSTRUCAO-COMPLETA"
}

# =============================================================================
# resolve_verdict_cached SHA -> imprime good|bad|skip, resolvendo HERDA
# recursivamente pelo PRIMEIRO PAI e memorizando em BISECT_CACHE.
# =============================================================================
resolve_verdict_cached() {
  local sha="$1"
  local cache_file="$BISECT_CACHE/${sha}.verdict"
  if [ -f "$cache_file" ]; then
    cat "$cache_file"
    return
  fi

  local level
  level="$(classify_commit "$sha")"

  local verdict
  if [ "$level" = "HERDA" ]; then
    local parent
    parent="$(cd "$PS3_REPO" && git rev-parse "${sha}^1" 2>/dev/null)"
    if [ -z "$parent" ]; then
      verdict="skip"
    else
      verdict="$(resolve_verdict_cached "$parent")"
    fi
  else
    verdict="$(build_and_measure "$sha" "$level")"
  fi

  printf '%s' "$verdict" > "$cache_file"
  printf '%s' "$verdict"
}

# =============================================================================
# guarda anti-producao (T-09-01): aborta se WORKTREE nao resolver para dentro
# de /tmp/ps3recomp_bisect09_*
# =============================================================================
assert_worktree_safe() {
  local wt="$1"
  local abs
  abs="$(cd "$wt" 2>/dev/null && pwd)" || { echo "ERRO: worktree nao existe: $wt" >&2; exit 1; }
  case "$abs" in
    "${WORKTREE_PREFIX}"*) ;;
    *) echo "FATAL (T-09-01): worktree fora de ${WORKTREE_PREFIX}*: $abs" >&2; exit 1 ;;
  esac
}

# =============================================================================
# build_and_measure SHA LEVEL -> imprime good|bad|skip
# =============================================================================
build_and_measure() {
  local sha="$1" level="$2"
  local wt="${WORKTREE_PREFIX}step_${sha:0:12}"

  rm -rf "$wt"
  if ! (cd "$PS3_REPO" && git worktree add --detach "$wt" "$sha" >&2 2>&1); then
    echo "skip"
    return
  fi
  assert_worktree_safe "$wt"
  ln -sf "$PS3_REPO/.venv" "$wt/.venv"

  # rebuild da lib runtime -- sempre necessario (RELINK e RECONSTRUCAO
  # dependem dela); build apenas, NUNCA ctest (reescreveria
  # runtime/spu/tests/out_*/spu_recomp.{c,h}, que sao versionados).
  if ! (cd "$wt" && cmake -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release . >&2 2>&1 \
        && cmake --build build-macos >&2 2>&1); then
    (cd "$PS3_REPO" && git worktree remove --force "$wt" 2>/dev/null)
    echo "skip"
    return
  fi
  # nunca deixar o ctest ter corrido por engano nesta worktree -- repor.
  (cd "$wt" && git checkout -- runtime/spu/tests/ 2>/dev/null || true)

  local candidate="/tmp/bisect09_candidate_${sha:0:12}"
  local rc_measure

  if [ "$level" = "RELINK" ]; then
    # so' valido se o lifter deste commit for byte-a-byte identico ao
    # REF_LIFTER (achado da Tarefa 1: senao o resultado fica confundido).
    if ! cmp -s "$wt/tools/ppu_lifter.py" "$REF_LIFTER"; then
      echo "  [aviso] tools/ppu_lifter.py difere do lifter de referencia -- RELINK escalado para RECONSTRUCAO-COMPLETA (evita o confundimento medido na Tarefa 1)" >&2
      level="RECONSTRUCAO-COMPLETA"
    else
      rm -f "${candidate}"
      if ! (PS3_ENGINE_ROOT="$wt" OUT="$candidate" "$HERE/build_macos.sh" "$REF_LIFT" >&2 2>&1); then
        (cd "$PS3_REPO" && git worktree remove --force "$wt" 2>/dev/null)
        echo "skip"
        return
      fi
    fi
  fi

  if [ "$level" = "RECONSTRUCAO-COMPLETA" ]; then
    # apply_all_patches.sh sempre resolve o dir de lift como
    # "$REPO/${LIFT_REL#./}" (relativo a gow2-recomp) -- por isso o lift
    # fresco tem de viver DENTRO de $HERE, nunca em /tmp directamente (senao
    # o join fica "gow2-recomp//tmp/..." e falha "dir de lift nao existe").
    local fresh_lift="$HERE/recomp_macos_bisect09_${sha:0:12}"
    rm -rf "$fresh_lift"
    mkdir -p "$fresh_lift"
    # relift com o ppu_lifter.py DESTE commit, contra o EBOOT/functions.json
    # estaveis de hoje -- gera so' o lift, nao compila ainda (para podermos
    # aplicar os patches de hoje ANTES de compilar; achado desta prova: sem
    # os patches, um lift limpo fica preso na intro, st620 nunca sai de 0 --
    # a pista dos patches ja' foi refutada como causa da REGRESSAO pelo
    # Plano 08-02, mas os patches continuam NECESSARIOS so' para passar da
    # intro, gow2-recomp/patches nao e' a variavel sob teste aqui).
    if ! (python3 "$wt/tools/ppu_lifter.py" "$EBOOT" --functions "$FUNCS" -o "$fresh_lift" -j 4 >&2 2>&1); then
      rm -rf "$fresh_lift"
      (cd "$PS3_REPO" && git worktree remove --force "$wt" 2>/dev/null)
      echo "skip"
      return
    fi
    # patches de HOJE (gow2-recomp, catalogo de recomp_mid_v2/patch_*.py) --
    # mantidos constantes de propósito: a Fase 8 (08-02) ja' mediu que variar
    # o conjunto de patches nao muda o resultado (patches de hoje reproduzem
    # a mesma REGRESSAO da producao; patches de uma era antiga nem compilam,
    # por deriva de nomes nao relacionada). A variavel sob teste aqui e' o
    # ENGINE (runtime+lifter) do commit `$sha`, nao os patches.
    # apply_all_patches.sh sempre resolve o argumento como "$REPO/<arg>" --
    # passar so' o nome-base (fresh_lift ja' vive dentro de $HERE).
    (cd "$HERE" && ./apply_all_patches.sh "$(basename "$fresh_lift")" >&2 2>&1) || true
    if ! (PS3_ENGINE_ROOT="$wt" OUT="$candidate" "$HERE/build_macos.sh" "$fresh_lift" >&2 2>&1); then
      # falha de compilacao dos chunks liftados: tentar 1x com flags de
      # compatibilidade MSVC (achado da Tarefa 1: lifters anteriores a
      # 145fe58 emitem __declspec sem guarda de plataforma) -- flags de
      # COMPILADOR, nunca edicao do .cpp gerado.
      local recompiled_ok=1
      for f in "$fresh_lift"/ppu_recomp_*.cpp; do
        [ -f "$f" ] || continue
        clang++ -std=c++20 -O0 -w -fms-extensions -fdeclspec -c -I "$fresh_lift" \
          -I "$wt/include" -I "$wt/runtime/ppu" "$f" -o "$f.o" 2>/dev/null || recompiled_ok=0
      done
      if [ "$recompiled_ok" != "1" ]; then
        rm -rf "$fresh_lift"
        (cd "$PS3_REPO" && git worktree remove --force "$wt" 2>/dev/null)
        echo "skip"
        return
      fi
      # retry do build_macos.sh (sem RELIFT desta vez -- .o ja existem e sao
      # mais novos que os .cpp, a etapa 1 salta a recompilacao).
      if ! (PS3_ENGINE_ROOT="$wt" OUT="$candidate" "$HERE/build_macos.sh" "$fresh_lift" >&2 2>&1); then
        # se o link falhar especificamente por _g_trampoline_fn undefined,
        # e' a incompatibilidade REAL de modelo TLS (nao recuperavel por
        # flags) -- documentada na Tarefa 1. Skip, nunca "bad".
        rm -rf "$fresh_lift"
        (cd "$PS3_REPO" && git worktree remove --force "$wt" 2>/dev/null)
        echo "skip"
        return
      fi
    fi
  fi

  if [ ! -x "$candidate" ]; then
    (cd "$PS3_REPO" && git worktree remove --force "$wt" 2>/dev/null)
    echo "skip"
    return
  fi

  # ---- medicao: minimo BISECT_RUNS corridas, veredicto por MAIORIA --------
  local tsv="/tmp/bisect09_verdict_${sha:0:12}.tsv"
  if "$HERE/smoke_chain_gate.sh" --bin "$candidate" "$BISECT_RUNS" "$tsv" >&2 2>&1; then
    rc_measure=0
  else
    rc_measure=1
  fi

  (cd "$PS3_REPO" && git worktree remove --force "$wt" 2>/dev/null)
  rm -f "$candidate"
  [ -n "${fresh_lift:-}" ] && rm -rf "$fresh_lift"

  if [ "$rc_measure" = "0" ]; then
    echo "good"
  else
    echo "bad"
  fi
}

# =============================================================================
# main
# =============================================================================
if [ "${1:-}" = "--classify-only" ]; then
  SHA="${2:-}"
  [ -z "$SHA" ] && { echo "ERRO: --classify-only precisa de um sha" >&2; exit 2; }
  classify_commit "$SHA"
  exit 0
fi

SHA="${1:-}"
if [ -z "$SHA" ]; then
  # Modo git-bisect-run: nenhum argumento -- resolver HEAD do worktree onde
  # fomos invocados (INVOKED_CWD, capturado ANTES do `cd "$HERE"` no topo do
  # script). E' assim que o `git bisect run` realmente chama o oraculo: ja fez
  # checkout do candidato nesse worktree antes de nos invocar.
  SHA="$(git -C "$INVOKED_CWD" rev-parse HEAD 2>/dev/null)"
  if [ -z "$SHA" ]; then
    echo "ERRO: sem argumento e nao foi possivel resolver HEAD de $INVOKED_CWD -- nao e' um worktree git valido?" >&2
    exit 2
  fi
  echo "bisect_verdict: modo git-bisect-run, HEAD de $INVOKED_CWD = $SHA" >&2
fi

VERDICT="$(resolve_verdict_cached "$SHA")"
echo "bisect_verdict: $SHA -> $VERDICT" >&2

case "$VERDICT" in
  good) exit 0 ;;
  bad)  exit 1 ;;
  *)    exit 125 ;;
esac
