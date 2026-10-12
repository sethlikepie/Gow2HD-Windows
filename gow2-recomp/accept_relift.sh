#!/usr/bin/env bash
# accept_relift.sh -- o aceite de promocao (D-5.1 + D-5.2 + GATE-03) num
# unico comando: QUATRO pernas, nenhuma delas reimplementada aqui, cada uma
# ja existente noutro script, mais uma seccao de contadores por delta
# (D-5.2). rc final = AND das quatro pernas E dos contadores bloqueantes.
#
# A LICAO que criou este script (05-CONTEXT.md, 2026-07-26): o lift
# regenerado passou o smoke M0 com st620=11 em 6/6 -- MELHOR do que o
# criterio 1 exige (>=3 em >=4 de 6) -- e foi promovido a producao com base
# so nisso. Faltavam-lhe 36 conversoes OPD, 35 guardas 0x4F000000u e 9
# familias de marcador. O smoke mede a intro ate st620=11; a perda estava
# no WADLD e no caminho de shaders, DEPOIS disso. A perna 2
# (games/gow2/verify_lift.sh) e a que teria apanhado essa perda -- e' por
# isso que o smoke sozinho e necessario mas NUNCA suficiente.
#
# A LICAO que acrescentou a PERNA 4 (07-CONTEXT.md, 2026-07-30, Fase 7): as
# tres pernas acima medem lift/patches, mas NENHUMA delas mede se a cadeia
# de boot chega ao thr_auto_load. Foi exactamente esse buraco que promoveu,
# em 29 Jul, um binario que nao chega ao AUTO_LOAD (StartSeq=1, thr_end=0).
#
# AS QUATRO PERNAS:
#   perna 1 -- smoke_relift_equiv.sh [LIFT] [RUNS] [TSV]
#              rc=0 se st620>=3 em >=4 de 6 execucoes reais.
#   perna 2 -- games/gow2/verify_lift.sh LIFT_DIR (via PS3_ENGINE_ROOT)
#              rc=0 os 3 passos (lift_parity/MANIFEST/audit_boundaries)
#              passam por DELTA contra os baselines congelados (Fase 4 +
#              Plano 05-02) -- ja NAO nasce vermelho por divida conhecida.
#   perna 3 -- VERIFICACAO DE CONVERGENCIA (lib_patch_convergence.sh)
#              este script NUNCA usa o `rc` bruto do apply_all_patches.sh
#              para a perna 3: esse `rc` tambem conta UNVERIFIED (D-4.1),
#              que e divida conhecida (42 patches FUNCIONAL sem contrato) e
#              NAO deve bloquear esta fase. Exige-se so zero NO-MATCH e zero
#              FAILED/FAILED-PARTIAL fora de classe PROBE -- exactamente o
#              que D-5.1 especifica.
#
#              CORRECCAO 2026-08-03 (defeito D2) -- as contagens deixam de
#              vir de uma SEGUNDA corrida do apply_all_patches.sh sobre a
#              arvore que a etapa 4 ja' patchou, e passam a vir do TSV da
#              ETAPA 4: a corrida que produziu o binario que as pernas 1 e 4
#              acabaram de testar. Porque, medido duas vezes (2026-08-02 e
#              2026-08-03), a 2a passagem: (1) MASCARA falhas de ordem --
#              um patch que so' aplica a' 2a vez aparece APPLIED no gate sem
#              nunca ter entrado no binario testado; (2) FABRICA falhas --
#              patch_1856a8_stream_opd.py e patch_icg_ctor_opd.py dao
#              APPLIED na etapa 4 e FAILED na perna 3 ("no ps3_indirect_call
#              decl"), porque procuram a declaracao que eles proprios
#              converteram, enquanto o MESMO log imprime 160 linhas abaixo
#              "[PASS] 1856A8 stream OPD" e "CHECKS: todos passaram"; (3)
#              APAGA o FAILED-PARTIAL, o sinal mais grave dos seis estados
#              (arvore meio escrita), que degrada para FAILED limpo.
#              A reaplicacao continua a correr -- mas so' para PROVAR
#              CONVERGENCIA por sha256 dos ppu_recomp_*.cpp (reaplicar nao
#              pode mudar um byte). O rc e o TSV dela sao INFORMATIVOS.
#              Sem o TSV da etapa 4 a perna 3 FALHA: um gate sem medicao
#              nunca passa (a mesma politica de check_boot_health.py).
#              Testado por games/gow2/test_patch_convergence.sh (11 testes,
#              prova nos dois sentidos).
#   perna 4 -- smoke_chain_gate.sh --bin BIN RUNS TSV (GATE-03, Fase 7)
#              mede a cadeia de elos + a saude do boot. Reutiliza o MESMO
#              binario que a PERNA 1 acabou de construir -- nunca dispara um
#              segundo build a partir do mesmo LIFT_REL.
#
#              CORRECCAO 2026-08-03 -- o criterio passa a ser NAO PIOR QUE A
#              PRODUCAO, e o rc bruto de smoke_chain_gate.sh deixa de ser
#              autoritativo (a mesma politica que a perna 3 ja' usa para o
#              seu). Porque: esse rc exige elo_stopped="nenhum", os 5 elos
#              bloqueantes passados. A PRODUCAO nunca o atinge -- para no elo
#              4 em 100% das corridas medidas -- e ha' uma razao MEDIDA,
#              escrita em lib_boot_chain_metrics.sh:94-118: a thread
#              AUTO_LOAD so' pode ser criada a jusante de func_00242C94, o
#              LOOP PRINCIPAL do jogo, que so' retorna em REQUEST_EXITGAME.
#              "AUTO_LOAD nunca criada" e' o comportamento CORRECTO de um
#              jogo que ainda esta a correr; um binario que passasse esse elo
#              teria SAIDO do jogo. O criterio, como estava, exigia que o
#              jogo se fechasse para o lift ser aceite -- e era, por
#              construcao, inatingivel por qualquer lift, incluindo o de
#              producao que o candidato ia substituir.
#              O rc autoritativo vem agora de
#              lift_baseline/check_boot_health.py, que compara
#              elo/st620/ICALL-BAD/OOB/0xFFFF/FATAL contra
#              lift_baseline/PRODUCTION_REFERENCE.tsv -- a producao medida,
#              declarada com DATA e PROVENIENCIA por linha, nunca um numero
#              hard-coded aqui que ninguem consiga refutar daqui a um mes.
#              O rc bruto do smoke_chain_gate.sh continua a ser impresso,
#              como informacao, e o elo_stopped mais frequente tambem.
#
# rc final = 0 SO se as quatro pernas passarem. As quatro correm sempre,
# mesmo que uma ja tenha falhado (nao aborta cedo) -- o relatorio final
# mostra sempre o estado das quatro, nunca so da primeira que falhou.
#
# Uso:
#   ./accept_relift.sh LIFT_DIR [RUNS] [OUTDIR] [--patch-status TSV]
#     LIFT_DIR default nenhum (obrigatorio); RUNS default 6; OUTDIR default /tmp.
#     --patch-status TSV: o status TSV escrito pela ETAPA 4 da sequencia
#       canonica (apply_all_patches.sh LIFT --status TSV, PRIMEIRA passagem
#       sobre o lift fresco). Equivalente por ambiente: ACCEPT_PATCH_STATUS.
#       OBRIGATORIO -- sem ele a perna 3 nao tem o que julgar (ver acima).
#
# Protocolo G6 (D-5.5): logs em /tmp (fora dos dois repositorios), kill
# sempre por PID dentro de smoke_relift_equiv.sh/smoke_chain_gate.sh (TERM
# depois -9), nunca pkill -f boot_gow2.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
REPO="$PWD"

