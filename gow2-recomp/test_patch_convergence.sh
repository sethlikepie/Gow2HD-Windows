#!/usr/bin/env bash
# test_patch_convergence.sh -- teste-ouro da PERNA 3 do accept_relift.sh
# (defeito D2, diagnosticado em games/gow2/notes/2026-08-03-seis-patches-que-
# nao-reaplicam.md §7 e medido duas vezes em 2026-08-03-promocao.md §4).
#
# O QUE ESTE TESTE EXISTE PARA IMPEDIR
# ------------------------------------
# A perna 3 corria `apply_all_patches.sh` POR CIMA da arvore que a etapa 4 ja
# tinha patchado, e reportava o resultado dessa SEGUNDA passagem. Consequencias
# medidas nos logs de 2026-08-02 e 2026-08-03:
#
#   1. MASCARA falhas de ordem -- um patch que so' aplica a' 2a passagem aparece
#      `APPLIED` no gate, mas NUNCA entrou no binario que o gate testou.
#   2. FABRICA falhas -- `patch_1856a8_stream_opd.py` e `patch_icg_ctor_opd.py`
#      dao `APPLIED` na etapa 4 e `FAILED` ("no ps3_indirect_call decl") na
#      perna 3: procuram a declaracao que eles proprios converteram.
#   3. APAGA o `FAILED-PARTIAL` -- o sinal de arvore MEIO ESCRITA, o mais grave
#      dos seis estados, degrada para `FAILED` limpo na 2a passagem.
#
# A perna 3 passa a VERIFICAR CONVERGENCIA em vez de repetir a aplicacao:
# consome o TSV da etapa 4 (a corrida que produziu o binario) para as contagens,
# e prova por `sha256` que reaplicar NAO MUDA a arvore.
#
# PROVA NOS DOIS SENTIDOS (sem o segundo lado o gate e' decorativo):
#   - arvore que converge  -> VERDE
#   - arvore que NAO converge (a reaplicacao muda um byte) -> VERMELHO
#
# Hermetico: zero builds, zero boots, zero lifts reais. Tudo em /tmp.
# Run: bash games/gow2/test_patch_convergence.sh
# Exit 0 = todos passaram.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="$HERE/lib_patch_convergence.sh"

if [ ! -f "$LIB" ]; then
  echo "FALHA: $LIB nao existe" >&2
  exit 1
fi
# shellcheck source=/dev/null
. "$LIB"

FAIL=0
N_OK=0
TMPROOT="$(mktemp -d "/tmp/test_patch_conv_XXXXXX")"
trap 'rm -rf "$TMPROOT"' EXIT

ok()   { N_OK=$((N_OK + 1)); echo "[PASS] $1"; }
bad()  { FAIL=1; echo "[FALHA] $1" >&2; }

# ---- fabrica de cenarios ----------------------------------------------------
# mk_lift NOME -> cria um dir de lift falso com 3 chunks
mk_lift() {
  local d="$TMPROOT/$1"
  mkdir -p "$d"
  local i
  for i in 000 001 002; do
    printf 'void func_%s(void) { /* chunk %s */ }\n' "$i" "$i" > "$d/ppu_recomp_$i.cpp"
  done
  echo "$d"
}

# mk_tsv FICHEIRO LINHAS... -> escreve um status TSV com cabecalho
mk_tsv() {
  local f="$1"; shift
  printf 'patch\tstatus\tclasse\n' > "$f"
  local l
  for l in "$@"; do
    printf '%s\n' "$l" >> "$f"
  done
  echo "$f"
}

# mk_reapply NOME MODO -> cria um comando de reaplicacao falso.
#   MODO=convergente   -> nao toca na arvore (a 2a passagem nao muda nada)
#   MODO=divergente    -> escreve um byte num chunk (a arvore NAO estava convergida)
#   MODO=mentiroso     -> nao toca na arvore mas imprime/regista FAILED no seu
#                         proprio TSV (o caso D2: falha fabricada pela 2a passagem)
mk_reapply() {
  local f="$TMPROOT/reapply_$1.sh"
  cat > "$f" <<EOF
#!/usr/bin/env bash
# \$1 = dir do lift
touch "$TMPROOT/sentinela_$1"
case "$2" in
  divergente) printf '/* injectado pela 2a passagem */\n' >> "\$1/ppu_recomp_001.cpp" ;;
  mentiroso)  echo "FAILED   patch_1856a8_stream_opd.py -- no ps3_indirect_call decl in 000"; exit 1 ;;
