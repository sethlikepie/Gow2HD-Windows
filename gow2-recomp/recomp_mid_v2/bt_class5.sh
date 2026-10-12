#!/usr/bin/env bash
# bt_class5.sh — Task 5 classificacao: recipe intro+spu1 + TODOS os traces de shader.
set +e
cd "$(dirname "$0")"
EXE=./boot_v2_new.exe
LOG=class5.log
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1; sleep 1
rm -f "$LOG"

export PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted/USRDIR"
export PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64
export PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE="../movie_cache" PS3_MOVIE_EOS=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
export PS3_VDEC_ASYNC=1
export PS3_SPU1=1
unset PS3_SPU_ALL
unset PS3_NOMOVIES
# --- traces de shader (classificacao) ---
export PS3_TRACE_POPUL=1
export PS3_TRACE_SHADERMAP=1
export PS3_TRACE_CRC=1
export PS3_TRACE_SHADERSRC=1
export PS3_MAP_SHADER=1

DUR=${DUR:-90}
timeout -k 5 $DUR "$EXE" ../EBOOT.ELF >"$LOG" 2>&1
EC=$?

echo "exit=$EC (124=timeout esperado)"
echo "=== contagens ==="
echo "invalid=$(grep -ac 'Invalid shader combination' "$LOG")"
echo "r_lgl=$(grep -ac \"open 'R_LglScA'\" "$LOG")"
echo "r_perm=$(grep -ac \"open 'R_PermA'\" "$LOG")"
echo "spu1_hit=$(grep -ac 'dispatch HIT fp=0x2A5C4E67A14505B8' "$LOG")"
echo "flips=$(grep -ac SetFlipCommand "$LOG")"
echo "POPUL=$(grep -ac '\[POPUL\]' "$LOG")"
echo "POPUL_POP=$(grep -ac 'POPULADO' "$LOG")"
echo "SHADERMAP=$(grep -ac '\[SHADERMAP\]' "$LOG")"
echo "CRC=$(grep -ac '\[CRC\]' "$LOG")"
echo "SHADERSRC=$(grep -ac '\[SHADERSRC\]' "$LOG" 2>/dev/null)"
echo "MAPSH=$(grep -ac '\[MAPSH\]' "$LOG")"
echo "FIXzero=$(grep -ac 'blocked spurious zero' "$LOG")"
echo "d3d12_setshader=$(grep -ac 'set_shader' "$LOG")"
echo "=== primeiras 3 [POPUL] ==="; grep -a '\[POPUL\]' "$LOG" | head -3
echo "=== primeiras 6 [SHADERMAP] ==="; grep -a '\[SHADERMAP\]' "$LOG" | head -6
echo "=== primeiras 8 [CRC] ==="; grep -a '\[CRC\]' "$LOG" | head -8
echo "=== [MAPSH] ==="; grep -a '\[MAPSH\]' "$LOG" | head -8
