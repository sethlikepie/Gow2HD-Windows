#!/usr/bin/env bash
# scripts/test_runtime.sh -- configura, compila e corre a suite nativa
# (C/C++/SPU) via CTest (task F0-6).
#
# Usa o que a task F0-5 ja ligou em CMakeLists.txt (PS3RECOMP_BUILD_TESTS=ON):
# este script NAO reimplementa a descoberta/lista de testes -- so invoca
# `cmake` + `ctest` e traduz o resultado do CTest num exit code coerente.
#
# Uso: scripts/test_runtime.sh
# Variaveis de ambiente honradas:
#   PS3RECOMP_BUILD_DIR  directorio de build (default: <raiz>/build, ja no
#                        .gitignore). Reconfigurar um directorio existente e
#                        seguro -- so acrescenta -DPS3RECOMP_BUILD_TESTS=ON.
#   PYTHON               interprete Python a usar (>=3.11); se definido, e
#                        passado ao CMake como Python3_EXECUTABLE para o
#                        CTest correr com o MESMO interprete que
#                        scripts/test_python.sh usaria. Sem isto, o CMake
#                        (find_package(Python3 3.11 REQUIRED)) descobre um
#                        sozinho -- e o comportamento ja provado pela F0-5.
#
# Efeito colateral conhecido (documentado, nao escondido -- ja apontado pela
# F0-5): a suite suite.runtime_spu regenera dois tipos de coisa dentro de
# runtime/spu/tests/ a cada corrida --
#   (a) reescreve out_<nome>/spu_recomp.{c,h} de 11 casos ja VERSIONADOS
#       (git status: " M ...");
#   (b) recria out_spu_channel_blocking/ (caso adicionado depois destes
#       ficheiros terem sido versionados -- nunca chegou a ser commitado),
#       que aparece sempre como directorio NOVO nao rastreado ("?? ...").
# Isto deixaria o working tree sujo depois de CADA corrida deste script; para
# nao ser "um runner que suja a tree a cada ctest" (atrapalha CI/F0-7 e devs),
# este script repoe (a) com `git checkout --` e remove (b) com `rm -rf` no
# fim -- MAS SO quando (i) estamos dentro de um working tree git, (ii) o
# snapshot de `git status` tirado ANTES do configure/build/ctest teve
# SUCESSO (nao bastou correr -- achado da re-revisao pos-review, ronda 3: se
# o proprio `git status` FALHAR -- ex. `.git/index.lock` transitorio, git
# indisponivel --, o script distingue isso de "nao havia sujidade" em vez de
# assumir silenciosamente a segunda hipotese; se a distincao nao for
# possivel, a limpeza e' SALTADA POR INTEIRO e o script so' avisa), (iii) a
# sujidade em causa NAO EXISTIA nesse snapshot (provenance real: prova que
# FOI esta corrida que a criou, nao um scratch dir pre-existente do
# developer que calhe bater no mesmo padrao de nome -- achado da revisao
# pos-review de F0-6) e (iv) TODA a sujidade NOVA sob runtime/spu/tests/
# bate exactamente com um destes dois padroes conhecidos. Sujidade que ja
# existia ANTES desta corrida (e cuja pre-existencia foi possivel provar)
# fica SEMPRE intacta, mesmo que bata o padrao; se houver sujidade nova fora
# dos padroes conhecidos, o script tambem nao toca em nada e so avisa; e se
# o snapshot de ANTES falhar, o script nao arrisca -- salta a limpeza
# inteira e avisa, em vez de tratar "nao consegui verificar" como "nao havia
# nada". Nunca descarta uma alteracao real do developer por engano.
#
# Achado importante (task F0-6): NAO configurar com -DCMAKE_BUILD_TYPE=
# Release (o comando "padrao" do motor, ver CLAUDE.md/build-macos). O preset
# Release deste projecto injecta -DNDEBUG (confirmado: `ninja -t commands`
# mostra "-O3 -DNDEBUG" na compilacao de libs/system/tests/test_autoload_map.c),
# o que desliga assert() da libc -- e esse ficheiro de teste (registado pela
# F0-5 como "test_autoload_map" no CTest) verifica TUDO via assert(), sem
# nenhum caminho de falha alternativo. Provado por injeccao: um `assert(0 &&
# "...")` inserido numa COPIA a parte (nunca commitada) continua a imprimir
# "test_autoload_map: PASS" e sair 0 sob -DCMAKE_BUILD_TYPE=Release -- um FAIL
# real escondido pelo build type, nao pelo runner. Configurar SEM build type
# (CMake nao define nenhum default no CMakeLists.txt deste projecto) tira o
# -DNDEBUG e a mesma injeccao passa a abortar com "Assertion failed" (exit
# 134), como devia. Por isso este script configura com -DCMAKE_BUILD_TYPE=
# vazio (explicito, para tambem limpar um valor Release cacheado de uma
# corrida anterior no mesmo BUILD_DIR) em vez de Release.
#
# Exit code: 0 se o CTest reportar 0 testes falhados (SKIP nao conta como
#            falha); != 0 se configure/build falhar ou se o CTest reportar
#            pelo menos 1 falha.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# --- cmake/ctest tem de estar no PATH (achado Minor da revisao pos-review de
#     F0-6: erro amigavel, em vez de deixar o "cmake: command not found" bruto
#     do shell explicar sozinho, no mesmo espirito da mensagem de erro do
#     scripts/test_python.sh quando falta um Python >=3.11). -------------------
if ! command -v cmake > /dev/null 2>&1 || ! command -v ctest > /dev/null 2>&1; then
    cat >&2 <<'EOF'
