#!/bin/bash
# repeat_runs.sh BINARY_NAME PREFIX N SECS [EXTRA_ENV_NAME=VALUE ...]
# N sequential run_for.sh boots of one binary; per run prints the post-movie FPS window,
# rwlock EDEADLK count, stalled seconds (<= 2 draws in [FPS] lines 58-100) and whether the
# [FPS] lines reach the end of the log (a present freeze stops them early).
set -u
G="$HOME/Documents/PESSOAL/gow2-recomp"
R="$G/claude_runs"
BIN_NAME="$1"; PREFIX="$2"; N="$3"; SECS="$4"
shift 4 2>/dev/null || shift $#
case "$BIN_NAME" in boot_gow2_[a-z0-9]*) ;; *) echo "bad binary name"; exit 2 ;; esac
case "$PREFIX" in [a-z][a-z0-9_]*) ;; *) echo "bad prefix"; exit 2 ;; esac
[[ "$N" =~ ^[1-9]$ ]] || { echo "bad n"; exit 2; }
[[ "$SECS" =~ ^[0-9]{1,3}$ ]] || { echo "bad secs"; exit 2; }
for kv in "$@"; do
  [[ "$kv" =~ ^PS3_[A-Z0-9_]+=[A-Za-z0-9_.,-]*$ ]] || { echo "bad extra env: $kv"; exit 2; }
done
SRC="$G/$BIN_NAME"
[ -f "$SRC" ] && [ ! -L "$SRC" ] || { echo "binary missing or symlink"; exit 2; }
SHA="$(shasum -a 256 "$SRC" | cut -d' ' -f1)"
[[ "$SHA" =~ ^[a-f0-9]{64}$ ]] || { echo "bad sha"; exit 2; }
for i in $(seq 1 "$N"); do
  name="${PREFIX}_${i}"
  bash "$R/run_for.sh" "$BIN_NAME" "$SHA" "$name" "$SECS" "$@" > /dev/null 2>&1
  L="$R/$name.log"
  win="$(python3 "$R/fps_window.py" "$L" 58 100 300)"
  rw="$(grep -c 'sys_rwlock_wlock failed' "$L")"
  st="$(grep '^\[FPS\]' "$L" | awk 'NR>=58 && NR<=100 {split($3,d,"="); if (d[2]+0<=2) z++} END {print z+0}')"
  last="$(awk '/^\[FPS\]/{l=NR} END {print l+0}' "$L")"
  total="$(wc -l < "$L" | tr -d ' ')"
  echo "$name: $win rwlock=$rw stalled_s=$st last_fps_line=$last/$total"
done
exit 0