# ---- nucleo da perna 3 (D2): convergencia, nao repeticao -------------------
LIB_CONV="$REPO/lib_patch_convergence.sh"
if [ ! -f "$LIB_CONV" ]; then
  echo "ERRO: $LIB_CONV nao encontrado -- sem ele a perna 3 nao pode ser avaliada" >&2
  exit 2
fi
# shellcheck source=/dev/null
. "$LIB_CONV"

# ---- args: posicionais + --patch-status (bash 3.2, set -u) -----------------
PATCH_STATUS_TSV="${ACCEPT_PATCH_STATUS:-}"
POS=""
while [ $# -gt 0 ]; do
  case "$1" in
    --patch-status)   PATCH_STATUS_TSV="${2:-}"; shift 2 || shift ;;
    --patch-status=*) PATCH_STATUS_TSV="${1#*=}"; shift ;;
    *)                POS="$POS $1"; shift ;;
  esac
done
# shellcheck disable=SC2086
set -- $POS

LIFT_REL="${1:?uso: accept_relift.sh LIFT_DIR [RUNS] [OUTDIR] [--patch-status TSV]}"
RUNS="${2:-6}"
OUTDIR="${3:-/tmp}"

LIFT="$REPO/${LIFT_REL#./}"
if [ ! -d "$LIFT" ]; then
  echo "ERRO: dir de lift nao existe: $LIFT" >&2
  exit 2
