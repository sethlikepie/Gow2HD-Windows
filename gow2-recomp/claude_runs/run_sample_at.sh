#!/bin/bash
# run_sample_at.sh BINARY_NAME SHA256 LOG_NAME AT_SECS SAMPLE_SECS [EXTRA_ENV_NAME=VALUE ...]
# One GoW2 boot with the user's launcher env (windowed) and PS3_TRACE_FPS=1; after AT_SECS
# runs `sample` for SAMPLE_SECS into <LOG>.sample.txt, then stops the process by PID.
set -u
G="$HOME/Documents/PESSOAL/gow2-recomp"
BIN_NAME="$1"; WANT="$2"; LOG_NAME="$3"; AT="$4"; SECS="$5"
shift 5 2>/dev/null || shift $#
case "$BIN_NAME" in boot_gow2_[a-z0-9]*) ;; *) echo "bad binary name"; exit 2 ;; esac
case "$LOG_NAME" in [a-z0-9_]*) ;; *) echo "bad log name"; exit 2 ;; esac
[[ "$AT" =~ ^[0-9]{1,3}$ ]] || { echo "bad at"; exit 2; }
[[ "$SECS" =~ ^[0-9]{1,2}$ ]] || { echo "bad secs"; exit 2; }
[[ "$WANT" =~ ^[a-f0-9]{64}$ ]] || { echo "bad sha256"; exit 2; }
EXTRA=()
for kv in "$@"; do
  [[ "$kv" =~ ^PS3_[A-Z0-9_]+=[A-Za-z0-9_.,-]*$ ]] || { echo "bad extra env: $kv"; exit 2; }
  EXTRA+=("$kv")
done
SRC="$G/$BIN_NAME"
L="$G/claude_runs/$LOG_NAME.log"
S="$G/claude_runs/$LOG_NAME.sample.txt"
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
  PS3_MOVIE_DONE_MS=3000 PS3_METAL_PER_DRAW_RT=1 PS3_METAL_DEBUG_NODEPTH=0 \
  PS3_METAL_DEBUG_DEPTH=rsx PS3_METAL_DEBUG_CLEAR_FRAME=0 PS3_FULLSCREEN=0 \
  PS3_TRACE_FPS=1 ${EXTRA[@]+"${EXTRA[@]}"} \
  "$T/boot" EBOOT.ELF > "$L" 2>&1 &
PID=$!
echo "pid=$PID at=$AT sample=$SECS extra=${EXTRA[*]+${EXTRA[*]}}"
n=0
while [ "$n" -lt "$AT" ]; do
  sleep 1
  n=$((n + 1))
  if ! kill -0 "$PID" 2>/dev/null; then echo "processo saiu em ${n}s"; break; fi
done
kill -0 "$PID" 2>/dev/null && /usr/bin/sample "$PID" "$SECS" -file "$S" > /dev/null 2>&1
kill -TERM "$PID" 2>/dev/null
sleep 2
kill -9 "$PID" 2>/dev/null
echo "log: $L ($(wc -l < "$L") linhas)"
[ -f "$S" ] && echo "sample: $S ($(wc -l < "$S") linhas)"
exit 0
