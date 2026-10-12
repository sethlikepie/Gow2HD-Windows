#!/usr/bin/env bash
# lib_patch_convergence.sh -- nucleo da PERNA 3 do accept_relift.sh (correccao
# do defeito D2, 2026-08-03).
#
# O DEFEITO QUE ISTO CORRIGE
# --------------------------
# A perna 3 corria `apply_all_patches.sh` POR CIMA da arvore que a etapa 4 ja
# tinha patchado, e usava o resultado dessa SEGUNDA passagem como veredicto.
# Medido duas vezes (2026-08-02 e 2026-08-03):
#
#   1. MASCARA falhas de ordem. Um patch que so' aplica a' 2a passagem aparece
#      `APPLIED` no relatorio do gate -- mas NUNCA entrou no binario que o gate
#      acabou de testar. O gate declarava verde uma arvore que a 1a passagem --
#      a que produziu o binario -- nao tinha.
#   2. FABRICA falhas. `patch_1856a8_stream_opd.py` e `patch_icg_ctor_opd.py`
#      dao `APPLIED` na etapa 4 e `FAILED` na perna 3 com a razao "no
#      ps3_indirect_call decl": procuram a declaracao que eles proprios
#      converteram para `ps3_call_opd`. O MESMO log, 160 linhas abaixo, imprime
#      `[PASS] 1856A8 stream OPD` e `CHECKS: todos passaram`.
#   3. APAGA o `FAILED-PARTIAL`. O sinal de arvore MEIO ESCRITA -- o mais grave
#      dos seis estados -- degrada para `FAILED` limpo na 2a passagem, porque
#      ja' nao ha' nada para escrever. O estado que o gate existe para apanhar
#      desaparecia precisamente na medicao do gate.
#
# A CORRECCAO
# -----------
# A perna 3 passa a VERIFICAR CONVERGENCIA em vez de repetir a aplicacao:
#
#   A. as CONTAGENS vem do TSV da ETAPA 4 -- a corrida que produziu o binario.
#      Sem esse TSV nao ha' veredicto possivel: um gate sem medicao nunca passa
#      (a mesma politica que check_boot_health.py ja' usa para os logs de boot).
#   B. a CONVERGENCIA e' provada por `sha256` dos ppu_recomp_*.cpp antes e
#      depois de reaplicar. Se a reaplicacao mudar um byte, a arvore nao estava
#      convergida -- e isso e' exactamente a classe "ordem" -> FAIL.
#      O rc e o TSV dessa reaplicacao sao INFORMATIVOS, nunca autoritativos
#      (e' deles que vinham as falhas fabricadas).
#
# Referencias: games/gow2/notes/2026-08-03-seis-patches-que-nao-reaplicam.md §7
# (o diagnostico, opcoes A+B) e 2026-08-03-promocao.md §4 (a medicao das duas
# falhas fabricadas e da convergencia sha256 identica).
#
# Testado por: games/gow2/test_patch_convergence.sh (11 testes, hermeticos).

# ---- lift_chunks_sha LIFT_DIR ----------------------------------------------
# sha256 estavel do CONTEUDO dos ppu_recomp_*.cpp (nunca mtime -- a 2a passagem
# toca mtimes sem mudar bytes). Ordem lexicografica fixa, para o hash nao
# depender da ordem que o filesystem devolve. Imprime vazio se nao houver
# nenhum chunk -- caso que o chamador TEM de tratar como "nao medido".
lift_chunks_sha() {
  local lift="$1"
  local f found=0 acc=""
  [ -d "$lift" ] || return 0
  for f in $(ls "$lift"/ppu_recomp_*.cpp 2>/dev/null | sort); do
    found=1
    acc="$acc$(shasum -a 256 "$f" | cut -d' ' -f1)  $(basename "$f")
"
  done
  [ "$found" -eq 1 ] || return 0
  printf '%s' "$acc" | shasum -a 256 | cut -d' ' -f1
}

# ---- status_count TSV EXPR --------------------------------------------------
# Contagem por awk sobre um status TSV (patch/status/classe). Nunca conta o
# cabecalho (NR>1).
status_count() {
  awk -F'\t' "NR>1 && ($2) {n++} END{print n+0}" "$1" 2>/dev/null || echo 0
}

