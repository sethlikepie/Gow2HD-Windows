#!/usr/bin/env bash
# bt_intro_spu1.sh — Task 4: mesmo recipe FORCE do bt_intro_wads.sh, porem com
# PS3_SPU1=1 para LIGAR o spu1 (dearch/EDGE-zlib) no caminho intro->WAD.
# Objetivo: verificar dispatch HIT do fp 0x2A5C4E67A14505B8 e se o inflate
# dos WADs acontece, com o host sobrevivendo (SEH isola crash de job).
# NAO altera o baseline bt_intro_wads.sh (mede WAD-open sem spu1).
set +e
cd "$(dirname "$0")"
EXE=./boot_v2_new.exe
LOG=intro_spu1.log
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1; sleep 1
rm -f "$LOG"

export PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted/USRDIR"
export PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64
export PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE="../movie_cache" PS3_MOVIE_EOS=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
export PS3_VDEC_ASYNC=1
# --- LIGA o spu1 (dearch) ---
export PS3_SPU1=1
unset PS3_SPU_ALL
unset PS3_NOMOVIES
# trace opcional de DMA se TRACE_DMA=1 no ambiente
[ "${TRACE_DMA:-0}" = 1 ] && export PS3_TRACE_SPUDMA=1

timeout -k 5 120 "$EXE" ../EBOOT.ELF >"$LOG" 2>&1
EC=$?

echo "exit=$EC (124=timeout esperado)"
echo "force=$(grep -ac 'FORCE SEQDONE' "$LOG")"
echo "r_lgl=$(grep -ac "open 'R_LglScA'" "$LOG")"
echo "r_perm=$(grep -ac "open 'R_PermA'" "$LOG")"
echo "invalid=$(grep -ac 'Invalid shader combination' "$LOG")"
# spu1_miss/hit contados por linha PRECISA (a linha HIT tambem contem
# 'fp=0x2A5C...', entao NAO use grep so pelo fp — daria falso-positivo).
echo "spu1_miss=$(grep -ac 'dispatch MISS fp=0x2A5C4E67A14505B8' "$LOG")"
echo "spu1_hit=$(grep -ac 'dispatch HIT fp=0x2A5C4E67A14505B8' "$LOG")"
echo "spujob_clean=$(grep -ac 'spu job returned cleanly' "$LOG")"
echo "spucrash=$(grep -ac 'SPUCRASH' "$LOG")"
echo "spujob_aborted=$(grep -ac 'aborted by SEH' "$LOG")"
echo "flips=$(grep -ac SetFlipCommand "$LOG")"
