#!/usr/bin/env bash
# Verifica que um lift contem todo o codigo host que o lift de producao tinha.
#
# O lift do GoW2 e' gitignored e continha (2026-07-25) 1662 linhas de host
# escritas a mao que nao existiam em mais lado nenhum. Este gate falha se um
# re-lift perder qualquer marcador, simbolo ou global desse inventario.
#
# Uso:  ./verify_lift_baseline.sh [LIFT_DIR]     (default: recomp_macos_v2)
# rc=0  todos os marcadores presentes
# rc=1  falta pelo menos um  -> ha comportamento perdido no re-lift
# rc=2  erro de uso
#
# Regenerar o manifesto (so' quando o baseline mudar DE PROPOSITO):
#   python3 lift_baseline/gen_manifest.py <LIFT> > lift_baseline/MANIFEST.tsv
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
LIFT="${1:-$HERE/recomp_macos_v2}"
MANIFEST="$HERE/lift_baseline/MANIFEST.tsv"
PS3_ROOT="${PS3_ENGINE_ROOT:-$HERE/../ps3recomp}"

PY="${PS3_PATCH_PYTHON:-}"
if [ -z "$PY" ]; then
    if [ -x "$PS3_ROOT/.venv/bin/python" ]; then PY="$PS3_ROOT/.venv/bin/python"
    else PY="$(command -v python3 || true)"; fi
fi
[ -n "$PY" ] || { echo "sem interpretador python3"; exit 2; }
[ -f "$MANIFEST" ] || { echo "manifesto em falta: $MANIFEST"; exit 2; }
[ -d "$LIFT" ] || { echo "dir de lift inexistente: $LIFT"; exit 2; }

echo "-- baseline do lift: $(basename "$LIFT") --"
"$PY" "$HERE/lift_baseline/gen_manifest.py" "$LIFT" --verify "$MANIFEST"
