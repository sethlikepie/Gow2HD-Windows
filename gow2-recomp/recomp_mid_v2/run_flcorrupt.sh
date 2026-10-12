set +e
SCRIPT_DIR="/c/Users/softlive/Documents/self-projects/gow2_work"
RUN_DIR="$SCRIPT_DIR/recomp_mid_v2"
LOG="$RUN_DIR/flcorrupt_trace.log"
TIMEOUT_S="${1:-160}"
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
export PS3_TRACE_FLCORRUPT=1
# optional arena bounds override: PS3_FL_ALO / PS3_FL_AHI
[ -n "$2" ] && export PS3_FL_ALO="$2"
[ -n "$3" ] && export PS3_FL_AHI="$3"
unset PS3_SPU_ALL PS3_NOMOVIES
timeout -k 5 "$TIMEOUT_S" ./boot_v2_new.exe ../EBOOT.ELF > "$LOG" 2>&1
echo "exit=$?"
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1
echo "=== log lines: $(wc -l < "$LOG") ==="
echo "=== bytes_read R_PermA (last) ==="; grep -a 'stats.*R_PermA' "$LOG" | tail -2
echo "=== FLCORRUPT (first transitions) ==="; grep -a -A12 'FLCORRUPT #1 ' "$LOG" | head -60
echo "=== all FLCORRUPT headers ==="; grep -a 'FLCORRUPT #' "$LOG" | head -40
