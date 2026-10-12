#!/usr/bin/env bash
# bt_visual_combo.sh — Task 2 item 6: combo VISUAL (D3D12 overlay) SEM trace.
# Recipe FORCE/EOS/ASYNC + camada visual (HLE overlay + d3d12 + fifo + pad).
# Objetivo: com overlay visual ativo, overlay_done vira true e o arm de EOS
# (agora desacoplado do trace) deve DISPARAR. Verifica se WADs abrem.
set +e
cd "$(dirname "$0")"
EXE=./boot_v2_new.exe
LOG=visual_combo.log
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1; sleep 1
rm -f "$LOG"

export PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted/USRDIR"
export PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64
export PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE="../movie_cache" PS3_MOVIE_EOS=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
export PS3_VDEC_ASYNC=1
# camada visual
export PS3_MOVIE_HLE=1 PS3_RSX_BACKEND=d3d12 PS3_RSX_FIFO=1 PS3_PAD_AUTOSTART=1
# SEM PS3_TRACE_MOVIEOBJ (prova que MOVIEFSM/arm rodam sem trace)
unset PS3_TRACE_MOVIEOBJ
unset PS3_SPU1 PS3_SPU_ALL PS3_NOMOVIES

timeout -k 5 120 "$EXE" ../EBOOT.ELF >"$LOG" 2>&1
EC=$?

echo "exit=$EC (124=timeout esperado)"
echo "MOVIEFSM_transitions=$(grep -ac MOVIEFSM "$LOG")"
echo "arm_fired=$(grep -ac 'arming EOS read-hook' "$LOG")"
echo "overlay_done_true=$(grep -ac 'overlay_done=1' "$LOG")"
echo "force=$(grep -ac 'FORCE SEQDONE' "$LOG")"
echo "r_lgl=$(grep -ac "open 'R_LglScA'" "$LOG")"
echo "r_perm=$(grep -ac "open 'R_PermA'" "$LOG")"
echo "flips=$(grep -ac SetFlipCommand "$LOG")"
echo "=== MOVIEFSM seq ==="; grep -a MOVIEFSM "$LOG"
echo "=== arm line ==="; grep -a 'arming EOS read-hook' "$LOG"
