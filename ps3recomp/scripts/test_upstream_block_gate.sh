#!/usr/bin/env bash
# scripts/test_upstream_block_gate.sh -- auto-teste do PORTAO DE ADOPCAO
# (scripts/upstream_block_gate.sh, Fase 23 / UPST-02, marco v1.4).
#
# Porque existe: o Plano 23-01 entregou o portao e viu-o VERDE. Ver verde nao
# prova nada sobre a capacidade de REPROVAR. Este auto-teste exercita as
# funcoes de veredicto do portao nos DOIS sentidos, com vectores sinteticos
# deterministas, e transforma as regras G6 em guardas permanentes da CI em vez
# de promessas de cabecalho.
#
# Corre em SEGUNDOS: nenhum build, nenhum boot, nenhum worktree. As funcoes
# puras do portao sao obtidas com UPSTREAM_GATE_LIB_ONLY=1, que faz o portao
# definir `derive_rc_class`/`perna_a_veredicto`/`perna_b_veredicto`/
# `perna_c_veredicto` e devolver o controlo sem construir nem arrancar nada.
#
# Grupos:
#   1  disciplina G6, verificavel no proprio ficheiro do portao
#   2  derive_rc_class -- o nosso timeout nunca e' lido como falha do jogo
#   3  perna_b_veredicto -- a divida de toolchain declarada nunca e' regressao
#   4  perna_c_veredicto -- um bloco nao medido nunca passa
#   5  perna_a_veredicto -- a intro, por vectores (a corrida real e' o 23-03/T2)
#   6  pre-requisitos do portao -- SO' com checkout do jogo; sem ele, salta
#
# Os Grupos 1 a 5 correm em QUALQUER maquina (incluindo a CI, que nao tem o
# checkout do jogo nem os 20 GB de dados). O Grupo 6 imprime a linha de salto
# da convencao do projecto e sai 0.
#
# Uso: bash scripts/test_upstream_block_gate.sh
# Honra PS3_GATE_GAME_ROOT (mesma variavel que o portao usa).
#
# rc: 0 se nao houver nenhum FAIL; 1 caso contrario.
set -uo pipefail

ESTE="${BASH_SOURCE[0]:-$0}"
ROOT="$(cd "$(dirname "$ESTE")/.." && pwd)"
GATE="$ROOT/scripts/upstream_block_gate.sh"
BASELINE="$ROOT/games/gow2/lift_baseline/INTRO_GATE_BASELINE.tsv"
GAME_ROOT="${PS3_GATE_GAME_ROOT:-$ROOT/../gow2-recomp}"

N_PASS=0
N_FAIL=0

pass() { N_PASS=$((N_PASS + 1)); printf 'PASS %s\n' "$*"; }
falha() { N_FAIL=$((N_FAIL + 1)); printf 'FAIL %s\n' "$*"; }

# igual NOME ESPERADO OBTIDO
igual() {
    if [ "$2" = "$3" ]; then pass "$1 [$3]"; else falha "$1 esperado='$2' obtido='$3'"; fi
}
# ao_menos NOME MINIMO OBTIDO
ao_menos() {
    if [ "${3:-0}" -ge "$2" ] 2>/dev/null; then pass "$1 [$3 >= $2]"
    else falha "$1 esperado >= $2, obtido='$3'"; fi
}
# contem NOME AGULHA PALHEIRO
contem() {
    case "$3" in
        *"$2"*) pass "$1" ;;
        *) falha "$1 -- saida sem '$2': $3" ;;
    esac
}

[ -f "$GATE" ] || { echo "FAIL portao ausente: $GATE"; echo "resumo: PASS=0 FAIL=1"; exit 1; }

# Linhas de CODIGO do portao (sem comentarios). O cabecalho do portao explica
# as regras G6 em prosa; contar comentarios faria este teste medir a
# documentacao em vez do codigo -- exactamente o erro que ele existe para
# impedir.
codigo() { grep -v '^[[:space:]]*#' "$GATE"; }

