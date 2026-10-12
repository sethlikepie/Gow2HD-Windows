#!/usr/bin/env bash
# Analise do EBOOT do GoW2 no Ghidra, headless -> JSON consumivel.
#
# Porque existe: ate 2026-07-25 o diagnostico do jogo era feito por grep no
# lift (1,3 GB de C com 56.072 funcoes chamadas func_XXXXXXXX). A integracao
# com o Ghidra ja' estava escrita em ps3recomp/tools/ (ghidra_analyze.py,
# ghidra_names.py, ghidra/ExportAnalysisJson.java) mas nunca foi corrida --
# nem o Ghidra estava instalado. Isto encadeia os passos com preflight.
#
# Produz em ghidra_out/:
#   functions.json   fronteiras + nomes que o Ghidra inferiu
#   symbols.json     simbolos
#   strings.json     strings referenciadas por funcao  <- otimo para dar nome
#   decompiled.json  o C decompilado do jogo ORIGINAL  <- o que muda o jogo
#   names.json       mapa {addr: nome} para o lifter (--names)
#   MANIFEST.json    proveniencia: sha256 do ELF analisado, data do export,
#                    versao do Ghidra e contagens  <- sem isto, "o oraculo diz
#                    X" nao tem binario nem data, e um export obsoleto passa
#                    por verdade. O passo opcional do verify_lift.sh
#                    (VERIFY_ORACLE=1) recusa-se a correr sem ele.
#
# Uso:  ./analyze_eboot_ghidra.sh [EBOOT] [OUTDIR]
# Custo: o EBOOT tem 5,6 MB e ~13 mil funcoes; a auto-analise com decompilacao
# demora dezenas de minutos. Corre uma vez; os JSON ficam em disco.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PS3="${PS3_ENGINE_ROOT:-$HERE/../ps3recomp}"
ELF="${1:-$HERE/EBOOT.ELF}"
OUT="${2:-$HERE/ghidra_out}"

PY="$PS3/.venv/bin/python"
[ -x "$PY" ] || PY="$(command -v python3)"

fail() { echo "FALTA: $1" >&2; shift; printf '  %s\n' "$@" >&2; exit 1; }

# ---- preflight ------------------------------------------------------------
[ -f "$ELF" ] || fail "o EBOOT decifrado ($ELF)" \
    "Se so' tens o EBOOT.BIN cifrado: $PY $HERE/decrypt_self.py EBOOT.BIN EBOOT.ELF"

if ! file "$ELF" | grep -q 'ELF 64-bit MSB.*PowerPC'; then
    fail "um ELF64 BE PowerPC valido em $ELF" "file diz: $(file -b "$ELF")"
fi

# `command -v java` NAO chega: o macOS traz um stub em /usr/bin/java que existe
# e falha ao correr ("Unable to locate a Java Runtime"). E o openjdk@21 do brew
# e' keg-only -- fica fora do PATH mesmo depois de `brew install ghidra`.
# Testar a execucao e, se preciso, apontar o JAVA_HOME ao keg.
if ! java -version >/dev/null 2>&1; then
    for _j in "${JAVA_HOME:-}" /opt/homebrew/opt/openjdk@21 /opt/homebrew/opt/openjdk \
              /usr/local/opt/openjdk@21; do
        if [ -n "$_j" ] && [ -x "$_j/bin/java" ]; then
            export JAVA_HOME="$_j"
            export PATH="$_j/bin:$PATH"
            echo "[java] keg-only detectado: JAVA_HOME=$JAVA_HOME"
            break
        fi
    done
fi
if ! java -version >/dev/null 2>&1; then
    fail "um JDK a funcionar (o Ghidra exige JDK 21)" \
        "brew install ghidra    # puxa o openjdk@21 como dependencia" \
        "nota: /usr/bin/java existe no macOS mesmo sem JDK -- e' um stub," \
        "      e o openjdk do brew e' keg-only (fora do PATH)"
fi

if ! "$PY" "$PS3/tools/ghidra_analyze.py" --help >/dev/null 2>&1; then
    fail "tools/ghidra_analyze.py utilizavel" "verifica $PS3/tools/"
fi

# O find_ghidra do ghidra_analyze.py procura por GHIDRA_INSTALL_DIR/GHIDRA_HOME
# e por uma lista de caminhos comuns (ja' inclui o libexec do brew).
if [ -z "${GHIDRA_INSTALL_DIR:-}" ] && [ -d /opt/homebrew/opt/ghidra/libexec ]; then
    export GHIDRA_INSTALL_DIR=/opt/homebrew/opt/ghidra/libexec
fi
if [ -n "${GHIDRA_INSTALL_DIR:-}" ] && [ ! -x "$GHIDRA_INSTALL_DIR/support/analyzeHeadless" ]; then
    fail "analyzeHeadless em $GHIDRA_INSTALL_DIR/support/" \
        "GHIDRA_INSTALL_DIR aponta para o sitio errado?"
fi

echo "== 1/4 analise Ghidra headless (isto demora; --decompile e' o que vale a pena) =="
echo "   elf=$ELF  out=$OUT"
"$PY" "$PS3/tools/ghidra_analyze.py" "$ELF" --decompile -o "$OUT"

echo
echo "== 2/4 mapa de nomes para o lifter =="
"$PY" "$PS3/tools/ghidra_names.py" "$OUT" -o "$OUT/names.json"

echo
# O ghidra_analyze.py ja' carimbou o MANIFEST no fim do export -- mas ANTES do
# names.json existir, por isso a contagem de nomes ficava a null. Re-carimbar
# aqui (idempotente) fecha o export com as 5 contagens certas.
echo "== 3/4 proveniencia (MANIFEST.json) =="
"$PY" "$PS3/tools/oracle_manifest.py" write --out "$OUT" --elf "$ELF"

echo
echo "== 4/4 resumo =="
"$PY" - "$OUT" <<'PY'
import json, os, sys
d = sys.argv[1]
def n(f):
    p = os.path.join(d, f)
    if not os.path.isfile(p): return None
    return json.load(open(p))
fn, dc, nm = n("functions.json"), n("decompiled.json"), n("names.json")
print(f"  funcoes            : {len(fn) if fn is not None else 'ausente'}")
print(f"  com C decompilado  : {len(dc) if dc is not None else 'ausente (faltou --decompile?)'}")
print(f"  nomes para o lifter: {len(nm) if nm is not None else 'ausente'}")
PY

cat <<EOF

Proximos passos
  1) Ler o codigo ORIGINAL num endereco (o wall actual e' 0x002B2DD0):
       $PY $PS3/tools/ghidra_lookup.py $OUT 0x002B2DD0
       $PY $PS3/tools/ghidra_lookup.py $OUT 0x002B2DD0 --callers
       $PY $PS3/tools/ghidra_lookup.py $OUT --writes 0xF4

  2) O proximo re-lift nasce com nomes reais em vez de func_XXXXXXXX:
       ppu_lifter.py ... --names $OUT/names.json
     (ja' esta no comando canonico do RELIFT=1; ver o plano RDY-0)
EOF