ERRO: cmake e/ou ctest nao encontrados no PATH. scripts/test_runtime.sh
precisa dos dois para configurar, compilar e correr a suite nativa
(C/C++/SPU). Resolve de uma destas formas e corre de novo:
  - macOS (Homebrew): brew install cmake
  - Linux (Debian/Ubuntu/apt): sudo apt-get install cmake
  - confirma que ambos ficam no PATH depois de instalar
    (`cmake --version` e `ctest --version` tem de funcionar)
EOF
    exit 1
fi

BUILD_DIR="${PS3RECOMP_BUILD_DIR:-$ROOT/build}"

# --- Gerador: prefere Ninja se estiver disponivel, senao usa o default do
#     CMake para a plataforma (Unix Makefiles / Visual Studio / etc.). -----
GEN_ARGS=()
if command -v ninja > /dev/null 2>&1; then
    GEN_ARGS=(-G Ninja)
fi

# --- Interprete Python explicito (opcional, ver cabecalho) ----------------
CMAKE_PY_ARGS=()
if [ -n "${PYTHON:-}" ]; then
    CMAKE_PY_ARGS=(-DPython3_EXECUTABLE="$PYTHON")
fi

# --- Numero de jobs paralelos: nproc (Linux) ou sysctl (macOS/BSD) --------
if command -v nproc > /dev/null 2>&1; then
    JOBS="$(nproc)"
elif command -v sysctl > /dev/null 2>&1; then
    JOBS="$(sysctl -n hw.ncpu)"
else
    JOBS=2
fi

