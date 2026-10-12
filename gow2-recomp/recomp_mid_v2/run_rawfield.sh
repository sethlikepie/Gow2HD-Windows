set +e
SD="/c/Users/softlive/Documents/self-projects/gow2_work"; RD="$SD/recomp_mid_v2"; LOG="$RD/rawfield_trace.log"
cd "$RD" || exit 1
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1; sleep 1; rm -f "$LOG"
export PS3_VFS_ROOT="$SD/extracted/USRDIR" PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64
export PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE="../movie_cache" PS3_MOVIE_EOS=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000 PS3_VDEC_ASYNC=1 PS3_SPU1=1
export PS3_RSX_BACKEND=d3d12 PS3_RSX_FIFO=1 PS3_PAD_AUTOSTART=1
export PS3_TRACE_RAWFIELD=1 PS3_TRACE_ARRSEQ=1
unset PS3_SPU_ALL PS3_NOMOVIES PS3_TRACE_ASSET
timeout -k 5 "${1:-120}" ./boot_v2_new.exe ../EBOOT.ELF > "$LOG" 2>&1
echo "exit=$?"; taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1
echo "=== RAWFIELD ==="; grep -a '\[RAWFIELD\]' "$LOG" | head -60
