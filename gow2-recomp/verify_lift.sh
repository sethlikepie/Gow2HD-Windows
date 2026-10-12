#!/usr/bin/env bash
# verify_lift.sh -- ponto de entrada UNICO para os 3 verificadores do lift.
#
# Corre, contra um LIFT_DIR dado:
#   1. tools/lift_parity.py     (--functions, autoridade das fronteiras)
#   2. games/gow2/verify_lift_baseline.sh  (MANIFEST.tsv, script INALTERADO)
#   3. tools/audit_boundaries.py (I1/I3; I2 e' informativo, fora do gate)
#
# Os 3 passos falham por DELTA contra um baseline CONGELADO
# (lift_baseline/PARITY_BASELINE.json, MANIFEST_DEBT_BASELINE.json,
# BOUNDS_BASELINE.json), nunca por limiar absoluto -- ja provado na Fase 4
# que um limiar absoluto pune melhorias (D-4.5, 04-CONTEXT.md), e estendido
# ao passo 2 na Fase 5 (D-5.1, 05-CONTEXT.md): o passo 2 continua a imprimir
# o detalhe por-marcador de verify_lift_baseline.sh (transparencia humana),
# mas o rc autoritativo vem de lift_baseline/manifest_delta_gate.py --
# so' falha por achado NOVO face a divida ja' declarada (grupo opd-dispatch,
# Fase 3), nao pelo rc absoluto de gen_manifest.py --verify.
#
# O oraculo (I4/I5, item 3 do plano de adopcao) e' um 4o passo OPT-IN
# (Fase 20): so' corre com VERIFY_ORACLE=1. SEM a variavel este script corre
# exactamente como antes -- os mesmos 3 passos, o mesmo rc, sem consultar o
# Ghidra e sem exigir que ele esteja instalado. Isto nao e' preferencia: este
# script esta no caminho de aceitacao (accept_relift.sh, perna 2) e mudar o
# default alteraria o que o projecto aceita hoje.
#
# Com VERIFY_ORACLE=1 corre-se:
#   4. oracle_manifest.py check   (proveniencia: sha256 do ELF no
#                                  ghidra_out/MANIFEST.json; absent/stale
#                                  falham ALTO, nunca passam em silencio)
#      + audit_boundaries.py --oracle  ->  ids I4/I5
#      + baseline_delta.py vs lift_baseline/ORACLE_BASELINE.json
# Tambem aqui o criterio e' DELTA, nunca limiar absoluto: o corpus tem 137 I4
# e 5154 I5 historicos (medido 2026-08-02) -- um gate por contagem nascia
# vermelho e seria ignorado. Politica do conjunto: todos os I4 + I5
# `extende-seguro` e `encurta`; `extende-funde` fica contado mas FORA do gate
# (a lacuna ja' e' reclamada por outra funcao nossa e a discordancia pode ser
# legitima). Ver tools/oracle_manifest.py.
#
# Uso:  ./verify_lift.sh LIFT_DIR
#       VERIFY_ORACLE=1 ./verify_lift.sh LIFT_DIR
# rc=0  os passos activos passam (delta limpo)
# rc=1  pelo menos um passo falhou
# rc=2  erro de uso (LIFT_DIR ausente, ELF/functions.json/baseline em falta)
#
# Overrides:
#   PS3_ELF          caminho do EBOOT.ELF (default: $LIFT_DIR/../EBOOT.ELF)
#   PS3_FUNCTIONS    caminho do functions.json (default: $LIFT_DIR/../functions.json)
#   PS3_ENGINE_ROOT  raiz do motor (default: dois niveis acima deste script)
#   PS3_PATCH_PYTHON interpretador python (default: .venv/bin/python3 do motor)
#   VERIFY_ORACLE    1 liga o 4o passo (default: desligado)
#   PS3_GHIDRA_OUT       export do Ghidra (default: $LIFT_DIR/../ghidra_out)
#   PS3_ORACLE_FUNCTIONS default: $PS3_GHIDRA_OUT/functions.json
#   PS3_ORACLE_MANIFEST  default: $PS3_GHIDRA_OUT/MANIFEST.json
#   PS3_ORACLE_BASELINE  default: lift_baseline/ORACLE_BASELINE.json
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PS3_ROOT="${PS3_ENGINE_ROOT:-$(cd "$HERE/../.." && pwd)}"
BASE="$HERE/lift_baseline"

