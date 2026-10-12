#!/usr/bin/env bash
# Roda o God of War II HD nativo no macOS/arm64.
# Equivalente POSIX do recomp_mid_v2/rodar_gow2.cmd.
#
# Uso:
#   ./rodar_gow2.sh                 # backend metal, FULLSCREEN (padrao de jogo)
#   PS3_FULLSCREEN=0 ./rodar_gow2.sh# janela (dev / frame dump)
#   PS3_RSX_BACKEND=sdl ./rodar_gow2.sh
#   PS3_RSX_BACKEND=vulkan ./rodar_gow2.sh
#   PS3_NO_RSX=1 ./rodar_gow2.sh    # headless (caminho CPU/SPURS)
#
# Sair: ESC (fecha a janela) ou timeout. O guest fica em loop de render.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

# Experiencia de jogo: fullscreen por default (override com PS3_FULLSCREEN=0).
# So' faz sentido com backend grafico; no headless (PS3_NO_RSX) e' ignorado.
export PS3_FULLSCREEN="${PS3_FULLSCREEN:-1}"

if [ ! -x ./boot_gow2 ]; then
    echo "boot_gow2 nao encontrado -- rode ./build_macos.sh primeiro" >&2
    exit 1
fi
if [ ! -f EBOOT.ELF ]; then
    echo "EBOOT.ELF nao encontrado. Extraia e decripte o PKG antes:" >&2
    echo "  python extract_pkg.py <PKG> --out extracted" >&2
    echo "  python decrypt_self.py extracted/USRDIR/EBOOT.BIN EBOOT.ELF --rap <arquivo.rap>" >&2
    exit 1
fi

. "$HERE/env_gow2.sh"          # env canonico: PS3_RSX_BACKEND=metal no Darwin

TIMEOUT="${TIMEOUT:-120}"

if [ -n "${PS3_NO_RSX:-}" ]; then
    BACKEND_DESC="none (PS3_NO_RSX)"
else
    BACKEND_DESC="${PS3_RSX_BACKEND:-metal}"
fi
echo "[rodar] backend=$BACKEND_DESC timeout=${TIMEOUT}s"
echo "[rodar] vfs=$PS3_VFS_ROOT"
echo "[boot] RSX backend=$BACKEND_DESC"

./boot_gow2 EBOOT.ELF &
BPID=$!
for _ in $(seq "$TIMEOUT"); do
    kill -0 "$BPID" 2>/dev/null || break
    sleep 1
done

if kill -0 "$BPID" 2>/dev/null; then
    echo "[rodar] timeout de ${TIMEOUT}s -- encerrando (loop do guest e o esperado)"
    kill -9 "$BPID" 2>/dev/null
    exit 124
fi

wait "$BPID"
rc=$?
echo "[rodar] boot_gow2 saiu com rc=$rc"
exit $rc
