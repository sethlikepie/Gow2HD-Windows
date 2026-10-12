set +e
SCRIPT_DIR="/c/Users/softlive/Documents/self-projects/gow2_work"
RUN_DIR="$SCRIPT_DIR/recomp_mid_v2"
LOG="$RUN_DIR/scope_trace.log"
TIMEOUT_S="${1:-150}"
cd "$RUN_DIR" || exit 1
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1; sleep 1; rm -f "$LOG"
export PS3_VFS_ROOT="$SCRIPT_DIR/extracted/USRDIR"
export PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64
export PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE="../movie_cache" PS3_MOVIE_EOS=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
export PS3_VDEC_ASYNC=1
export PS3_SPU1=1
export PS3_RSX_BACKEND=d3d12 PS3_RSX_FIFO=1
export PS3_TRACE_RSX_SHADERS=1
export PS3_PAD_AUTOSTART=1
export PS3_TRACE_ASSET=1
export PS3_TRACE_SCOPE=1
export PS3_TRACE_BADPUSH=1
unset PS3_SPU_ALL PS3_NOMOVIES
timeout -k 5 "$TIMEOUT_S" ./boot_v2_new.exe ../EBOOT.ELF > "$LOG" 2>&1
echo "exit=$?"
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1
echo "=== log lines: $(wc -l < "$LOG") ==="
echo "=== bytes_read R_PermA (last) ==="; grep -a 'stats.*R_PermA' "$LOG" | tail -2
echo "=== SCOPE writes (first 40) ==="; grep -a '\[SCOPE\]' "$LOG" | head -40
echo "=== SCOPE writes (last 10) ==="; grep -a '\[SCOPE\]' "$LOG" | tail -10
echo "=== MAGIC stamps (all, capped) ==="; grep -a '\[MAGIC\]' "$LOG" | head -80
echo "=== BADPUSH ==="; grep -a '\[BADPUSH\]' "$LOG" | head -10
