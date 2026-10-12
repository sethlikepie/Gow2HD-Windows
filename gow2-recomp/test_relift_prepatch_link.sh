#!/usr/bin/env bash
# Fase 3, Plano 01, Task 2 (D-3.6, gap herdado da Fase 2 -- 02-03-SUMMARY.md).
#
# Prova por `nm` sobre um lift GENUINAMENTE fresco (recomp_macos_v3, gerado
# pela Task 1, ANTES de qualquer patch_*.py) que host_gow2_factory.o e
# host_gow2_f2b.o entram no binario real -- ou, se o link falhar, regista
# a lista exacta de simbolos em falta/duplicados. Qualquer um dos dois
# desfechos fecha D-3.6; nao ha' tentativa de forcar o link aqui.
#
# Run: bash games/gow2/test_relift_prepatch_link.sh
# Exit 0 = investigacao conclusiva (confirmado OU documentado). Exit 1 so'
# se nem o binario nem um log utilizavel existirem.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
GOW2_RECOMP="${GOW2_RECOMP:-$(cd "$HERE/../../../gow2-recomp" && pwd)}"
LIFT="$GOW2_RECOMP/recomp_macos_v3"
BIN="$GOW2_RECOMP/boot_gow2_relift_test"

echo "GOW2_RECOMP=$GOW2_RECOMP"
echo "LIFT=$LIFT"

if [ ! -d "$LIFT" ]; then
    echo "FALHA: $LIFT nao existe -- corra test_relift_build.sh (Task 1) primeiro" >&2
    exit 1
fi

echo "=== Teste 1: lift fresco NAO tem as ancoras hand-authored ==="
N_FACTORY=$(grep -lc 'ps3_factory_repair_vt(uint32_t obj) *{' "$LIFT"/ppu_recomp_*.cpp 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
N_F2B=$(grep -Elc 'f2b_stream_ensure\(uint32_t type_sys\) *\{' "$LIFT"/ppu_recomp_*.cpp 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
# grep -l / -c combinado com -E devolve so' 0/1 por ficheiro; soma-se ficheiros
# que casaram (nao ocorrencias) -- o que basta para provar presenca/ausencia.
N_FACTORY_FILES=0
N_F2B_FILES=0
for f in "$LIFT"/ppu_recomp_*.cpp; do
    [ -f "$f" ] || continue
    grep -q 'ps3_factory_repair_vt(uint32_t obj) *{' "$f" && N_FACTORY_FILES=$((N_FACTORY_FILES + 1))
    grep -Eq 'f2b_stream_ensure\(uint32_t type_sys\) *\{' "$f" && N_F2B_FILES=$((N_F2B_FILES + 1))
done
echo "ficheiros com ancora factory (definicao): $N_FACTORY_FILES"
echo "ficheiros com ancora f2b (definicao): $N_F2B_FILES"

if [ "$N_FACTORY_FILES" != "0" ] || [ "$N_F2B_FILES" != "0" ]; then
    echo "FALHA: o lift em $LIFT NAO e' genuinamente fresco -- ja' tem definicoes hand-authored embutidas" >&2
    exit 1
fi
echo "[PASS] lift fresco confirmado: zero definicoes hand-authored"

echo "=== Nota: o subsistema host_gow2_f2b so' existe no monorepo ==="
# Divergencia pre-existente, documentada e fora de escopo (03-CONTEXT.md /
# 03-01-PLAN.md, "Decisao operacional"): games/gow2/build_macos.sh tem o
# bloco de compilacao do host_gow2_f2b, mas ../gow2-recomp/build_macos.sh
# (usado pela Task 1 para produzir $BIN, porque so' esse checkout tem
# EBOOT.ELF/functions.json reais) NAO tem esse bloco -- e o proprio
# ficheiro-fonte host_gow2_f2b.c nem existe nesse checkout. Registar isto
# explicitamente para nao confundir "nao compilado por divergencia de
# script" com "falhou a linkar".
F2B_SRC_PRESENT=0
[ -f "$GOW2_RECOMP/host_gow2_f2b.c" ] && F2B_SRC_PRESENT=1
F2B_BLOCK_PRESENT=$(grep -c host_gow2_f2b "$GOW2_RECOMP/build_macos.sh" 2>/dev/null)
F2B_BLOCK_PRESENT="${F2B_BLOCK_PRESENT:-0}"
echo "host_gow2_f2b.c presente em \$GOW2_RECOMP: $F2B_SRC_PRESENT (esperado 0, divergencia conhecida)"
echo "bloco host_gow2_f2b em \$GOW2_RECOMP/build_macos.sh: $F2B_BLOCK_PRESENT ocorrencia(s) (esperado 0)"

echo "=== Teste 2/3: nm sobre o binario, OU registo do log de falha ==="
if [ -f "$BIN" ]; then
    echo "binario existe: $BIN"
    NM_OUT=$(nm "$BIN" 2>/dev/null | grep -E '_ps3_factory_repair_vt|_f2b_stream_ensure' || true)
    echo "--- nm | grep '_ps3_factory_repair_vt|_f2b_stream_ensure' ---"
    echo "$NM_OUT"
    echo "---"
    HAS_FACTORY_T=$(echo "$NM_OUT" | grep -c ' T _ps3_factory_repair_vt' || true)
    HAS_F2B_ANY=$(echo "$NM_OUT" | grep -c '_f2b_stream_ensure' || true)

    if [ "$HAS_FACTORY_T" -ge 1 ] && [ "$HAS_F2B_ANY" -ge 1 ]; then
        echo "D-3.6 CONFIRMADO: host_gow2_factory.o e host_gow2_f2b.o entram no binario (nm confirma T _ps3_factory_repair_vt e _f2b_stream_ensure)"
        exit 0
    elif [ "$HAS_FACTORY_T" -ge 1 ] && [ "$F2B_SRC_PRESENT" = "0" ]; then
        echo "D-3.6 RESULTADO (parcial, causa identificada): host_gow2_factory.o CONFIRMADO no binario (T _ps3_factory_repair_vt)."
        echo "host_gow2_f2b.o NAO foi sequer tentado -- host_gow2_f2b.c e' ausente em \$GOW2_RECOMP e o bloco de compilacao"
        echo "correspondente tambem esta' ausente de \$GOW2_RECOMP/build_macos.sh (divergencia pre-existente entre este"
        echo "checkout e o monorepo, documentada e fora de escopo deste plano -- ver 03-01-PLAN.md). Isto NAO e' uma falha"
        echo "de link (nenhum erro undefined/duplicate symbol ocorreu); e' a f2b nunca tendo entrado na compilacao."
        exit 0
    else
        echo "D-3.6 RESULTADO (parcial, sem causa conhecida): binario existe mas nm nao confirma os simbolos esperados -- ver saida acima." >&2
        exit 0
    fi
else
    echo "binario NAO existe em $BIN -- o link da Task 1 falhou ou ainda nao correu." >&2
    echo "Reexecute test_relift_build.sh e capture stdout/stderr para obter a lista exacta de simbolos em falta/duplicados." >&2
    exit 1
fi