fi
TAG="$(basename "$LIFT")"

# Falha CEDO se o TSV da etapa 4 nao foi dado: a perna 3 e' insatisfazivel sem
# ele (D2) e nao ha' razao para gastar um build e 12 boots antes de o dizer.
if [ -z "$PATCH_STATUS_TSV" ] || [ ! -f "$PATCH_STATUS_TSV" ]; then
  echo "ERRO: falta o TSV da ETAPA 4 (--patch-status TSV ou ACCEPT_PATCH_STATUS)." >&2
  echo "      A perna 3 julga a arvore que produziu o binario -- a PRIMEIRA passagem" >&2
  echo "      do apply_all_patches.sh sobre o lift fresco. Sem esse TSV nao ha' o que" >&2
  echo "      julgar, e um gate sem medicao nunca passa (defeito D2, 2026-08-03)." >&2
  echo "      Recebido: '${PATCH_STATUS_TSV:-<vazio>}'" >&2
  exit 2
fi

# ---- ponte cross-repo (mesma convencao de apply_all_patches.sh:112) --------
PS3_ENGINE_ROOT="${PS3_ENGINE_ROOT:-$REPO/../ps3recomp}"
VERIFY_LIFT="$PS3_ENGINE_ROOT/games/gow2/verify_lift.sh"
if [ ! -x "$VERIFY_LIFT" ]; then
  echo "ERRO: verify_lift.sh nao encontrado ou nao executavel ($VERIFY_LIFT) -- sem a perna 2 nao ha aceite possivel" >&2
  exit 2
fi

echo "=============================================================="
echo " accept_relift.sh -- aceite de tres pernas + contadores (D-5.1 + D-5.2)"
echo " lift   : $LIFT"
echo " runs   : $RUNS"
echo " outdir : $OUTDIR"
echo "=============================================================="
echo

# ---- PERNA 1: smoke_relift_equiv.sh ----------------------------------------
TSV1="$OUTDIR/accept_${TAG}_smoke.tsv"
echo "---- PERNA 1: smoke_relift_equiv.sh ----"
"$REPO/smoke_relift_equiv.sh" "$LIFT_REL" "$RUNS" "$TSV1"
leg1_rc=$?
leg1_ok=$(awk -F'\t' 'NR>1 && $8=="OK"{n++} END{print n+0}' "$TSV1" 2>/dev/null)
leg1_ok=${leg1_ok:-0}
echo "PERNA 1: OK=$leg1_ok de $RUNS, rc=$leg1_rc"
echo

# ---- PERNA 4: smoke_chain_gate.sh (cadeia, GATE-03) ------------------------
# Reutiliza o MESMO binario que a PERNA 1 acabou de construir -- mesma
# convencao de nomes de smoke_relift_equiv.sh:84 (BIN="$HERE/boot_gow2_${TAG#recomp_macos_}").
# NUNCA dispara um segundo build a partir do mesmo LIFT_REL (~1.5min
# desperdicados e um segundo binario que podia divergir do primeiro por
# timing de compilacao).
BIN4="$REPO/boot_gow2_${TAG#recomp_macos_}"
TSV4="$OUTDIR/accept_${TAG}_chain.tsv"
LOG4="$OUTDIR/accept_${TAG}_chain.log"
LOG4H="$OUTDIR/accept_${TAG}_boot_health.log"
CHECK_HEALTH="$PS3_ENGINE_ROOT/games/gow2/lift_baseline/check_boot_health.py"
REF_PROD="$PS3_ENGINE_ROOT/games/gow2/lift_baseline/PRODUCTION_REFERENCE.tsv"