# ---------------------------------------------------------------------------
# corpo FUNCAO -- imprime o CORPO da funcao homonima do portao, sem comentarios.
# Abre em `nome() {` ou `function nome` na coluna 0 e fecha na primeira `}` na
# coluna 0 (o estilo do portao inteiro). rc=1 se a funcao nao existir.
#
# Existe porque contar um token no FICHEIRO INTEIRO nao mede disciplina nenhuma:
# o portao tem, para cada token de G6, uma ocorrencia PORTADORA (a que faz o
# trabalho) e uma DECORATIVA (uma mensagem de log, ou a string de proveniencia
# RECIPE_LITERAL). Um limiar de >=1 sobre o ficheiro fica verde quando se apaga
# a portadora e sobra a decorativa -- MEDIDO: as cinco disciplinas de G1.3 a
# G1.7 removidas de uma so' vez davam PASS=42 FAIL=0.
# ---------------------------------------------------------------------------
corpo() {
    local f="$1" out
    out="$(awk -v f="$f" '
        !dentro && ($0 ~ "^"f"\\(\\)[[:space:]]*\\{" || $0 ~ "^function[[:space:]]+"f"([[:space:]({]|$)") { dentro=1; next }
        dentro && /^\}/ { exit }
        dentro { print }
    ' "$GATE" | grep -v '^[[:space:]]*#')"
    [ -n "$out" ] || return 1
    printf '%s\n' "$out"
}

# ---------------------------------------------------------------------------
# constructo NOME FUNCAO ERE
#
# Exige o CONSTRUCTO (nao o token) dentro da FUNCAO que a disciplina obriga.
# Dois niveis, para reprovar a mutacao real sem gritar por um refactor legitimo:
#
#   nivel 1  o constructo tem de existir no CODIGO do portao. Um comentario, uma
#            mensagem de `echo` ou uma string de proveniencia nunca o satisfazem
#            -- as ERE exigem forma de atribuicao/comando com operando.
#   nivel 2  tem de estar DENTRO da funcao ancora. Se existir no ficheiro mas
#            fora dela (ou se a funcao mudou de nome), a mensagem diz-lhe qual
#            dos dois casos e', porque o remedio e' diferente: mover a ancora
#            deste guarda (uma palavra) ou repor a disciplina.
# ---------------------------------------------------------------------------
constructo() {
    local nome="$1" fn="$2" ere="$3" body n_fn n_fich
    n_fich="$(codigo | grep -cE "$ere" || true)"
    if ! body="$(corpo "$fn")"; then
        falha "$nome -- a funcao '$fn()' nao existe no portao (constructo no resto do ficheiro: ${n_fich:-0}). Se foi RENOMEADA, actualiza a ancora deste guarda; nao o apagues."
        return 1
    fi
    n_fn="$(printf '%s\n' "$body" | grep -cE "$ere" || true)"
    if [ "${n_fn:-0}" -ge 1 ]; then
        pass "$nome [$n_fn em $fn()]"
        return 0
    fi
    if [ "${n_fich:-0}" -ge 1 ]; then
        falha "$nome -- o constructo existe no portao (${n_fich}x) mas FORA de '$fn()'. Se o moveste de proposito, actualiza a ancora deste guarda."
    else
        falha "$nome -- DISCIPLINA G6 AUSENTE: '$fn()' nao contem nenhum /$ere/ e o portao tambem nao, em lado nenhum."
    fi
    return 1
}

# ordem NOME FUNCAO ERE_PRIMEIRO ERE_DEPOIS -- G6(2) e' uma regra de ORDEM, e
# uma ordem nao se mede contando ocorrencias.
ordem() {
    local nome="$1" fn="$2" e1="$3" e2="$4" body n1 n2
    if ! body="$(corpo "$fn")"; then
        falha "$nome -- a funcao '$fn()' nao existe no portao"
        return 1
    fi
    n1="$(printf '%s\n' "$body" | grep -nE "$e1" | head -1 | cut -d: -f1)"
    n2="$(printf '%s\n' "$body" | grep -nE "$e2" | head -1 | cut -d: -f1)"
    if [ -z "$n1" ] || [ -z "$n2" ]; then
        falha "$nome -- falta um dos dois lados em $fn() (primeiro='${n1:-ausente}' depois='${n2:-ausente}')"
        return 1
    fi
    if [ "$n1" -lt "$n2" ]; then pass "$nome [linha $n1 antes de $n2 em $fn()]"
    else falha "$nome -- ORDEM INVERTIDA em $fn(): linha $n1 nao vem antes de $n2"; fi
}