esac
echo "reaplicacao falsa modo=$2"
exit 0
EOF
  chmod +x "$f"
  echo "$f"
}

echo "=============================================================="
echo " test_patch_convergence.sh -- perna 3 (D2)"
echo " tmp: $TMPROOT"
echo "=============================================================="

# ---- T1: arvore convergida + TSV limpo -> VERDE ----------------------------
L=$(mk_lift t1); T=$(mk_tsv "$TMPROOT/t1.tsv" \
  "$(printf 'patch_a.py\tAPPLIED\tFUNCIONAL')" \
  "$(printf 'patch_b.py\tALREADY-APPLIED\tFUNCIONAL')" \
  "$(printf 'patch_c.py\tUNVERIFIED\tFUNCIONAL')")
R=$(mk_reapply t1 convergente)
out=$(leg3_evaluate "$L" "$T" "$R" "$L" 2>&1); rc=$?
if [ "$rc" -eq 0 ]; then ok "T1 arvore convergida + TSV limpo -> PASS"
else bad "T1 devia passar (rc=$rc)"; printf '%s\n' "$out" >&2; fi

# ---- T2: a reaplicacao MUDA a arvore -> VERMELHO (o segundo sentido) -------
L=$(mk_lift t2); T=$(mk_tsv "$TMPROOT/t2.tsv" \
  "$(printf 'patch_a.py\tAPPLIED\tFUNCIONAL')")
R=$(mk_reapply t2 divergente)
out=$(leg3_evaluate "$L" "$T" "$R" "$L" 2>&1); rc=$?
if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -q 'convergencia=FALHOU'; then
  ok "T2 arvore NAO convergida -> FAIL com convergencia=FALHOU"
else bad "T2 devia falhar por convergencia (rc=$rc)"; printf '%s\n' "$out" >&2; fi

# ---- T3: FAILED-PARTIAL fora de PROBE continua a contar --------------------
L=$(mk_lift t3); T=$(mk_tsv "$TMPROOT/t3.tsv" \
  "$(printf 'patch_a.py\tAPPLIED\tFUNCIONAL')" \
  "$(printf 'patch_b71.py\tFAILED-PARTIAL\tCORRECTNESS')")
R=$(mk_reapply t3 convergente)
out=$(leg3_evaluate "$L" "$T" "$R" "$L" 2>&1); rc=$?
if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -q 'failed_partial_nao_probe=1'; then
  ok "T3 FAILED-PARTIAL fora de PROBE -> FAIL e e' contado a parte"
else bad "T3 FAILED-PARTIAL devia bloquear e ser contado (rc=$rc)"; printf '%s\n' "$out" >&2; fi

# ---- T4: FAILED de classe PROBE nao bloqueia -------------------------------
L=$(mk_lift t4); T=$(mk_tsv "$TMPROOT/t4.tsv" \
  "$(printf 'patch_x_probe.py\tFAILED\tPROBE')" \
  "$(printf 'patch_y_probe.py\tFAILED-PARTIAL\tPROBE')")
R=$(mk_reapply t4 convergente)
out=$(leg3_evaluate "$L" "$T" "$R" "$L" 2>&1); rc=$?
if [ "$rc" -eq 0 ]; then ok "T4 FAILED/FAILED-PARTIAL de classe PROBE -> PASS (fora do gate)"
else bad "T4 PROBE nao devia bloquear (rc=$rc)"; printf '%s\n' "$out" >&2; fi

# ---- T5: NO-MATCH fora de PROBE bloqueia -----------------------------------
L=$(mk_lift t5); T=$(mk_tsv "$TMPROOT/t5.tsv" \
  "$(printf 'patch_a.py\tNO-MATCH\tFUNCIONAL')")
R=$(mk_reapply t5 convergente)
out=$(leg3_evaluate "$L" "$T" "$R" "$L" 2>&1); rc=$?
if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -q 'nomatch=1'; then
  ok "T5 NO-MATCH fora de PROBE -> FAIL"
else bad "T5 NO-MATCH devia bloquear (rc=$rc)"; printf '%s\n' "$out" >&2; fi