# interpretador: a mesma resolucao de verify_lift.sh (nunca assumir o do PATH)
PY4="${PS3_PATCH_PYTHON:-}"
if [ -z "$PY4" ]; then
  if [ -x "$PS3_ENGINE_ROOT/.venv/bin/python3" ]; then PY4="$PS3_ENGINE_ROOT/.venv/bin/python3"
  else PY4="$(command -v python3 || true)"; fi
fi

echo "---- PERNA 4: smoke_chain_gate.sh (cadeia) + check_boot_health.py (nao pior que a producao) ----"
if [ -x "$BIN4" ]; then
  "$REPO/smoke_chain_gate.sh" --bin "$BIN4" "$RUNS" "$TSV4" 2>&1 | tee "$LOG4"
  leg4_rc_bruto=${PIPESTATUS[0]}
  # Rule 1 (2026-08-03): estes dois liam as colunas ERRADAS do TSV do
  # smoke_chain_gate.sh -- o cabecalho e' run(1)..pad_total(9) elo_stopped(10)
  # class(11), e estava a testar $10=="OK" (a coluna do elo) e a imprimir $9
  # (pad_total) como se fosse o elo. Consequencia medida: leg4_ok era SEMPRE 0
  # e o elo mais frequente saia VAZIO. Os dois campos sao informativos (nunca
  # entraram em nenhum rc), mas um relatorio que mente e' pior do que um
  # relatorio que cala. Corrigidos para $11/$10.
  leg4_ok=$(awk -F'\t' 'NR>1 && $11=="OK"{n++} END{print n+0}' "$TSV4" 2>/dev/null)
  leg4_ok=${leg4_ok:-0}
  leg4_elo="$(awk -F'\t' 'NR>1 && $11=="REGRESSAO"{print $10}' "$TSV4" 2>/dev/null \
    | sort | uniq -c | sort -rn | head -1 | sed 's/^ *[0-9]* //')"

  # Os logs de boot sao os que o proprio smoke_chain_gate.sh anunciou
  # ("log: /tmp/..."), nunca um glob adivinhado -- um glob apanharia corridas
  # de outra sessao e mediria o binario errado.
  health_args=()
  while IFS= read -r l; do
    [ -n "$l" ] && health_args+=(--log "$l")
  done < <(grep '^log: ' "$LOG4" 2>/dev/null | sed 's/^log: //')

  if [ -z "$PY4" ] || [ ! -f "$CHECK_HEALTH" ]; then
    echo "ERRO: sem python3 ou sem $CHECK_HEALTH -- a perna 4 nao pode ser avaliada" >&2
    leg4_rc=2
  else
    # ${a[@]+"${a[@]}"}: o bash 3.2 do macOS estoira com `set -u` num array
    # VAZIO expandido como "${a[@]}" -- e um array vazio aqui e' exactamente o
    # caso "nao houve logs", que tem de chegar ao check e ser REJEITADO la',
    # nao rebentar o script antes de o gate opinar.
    "$PY4" "$CHECK_HEALTH" --chain-tsv "$TSV4" --reference "$REF_PROD" \
      ${health_args[@]+"${health_args[@]}"} 2>&1 | tee "$LOG4H"
    leg4_rc=${PIPESTATUS[0]}
  fi
else
  echo "ERRO: binario da PERNA 1 nao encontrado ($BIN4) -- PERNA 1 deve ter falhado antes de construir" >&2
  leg4_rc=2
  leg4_rc_bruto=2
  leg4_ok=0
  leg4_elo="binario nao construido"