echo "== Grupo 1: disciplina G6 verificavel no ficheiro do portao =="

# O token do matador por PADRAO DE NOME DE PROCESSO e' proibido neste projecto
# (custou load 75 nesta maquina). E' construido aqui por concatenacao em tempo
# de execucao: se este ficheiro contivesse o literal, seria a sua propria
# contra-prova (T-23-13).
TOK_PROIBIDO="p""kill"
igual "G1.1 token do matador por nome: zero ocorrencias no portao" \
    "0" "$(grep -c -- "$TOK_PROIBIDO" "$GATE" || true)"
igual "G1.2 token do matador por nome: zero ocorrencias neste proprio teste" \
    "0" "$(grep -c -- "$TOK_PROIBIDO" "$ESTE" || true)"

# G1.3/G1.4 -- a morte por PID tem DOIS sitios obrigatorios e distintos:
# correr_intro (o nosso timeout expirou) e o trap limpar (saimos a meio). A ERE
# exige um OPERANDO variavel: 'kill -TERM' escrito em prosa nunca a satisfaz.
constructo "G1.3 correr_intro mata o boot por PID (kill -TERM \$pid)" \
    correr_intro '(^|[[:space:];&|(])kill[[:space:]]+(-TERM|-s[[:space:]]+(TERM|15))[[:space:]]+"?\$'
constructo "G1.4 correr_intro escala para kill -9 \$pid" \
    correr_intro '(^|[[:space:];&|(])kill[[:space:]]+(-9|-KILL|-s[[:space:]]+(KILL|9))[[:space:]]+"?\$'
ordem "G1.4a correr_intro escala na ordem certa (TERM antes de -9)" correr_intro \
    '(^|[[:space:];&|(])kill[[:space:]]+(-TERM|-s[[:space:]]+(TERM|15))[[:space:]]+"?\$' \
    '(^|[[:space:];&|(])kill[[:space:]]+(-9|-KILL|-s[[:space:]]+(KILL|9))[[:space:]]+"?\$'
constructo "G1.4b o trap de limpeza tambem mata por PID (sem boots orfaos)" \
    limpar '(^|[[:space:];&|(])kill[[:space:]]+(-TERM|-9|-KILL|-s[[:space:]]+(TERM|KILL|9|15))[[:space:]]+"?\$'

# G1.5/G1.6 -- a recipe headless tem de ser EXPORTADA no processo que arranca o
# boot. A ERE exige forma de atribuicao no inicio da linha: a string de
# proveniencia RECIPE_LITERAL='PS3_NO_RSX=1 ...' (que existe para o cabecalho do
# baseline, e e' registo, nao recipe) nunca a satisfaz.
constructo "G1.5 correr_intro EXPORTA PS3_NO_RSX=1 (nao so' o nomeia)" \
    correr_intro '^[[:space:]]*(export[[:space:]]+.*)?PS3_NO_RSX=1([[:space:]]|$)'
constructo "G1.6 correr_intro EXPORTA PS3_RSX_BACKEND=trace (nao so' o nomeia)" \
    correr_intro '^[[:space:]]*(export[[:space:]]+.*)?PS3_RSX_BACKEND=trace([[:space:]]|$)'
ordem "G1.6a G6(2): a recipe e' exportada DEPOIS do . env_gow2.sh" correr_intro \
    'env_gow2\.sh' \
    '^[[:space:]]*export[[:space:]]+.*PS3_NO_RSX=1'

# G1.7 -- a razao de ser do portao. Sem esta ATRIBUICAO os dois legs linkam o
# mesmo motor e o portao passa sempre por nao ter medido nada. O `echo` da mesma
# funcao imprime 'PS3_ENGINE_ROOT=$wt' e nao conta: mencionar nao e' atribuir.
constructo "G1.7 linkar_binario ATRIBUI PS3_ENGINE_ROOT por leg (mencionar nao chega)" \
    linkar_binario '^[[:space:]]*(export[[:space:]]+)?PS3_ENGINE_ROOT=("?\$|"[^"]*\$)'

