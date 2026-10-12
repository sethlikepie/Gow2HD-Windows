#!/usr/bin/env bash
# Fase 3, Plano 01, Task 1 (D-3.5, 03-CONTEXT.md).
#
# Teste-ouro do modo RELIFT=1 de build_macos.sh: prova, por HASH DE CONTEUDO
# (nao mtime), que uma corrida RELIFT=1 completa gera recomp_macos_v3 e
# compila, e que recomp_macos_v2 (producao) fica byte-a-byte intacto. Prova
# tambem que sem RELIFT o script mantem o comportamento de hoje (abort com
# rc=1 contra um directorio sem lift).
#
# So' pode ser exercitado com cwd efectivo em ../gow2-recomp (o unico
# checkout com EBOOT.ELF/functions.json/recomp_macos_v2 reais) -- ver
# 03-CONTEXT.md e a nota de decisao operacional no PLAN.md desta task.
#
# Run: bash games/gow2/test_relift_build.sh
# Exit 0 = TODOS OS TESTES PASSARAM. Duracao esperada: minutos (inclui o
# build completo real, nao so' o lifter).
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
GOW2_RECOMP="${GOW2_RECOMP:-$(cd "$HERE/../../../gow2-recomp" && pwd)}"

hash_dir() {
    find "$1" -type f -print0 | sort -z | tar --null -T - -cf - 2>/dev/null | md5 -q
}

echo "GOW2_RECOMP=$GOW2_RECOMP"
if [ ! -d "$GOW2_RECOMP/recomp_macos_v2" ]; then
    echo "FALHA: $GOW2_RECOMP/recomp_macos_v2 nao existe -- GOW2_RECOMP resolveu mal" >&2
    exit 1
fi

FAIL=0

echo "=== (a) hash recomp_macos_v2 ANTES ==="
H_BEFORE=$(hash_dir "$GOW2_RECOMP/recomp_macos_v2")
echo "H_BEFORE=$H_BEFORE"

echo "=== (b) RELIFT=1 ./build_macos.sh recomp_macos_v3 (dentro de $GOW2_RECOMP) ==="
(
    cd "$GOW2_RECOMP" && \
    RELIFT=1 OUT="$GOW2_RECOMP/boot_gow2_relift_test" ./build_macos.sh "$GOW2_RECOMP/recomp_macos_v3"
)
RC_RELIFT=$?
echo "rc(RELIFT build)=$RC_RELIFT"

echo "=== (c) hash recomp_macos_v2 DEPOIS ==="
H_AFTER=$(hash_dir "$GOW2_RECOMP/recomp_macos_v2")
echo "H_AFTER=$H_AFTER"
if [ "$H_BEFORE" != "$H_AFTER" ]; then
    echo "FALHA: recomp_macos_v2 mudou apos RELIFT=1 (H_BEFORE=$H_BEFORE H_AFTER=$H_AFTER)" >&2
    FAIL=1
else
    echo "[PASS] recomp_macos_v2 hash-intacto (H_BEFORE==H_AFTER)"
fi

echo "=== (d) recomp_macos_v3/ppu_recomp.h existe? ==="
if [ -f "$GOW2_RECOMP/recomp_macos_v3/ppu_recomp.h" ]; then
    echo "[PASS] recomp_macos_v3/ppu_recomp.h existe"
else
    echo "FALHA: recomp_macos_v3/ppu_recomp.h nao existe" >&2
    FAIL=1
fi

if [ "$RC_RELIFT" != "0" ]; then
    echo "FALHA: RELIFT=1 ./build_macos.sh saiu rc=$RC_RELIFT (esperado 0)" >&2
    FAIL=1
else
    echo "[PASS] RELIFT=1 ./build_macos.sh saiu rc=0 (build completo)"
fi

echo "=== (e) sem RELIFT, contra directorio sem lift -- mesmo abort de sempre ==="
TMP_EMPTY="/tmp/lift_inexistente_$$"
( cd "$GOW2_RECOMP" && ./build_macos.sh "$TMP_EMPTY" ) >/tmp/test_relift_build_e.log 2>&1
RC_NORELIFT=$?
if [ "$RC_NORELIFT" = "1" ] && grep -q "no lift output in .* run ppu_lifter.py first" /tmp/test_relift_build_e.log; then
    echo "[PASS] sem RELIFT: rc=1, mensagem de abort igual a hoje"
else
    echo "FALHA: sem RELIFT deveria abortar rc=1 com 'no lift output in ... run ppu_lifter.py first' (rc=$RC_NORELIFT)" >&2
    cat /tmp/test_relift_build_e.log >&2
    FAIL=1
fi
rm -f /tmp/test_relift_build_e.log

if [ "$FAIL" = "0" ]; then
    echo "TODOS OS TESTES PASSARAM"
    exit 0
else
    echo "FALHA: um ou mais testes falharam" >&2
    exit 1
fi