fi
echo "PERNA 4: rc=$leg4_rc (check_boot_health.py, autoritativo) | elo 'nenhum' em $leg4_ok de $RUNS, rc bruto do smoke_chain_gate=$leg4_rc_bruto [INFORMATIVO -- exige REQUEST_EXITGAME, ver cabecalho]"
[ "$leg4_ok" -eq 0 ] && echo "PERNA 4: elo_stopped mais frequente: ${leg4_elo:-desconhecido}"
echo

# ---- PERNA 2: games/gow2/verify_lift.sh ------------------------------------
LOG2="$OUTDIR/accept_${TAG}_verify.log"
echo "---- PERNA 2: verify_lift.sh ----"
"$VERIFY_LIFT" "$LIFT" > "$LOG2" 2>&1
leg2_rc=$?
leg2_detail="$(grep -E '^(lift_parity|MANIFEST|audit_boundaries)\s' "$LOG2" 2>/dev/null)"
printf '%s\n' "$leg2_detail"
echo "PERNA 2: rc=$leg2_rc (log completo: $LOG2)"
echo

# ---- PERNA 3: convergencia (D2) -- consome o TSV da ETAPA 4 ---------------
# NAO julga o resultado de uma segunda passagem sobre a arvore ja' patchada
# (era isso que mascarava falhas de ordem, fabricava duas falhas e apagava o
# FAILED-PARTIAL -- ver o cabecalho). A reaplicacao corre, mas so' para provar
# por sha256 que nao muda um byte.
TSV3="$OUTDIR/accept_${TAG}_status.tsv"     # TSV da REAPLICACAO (informativo)
LOG3="$OUTDIR/accept_${TAG}_patches.log"
LOG3C="$OUTDIR/accept_${TAG}_convergencia.log"
echo "---- PERNA 3: convergencia (TSV da etapa 4 + sha256 antes/depois de reaplicar) ----"
# a reaplicacao e' uma FUNCAO para que a sua saida (milhares de linhas) va toda
# para LOG3 e nao polua o relatorio da perna 3
leg3_reapply() { "$REPO/apply_all_patches.sh" "$LIFT_REL" --status "$TSV3" > "$LOG3" 2>&1; }
leg3_evaluate "$LIFT" "$PATCH_STATUS_TSV" leg3_reapply 2>&1 | tee "$LOG3C"
leg3_pass=0
grep -q 'LEG3 veredicto=PASS' "$LOG3C" && leg3_pass=1
# campos para o relatorio final -- extraidos do relatorio da lib, nunca recontados
leg3_field() { sed -n "s/.*[ =]$1=\([^ ]*\).*/\1/p" "$LOG3C" | head -1; }
n_nomatch=$(leg3_field nomatch);                     n_nomatch=${n_nomatch:-0}
n_failed_nao_probe=$(leg3_field failed_nao_probe);   n_failed_nao_probe=${n_failed_nao_probe:-0}
n_failed_partial=$(leg3_field failed_partial_nao_probe); n_failed_partial=${n_failed_partial:-0}
n_unverified=$(leg3_field unverified);               n_unverified=${n_unverified:-0}
leg3_conv=$(leg3_field convergencia);                leg3_conv=${leg3_conv:-NAO-MEDIDA}
echo "PERNA 3: leg3_pass=$leg3_pass (detalhe em $LOG3C; reaplicacao informativa em $LOG3 / $TSV3)"
echo

# ---- CONTADORES (D-5.2): baseline REAL congelado vs candidato, por DELTA ---
# 56072 (function_table_count) e 56223 (lifted functions registered), citados
# em ROADMAP.md/REQUIREMENTS.md, sao do lift ANTIGO escrito a mao (31 chunks --
# ver games/gow2/lift_baseline/counters_pre.tsv). O lift regenerado (producao
# E candidato, medido nesta sessao) tem function_table_count=51917 nos DOIS --
# gatear pelos absolutos antigos faria este gate falhar sempre contra
# qualquer lift novo. Em vez disso: congela-se UMA VEZ um baseline real
# contra a producao atual (recomp_macos_v2/boot_gow2), e mede-se o candidato
# por delta. imp_modules/imp_imports/orfaos NAO dependem do lift -- ficam
# absolutos e BLOQUEANTES. function_table_count/boot_lifted_functions ficam
# INFORMATIVOS (delta reportado, nunca bloqueia este rc -- a justificacao por
# escrito do delta fica no relatorio de aceite, Plano 05-05).
BASELINE_TSV="$PS3_ENGINE_ROOT/games/gow2/lift_baseline/COUNTERS_BASELINE.tsv"