igual "G1.8 zero 'eval' no codigo do portao (T-23-01)" \
    "0" "$(codigo | grep -cE '(^|[[:space:];&|(])eval([[:space:]]|$)' || true)"

echo "== Grupo 2: derive_rc_class -- o nosso timeout nao e' falha do jogo =="

# Modo biblioteca: define as funcoes puras e devolve o controlo. Feito por
# `source` dentro DESTE processo -- `bash "$GATE"` faria o `return` falhar e o
# `exit 0` de recurso mataria o subprocesso antes de nos dar as funcoes.
UPSTREAM_GATE_LIB_ONLY=1
export UPSTREAM_GATE_LIB_ONLY
# shellcheck source=/dev/null
. "$GATE"

if ! command -v derive_rc_class >/dev/null 2>&1; then
    falha "G2.0 UPSTREAM_GATE_LIB_ONLY=1 nao expos as funcoes de veredicto"
    echo "resumo: PASS=$N_PASS FAIL=$N_FAIL"
    exit 1
fi
pass "G2.0 UPSTREAM_GATE_LIB_ONLY=1 expos as funcoes de veredicto"

igual "G2.1 rc=143 sem termos matado -> timeout-normal" "timeout-normal" "$(derive_rc_class 143 0)"
igual "G2.2 rc=137 sem termos matado -> timeout-normal" "timeout-normal" "$(derive_rc_class 137 0)"
igual "G2.3 rc=124 sem termos matado -> timeout-normal" "timeout-normal" "$(derive_rc_class 124 0)"
igual "G2.4 rc=0 sem termos matado -> saida-0" "saida-0" "$(derive_rc_class 0 0)"
igual "G2.5 rc=97 sem termos matado -> saida-anormal" "saida-anormal" "$(derive_rc_class 97 0)"
igual "G2.6 rc=97 mas fomos NOS a matar -> timeout-normal" "timeout-normal" "$(derive_rc_class 97 1)"
igual "G2.7 rc=0 e fomos NOS a matar -> timeout-normal" "timeout-normal" "$(derive_rc_class 0 1)"

echo "== Grupo 3: perna_b_veredicto -- a divida declarada nunca e' regressao =="

# A divida e' lida do PROPRIO portao, nao copiada para aqui: um vector que
# divergisse do default de producao testaria outra coisa.
DIVIDA="$(grep -E "^DIVIDA_PYTHON_DEFAULT=" "$GATE" | head -1 | sed "s/^[^=]*='//; s/'[[:space:]]*$//")"
igual "G3.0 divida declarada lida do portao" \
    "tools/test_ppu_lift.py,runtime/ppu/tests/test_ppu_lifter_emit.py" "$DIVIDA"
if [ -f "$BASELINE" ]; then
    igual "G3.0b baseline congelado declara a MESMA divida" \
        "$DIVIDA" "$(awk -F'\t' '$1=="known_debt_python"{print $2; exit}' "$BASELINE")"
fi

SAIDA="$(perna_b_veredicto "$DIVIDA" "$DIVIDA" "$DIVIDA")"; RC=$?
igual "G3.1 VERDE: FAIL(antes)=FAIL(depois)=divida -> rc=0" "0" "$RC"
contem "G3.2 VERDE: nao nomeia nada" "perna_b=PASSA novos=nenhum" "$SAIDA"

SAIDA="$(perna_b_veredicto "$DIVIDA" "$DIVIDA,tools/test_upstream_delta.py" "$DIVIDA")"; RC=$?
igual "G3.3 VERMELHO: FAIL novo fora da divida -> rc=1" "1" "$RC"
contem "G3.4 VERMELHO: nomeia o teste novo" "tools/test_upstream_delta.py" "$SAIDA"
contem "G3.5 VERMELHO: diz FALHA" "perna_b=FALHA" "$SAIDA"

SAIDA="$(perna_b_veredicto "tools/test_ppu_lift.py" "$DIVIDA" "$DIVIDA")"; RC=$?
igual "G3.6 CONTROLO: so' mais uma da divida conhecida -> rc=0" "0" "$RC"

