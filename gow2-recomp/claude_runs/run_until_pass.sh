#!/bin/bash
# run_until_pass.sh BINARY_NAME SHA256 LOG_NAME
# Runs one GoW2 boot (e435 env) with the PASS log and stops as soon as it prints.
# Hash is checked before the copy and again on the copy; the temp dir is removed on exit.
set -u
G="$HOME/Documents/PESSOAL/gow2-recomp"
BIN_NAME="$1"; WANT="$2"; LOG_NAME="$3"
case "$BIN_NAME" in boot_gow2_[a-z0-9]*) ;; *) echo "bad binary name"; exit 2 ;; esac
case "$LOG_NAME" in [a-z0-9_]*) ;; *) echo "bad log name"; exit 2 ;; esac
SRC="$G/$BIN_NAME"
L="$G/claude_runs/$LOG_NAME.log"
cd "$G" || exit 2
[ -f "$SRC" ] && [ ! -L "$SRC" ] || { echo "binary missing or symlink"; exit 2; }
[ "$(shasum -a 256 "$SRC" | cut -d' ' -f1)" = "$WANT" ] || { echo "source checksum mismatch"; exit 2; }
T=$(mktemp -d /tmp/gow2run.XXXXXX) || exit 2
trap 'rm -rf "$T"' EXIT
cp "$SRC" "$T/boot" || exit 2
chmod 500 "$T/boot"
[ "$(shasum -a 256 "$T/boot" | cut -d' ' -f1)" = "$WANT" ] || { echo "copy checksum mismatch"; exit 2; }
env -i HOME="$HOME" PATH=/usr/bin:/bin TMPDIR=/tmp \
  PS3_VFS_ROOT="$G/extracted/USRDIR" PS3_MOVIE_CACHE="$G/movie_cache" \
  PS3_MOVIE_HLE=1 PS3_NOMOVIES=0 PS3_MOVIE_IO=1 PS3_MOVIE_EOS=1 PS3_VDEC_ASYNC=1 \
  PS3_PAD_AUTOSTART=1 PS3_MUTE=1 PS3_SPU1=1 PS3_RSX_FIFO=1 PS3_RSX_BACKEND=metal \
  PS3_CELLSYS_REORDER=1 PS3_GCM_CB=1 PS3_LWMUTEX_REAL=1 PS3_FIOS_STICKY_OWNER=1 \
  PS3_MOVIE_DONE_MS=3000 PS3_METAL_PER_DRAW_RT=1 PS3_METAL_DEBUG_DEPTH=rsx \
  PS3_METAL_DEBUG_CLEAR_FRAME=1 PS3_METAL_SURF_DUMP=auto PS3_METAL_SURF_DUMP_MINREC=800 \
  PS3_TRACE_METAL_PASSES=1 \
  "$T/boot" EBOOT.ELF > "$L" 2>&1 &
PID=$!
echo "pid=$PID"
n=0
while [ "$n" -lt 300 ]; do
  sleep 2
  n=$((n + 1))
  if [ "$(grep -c '^\[PASS\] f=' "$L")" -ge 1 ]; then sleep 2; echo "pronto em $((n * 2))s"; break; fi
  if ! kill -0 "$PID" 2>/dev/null; then echo "processo saiu"; break; fi
done
kill -TERM "$PID" 2>/dev/null
sleep 2
kill -9 "$PID" 2>/dev/null
exit 0