if [ $# -lt 1 ]; then
    echo "uso: $0 LIFT_DIR" >&2
    exit 2
fi
LIFT="$(cd "$1" 2>/dev/null && pwd)"
if [ -z "$LIFT" ]; then
    echo "ERRO: dir de lift inexistente: $1" >&2
    exit 2
fi
GOW2_ROOT="$(dirname "$LIFT")"
ELF="${PS3_ELF:-$GOW2_ROOT/EBOOT.ELF}"
FUNCTIONS="${PS3_FUNCTIONS:-$GOW2_ROOT/functions.json}"

# Oraculo (so' usado com VERIFY_ORACLE=1; resolver aqui nao custa nada e
# mantem os defaults num sitio so').
GHIDRA_OUT="${PS3_GHIDRA_OUT:-$GOW2_ROOT/ghidra_out}"
ORACLE_FUNCTIONS="${PS3_ORACLE_FUNCTIONS:-$GHIDRA_OUT/functions.json}"
ORACLE_MANIFEST="${PS3_ORACLE_MANIFEST:-$GHIDRA_OUT/MANIFEST.json}"
ORACLE_BASELINE="${PS3_ORACLE_BASELINE:-$BASE/ORACLE_BASELINE.json}"

[ -f "$ELF" ] || { echo "ERRO: ELF em falta: $ELF (defina PS3_ELF)" >&2; exit 2; }
[ -f "$FUNCTIONS" ] || { echo "ERRO: functions.json em falta: $FUNCTIONS (defina PS3_FUNCTIONS)" >&2; exit 2; }
[ -f "$BASE/PARITY_BASELINE.json" ] || { echo "ERRO: baseline em falta: $BASE/PARITY_BASELINE.json" >&2; exit 2; }
[ -f "$BASE/BOUNDS_BASELINE.json" ] || { echo "ERRO: baseline em falta: $BASE/BOUNDS_BASELINE.json" >&2; exit 2; }

PY="${PS3_PATCH_PYTHON:-}"
if [ -z "$PY" ]; then
    if [ -x "$PS3_ROOT/.venv/bin/python3" ]; then PY="$PS3_ROOT/.venv/bin/python3"
    else PY="$(command -v python3 || true)"; fi
fi
[ -n "$PY" ] || { echo "ERRO: sem interpretador python3" >&2; exit 2; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if command -v shasum >/dev/null 2>&1; then
    SHA="$(cat "$LIFT"/ppu_recomp_*.cpp 2>/dev/null | shasum -a 256 | cut -d' ' -f1)"
else
    SHA="$(cat "$LIFT"/ppu_recomp_*.cpp 2>/dev/null | sha256sum | cut -d' ' -f1)"
fi

echo "== verify_lift.sh =="
echo "LIFT:      $LIFT"
echo "SHA256:    ${SHA:-<sem chunks ppu_recomp_*.cpp>}"
echo "ELF:       $ELF"
echo "FUNCTIONS: $FUNCTIONS"
case "${VERIFY_ORACLE:-0}" in
    1|true|yes|on) echo "ORACULO:   LIGADO (VERIFY_ORACLE=1) -- $GHIDRA_OUT" ;;
    *)             echo "ORACULO:   desligado (opcional: VERIFY_ORACLE=1)" ;;
esac
echo

NAMES=()
RESULTS=()

run_step() {
    local nome="$1" fn="$2"
    echo "---- [$nome] ----"
    if "$fn"; then
        echo "[$nome] PASS"
        NAMES+=("$nome"); RESULTS+=(0)
    else
        local rc=$?
        echo "[$nome] FAIL (rc=$rc)"
        NAMES+=("$nome"); RESULTS+=(1)
    fi
    echo
}

parity_step() {
    "$PY" "$PS3_ROOT/tools/lift_parity.py" "$ELF" "$LIFT" \
        --functions "$FUNCTIONS" --limit 0 --json "$TMP/parity_current.json" || return 1
    "$PY" - "$TMP/parity_current.json" "$TMP/parity_current.ids.json" <<'PYEOF'
import json, sys
with open(sys.argv[1]) as f:
    d = json.load(f)
ids = sorted(f"{x['ea']}/{x['signal']}" for x in d["findings"])
with open(sys.argv[2], "w") as f:
    json.dump({"ids": ids}, f)
PYEOF
    "$PY" "$BASE/baseline_delta.py" "$TMP/parity_current.ids.json" "$BASE/PARITY_BASELINE.json" \
        --label lift_parity
}