echo "== Grupo 4: perna_c_veredicto -- um bloco nao medido nunca passa =="

SAIDA="$(perna_c_veredicto "runtime/syscalls/lv2_register.c" "aaa" "aaa")"; RC=$?
igual "G4.1 VERMELHO: codigo compilado + sha256 iguais -> rc=1" "1" "$RC"
contem "G4.2 VERMELHO: nomeia os binarios identicos" "binarios identicos" "$SAIDA"

SAIDA="$(perna_c_veredicto "runtime/syscalls/lv2_register.c" "aaa" "bbb")"; RC=$?
igual "G4.3 VERDE: codigo compilado + sha256 distintos -> rc=0" "0" "$RC"
contem "G4.4 VERDE: declara binarios distintos" "binarios=distintos" "$SAIDA"

SAIDA="$(perna_c_veredicto "" "aaa" "aaa")"; RC=$?
igual "G4.5 VERDE: bloco vazio -> rc=0" "0" "$RC"
contem "G4.6 VERDE: declara bloco_vazio" "bloco_vazio=sim" "$SAIDA"

SAIDA="$(perna_c_veredicto "docs/upstream-blocks/README.md" "aaa" "aaa")"; RC=$?
igual "G4.7 VERDE: so' documentacao, mesmo com sha256 iguais -> rc=0" "0" "$RC"
contem "G4.8 VERDE: declara bloco_sem_codigo" "bloco_sem_codigo=sim" "$SAIDA"

echo "== Grupo 5: perna_a_veredicto -- a intro por vectores =="

# st620_min=3 e RUNS=3 -> limiar = (3*2+2)/3 = 2.
SAIDA="$(perna_a_veredicto 3 3 "3,3,3" "3,4,3" "" "")"; RC=$?
igual "G5.1 VERDE: antes=[3,3,3] depois=[3,4,3] -> rc=0" "0" "$RC"
contem "G5.2 VERDE: limiar aritmetico (RUNS*2+2)/3 = 2" "limiar=2" "$SAIDA"

SAIDA="$(perna_a_veredicto 3 3 "3,3,3" "0,0,0" "" "")"; RC=$?
igual "G5.3 VERMELHO: depois=[0,0,0] -> rc=1" "1" "$RC"
contem "G5.4 VERMELHO: motivo e' o limiar" "ok(depois)=0 < limiar=2" "$SAIDA"

# FRONTEIRA. O 23-03-PLAN sugeria PASSA e mandava "decidir pela regra escrita
# no portao". A regra escrita exige `ok(depois) >= ok(antes)` ALEM do limiar,
# logo este vector REPROVA. Esperado codificado pela regra, nao pela sugestao.
SAIDA="$(perna_a_veredicto 3 3 "3,3,3" "3,3,0" "" "")"; RC=$?
igual "G5.5 FRONTEIRA: antes=[3,3,3] depois=[3,3,0] -> rc=1 (regra ok(depois)>=ok(antes))" "1" "$RC"
contem "G5.6 FRONTEIRA: motivo e' a comparacao com o leg antes" "ok(depois)=2 < ok(antes)=3" "$SAIDA"

SAIDA="$(perna_a_veredicto 3 3 "3,3,3" "3,3,3" \
    "timeout-normal,timeout-normal,timeout-normal" \
    "timeout-normal,timeout-normal,saida-anormal")"; RC=$?
igual "G5.7 VERMELHO: saida anormal NOVA com st620 identico -> rc=1" "1" "$RC"
contem "G5.8 VERMELHO: motivo e' a saida anormal" "saidas anormais" "$SAIDA"