# ---- leg3_evaluate LIFT_DIR STATUS_TSV_ETAPA4 CMD [args...] ----------------
# rc=0 se a perna 3 passa. Imprime um relatorio de linhas `LEG3 ...`, todas
# rasteaveis pelo teste e pelo relatorio de aceite.
leg3_evaluate() {
  local lift="$1"; shift
  local tsv="$1"; shift
  # o resto e' o comando de reaplicacao (injectavel -- e' o que torna isto
  # testavel sem correr o corpus real de 140 patches)

  echo "LEG3 lift=$lift"
  echo "LEG3 fonte=$tsv (TSV da ETAPA 4 -- a corrida que produziu o binario)"

  # --- A. as contagens vem da etapa 4, e sem elas nao ha' veredicto ---------
  if [ ! -f "$tsv" ]; then
    echo "LEG3 veredicto=FAIL razao=sem TSV da etapa 4 ($tsv) -- um gate sem medicao nunca passa"
    return 1
  fi
  local n_linhas
  n_linhas=$(awk 'NR>1 && NF>0{n++} END{print n+0}' "$tsv" 2>/dev/null)
  n_linhas=${n_linhas:-0}
  if [ "$n_linhas" -eq 0 ]; then
    echo "LEG3 veredicto=FAIL razao=sem TSV da etapa 4 com linhas de dados ($tsv tem $n_linhas patches) -- nada foi medido"
    return 1
  fi

  local n_nomatch n_failed_nao_probe n_failed_partial n_unverified
  n_nomatch=$(status_count "$tsv" '$2=="NO-MATCH" && $3!="PROBE"')
  n_failed_nao_probe=$(status_count "$tsv" '($2=="FAILED" || $2=="FAILED-PARTIAL") && $3!="PROBE"')
  # FAILED-PARTIAL a parte: e' o sinal mais grave dos seis estados (arvore meio
  # escrita) e era o que a 2a passagem apagava. Continua a contar para o gate
  # (esta' incluido em n_failed_nao_probe) E aparece sozinho no relatorio.
  n_failed_partial=$(status_count "$tsv" '$2=="FAILED-PARTIAL" && $3!="PROBE"')
  n_unverified=$(status_count "$tsv" '$2=="UNVERIFIED"')

  echo "LEG3 patches_medidos=$n_linhas nomatch=$n_nomatch failed_nao_probe=$n_failed_nao_probe failed_partial_nao_probe=$n_failed_partial unverified=$n_unverified"

  # --- B. convergencia provada por sha256 -----------------------------------
  local sha_antes sha_depois
  sha_antes=$(lift_chunks_sha "$lift")
  if [ -z "$sha_antes" ]; then
    echo "LEG3 veredicto=FAIL razao=sem ppu_recomp_*.cpp em $lift -- nao ha' arvore para medir"
    return 1
  fi

  "$@"
  local reap_rc=$?

  sha_depois=$(lift_chunks_sha "$lift")

  local conv
  if [ "$sha_antes" = "$sha_depois" ]; then conv=OK; else conv=FALHOU; fi
  echo "LEG3 convergencia=$conv sha_antes=$sha_antes sha_depois=$sha_depois reaplicacao_rc=$reap_rc [rc e TSV da reaplicacao sao INFORMATIVOS -- e' deles que vinham as falhas fabricadas do D2]"

  # --- veredicto -------------------------------------------------------------
  local razoes=""
  [ "$n_nomatch" -ne 0 ] && razoes="$razoes NO-MATCH=$n_nomatch;"
  [ "$n_failed_nao_probe" -ne 0 ] && razoes="$razoes FAILED-fora-de-PROBE=$n_failed_nao_probe (dos quais FAILED-PARTIAL=$n_failed_partial);"
  [ "$conv" != "OK" ] && razoes="$razoes reaplicar mudou a arvore -- nao estava convergida (classe ORDEM: o binario testado nao tem estes patches);"

  if [ -z "$razoes" ]; then
    echo "LEG3 veredicto=PASS razao=zero NO-MATCH, zero FAILED/FAILED-PARTIAL fora de PROBE na etapa 4, e reaplicar nao muda um byte"
    return 0
  fi
  echo "LEG3 veredicto=FAIL razao=$razoes"
  return 1
}