# --- Snapshot do git ANTES de configurar/compilar (provenance real) -------
# Achado da revisao pos-review (F0-6): o cleanup no fim deste script (ver
# nota no cabecalho) NAO pode decidir o que apagar so' pela FORMA do path
# (ex.: "?? runtime/spu/tests/out_[^/]+/") -- isso apagaria por engano um
# directorio de scratch do PROPRIO developer que calhe usar o mesmo padrao
# de nome, mesmo que o ctest desta corrida nunca lhe tenha tocado. Por isso
# tira-se aqui um snapshot de `git status --porcelain -- runtime/spu/tests`
# ANTES de qualquer configure/build/ctest; no fim, so' se mexe no que
# TRANSITOU de ausente para presente NESTA corrida (before -> depois) --
# nunca em sujidade que ja existia antes de comecar, mesmo que bata o
# padrao de nome conhecido.
IS_GIT_WORKTREE=0
BEFORE_DIRTY=""
BEFORE_SNAPSHOT_OK=0
if git -C "$ROOT" rev-parse --is-inside-work-tree > /dev/null 2>&1; then
    IS_GIT_WORKTREE=1
    # Achado da re-revisao pos-review (ronda 3): NAO usar "|| true" aqui --
    # isso colapsava "nao havia sujidade" e "o proprio `git status` FALHOU"
    # (ex.: `.git/index.lock` transitorio, git indisponivel) na MESMA string
    # vazia; mais abaixo, uma BEFORE_DIRTY vazia por QUALQUER motivo caia no
    # ramo que trata TODA a sujidade actual como criada por esta corrida --
    # reabrindo exactamente o buraco pre-fix (rm -rf num scratch dir
    # pre-existente do developer que calhe bater no padrao). O idioma
    # `if VAR="$(cmd)"; then` classico apanha o exit code de `cmd` sem
    # disparar `set -e` (a condicao de um `if` fica sempre isenta de
    # errexit) e sem precisar de "|| true" -- por isso da' para distinguir
    # os dois casos em vez de os colapsar.
    if BEFORE_DIRTY="$(git -C "$ROOT" status --porcelain -- runtime/spu/tests 2>/dev/null)"; then
        BEFORE_SNAPSHOT_OK=1
    else
        BEFORE_DIRTY=""
        echo "AVISO: 'git status --porcelain -- runtime/spu/tests' FALHOU antes do configure/build/ctest (nao voltou vazio -- o proprio comando deu erro). Sem esta prova positiva de que NAO havia sujidade previa, a limpeza automatica no fim deste script vai ser SALTADA por inteiro: mais seguro deixar a tree suja do que arriscar apagar algo que nao se pode provar que esta corrida criou." >&2
    fi
fi

echo "== test_runtime.sh: raiz=$ROOT build_dir=$BUILD_DIR jobs=$JOBS =="
echo

echo "=== 1. configure (PS3RECOMP_BUILD_TESTS=ON) ==="
# NOTA de portabilidade: "${arr[@]}" sozinho, com o array vazio, e' "unbound
# variable" sob `set -u` no bash 3.2 (o /bin/bash de sistema no macOS -- so'
# foi corrigido no bash 4.4). "${arr[@]+"${arr[@]}"}" e' o idioma classico
# que evita isso em qualquer bash, incl. 3.2: expande para nada se o array
# estiver vazio, ou para os elementos (cada um a sua propria word) senao.
cmake -B "$BUILD_DIR" ${GEN_ARGS[@]+"${GEN_ARGS[@]}"} \
    -DCMAKE_BUILD_TYPE= \
    -DPS3RECOMP_BUILD_TESTS=ON \
    ${CMAKE_PY_ARGS[@]+"${CMAKE_PY_ARGS[@]}"} \
    "$ROOT"
echo

echo "=== 2. build ==="
cmake --build "$BUILD_DIR" -j"$JOBS"
echo

echo "=== 3. ctest ==="
# Guarda tambem o stdout do ctest (alem de o mostrar ao vivo via tee) para o
# cross-check de SKIP logo a seguir. "${PIPESTATUS[0]}" (bash) apanha o exit
# code do PROPRIO ctest, nao o do tee -- necessario porque, numa pipeline, "$?"
# sozinho devolveria o exit code do ULTIMO comando (tee), sempre 0.
CTEST_STDOUT_LOG="$BUILD_DIR/test_runtime_ctest_stdout.log"
ctest --test-dir "$BUILD_DIR" --output-on-failure 2>&1 | tee "$CTEST_STDOUT_LOG"
CTEST_RC=${PIPESTATUS[0]}
echo