# O limiar real do portao vem do baseline CONGELADO, nao de um numero citado.
if [ -f "$BASELINE" ]; then
    ST620_MIN="$(awk -F'\t' '$1=="st620_min"{print $2; exit}' "$BASELINE")"
    case "${ST620_MIN:-}" in
        ''|*[!0-9]*) falha "G5.9 baseline sem st620_min numerico ('${ST620_MIN:-}')" ;;
        *) ao_menos "G5.9 baseline congelado tem st620_min > 0 (um limiar que nada falha nao e' portao)" \
               1 "$ST620_MIN" ;;
    esac
    if [ -n "${ST620_MIN:-}" ] && [ "${ST620_MIN:-0}" -gt 0 ] 2>/dev/null; then
        SAIDA="$(perna_a_veredicto "$ST620_MIN" 3 "$ST620_MIN,$ST620_MIN,$ST620_MIN" "0,0,0" "" "")"; RC=$?
        igual "G5.10 VERMELHO contra o baseline REAL (st620_min=$ST620_MIN): depois=[0,0,0] -> rc=1" "1" "$RC"
    fi
fi

echo "== Grupo 6: pre-requisitos do portao (precisa do checkout do jogo) =="

# Nao basta o directorio: sem os quatro ficheiros, sem o dir de lift e sem
# cmake/ninja, o portao sai rc=2 pelo motivo ERRADO e os dois casos abaixo
# ficariam verdes por engano. Por isso o salto e' decidido pelo conjunto todo,
# e cada caso assevera TAMBEM a mensagem, nunca so' o rc.
FALTA=""
for p in "$GAME_ROOT/EBOOT.ELF" "$GAME_ROOT/env_gow2.sh" "$GAME_ROOT/build_macos.sh" \
         "$GAME_ROOT/lib_boot_chain_metrics.sh" "$BASELINE"; do
    [ -f "$p" ] || FALTA="${FALTA:+$FALTA }$p"
done
[ -d "${PS3_GATE_LIFT:-$GAME_ROOT/recomp_macos_v2}" ] || FALTA="${FALTA:+$FALTA }${PS3_GATE_LIFT:-$GAME_ROOT/recomp_macos_v2}"
command -v cmake >/dev/null 2>&1 || FALTA="${FALTA:+$FALTA }cmake"
command -v ninja >/dev/null 2>&1 || FALTA="${FALTA:+$FALTA }ninja"

if [ -z "$FALTA" ]; then
    # (a) rotulo com caracteres fora de ^[A-Za-z0-9_.-]+$ -- rejeitado ANTES de
    #     qualquer build (T-23-01: o rotulo entra em nomes de ficheiro).
    SAIDA="$(UPSTREAM_GATE_LIB_ONLY=0 bash "$GATE" --bloco 'mau nome;rm' --antes HEAD 2>&1)"; RC=$?
    igual "G6.1 rotulo invalido -> rc=2" "2" "$RC"
    contem "G6.2 rotulo invalido -> mensagem que nomeia a regra" "nome invalido" "$SAIDA"

    # (b) --depois-arvore para um directorio inexistente.
    DIR_FANTASMA="${TMPDIR:-/tmp}/nao-existe-portao-$$"
    rm -rf "$DIR_FANTASMA"
    SAIDA="$(UPSTREAM_GATE_LIB_ONLY=0 bash "$GATE" --bloco G6TESTE --antes HEAD \
        --depois-arvore "$DIR_FANTASMA" 2>&1)"; RC=$?
    igual "G6.3 --depois-arvore inexistente -> rc=2" "2" "$RC"
    contem "G6.4 --depois-arvore inexistente -> mensagem certa (nao um rc=2 de outro motivo)" \
        "directorio inexistente" "$SAIDA"
fi

echo "--------------------------------------------------------------"
echo "resumo: PASS=$N_PASS FAIL=$N_FAIL"

if [ "$N_FAIL" -gt 0 ]; then
    echo "AUTOTESTE DO PORTAO: REPROVADO"
    exit 1
fi

# A linha de salto so' e' impressa quando NAO houve nenhum FAIL: o
# SKIP_REGULAR_EXPRESSION do CTest e' desancorado e mascararia um vermelho dos
# Grupos 1 a 5 se ela aparecesse sempre.
if [ -n "$FALTA" ]; then
    echo "SKIP: Grupo 6 saltado -- sem checkout do jogo/toolchain: $FALTA"
    echo "      (Grupos 1 a 5 correram e passaram: $N_PASS verificacoes)"
    exit 0
fi

echo "AUTOTESTE DO PORTAO: OK"
exit 0
