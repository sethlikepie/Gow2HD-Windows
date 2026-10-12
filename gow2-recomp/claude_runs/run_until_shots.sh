#!/bin/bash
# run_until_shots.sh BINARY_NAME SHA256 LOG_NAME EVERY MAX
# One GoW2 boot with the user's launcher env (env_gow2.sh values, windowed),
# writes MAX gameplay screenshots every EVERY frames and stops after the last one.
set -u
G="$HOME/Documents/PESSOAL/gow2-recomp"
BIN_NAME="$1"; WANT="$2"; LOG_NAME="$3"; EVERY="$4"; MAX="$5"; FILL="${6:-none}"; FILL_FP="${7:-}"; SKIP_FP="${8:-}"; SKIP_VP="${9:-}"; MINREC="${10:-800}"
[[ "$MINREC" =~ ^[0-9]{1,4}$ ]] || { echo "bad minrec"; exit 2; }
EXTRA_ENV="${11:-PS3_SHOTS_EXTRA=0}"
[[ "$EXTRA_ENV" =~ ^PS3_[A-Z0-9_]+=[A-Za-z0-9_.,-]*$ ]] || { echo "bad extra env"; exit 2; }
case "$BIN_NAME" in boot_gow2_[a-z0-9]*) ;; *) echo "bad binary name"; exit 2 ;; esac
case "$LOG_NAME" in [a-z0-9_]*) ;; *) echo "bad log name"; exit 2 ;; esac
[[ "$EVERY" =~ ^[0-9]{1,5}$ ]] || { echo "bad every"; exit 2; }
[[ "$MAX" =~ ^[0-9]{1,2}$ ]] || { echo "bad max"; exit 2; }
[[ "$WANT" =~ ^[a-f0-9]{64}$ ]] || { echo "bad sha256"; exit 2; }
case "$FILL" in none|magenta) ;; *) echo "bad fill"; exit 2 ;; esac
[[ -z "$FILL_FP" || "$FILL_FP" =~ ^[0-9A-F]{8}(,[0-9A-F]{8}){0,7}$ ]] || { echo "bad fp list"; exit 2; }
[[ -z "$SKIP_FP" || "$SKIP_FP" =~ ^[0-9A-F]{8}(,[0-9A-F]{8}){0,7}$ ]] || { echo "bad skip fp"; exit 2; }
[[ -z "$SKIP_VP" || "$SKIP_VP" =~ ^[0-9A-F]{8}(,[0-9A-F]{8}){0,7}$ ]] || { echo "bad skip vp"; exit 2; }
FILL_ENV="PS3_METAL_DEBUG_FILL=0"
[ "$FILL" != "none" ] && FILL_ENV="PS3_METAL_DEBUG_FILL=$FILL"
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
rm -f "$G"/frame_gp[0-9]*_f*.bmp "$G"/frame_gp[0-9]*_s*.bmp "$G"/fxtex_*.bmp
env -i HOME="$HOME" PATH=/usr/bin:/bin TMPDIR=/tmp \
  PS3_VFS_ROOT="$G/extracted/USRDIR" PS3_MOVIE_CACHE="$G/movie_cache" \
  PS3_MOVIE_HLE=1 PS3_NOMOVIES=0 PS3_MOVIE_IO=1 PS3_MOVIE_EOS=1 PS3_VDEC_ASYNC=1 \
  PS3_PAD_AUTOSTART=1 PS3_MUTE=1 PS3_SPU1=1 PS3_RSX_FIFO=1 PS3_RSX_BACKEND=metal \
  PS3_CELLSYS_REORDER=1 PS3_GCM_CB=1 PS3_LWMUTEX_REAL=1 PS3_FIOS_STICKY_OWNER=1 \
  PS3_MOVIE_DONE_MS=3000 PS3_METAL_PER_DRAW_RT=1 PS3_METAL_DEBUG_NODEPTH=0 \
  PS3_METAL_DEBUG_DEPTH=rsx PS3_METAL_DEBUG_CLEAR_FRAME=0 PS3_FULLSCREEN=0 \
  PS3_FRAME_DUMP_GAMEPLAY="$EVERY" PS3_FRAME_DUMP_GAMEPLAY_MAX="$MAX" PS3_METAL_SURF_DUMP_MINREC="$MINREC" PS3_TRACE_METAL_PASSES=1 "$FILL_ENV" PS3_METAL_DEBUG_FILL_FP="$FILL_FP" PS3_METAL_SURF_DUMP_ALPHA=1 PS3_METAL_SKIP_FP="$SKIP_FP" PS3_METAL_SKIP_VP="$SKIP_VP" "$EXTRA_ENV" \
  "$T/boot" EBOOT.ELF > "$L" 2>&1 &
PID=$!
echo "pid=$PID every=$EVERY max=$MAX"
n=0
while [ "$n" -lt 450 ]; do
  sleep 2
  n=$((n + 1))
  if [ "$(grep -c 'gameplay shot frame_gp' "$L")" -ge "$MAX" ]; then echo "shots prontos em $((n * 2))s"; break; fi
  if ! kill -0 "$PID" 2>/dev/null; then echo "processo saiu"; break; fi
done
kill -TERM "$PID" 2>/dev/null
sleep 2
kill -9 "$PID" 2>/dev/null
grep 'gameplay shot' "$L"
mkdir -p "$G/claude_runs/frames"
for f in "$G"/frame_gp[0-9]*_f*.bmp "$G"/frame_gp[0-9]*_s*.bmp; do
  [ -f "$f" ] || continue
  b=$(basename "$f" .bmp)
  sips -s format png -Z 1000 "$f" --out "$G/claude_runs/frames/${LOG_NAME}_$b.png" > /dev/null && echo "converted $b"
done
exit 0
