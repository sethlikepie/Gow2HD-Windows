#!/usr/bin/env python3
"""Fix brhz/brhnz/ihz/ihnz in lifted SPU C: test the preferred halfword.

The SPU lifter from 2026-07-21 (11a1c3c5) to 2026-09-23 emitted the condition as
`ctx->gpr[N]._u16[1]`. u128 stores big-endian word i as a host integer in _u32[i],
so on the little-endian host _u16[1] is bytes 0-1 of the preferred word, not the
preferred halfword (bytes 2-3). Measured 2026-09-23: SCREAM's policy module (spu6)
pops its command queue with `ceqhi` + `brhnz` on entry 0x00900001; reading 0x0090
instead of 0x0001 made every pop look empty, so the mixer never mixed and the game
wrote silence. spu1/spu4/spu5/spu6 carry the wrong form (159 sites); spu0/2/3 came
from the older lifter, which masked the word -- the form written here.

Usage: patch_spu_halfword_cond.py <spuN_v2 dir> [...]   (idempotent)
"""
import pathlib
import re
import sys

PAT = re.compile(r"ctx->gpr\[(\d+)\]\._u16\[1\] ([=!]=) 0\)")


def fix(d: pathlib.Path) -> int:
    p = d / "spu_recomp.c"
    t = p.read_text(encoding="utf-8", errors="replace")
    new, n = PAT.subn(r"(ctx->gpr[\1]._u32[0] & 0xFFFF) \2 0)", t)
    if n:
        p.write_text(new, encoding="utf-8")
    print(f"[spu-halfword] {d.name}: {'APPLIED ' + str(n) if n else 'ALREADY'}")
    return n


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    for a in sys.argv[1:]:
        fix(pathlib.Path(a))
