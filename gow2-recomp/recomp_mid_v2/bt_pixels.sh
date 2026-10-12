#!/usr/bin/env bash
# bt_pixels.sh — recipe full path to pixels: intro+spu1+d3d12+movie HLE + shader traces
set +e
cd "$(dirname "$0")"
EXE=./boot_v2_new.exe
LOG=pixels.log
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1; sleep 1
rm -f "$LOG"

export PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted/USRDIR"
export PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64
export PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE="../movie_cache" PS3_MOVIE_EOS=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
export PS3_VDEC_ASYNC=1
export PS3_SPU1=1
export PS3_RSX_BACKEND=d3d12 PS3_RSX_FIFO=1
export PS3_TRACE_RSX_SHADERS=1
export PS3_PAD_AUTOSTART=1
export PS3_MOVIE_HLE=1
# shader traces
export PS3_TRACE_SHADERSRC=1
export PS3_TRACE_SHADERMAP=1
export PS3_MAP_SHADER=1
export PS3_TRACE_CRC=1
# WAD type-loader SM (R_PermA is raw — not HOSTINFL)
export PS3_TRACE_TYMAP=1
# GATE_FORCE synthetic ICGLdr spins CCPLdr without real EFCT — keep OFF for
# content-pixel runs (packages TXR/GFX/SBI already load naturally from R_PermA).
unset PS3_GATE_FORCE
# optional diagnostics:
#   PS3_GATE_FORCE=1     synthetic ICGLdr kick after full R_PermA
#   PS3_FIX_ARENA_PEEK=1  if peek returns non-magic scope, walk to parent arena
#   PS3_TRACE_BASECASE=1  freelist grow/peek probes
unset PS3_SPU_ALL PS3_NOMOVIES

DUR=${DUR:-75}
echo "=== bt_pixels DUR=$DUR $(date +%H:%M:%S) ==="
# Prefer GNU timeout (Git/MSYS); Windows timeout.exe is incompatible.
if command -v gtimeout >/dev/null 2>&1; then
  gtimeout -k 5 "$DUR" "$EXE" ../EBOOT.ELF >"$LOG" 2>&1
  EC=$?
elif command -v timeout >/dev/null 2>&1 && timeout --version >/dev/null 2>&1; then
  timeout -k 5 "$DUR" "$EXE" ../EBOOT.ELF >"$LOG" 2>&1
  EC=$?
else
  "$EXE" ../EBOOT.ELF >"$LOG" 2>&1 &
  PID=$!
  ( sleep "$DUR"; kill "$PID" 2>/dev/null ) &
  wait "$PID" 2>/dev/null
  EC=$?
fi

echo "exit=$EC (124=timeout esperado)"
echo "=== contagens ==="
echo "invalid=$(grep -ac 'Invalid shader combination' "$LOG")"
echo "r_lgl=$(grep -ac "open 'R_LglScA'" "$LOG")"
echo "r_perm=$(grep -ac "open 'R_PermA'" "$LOG")"
echo "spu1_hit=$(grep -ac 'dispatch HIT fp=0x2A5C4E67A14505B8' "$LOG")"
echo "hostinfl=$(grep -ac '\[HOSTINFL\]' "$LOG")"
echo "hostres=$(grep -ac '\[HOSTRES\]' "$LOG")"
echo "hostres_ctxr=$(grep -ac '\.ctxr' "$LOG")"
echo "wadld_call=$(grep -ac '\[WADLD-CALL\]' "$LOG")"
echo "wadld_body=$(grep -ac '\[WADLD-BODY\]' "$LOG")"
echo "wadld_sm=$(grep -ac '\[WADLD-SM\]' "$LOG")"
echo "txr=$(grep -ac "name='TXR_" "$LOG")"
echo "sbi=$(grep -ac "name='SBI_" "$LOG")"
echo "flips=$(grep -ac SetFlipCommand "$LOG")"
echo "MAPSH=$(grep -ac '\[MAPSH\]' "$LOG")"
echo "SHADERSRC=$(grep -ac '\[SHADERSRC\]' "$LOG")"
echo "set_shader=$(grep -ac 'set_shader' "$LOG")"
echo "RSX_SH=$(grep -ac 'RSX-SH\|\[RSX-SH\]' "$LOG")"
echo "draw=$(grep -ac -iE 'draw_arrays|draw_indexed|DrawArrays|DRAW' "$LOG")"
echo "bind_tex=$(grep -ac 'bind_texture' "$LOG")"
echo "bind_content=$(grep -ac 'CONTENT' "$LOG")"
echo "content_hold=$(grep -ac '\[CONTENT\]' "$LOG")"
echo "frame_content=$(ls frame_content_*.ppm 2>/dev/null | wc -l)"
echo "d3d12=$(grep -ac '\[D3D12\]' "$LOG")"
echo "overlay=$(grep -ac -iE 'overlay|movie_present|live_draw|present_rgba' "$LOG")"
echo "force=$(grep -ac 'FORCE SEQDONE' "$LOG")"
echo "gate_force=$(grep -ac 'GATE-FORCE' "$LOG")"
echo "unknown_prop=$(grep -ac 'Unknown property' "$LOG")"
echo "ITEMBODY=$(grep -ac 'ITEMBODY' "$LOG")"
echo "crash=$(grep -aciE 'access violation|0xC0000005' "$LOG")"
echo "=== HOSTRES / bind / TXR sample ==="
grep -aE 'HOSTRES|bind_texture|CONTENT|name=.TXR_|name=.SBI_|name=.GFX_|MAPSH|open .R_|FORCE SEQDONE|GATE-FORCE base' "$LOG" | head -50
echo "=== tail ==="
tail -20 "$LOG"
echo FIM $(date +%H:%M:%S)
