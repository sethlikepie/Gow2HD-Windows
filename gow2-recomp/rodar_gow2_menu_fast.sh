#!/usr/bin/env bash
# Skip intro fast → AUTO_LOAD → try to reach main menu (bring-up recipe).
# Mutated path: short FORCE SEQDONE, short boot logos, TYPE15-UNSTICK default,
# SPU1+4+5, PAD autostart, Metal window (not fullscreen so agent can run).
#
# Uso:
#   ./rodar_gow2_menu_fast.sh
#   TIMEOUT=90 ./rodar_gow2_menu_fast.sh
#   PS3_NO_RSX=1 ./rodar_gow2_menu_fast.sh   # headless metrics only
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

[ -x ./boot_gow2 ] || { echo "boot_gow2 missing — ./build_macos.sh" >&2; exit 1; }
[ -f EBOOT.ELF ] || { echo "EBOOT.ELF missing" >&2; exit 1; }

. "$HERE/env_gow2.sh"

# --- skip intro / logos ---
export PS3_VDEC_ASYNC=1
export PS3_VDEC_FORCE_SEQDONE_MS="${PS3_VDEC_FORCE_SEQDONE_MS:-1500}"
export PS3_MOVIE_EOS=1
export PS3_MOVIE_HLE=1
export PS3_MOVIE_IO=1
export PS3_BOOT_LOGO_MS="${PS3_BOOT_LOGO_MS:-300}"
# Keep movie path for WAD/R_Perm (do NOT set PS3_NOMOVIES=1)

# --- post-intro / menu ---
export PS3_AUTO_LOAD_RUN=1
export PS3_PAD_AUTOSTART=1
export PS3_TYPE15_UNSTICK="${PS3_TYPE15_UNSTICK:-1}"
export PS3_SPU1=1
# Frontend schedul workloads (optional; can fault — isolated)
export PS3_SPU4="${PS3_SPU4:-1}"
export PS3_SPU5="${PS3_SPU5:-1}"

# Visual: windowed Metal so we can see menu if it appears
export PS3_FULLSCREEN="${PS3_FULLSCREEN:-0}"
if [ -z "${PS3_NO_RSX:-}" ]; then
  export PS3_RSX_BACKEND="${PS3_RSX_BACKEND:-metal}"
  export PS3_RSX_FIFO=1
fi

# Observability
export PS3_PERF_FSM=1
export PS3_TRACE_POSTINTRO="${PS3_TRACE_POSTINTRO:-0}"
export PS3_TRACE_CC9D0="${PS3_TRACE_CC9D0:-0}"

TIMEOUT="${TIMEOUT:-75}"
LOG="${LOG:-/tmp/gow2_menu_fast.log}"

echo "[menu-fast] FORCE_SEQDONE=${PS3_VDEC_FORCE_SEQDONE_MS}ms logos=${PS3_BOOT_LOGO_MS}ms"
echo "[menu-fast] UNSTICK=$PS3_TYPE15_UNSTICK SPU1/4/5 PAD AUTO_LOAD backend=${PS3_RSX_BACKEND:-none}"
echo "[menu-fast] log=$LOG timeout=${TIMEOUT}s"

./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 &
BPID=$!
# Do not match early "FlashUI.ps3fx" shader compile noise as menu.
SAW_THR=0
for i in $(seq 1 "$TIMEOUT"); do
  kill -0 "$BPID" 2>/dev/null || break
  if [ "$SAW_THR" = 0 ] && grep -q 'thr_auto_load() end' "$LOG" 2>/dev/null; then
    SAW_THR=1
    echo "[menu-fast] thr_auto_load end at ${i}s — +25s for menu/pad"
    # keep running; do not break
  fi
  sleep 1
done

if kill -0 "$BPID" 2>/dev/null; then
  echo "[menu-fast] timeout ${TIMEOUT}s — kill"
  kill -TERM "$BPID" 2>/dev/null; sleep 1; kill -9 "$BPID" 2>/dev/null
fi
wait "$BPID" 2>/dev/null || true

echo "=== MENU-FAST COUNTS ==="
python3 - <<'PY'
import re, os
log=os.environ.get("LOG","/tmp/gow2_menu_fast.log")
s=open(log,errors="replace").read()
def c(p): return len(re.findall(p,s,re.I))
print("StartSeq", c(r"StartSeq\(handle"))
print("REPLAY_NOPIC", c(r"REPLAY-NOPIC"))
print("UNSTICK", c(r"TYPE15\] UNSTICK"))
print("thr_end", c(r"thr_auto_load\(\) end"))
print("AutoLoad", c(r"AutoLoad2|thr_auto_load"))
print("R_Perm", 1 if "20169344" in s else 0)
print("CC9D0", c(r"\[CC9D0\]"))
print("cellPadGetData", c(r"cellPadGetData|PadGetData"))
print("pad_init", c(r"\[cellPad\] Init"))
print("FORCE", c(r"FORCE SEQDONE"))
print("logo_DONE", c(r"boot logo queue DONE"))
print("B71", c(r"exit func_000B71B8|\+0x64=1"))
print("SPU_HIT", c(r"dispatch HIT"))
print("SPU_MISS", c(r"dispatch MISS"))
print("SPUJOB", c(r"SPUJOB.*cleanly"))
print("FATAL", c(r"FATAL"))
print("menuish", c(r"menu|FlashUI|NewGame|Press Start|TITLE|main.?menu"))
print("st620", re.findall(r"\[MOVIEFSM\] st620 (\S+) -> (\S+)", s)[:20])
# Gate A: flips/pad strictly after last R_Perm full line
lines = s.splitlines()
rperm_idxs = [i for i,l in enumerate(lines) if "R_Perm" in l and "20169344" in l]
if rperm_idxs:
    post = "\n".join(lines[max(rperm_idxs):])
    print("SetFlip_after_R_Perm", len(re.findall(r"SetFlip|cellGcmSetFlip", post, re.I)))
    print("Pad_after_R_Perm", len(re.findall(r"cellPadGetData", post, re.I)))
    print("ICALL_BAD_after_R_Perm", len(re.findall(r"ICALL-BAD", post, re.I)))
else:
    print("SetFlip_after_R_Perm", 0)
    print("Pad_after_R_Perm", 0)
    print("ICALL_BAD_after_R_Perm", 0)
print("SetFlip_total", c(r"SetFlip|cellGcmSetFlip"))
print("--- key ---")
for line in s.splitlines():
    if any(k in line for k in ["UNSTICK","thr_auto_load","AUTO_LOAD","FORCE SEQDONE",
        "logo queue DONE","R_Perm","dispatch HIT","dispatch MISS","FlashUI",
        "UNSTICK","Play 002C00DC","StartSeq","menu","NewGame","CLOSE-PRESERVE",
        "CC9D0-SKIP","CC9D0-YIELD"]):
        if "002B7188" in line: continue
        print(line[:160])
PY
# Companion counters (same metrics, stable CLI for scripts/CI)
python3 "$HERE/count_menu_gate.py" "$LOG" || true
