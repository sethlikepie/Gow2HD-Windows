#!/bin/bash
# run_until_dump.sh BINARY_NAME SHA256 LOG_NAME FILL_MODE MINREC
# One GoW2 boot (e435 env), stops as soon as the automatic surface dump is written.
# FILL_MODE: none | tex | uv | tc1 | tc1a.  MINREC: record threshold for the dump.
set -u
G="$HOME/Documents/PESSOAL/gow2-recomp"
BIN_NAME="$1"; WANT="$2"; LOG_NAME="$3"; FILL="$4"; MINREC="$5"; DELAY="${6:-0}"; FILL_FP="${7:-}"; PREC="${8:-0}"; NOPFX="${9:-0}"; FILL_VP="${10:-}"
case "$PREC" in 0|1) ;; *) echo "bad prec"; exit 2 ;; esac
case "$NOPFX" in 0|1) ;; *) echo "bad nopfx"; exit 2 ;; esac
[[ "$DELAY" =~ ^[0-9]{1,5}$ ]] || { echo "bad delay"; exit 2; }
[[ -z "$FILL_FP" || "$FILL_FP" =~ ^[0-9A-F]{8}(,[0-9A-F]{8}){0,7}$ ]] || { echo "bad fp list"; exit 2; }
[[ -z "$FILL_VP" || "$FILL_VP" =~ ^[0-9A-F]{8}(,[0-9A-F]{8}){0,7}$ ]] || { echo "bad vp list"; exit 2; }
case "$BIN_NAME" in boot_gow2_[a-z0-9]*) ;; *) echo "bad binary name"; exit 2 ;; esac
case "$LOG_NAME" in [a-z0-9_]*) ;; *) echo "bad log name"; exit 2 ;; esac
case "$FILL" in none|tex|uv|tc1|tc1a|fpid|vpid|blend|magenta) ;; *) echo "bad fill mode"; exit 2 ;; esac
case "$MINREC" in [0-9]|[0-9][0-9]|[0-9][0-9][0-9]|[0-9][0-9][0-9][0-9]) ;; *) echo "bad minrec"; exit 2 ;; esac
[[ "$WANT" =~ ^[a-f0-9]{64}$ ]] || { echo "bad sha256"; exit 2; }
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
env -i HOME="$HOME" PATH=/usr/bin:/bin TMPDIR=/tmp \
  PS3_VFS_ROOT="$G/extracted/USRDIR" PS3_MOVIE_CACHE="$G/movie_cache" \
  PS3_MOVIE_HLE=1 PS3_NOMOVIES=0 PS3_MOVIE_IO=1 PS3_MOVIE_EOS=1 PS3_VDEC_ASYNC=1 \
  PS3_PAD_AUTOSTART=1 PS3_MUTE=1 PS3_SPU1=1 PS3_RSX_FIFO=1 PS3_RSX_BACKEND=metal \
  PS3_CELLSYS_REORDER=1 PS3_GCM_CB=1 PS3_LWMUTEX_REAL=1 PS3_FIOS_STICKY_OWNER=1 \
  PS3_MOVIE_DONE_MS=3000 PS3_METAL_PER_DRAW_RT=1 PS3_METAL_DEBUG_DEPTH=rsx \
  PS3_METAL_SURF_DUMP=auto PS3_METAL_SURF_DUMP_MINREC="$MINREC" \
  PS3_METAL_SURF_DUMP_DELAY="$DELAY" PS3_METAL_DEBUG_FILL_FP="$FILL_FP" PS3_METAL_DEBUG_FILL_VP="$FILL_VP" \
  PS3_FP_PRECISION_CLAMP="$PREC" PS3_METAL_DUMP_MSL=1 PS3_METAL_DEBUG_NO_POSTFX="$NOPFX" \
  PS3_FRAME_DUMP=1 PS3_FRAME_DUMP_STRIDE=1000 \
  PS3_TRACE_METAL_PERDRAW=1 PS3_TRACE_METAL_PASSES=1 PS3_METAL_DEBUG_CLEAR_FRAME=0 "$FILL_ENV" \
  "$T/boot" EBOOT.ELF > "$L" 2>&1 &
PID=$!
echo "pid=$PID fill=$FILL minrec=$MINREC"
n=0
while [ "$n" -lt 300 ]; do
  sleep 2
  n=$((n + 1))
  if [ "$(grep -c 'dumped surf_f' "$L")" -ge 26 ]; then echo "dump pronto em $((n * 2))s"; break; fi
  if ! kill -0 "$PID" 2>/dev/null; then echo "processo saiu"; break; fi
done
kill -TERM "$PID" 2>/dev/null
sleep 2
kill -9 "$PID" 2>/dev/null
[ -f "$L" ] && [ ! -L "$L" ] || { echo "log missing or symlink"; exit 2; }
echo "--- distinct vertex layouts"
grep 'metal\] va n=' "$L" | sed -E 's/.*va n=//' | head -32
F=$(grep -oE 'dumped surf_f[0-9]+' "$L" | head -1 | grep -oE '[0-9]+$')
[[ "$F" =~ ^[0-9]+$ ]] || { echo "no dump frame"; exit 0; }
echo "frame=$F"
mkdir -p "$G/claude_runs/frames"
for s in s4_010A0000 s8_01E60000 s9_02720000; do
  [ -f "$G/surf_f${F}_$s.bmp" ] && sips -s format png -Z 800 "$G/surf_f${F}_$s.bmp" \
      --out "$G/claude_runs/frames/${LOG_NAME}_$s.png" > /dev/null && echo "converted $s"
done
exit 0
