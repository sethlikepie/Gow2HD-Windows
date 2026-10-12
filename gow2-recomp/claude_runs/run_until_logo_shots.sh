#!/bin/bash
# run_until_logo_shots.sh BIN LOG BACKEND SHOTDIR [EXTRA_ENV=VAL]...
# One GoW2 boot in the legacy host-content env (PS3_MOVIE_HLE=1 +
# PS3_BOOT_LOGO_QUEUE=1, windowed, muted, no pad autostart) that dumps the
# host-content (legal / logo) holds to SHOTDIR via PS3_HOST_CONTENT_SHOT, and
# stops after 3 [HC-SHOT] lines, process exit or 150 s (+5 s for late
# validation messages). Kills only its own PID. Opens a game window.
#   BIN      binary in the gow2-recomp root, name ^g2[a-z0-9]+$
#   LOG      claude_runs/LOG.log, name ^[a-z0-9_]+$
#   BACKEND  metal | vulkan
#   SHOTDIR  directory for the hcN_*.bmp dumps (created)
#   extras   PS3_*=... or VK_*=... (set before env_gow2.sh, which keeps them)
set -u
G="$HOME/Documents/PESSOAL/gow2-recomp"
[ "$#" -ge 4 ] || { echo "usage: $0 BIN LOG BACKEND SHOTDIR [EXTRA_ENV=VAL]..."; exit 2; }
BIN="$1"; LOG="$2"; BACKEND="$3"; SHOTDIR="$4"; shift 4
[[ "$BIN" =~ ^g2[a-z0-9]+$ ]] || { echo "bad binary name"; exit 2; }
[[ "$LOG" =~ ^[a-z0-9_]+$ ]] || { echo "bad log name"; exit 2; }
case "$BACKEND" in metal|vulkan) ;; *) echo "bad backend"; exit 2 ;; esac
[[ "$SHOTDIR" =~ ^[A-Za-z0-9_./-]+$ ]] || { echo "bad shot dir"; exit 2; }
EXTRAS=()
for e in "$@"; do
  if [[ "$e" =~ ^PS3_[A-Z0-9_]+=[A-Za-z0-9_.,/-]*$ ]] || [[ "$e" =~ ^VK_[A-Z_]+=[A-Za-z0-9_./-]*$ ]]; then
    EXTRAS+=("$e")
  else
    echo "bad extra env: $e"; exit 2
  fi
done
cd "$G" || exit 2
[ -f "$BIN" ] && [ ! -L "$BIN" ] || { echo "binary missing or not a regular file"; exit 2; }
mkdir -p "$SHOTDIR" || exit 2
SHOTDIR="$(cd "$SHOTDIR" && pwd)" || exit 2
L="$G/claude_runs/$LOG.log"
env -i HOME="$HOME" PATH=/usr/bin:/bin TMPDIR=/tmp G="$G" BIN="$BIN" \
  PS3_RSX_BACKEND="$BACKEND" PS3_MOVIE_HLE=1 PS3_BOOT_LOGO_QUEUE=1 \
  PS3_FULLSCREEN=0 PS3_MUTE=1 \
  PS3_HOST_CONTENT_SHOT="$SHOTDIR" PS3_HOST_CONTENT_SHOT_AT=2 \
  ${EXTRAS[@]+"${EXTRAS[@]}"} \
  bash -c 'cd "$G" && . ./env_gow2.sh && unset PS3_PAD_AUTOSTART && exec ./"$BIN" EBOOT.ELF' \
  > "$L" 2>&1 &
PID=$!
echo "pid=$PID backend=$BACKEND shots=$SHOTDIR log=$L"
n=0
while [ "$n" -lt 75 ]; do
  sleep 2
  n=$((n + 1))
  if [ "$(grep -c '\[HC-SHOT\]' "$L")" -ge 3 ]; then echo "3 shots em $((n * 2))s"; break; fi
  if ! kill -0 "$PID" 2>/dev/null; then echo "processo saiu em $((n * 2))s"; break; fi
done
[ "$n" -ge 75 ] && echo "timeout 150s"
sleep 5
kill -TERM "$PID" 2>/dev/null
sleep 2
kill -9 "$PID" 2>/dev/null
grep -E '\[HC-SHOT\]|\[CONTENT\]|\[boot\]|\[rsx-bridge\]|\[RSX vulkan\]' "$L"
exit 0