# --- Cross-check do SKIP_REGULAR_EXPRESSION do CMakeLists.txt -------------
# ACTUALIZADO na revisao pos-review (2 tasks depois da F0-6): o achado desta
# secao ERA que o `^` de SKIP_REGULAR_EXPRESSION "^SKIP:" (CMake) ancora ao
# INICIO DE TODO O OUTPUT capturado, nao ao inicio de cada linha -- por isso
# py.test_ppu_lift (que imprime 7 linhas de checks fpscr/vrsave ANTES do
# "SKIP: ...", quando falta um compilador C++ host) era classificado
# "Passed" pelo CTest em vez de "Skipped". Isso ESTA CORRIGIDO em
# CMakeLists.txt (PS3RECOMP_SKIP_REGEX mudou de "^SKIP:" para "SKIP:", sem
# ancora -- ver comentario la e task-F0-5-report.md secao "Correcoes
# pos-review"): reproduzido com
# `env -i PATH=/opt/homebrew/bin ctest --test-dir build -R py.test_ppu_lift -V`
# apos o fix, o mesmo cenario agora reporta "***Skipped", nao "Passed".
#
# O cross-check abaixo fica-- ja nao para "mitigar" um bug conhecido, mas
# como guarda de regressao barata (sem tentar atribuir por-teste, o que
# exigiria fazer parse do formato interno, nao documentado, do
# LastTest.log): conta quantas linhas "SKIP:" aparecem no log completo de
# TODOS os testes vs quantos testes o proprio CTest classificou como
# Skipped. Com o fix, os dois numeros devem ser SEMPRE iguais; se um dia
# houver mais SKIP: do que Skipped reportados, isso significa que o anchor
# do regex voltou (regressao em CMakeLists.txt) ou que um novo self-test
# imprime "SKIP:" fora de um caminho de skip genuino -- avisa em qualquer um
# dos casos (nao falha: nenhum teste em si reportou um FAIL genuino).
LASTTEST_LOG="$BUILD_DIR/Testing/Temporary/LastTest.log"
if [ -f "$LASTTEST_LOG" ]; then
    SKIP_LINES_FOUND="$(grep -c '^SKIP:' "$LASTTEST_LOG" 2>/dev/null || true)"
    CTEST_REPORTED_SKIPPED="$(grep -Fc '***Skipped' "$CTEST_STDOUT_LOG" 2>/dev/null || true)"
    [ -z "$SKIP_LINES_FOUND" ] && SKIP_LINES_FOUND=0
    [ -z "$CTEST_REPORTED_SKIPPED" ] && CTEST_REPORTED_SKIPPED=0
    if [ "$SKIP_LINES_FOUND" -gt "$CTEST_REPORTED_SKIPPED" ]; then
        echo "AVISO: $LASTTEST_LOG tem $SKIP_LINES_FOUND linha(s) \"SKIP:\" mas o" >&2
        echo "CTest so' classificou $CTEST_REPORTED_SKIPPED teste(s) como Skipped --" >&2
        echo "PS3RECOMP_SKIP_REGEX ja foi corrigido para \"SKIP:\" sem ancora em" >&2
        echo "CMakeLists.txt, por isso isto e' inesperado: ou o anchor \"^\" voltou" >&2
        echo "(regressao) ou um self-test novo imprime \"SKIP:\" fora de um caminho" >&2
        echo "de skip genuino. Confirma manualmente com:" >&2
        echo "  grep -B5 '^SKIP:' '$LASTTEST_LOG'" >&2
        echo "Isto NAO e' um FAIL (nenhum teste reportou falha genuina), so' um" >&2
        echo "SKIP que o resumo do CTest nao esta a mostrar como tal." >&2
    fi
fi
echo

# --- Repor ficheiros gerados versionados que a suite SPU reescreve --------
# (ver nota no cabecalho e o snapshot BEFORE_DIRTY tirado antes do
# configure/build, mais acima). So actua se isto for um working tree git, o
# snapshot de ANTES tiver tido sucesso (BEFORE_SNAPSHOT_OK -- se o `git
# status` de ANTES falhou, nao ha' prova do que e' pre-existente, por isso
# a limpeza e' saltada por inteiro em vez de arriscar, ver bloco abaixo) E a
# sujidade NOVA (que nao estava no snapshot de antes) corresponder
# exactamente ao padrao conhecido; sujidade PRE-EXISTENTE (provada pelo
# snapshot de antes) fica sempre intacta -- nunca e' candidata a apagar,
# mesmo que bata o mesmo padrao de nome -- e sujidade nova fora do padrao
# conhecido so' gera aviso, nunca e' descartada.
if [ "$IS_GIT_WORKTREE" -eq 1 ] && [ "$BEFORE_SNAPSHOT_OK" -ne 1 ]; then
    echo "== snapshot 'git status' ANTES da corrida falhou (aviso ja emitido mais acima) -- a saltar a limpeza automatica de runtime/spu/tests/ POR INTEIRO: sem essa prova nao ha' como distinguir sujidade criada por esta corrida de sujidade pre-existente do developer, e mais seguro nao apagar nada ==" >&2
    echo