baseline_val() {
  awk -F'\t' -v k="$1" '$1==k{print $2; exit}' "$BASELINE_TSV"
}

counters_baseline_freeze() {
  if [ -f "$BASELINE_TSV" ]; then
    return 0
  fi
  echo "AVISO: $BASELINE_TSV nao existe -- congelando baseline REAL contra a producao (recomp_macos_v2/boot_gow2)" >&2
  local ftc_prod prod_tsv lifted_prod modules_prod imports_prod
  # Rule 1 fix (encontrado nesta corrida real): "grep -oE '[0-9]+'" sozinho
  # casa o "64" de "uint64_t" ANTES do valor real apos "= " -- extrair so o
  # numero apos "= " evita esse falso-positivo.
  ftc_prod=$(grep -h "function_table_count = " "$REPO"/recomp_macos_v2/ppu_recomp_*.cpp 2>/dev/null | grep -oE '= [0-9]+' | grep -oE '[0-9]+' | head -1)
  prod_tsv="$OUTDIR/counters_baseline_prod.tsv"
  "$REPO/smoke_relift_equiv.sh" --bin "$REPO/boot_gow2" 1 "$prod_tsv"
  lifted_prod=$(tail -1 "$prod_tsv" | awk -F'\t' '{print $3}')
  modules_prod=$(tail -1 "$prod_tsv" | awk -F'\t' '{print $4}')
  imports_prod=$(tail -1 "$prod_tsv" | awk -F'\t' '{print $5}')
  mkdir -p "$(dirname "$BASELINE_TSV")"
  {
    echo "# COUNTERS_BASELINE.tsv -- baseline REAL congelado (D-5.2), medido por"
    echo "# accept_relift.sh contra a producao atual (recomp_macos_v2 / boot_gow2)"
    echo "# na primeira corrida em que este ficheiro nao existia."
    echo "#"
    echo "# NOTA: 56072/56223 (ROADMAP.md/REQUIREMENTS.md) sao do lift ANTIGO"
    echo "# escrito a mao (31 chunks -- ver games/gow2/lift_baseline/counters_pre.tsv)."
    echo "# O lift regenerado (medido nesta sessao) tem function_table_count=51917"
    echo "# tanto na producao como no candidato -- gatear pelos absolutos antigos"
    echo "# faria este gate falhar sempre contra qualquer lift novo (D-5.2)."
    printf 'contador\tvalor\n'
    printf 'function_table_count\t%s\n' "$ftc_prod"
    printf 'boot_lifted_functions\t%s\n' "$lifted_prod"
    printf 'imp_modules\t%s\n' "$modules_prod"
    printf 'imp_imports\t%s\n' "$imports_prod"
    printf 'orfaos\t0\n'
  } > "$BASELINE_TSV"
  echo "COUNTERS_BASELINE.tsv congelado: $BASELINE_TSV"
}

counters_baseline_freeze

# Rule 1 fix: mesmo cuidado que em counters_baseline_freeze() -- "64" de
# "uint64_t" precede o valor real de function_table_count na mesma linha.
ftc_cand=$(grep -h "function_table_count = " "$LIFT"/ppu_recomp_*.cpp 2>/dev/null | grep -oE '= [0-9]+' | grep -oE '[0-9]+' | head -1)
lifted_cand=$(tail -1 "$TSV1" | awk -F'\t' '{print $3}')
modules_cand=$(tail -1 "$TSV1" | awk -F'\t' '{print $4}')
imports_cand=$(tail -1 "$TSV1" | awk -F'\t' '{print $5}')
orfaos_cand=$(pgrep -f "$TAG" | wc -l | tr -d ' ')

