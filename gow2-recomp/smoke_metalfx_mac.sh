#!/usr/bin/env bash
# Prova que o upscaler MetalFX corre no present path do backend REAL.
# Usa o caminho de demo draw (movie OFF) para forcar o ramo guest do present:
# o triangulo de demo desenha no RT guest-res (1280x720) e o MetalFX faz upscale
# para o drawable (retina 2x). Aceite: linha "[RSX metal] MetalFX upscale ...".
#
# Gated: sem PS3_METALFX=1 o baseline nao chama MetalFX (no-op).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
. ./env_gow2.sh
unset PS3_NO_RSX || true
export PS3_RSX_BACKEND=metal
export PS3_METAL_DEMO_DRAW=1
export PS3_METALFX=1
export PS3_PERF_FSM=1
# movie OFF para o triangulo de demo chegar ao present (ramo guest)
export PS3_MOVIE_HLE=0
unset PS3_MOVIE_HLE || true
export PS3_MOVIE_EOS=0
export PS3_MOVIE_DONE_MS=0
export PS3_FRAME_DUMP=1
export PS3_FRAME_DUMP_STRIDE=30

LOG=/tmp/smoke_metalfx.log
rm -f frame_*.bmp 2>/dev/null || true
./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 &
BPID=$!
sleep 10
kill -TERM "$BPID" 2>/dev/null || true
sleep 1
kill -9 "$BPID" 2>/dev/null || true
wait "$BPID" 2>/dev/null || true

echo "=== janela / drawable ==="
grep -E 'Window created|device:' "$LOG" | head -4
echo "=== MetalFX (3D guest RT e/ou overlay content-hold) ==="
grep -E 'MetalFX (overlay )?upscale' "$LOG" | head -6
echo "=== frames dumped (drawable, res do ecra) ==="
ls -la frame_*.bmp 2>/dev/null | tail -3 || echo "(nenhum frame_*.bmp)"

if grep -qE 'MetalFX (overlay )?upscale' "$LOG"; then
  echo "GREEN: MetalFX exercitado no present path do backend real"
  exit 0
fi
if grep -qE 'guest_present' "$LOG"; then
  echo "NOTA: ramo guest correu mas drawable==RT -> MetalFX nao dispara (correto)."
  exit 2
fi
echo "NOTA: nem guest nem content-hold dispararam MetalFX neste run."
exit 3
