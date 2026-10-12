#!/usr/bin/env bash
# bt_intro_wads.sh — Baseline reprodutivel: intro skip -> abertura dos WADs
# (R_LglScA, R_PermA). Congela o recipe de env vars que ja provou abrir os
# WADs, evidenciado em longskip.log/skip.log (gerados por run_long_skip.sh
# e bt_skip.sh, respectivamente).
#
# FIEL a run_long_skip.sh / bt_skip.sh: usa EXATAMENTE o mesmo recipe de env
# que produziu a evidencia (force=1, r_lgl=1, r_perm=1, flips=303 em
# longskip.log). Nao acrescenta PS3_RSX_BACKEND / PS3_RSX_FIFO /
# PS3_PAD_AUTOSTART — essas vars aparecem no plano mas NAO em
# run_long_skip.sh/bt_skip.sh, e o longskip.log de referencia prova que o
# WAD-open (e ate os 303 flips) acontecem sem elas. Ver relatorio da Task 1
# para o diff completo vs o plano.
set +e
cd "$(dirname "$0")"
EXE=./boot_v2_new.exe
LOG=intro_wads.log
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1; sleep 1
rm -f "$LOG"

export PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted/USRDIR"
export PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64
export PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE="../movie_cache" PS3_MOVIE_EOS=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
# PS3_VDEC_ASYNC=1: NECESSARIO hoje (nao estava no run_long_skip.sh original).
# O runtime ps3recomp mudou o default de StartSeq entre a captura da evidencia
# (10/07) e agora: commit f811ee3 "reverter default p/ sync (async crashava o
# backend D3D12)" fez SYNC virar o default e ASYNC virar opt-in. O loop que
# dispara FORCE SEQDONE (vdec_driver, libs/codec/cellVdec.c) so roda na thread
# assincrona -- sem esta var o FORCE nunca dispara e os WADs nunca abrem
# (1a tentativa desta task: force=0 r_lgl=0 r_perm=0). Como este recipe nao
# usa PS3_RSX_BACKEND=d3d12, o motivo do revert (crash no backend D3D12) nao
# se aplica aqui.
export PS3_VDEC_ASYNC=1
# spu1 off no baseline Task 1 (mede WAD open only; Task 4 liga depois)
unset PS3_SPU1 PS3_SPU_ALL
# nunca setar PS3_NOMOVIES aqui — conflita com o movie path do recipe
unset PS3_NOMOVIES

timeout -k 5 120 "$EXE" ../EBOOT.ELF >"$LOG" 2>&1
EC=$?

echo "exit=$EC (124=timeout esperado)"
echo "force=$(grep -ac 'FORCE SEQDONE' "$LOG")"
echo "eos_gate=$(grep -ac 'state-3 gate' "$LOG")"
echo "r_lgl=$(grep -ac "open 'R_LglScA'" "$LOG")"
echo "r_perm=$(grep -ac "open 'R_PermA'" "$LOG")"
echo "invalid=$(grep -ac 'Invalid shader combination' "$LOG")"
echo "spu1_miss=$(grep -ac 'fp=0x2A5C4E67A14505B8' "$LOG")"
echo "flips=$(grep -ac SetFlipCommand "$LOG")"

# Aceite Task 1: WADs abrem; nao exige pixels
fail=0
[ "$(grep -ac 'FORCE SEQDONE' "$LOG")" -ge 1 ] || { echo "FAIL no FORCE"; fail=1; }
[ "$(grep -ac "open 'R_LglScA'" "$LOG")" -ge 1 ] || { echo "FAIL no R_LglScA"; fail=1; }
[ "$(grep -ac "open 'R_PermA'" "$LOG")" -ge 1 ] || { echo "FAIL no R_PermA"; fail=1; }
[ "$(grep -ac CRASH "$LOG")" -eq 0 ] || { echo "FAIL crash marker"; fail=1; }
exit $fail