ftc_prod="$(baseline_val function_table_count)"
lifted_prod="$(baseline_val boot_lifted_functions)"
modules_prod="$(baseline_val imp_modules)"
imports_prod="$(baseline_val imp_imports)"

echo "---- CONTADORES (D-5.2) ----"
printf '%-24s producao=%-8s candidato=%-8s delta=%-6s %s\n' \
  "function_table_count" "$ftc_prod" "$ftc_cand" "$((ftc_cand - ftc_prod))" "[INFORMATIVO -- delta justificado no relatorio de aceite]"
printf '%-24s producao=%-8s candidato=%-8s delta=%-6s %s\n' \
  "boot_lifted_functions" "$lifted_prod" "$lifted_cand" "$((lifted_cand - lifted_prod))" "[INFORMATIVO -- delta justificado no relatorio de aceite]"
printf '%-24s producao=%-8s candidato=%-8s delta=%-6s %s\n' \
  "imp_modules" "$modules_prod" "$modules_cand" "$((modules_cand - modules_prod))" "[BLOQUEANTE]"
printf '%-24s producao=%-8s candidato=%-8s delta=%-6s %s\n' \
  "imp_imports" "$imports_prod" "$imports_cand" "$((imports_cand - imports_prod))" "[BLOQUEANTE]"
printf '%-24s producao=%-8s candidato=%-8s delta=%-6s %s\n' \
  "orfaos" "0" "$orfaos_cand" "$((orfaos_cand - 0))" "[BLOQUEANTE]"
echo

if [ "$modules_cand" = "$modules_prod" ] && [ "$imports_cand" = "$imports_prod" ] && [ "$orfaos_cand" -eq 0 ] 2>/dev/null; then
  counters_pass=1
else
  counters_pass=0
fi

# ---- relatorio final --------------------------------------------------------
echo "=============================================================="
echo " RELATORIO DE ACEITE (D-5.1 + D-5.2 + GATE-03)"
echo "=============================================================="
printf 'PERNA 1 (smoke)          %s   (OK=%s/%s)\n' "$([ "$leg1_rc" -eq 0 ] && echo PASS || echo FAIL)" "$leg1_ok" "$RUNS"
printf 'PERNA 2 (verify_lift)    %s   (rc=%s)\n' "$([ "$leg2_rc" -eq 0 ] && echo PASS || echo FAIL)" "$leg2_rc"
printf 'PERNA 3 (convergencia)   %s   (etapa 4: NO-MATCH=%s FAILED-fora-PROBE=%s dos quais FAILED-PARTIAL=%s | reaplicar muda a arvore? convergencia=%s | UNVERIFIED=%s informativo)\n' \
  "$([ "$leg3_pass" -eq 1 ] && echo PASS || echo FAIL)" "$n_nomatch" "$n_failed_nao_probe" "$n_failed_partial" "$leg3_conv" "$n_unverified"
if [ "$leg4_rc" -eq 0 ]; then
  printf 'PERNA 4 (chain+saude)    %s   (nao pior que a producao declarada; detalhe: %s)\n' "PASS" "$LOG4H"
else
  printf 'PERNA 4 (chain+saude)    %s   (pior que a producao declarada, ou nao medido; elo_stopped mais frequente: %s; detalhe: %s)\n' "FAIL" "${leg4_elo:-desconhecido}" "$LOG4H"
fi
printf 'CONTADORES (D-5.2)       %s   (imp_modules/imp_imports/orfaos bloqueantes)\n' \
  "$([ "$counters_pass" -eq 1 ] && echo PASS || echo FAIL)"
echo

if [ "$leg1_rc" -eq 0 ] && [ "$leg2_rc" -eq 0 ] && [ "$leg3_pass" -eq 1 ] && [ "$leg4_rc" -eq 0 ] && [ "$counters_pass" -eq 1 ]; then
  echo "ACEITE: rc=0 -- as quatro pernas + contadores bloqueantes passaram."
  exit 0
else
  echo "REJEITADO: rc=1 -- pelo menos uma perna ou os contadores bloqueantes falharam (ver PASS/FAIL acima)."
  exit 1
fi