manifest_step() {
    # Detalhe por-marcador (transparencia humana) -- o rc deste sub-passo ja
    # NAO e' autoritativo: um deficit ja' declarado (ex.: grupo opd-dispatch,
    # Fase 3) faz verify_lift_baseline.sh sair rc=1 mesmo sem regressao nova.
    PS3_ENGINE_ROOT="$PS3_ROOT" "$HERE/verify_lift_baseline.sh" "$LIFT"
    # Interpretacao por DELTA contra a divida congelada (D-5.1): so' falha
    # por achado NOVO face a MANIFEST_DEBT_BASELINE.json, nunca por limiar
    # absoluto -- fecha o gap deixado em aberto por 04-03-SUMMARY.md.
    "$PY" "$BASE/manifest_delta_gate.py" "$LIFT" --manifest "$BASE/MANIFEST.tsv" \
        --debt "$BASE/MANIFEST_DEBT_BASELINE.json"
    local rc=$?
    return $rc
}

bounds_step() {
    "$PY" "$PS3_ROOT/tools/audit_boundaries.py" "$FUNCTIONS" --elf "$ELF" \
        --json "$TMP/bounds_current.json" --max-report 3 || return 1
    "$PY" - "$TMP/bounds_current.json" "$TMP/bounds_current.ids.json" <<'PYEOF'
import json, sys
with open(sys.argv[1]) as f:
    d = json.load(f)
ids = sorted([f"I1:{x['a']}|{x['b']}" for x in d["I1"]] + [f"I3:{x['func']}" for x in d["I3"]])
with open(sys.argv[2], "w") as f:
    json.dump({"ids": ids}, f)
PYEOF
    "$PY" "$BASE/baseline_delta.py" "$TMP/bounds_current.ids.json" "$BASE/BOUNDS_BASELINE.json" \
        --label audit_boundaries
}

oracle_step() {
    # 1. Proveniencia primeiro: um export sem MANIFEST (absent) ou de outro
    #    ELF (stale) nao pode ser tratado como oraculo. Falha ALTO.
    if [ ! -f "$ORACLE_FUNCTIONS" ]; then
        echo "ORACLE: absent -- sem $ORACLE_FUNCTIONS"
        echo "  produz com: ./analyze_eboot_ghidra.sh   (ou define PS3_GHIDRA_OUT)"
        return 1
    fi
    "$PY" "$PS3_ROOT/tools/oracle_manifest.py" check \
        --manifest "$ORACLE_MANIFEST" --elf "$ELF" || return 1
    if [ ! -f "$ORACLE_BASELINE" ]; then
        echo "ORACLE: baseline em falta: $ORACLE_BASELINE"
        return 1
    fi
    # 2. I4/I5 medidos agora. SEM --gate I4,I5 (limiar absoluto e' proibido
    #    aqui: ver cabecalho e tools/audit_boundaries.py).
    "$PY" "$PS3_ROOT/tools/audit_boundaries.py" "$FUNCTIONS" --elf "$ELF" \
        --oracle "$ORACLE_FUNCTIONS" --json "$TMP/oracle_current.json" \
        --max-report 0 || return 1
    # 3. Achados -> ids opacos -> delta contra o baseline congelado.
    "$PY" "$PS3_ROOT/tools/oracle_manifest.py" ids \
        --report "$TMP/oracle_current.json" --out "$TMP/oracle_current.ids.json" || return 1
    "$PY" "$BASE/baseline_delta.py" "$TMP/oracle_current.ids.json" "$ORACLE_BASELINE" \
        --label oracle_i4_i5
}

run_step "lift_parity" parity_step
run_step "MANIFEST" manifest_step
run_step "audit_boundaries" bounds_step

# 4o passo OPT-IN. Sem VERIFY_ORACLE=1 nada disto corre e o rc do script e'
# identico ao de antes da Fase 20.
case "${VERIFY_ORACLE:-0}" in
    1|true|yes|on) run_step "oracle_ghidra" oracle_step ;;
esac

echo "== rodape =="
overall=0
for i in "${!NAMES[@]}"; do
    if [ "${RESULTS[$i]}" = "0" ]; then
        printf '%-20s PASS\n' "${NAMES[$i]}"
    else
        printf '%-20s FAIL\n' "${NAMES[$i]}"
        overall=1
    fi
done

exit $overall
