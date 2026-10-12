#!/usr/bin/env bash
# scripts/test_all.sh -- comando unico: corre toda a suite de testes possivel
# nesta plataforma (task F0-6 / criterio de aceite da Fase 0).
#
# Corre scripts/test_python.sh e depois scripts/test_runtime.sh -- SEMPRE OS
# DOIS, mesmo que o primeiro falhe (nao para no primeiro FAIL: o objectivo e
# o developer/CI ver o quadro completo numa unica passagem). No fim, agrega
# um resumo combinado.
#
# scripts/smoke_macos.sh NAO entra aqui: a auditoria F0-1 provou que o seu
# passo 3 (movie_hle.c) ja nao linka com a receita de compilacao documentada
# no proprio script (precisa de simbolos de cellGcmSys.c/movie_vt_metal.m
# ausentes dessa lista) -- esta partido hoje, por uma razao alheia a esta
# task, e corrigi-lo esta fora de ambito aqui. Nao o tornamos passo
# obrigatorio; quem quiser corre-lo directamente e a proprio risco, sabendo
# que falha (`./scripts/smoke_macos.sh`).
#
# Uso: scripts/test_all.sh
# Honra as mesmas variaveis de scripts/test_python.sh e scripts/test_runtime.sh
# ($PYTHON, $PS3RECOMP_BUILD_DIR) -- propagadas automaticamente pelo ambiente.
#
# Exit code: 0 SO se AMBAS as suites saírem com exit 0 (nenhum FAIL em
#            nenhuma delas); != 0 se qualquer uma delas reportar falha.
set -u
# Sem "set -e": este agregador tem de correr a SEGUNDA suite mesmo que a
# PRIMEIRA falhe -- e essa e exactamente a razao de nao usar -e aqui.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "######################################################"
echo "# test_all.sh -- 1/2: scripts/test_python.sh"
echo "######################################################"
echo
# A condicao do "if" corre independentemente de "set -e" (que nem esta activo
# aqui, mas mantem-se o mesmo padrao dos outros dois scripts por consistencia
# e para sobreviver a um eventual "set -e" herdado de quem invoca isto com
# `source`).
if "$ROOT/scripts/test_python.sh"; then
    PY_RC=0
else
    PY_RC=$?
fi

echo
echo "######################################################"
echo "# test_all.sh -- 2/2: scripts/test_runtime.sh"
echo "######################################################"
echo
if "$ROOT/scripts/test_runtime.sh"; then
    RT_RC=0
else
    RT_RC=$?
fi

echo
echo "======================================================"
echo "test_all.sh -- resumo combinado"
echo "======================================================"
if [ "$PY_RC" -eq 0 ]; then
    echo "  test_python.sh  : OK (exit 0)"
else
    echo "  test_python.sh  : FALHOU (exit $PY_RC)"
fi
if [ "$RT_RC" -eq 0 ]; then
    echo "  test_runtime.sh : OK (exit 0)"
else
    echo "  test_runtime.sh : FALHOU (exit $RT_RC)"
fi
echo "======================================================"

if [ "$PY_RC" -eq 0 ] && [ "$RT_RC" -eq 0 ]; then
    exit 0
fi
exit 1
