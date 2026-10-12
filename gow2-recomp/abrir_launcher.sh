#!/usr/bin/env bash
# Abre a tela de setup (ELF + USRDIR) e mods. JOGAR dispara o boot.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
exec python3 "$HERE/gow2_launcher.py" "$@"