# ---- T6: sem TSV da etapa 4 -> FAIL (um gate sem medicao nunca passa) ------
L=$(mk_lift t6)
R=$(mk_reapply t6 convergente)
out=$(leg3_evaluate "$L" "$TMPROOT/nao_existe.tsv" "$R" "$L" 2>&1); rc=$?
if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -q 'sem TSV'; then
  ok "T6 TSV da etapa 4 ausente -> FAIL (gate sem medicao nunca passa)"
else bad "T6 sem TSV devia falhar (rc=$rc)"; printf '%s\n' "$out" >&2; fi

# ---- T7: TSV so' com cabecalho (zero patches medidos) -> FAIL --------------
L=$(mk_lift t7); T=$(mk_tsv "$TMPROOT/t7.tsv")
R=$(mk_reapply t7 convergente)
out=$(leg3_evaluate "$L" "$T" "$R" "$L" 2>&1); rc=$?
if [ "$rc" -ne 0 ]; then ok "T7 TSV sem linhas de dados -> FAIL"
else bad "T7 TSV vazio devia falhar (rc=$rc)"; printf '%s\n' "$out" >&2; fi

# ---- T8: o caso D2 -- a 2a passagem MENTE, o veredicto vem da etapa 4 ------
# A reaplicacao devolve rc!=0 e diz FAILED (exactamente o que os dois patches
# OPD fazem: procuram a declaracao que eles proprios converteram). A arvore NAO
# muda. O veredicto TEM de vir do TSV da etapa 4 -> PASS.
L=$(mk_lift t8); T=$(mk_tsv "$TMPROOT/t8.tsv" \
  "$(printf 'patch_1856a8_stream_opd.py\tAPPLIED\tFUNCIONAL')" \
  "$(printf 'patch_icg_ctor_opd.py\tAPPLIED\tFUNCIONAL')")
R=$(mk_reapply t8 mentiroso)
out=$(leg3_evaluate "$L" "$T" "$R" "$L" 2>&1); rc=$?
if [ "$rc" -eq 0 ]; then ok "T8 2a passagem reporta FAILED mas nao muda a arvore -> PASS (D2 fechado)"
else bad "T8 o veredicto tem de vir da etapa 4, nao da 2a passagem (rc=$rc)"; printf '%s\n' "$out" >&2; fi

# ---- T9: a convergencia e' PROVADA, nao assumida ---------------------------
# O comando de reaplicacao tem de ser mesmo executado (deixa sentinela).
if [ -f "$TMPROOT/sentinela_t1" ] && [ -f "$TMPROOT/sentinela_t8" ]; then
  ok "T9 o comando de reaplicacao foi executado (convergencia provada, nao assumida)"
else bad "T9 a reaplicacao nunca correu -- a convergencia estaria a ser assumida"; fi

# ---- T10: lift sem chunks -> FAIL ------------------------------------------
mkdir -p "$TMPROOT/t10_vazio"
T=$(mk_tsv "$TMPROOT/t10.tsv" "$(printf 'patch_a.py\tAPPLIED\tFUNCIONAL')")
R=$(mk_reapply t10 convergente)
out=$(leg3_evaluate "$TMPROOT/t10_vazio" "$T" "$R" "$TMPROOT/t10_vazio" 2>&1); rc=$?
if [ "$rc" -ne 0 ]; then ok "T10 lift sem ppu_recomp_*.cpp -> FAIL"
else bad "T10 lift vazio devia falhar (rc=$rc)"; printf '%s\n' "$out" >&2; fi

# ---- T11: lift_chunks_sha e' estavel e sensivel a 1 byte -------------------
L=$(mk_lift t11)
s1=$(lift_chunks_sha "$L")
s2=$(lift_chunks_sha "$L")
printf 'x' >> "$L/ppu_recomp_002.cpp"
s3=$(lift_chunks_sha "$L")
if [ -n "$s1" ] && [ "$s1" = "$s2" ] && [ "$s1" != "$s3" ]; then
  ok "T11 lift_chunks_sha estavel entre corridas e sensivel a 1 byte"
else bad "T11 sha instavel ou insensivel (s1=$s1 s2=$s2 s3=$s3)"; fi

echo
echo "--------------------------------------------------------------"
if [ "$FAIL" -eq 0 ]; then
  echo "TODOS OS TESTES PASSARAM ($N_OK)"
  exit 0
fi
echo "HOUVE FALHAS (passaram $N_OK)"
exit 1
