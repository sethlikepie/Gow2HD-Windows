#!/usr/bin/env bash
# scripts/test_python.sh -- corre os self-tests Python do ps3recomp (task F0-6).
#
# Descobre um interpretador Python >=3.11 (pyproject.toml/F0-2 fixa este
# minimo; o `python3` de sistema em muitas maquinas -- incl. macOS so com as
# Command Line Tools -- e mais antigo e nem consegue fazer parse de alguns
# scripts de tools/, ver docs/BASELINE_FASE0.md), corre cada self-test Python
# conhecido a partir da raiz do repo (independente do directorio de onde este
# script e invocado), classifica cada um em PASS/SKIP/FAIL -- convencao SKIP
# fixada pela F0-3: uma linha do stdout comeca por "SKIP: " e o processo sai
# com codigo 0. A classificacao consulta o exit code PRIMEIRO: um teste que
# sai != 0 e' SEMPRE FAIL, mesmo que o seu output contenha "SKIP:" (isso
# violaria a convencao F0-3 -- ver achado da revisao pos-review mais abaixo);
# so' com exit 0 confirmado e' que se distingue SKIP de PASS pela presenca de
# "SKIP:". Imprime um resumo agregado no fim.
#
# Uso: scripts/test_python.sh
# Honra $PYTHON se definido (mesma variavel que runtime/spu/tests/run_tests.sh
# ja usa para escolher interprete).
#
# Exit code: 0 se nao houver nenhum FAIL (SKIP nao conta como falha);
#            1 se houver pelo menos um FAIL, ou se nenhum interprete >=3.11
#              foi encontrado.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# ---------------------------------------------------------------------------
# 1. Descobrir um interpretador Python >=3.11
#
# Ordem: $PYTHON explicito -> .venv do repo -> nomes versionados conhecidos
# (mais recente primeiro) -> `python3` generico, aceite SO se a sua propria
# --version confirmar >=3.11 (evita rebentar com erro de sintaxe num Python
# de sistema antigo, como o 3.9.6 visto nalgumas maquinas de dev).
# ---------------------------------------------------------------------------
version_ok() {
    # $1: caminho/nome do interpretador candidato.
    "$1" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)' \
        > /dev/null 2>&1
}

PY=""

if [ -n "${PYTHON:-}" ]; then
    if version_ok "$PYTHON"; then
        PY="$PYTHON"
    else
        echo "ERRO: \$PYTHON='$PYTHON' nao satisfaz o minimo Python >=3.11 (pyproject.toml)." >&2
        exit 1
    fi
fi

if [ -z "$PY" ]; then
    for venv_py in "$ROOT/.venv/bin/python3" "$ROOT/.venv/bin/python"; do
        if [ -x "$venv_py" ] && version_ok "$venv_py"; then
            PY="$venv_py"
            break
        fi
    done
fi

if [ -z "$PY" ]; then
    for cand in python3.14 python3.13 python3.12 python3.11; do
        if command -v "$cand" > /dev/null 2>&1 && version_ok "$cand"; then
            PY="$(command -v "$cand")"
            break
        fi
    done
fi

if [ -z "$PY" ] && command -v python3 > /dev/null 2>&1 && version_ok python3; then
    PY="$(command -v python3)"
fi

if [ -z "$PY" ]; then
    cat >&2 <<'EOF'
ERRO: nenhum interpretador Python >=3.11 encontrado (pyproject.toml exige
"requires-python = >=3.11"; o python3 de sistema de varias maquinas -- ex.
macOS so com as Command Line Tools -- e mais antigo que isso). Resolve de uma
destas formas e corre de novo:
  - export PYTHON=/caminho/para/python3.11-ou-mais-recente
  - cria um venv do repo em .venv com Python >=3.11 (ex.: python3.12 -m venv .venv)
  - instala um python3.11/3.12/3.13/3.14 versionado e garante que fica no PATH
    (ex.: brew install python@3.12 no macOS)
EOF
    exit 1
fi

echo "== test_python.sh: usando $("$PY" --version 2>&1) ($PY) =="
echo "== raiz do repo: $ROOT =="
echo