elif [ "$IS_GIT_WORKTREE" -eq 1 ]; then
    DIRTY="$(git -C "$ROOT" status --porcelain -- runtime/spu/tests 2>/dev/null || true)"
    if [ -n "$DIRTY" ]; then
        # Provenance: NEW_DIRTY = linhas em DIRTY que NAO estavam em
        # BEFORE_DIRTY (isto e', sujidade que so' passou a existir DEPOIS do
        # snapshot tirado antes do configure/build/ctest -- prova real de
        # que esta corrida a criou). PRE_EXISTING = linhas identicas nos
        # dois snapshots (sujidade que ja' estava la' antes de comecar,
        # alheia a esta corrida). `comm` exige input ordenado -- daqui o
        # `sort` em ambos os lados antes de comparar.
        if [ -n "$BEFORE_DIRTY" ]; then
            NEW_DIRTY="$(comm -13 \
                <(printf '%s\n' "$BEFORE_DIRTY" | sort) \
                <(printf '%s\n' "$DIRTY" | sort))"
            PRE_EXISTING="$(comm -12 \
                <(printf '%s\n' "$BEFORE_DIRTY" | sort) \
                <(printf '%s\n' "$DIRTY" | sort))"
        else
            NEW_DIRTY="$DIRTY"
            PRE_EXISTING=""
        fi

        if [ -n "$PRE_EXISTING" ]; then
            echo "== runtime/spu/tests/ ja tinha sujidade ANTES desta corrida (nao criada por este ctest) -- deixada INTACTA, mesmo que bata o padrao conhecido:" >&2
            printf '%s\n' "$PRE_EXISTING" >&2
        fi

        if [ -n "$NEW_DIRTY" ]; then
            TRACKED_MOD="$(printf '%s\n' "$NEW_DIRTY" | grep -E '^ M runtime/spu/tests/out_[^/]+/spu_recomp\.(c|h)$' || true)"
            UNTRACKED_NEW="$(printf '%s\n' "$NEW_DIRTY" | grep -E '^\?\? runtime/spu/tests/out_[^/]+/$' || true)"
            KNOWN_COUNT=0
            [ -n "$TRACKED_MOD" ] && KNOWN_COUNT=$((KNOWN_COUNT + $(printf '%s\n' "$TRACKED_MOD" | wc -l)))
            [ -n "$UNTRACKED_NEW" ] && KNOWN_COUNT=$((KNOWN_COUNT + $(printf '%s\n' "$UNTRACKED_NEW" | wc -l)))
            NEW_COUNT="$(printf '%s\n' "$NEW_DIRTY" | wc -l)"

            if [ "$KNOWN_COUNT" -eq "$NEW_COUNT" ]; then
                echo "== a suite SPU regenerou saida versionada/gerada NESTA corrida (efeito colateral conhecido, F0-5); repondo =="
                printf '%s\n' "$NEW_DIRTY"
                if [ -n "$TRACKED_MOD" ]; then
                    printf '%s\n' "$TRACKED_MOD" | sed -E 's/^ M //' | while IFS= read -r f; do
                        git -C "$ROOT" checkout -- "$f"
                    done
                fi
                if [ -n "$UNTRACKED_NEW" ]; then
                    printf '%s\n' "$UNTRACKED_NEW" | sed -E 's/^\?\? //' | while IFS= read -r d; do
                        rm -rf "${ROOT:?}/${d:?}"
                    done
                fi
                echo "== working tree reposta (so' a sujidade criada por esta corrida) =="
            else
                echo "AVISO: runtime/spu/tests/ tem sujidade NOVA (criada nesta corrida) ALEM dos padroes conhecidos (out_*/spu_recomp.{c,h} reescrito, out_*/ novo) -- NAO revertido automaticamente para nao descartar trabalho real:" >&2
                printf '%s\n' "$NEW_DIRTY" >&2
            fi
        fi
        echo
    fi
fi

echo "=================================================="
if [ "$CTEST_RC" -eq 0 ]; then
    echo "test_runtime.sh -- ctest reportou 0 falhas"
    echo "=================================================="
    exit 0
else
    echo "test_runtime.sh -- ctest reportou falha(s) (exit $CTEST_RC)"
    echo "=================================================="
    exit 1
fi
