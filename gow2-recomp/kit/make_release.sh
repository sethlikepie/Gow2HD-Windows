#!/usr/bin/env bash
# kit/make_release.sh -- package the macOS build kit (maintainers).
#
#   ./kit/make_release.sh [out_dir]
#
# The zip holds only committed source: gow2-recomp (this repo) and the
# ps3recomp engine (MIT, without games/), side by side, plus the lifter
# revisions the kit replays (tools_pinned/<rev>/tools) so the user needs no
# git history. No game file is ever in a repository, so none is in the zip;
# the user brings EBOOT.ELF and the game folder to kit/setup.sh.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="$(cd "${PS3_ENGINE_ROOT:-$HERE/../ps3recomp}" && pwd)"
OUT="$(mkdir -p "${1:-$HERE/dist}" && cd "${1:-$HERE/dist}" && pwd)"
PINNED="5b004fc7 5f36a40e 11a1c3c5"   # PPU lifter; SPU lifters (spu0-3, spu4-5)

for r in "$HERE" "$ENGINE"; do
    [ -z "$(git -C "$r" status --porcelain --untracked-files=no)" ] \
        || { echo "working tree com mudancas por commitar: $r" >&2; exit 1; }
done
V="$(date +%Y%m%d)-$(git -C "$HERE" rev-parse --short HEAD)"
NAME="gow2-recomp-kit-macos-$V"
T="$(mktemp -d "${TMPDIR:-/tmp}/gow2_kit.XXXXXX")"
trap 'rm -rf "$T"' EXIT
R="$T/$NAME"; mkdir -p "$R/gow2-recomp" "$R/ps3recomp"

git -C "$HERE" archive HEAD | tar -x -C "$R/gow2-recomp"
git -C "$ENGINE" archive HEAD | tar -x -C "$R/ps3recomp"
# games/ is the monorepo copy of the ports. apply_all_patches.sh reads the
# patch order, catalog and contracts from games/gow2/lift_baseline, so that
# folder stays (text only); the rest goes.
mv "$R/ps3recomp/games/gow2/lift_baseline" "$T/lift_baseline"
rm -rf "$R/ps3recomp/games"
mkdir -p "$R/ps3recomp/games/gow2" && mv "$T/lift_baseline" "$R/ps3recomp/games/gow2/lift_baseline"
for rev in $PINNED; do
    mkdir -p "$R/ps3recomp/tools_pinned/$rev"
    git -C "$ENGINE" archive "$rev" tools | tar -x -C "$R/ps3recomp/tools_pinned/$rev"
done
{
    echo "gow2-recomp $(git -C "$HERE" rev-parse HEAD)"
    echo "ps3recomp   $(git -C "$ENGINE" rev-parse HEAD)"
    for rev in $PINNED; do echo "pinned      $(git -C "$ENGINE" rev-parse "$rev")"; done
} > "$R/VERSIONS.txt"
cp "$HERE/kit/README.md" "$R/README.md"

# Nothing from the game may ride along: refuse the usual suspects.
if find "$R" \( -iname 'EBOOT.*' -o -iname '*.psarc' -o -iname '*.self' -o -iname '*.m2v' \
        -o -iname '*.wad_ps3' -o -iname '*.wav' -o -iname 'spu_hit_*' -o -iname 'spu_miss_*' \) \
        -print | grep -q .; then
    echo "arquivo de jogo no pacote -- abortado" >&2; exit 1
fi
(cd "$T" && zip -qr -X "$OUT/$NAME.zip" "$NAME")
echo "$OUT/$NAME.zip ($(du -h "$OUT/$NAME.zip" | cut -f1))"