# ---------------------------------------------------------------------------
# 2. Self-tests Python conhecidos
#
# Mesma lista de ficheiros que o CMakeLists.txt (task F0-5) regista como
# testes "py.*" no CTest -- espelhada aqui (nao reimplementada com logica
# diferente) para permitir correr os self-tests Python sem precisar de
# configurar/compilar nada em C/C++ (isso e o scripts/test_runtime.sh).
# ---------------------------------------------------------------------------
TESTS="
tools/test_audit_boundaries.py
tools/test_audit_fallthrough.py
tools/test_check_block_record.py
tools/test_ghidra_lookup.py
tools/test_lift_parity.py
tools/test_ppu_disasm_regs.py
tools/test_ppu_jumptables.py
tools/test_spu_lift_ops.py
tools/test_upstream_delta.py
tools/test_upstream_jt_contrib.py
tools/test_find_functions_prologue_corroboration.py
tools/test_objdump_audit.py
tools/test_ppu_bounds_repair.py
tools/test_ppu_lift.py
tools/test_recomp_config.py
tools/test_wall4_subtag_fix.py
tools/test_wall1_subtag_fix.py
tools/lift_selftest.py
tools/spu_lift_selftest.py
runtime/ppu/tests/test_ppu_lifter_emit.py
"

PASS_N=0
SKIP_N=0
FAIL_N=0
SKIP_LIST=""
FAIL_LIST=""

for t in $TESTS; do
    [ -z "$t" ] && continue
    if [ ! -f "$ROOT/$t" ]; then
        echo "--- $t --- (ficheiro nao encontrado)"
        FAIL_N=$((FAIL_N + 1))
        FAIL_LIST="$FAIL_LIST
  - $t (ficheiro nao encontrado)"
        continue
    fi

    echo "--- $t ---"
    # A condicao do "if" e isenta de "set -e": mesmo que o teste saia com
    # codigo != 0, o script continua para o proximo em vez de abortar aqui.
    if OUT="$("$PY" "$ROOT/$t" 2>&1)"; then
        RC=0
    else
        RC=$?
    fi
    echo "$OUT"
    echo

    # A convencao (F0-3) e' "uma linha comecando por SKIP: aparece no
    # stdout" -- NAO diz que tem de ser a PRIMEIRA linha, e na pratica nao
    # e': tools/test_ppu_lift.py imprime varias linhas de preambulo (as
    # verificacoes fpscr/vrsave, que NAO dependem de nenhuma toolchain)
    # antes de eventualmente chegar a um "SKIP: ..." quando falta um
    # compilador C++ host. Por isso procura-se SKIP: em QUALQUER linha do
    # output, nao so na primeira -- procurar so na primeira classificaria
    # esse caso (real, ja observado) como FAIL em vez de SKIP.
    SKIP_MSG="$(printf '%s\n' "$OUT" | grep -E '^SKIP:' | head -n 1 || true)"

    # ACHADO da revisao pos-review (F0-6): a ordem das verificacoes importa.
    # A convencao F0-3 empareia "SKIP:" no output com exit 0 -- mas esta
    # classificacao NAO pode confiar cegamente nesse emparelhamento (seria
    # o mesmo erro de mascarar um FAIL real, ja visto 5 vezes antes neste
    # projecto). Por isso o exit code e' consultado PRIMEIRO: um teste que
    # sai != 0 e' SEMPRE FAIL, mesmo que o seu stdout contenha uma linha
    # "SKIP:" (script defeituoso ou editado no futuro que viole a
    # convencao). So' depois de RC==0 confirmado e' que se distingue
    # SKIP de PASS pela presenca de "SKIP:".
    if [ "$RC" -ne 0 ]; then
        FAIL_N=$((FAIL_N + 1))
        FAIL_LIST="$FAIL_LIST
  - $t (exit $RC)"
        if [ -n "$SKIP_MSG" ]; then
            echo "AVISO: $t saiu com exit $RC MAS imprimiu uma linha \"SKIP:\" -- isto viola a convencao F0-3 (SKIP: deve vir SEMPRE emparelhado com exit 0). Contado como FAIL, nao SKIP. Verifica o proprio teste: $SKIP_MSG" >&2
        fi
    elif [ -n "$SKIP_MSG" ]; then
        SKIP_N=$((SKIP_N + 1))
        SKIP_LIST="$SKIP_LIST
  - $t -- $SKIP_MSG"
    else
        PASS_N=$((PASS_N + 1))
    fi
done

TOTAL=$((PASS_N + SKIP_N + FAIL_N))

echo "=================================================="
echo "test_python.sh -- resumo ($TOTAL testes)"
echo "  PASS: $PASS_N"
echo "  SKIP: $SKIP_N"
echo "  FAIL: $FAIL_N"

if [ -n "$SKIP_LIST" ]; then
    echo
    echo "Testes SKIP:$SKIP_LIST"
fi

if [ -n "$FAIL_LIST" ]; then
    echo
    echo "Testes FAIL:$FAIL_LIST"
    echo "=================================================="
    exit 1
fi

echo "=================================================="
exit 0
