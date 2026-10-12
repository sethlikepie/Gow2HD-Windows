#!/usr/bin/env python3
"""E419b -- the third site of the lifter's reg-keyed callee-save snapshot defect (audit PS3_LIFT_CS_AUDIT=1).

Full re-lift with the fixed lifter (ps3recomp 28b22bbd) lists 3 sites in 2 functions; E419 fixes the two in
func_00286D44. This one: func_00290D44 declares `_cs_31 = vm_read64(r1+0x250)` and also rewrote a later
`ld r31,0x260(r1)` (@loc_002916E8 block) to `_cs_31`. The fixed lifter emits a plain load there; this script
makes an existing lift match that output (verified by diffing the function between the old lift and the re-lift:
this is the only difference). Idempotent (marker E419b-CS31). rc 0 ok / 2 no lift / 3 needle count != 1.
Usage: patch_e419b_cs_multi_offset_290d44.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import sys

MARK = "E419b-CS31"
HEAD = "void func_00290D44(ppu_context* ctx) {\n"
OLD = ("        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[5], 3, 0, 28);\n"
       "        ctx->gpr[31] = _cs_31;\n"
       "        ctx->gpr[11] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[5], 6, 0, 25);\n")
NEW = ("        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[5], 3, 0, 28);\n"
       "        ctx->gpr[31] = vm_read64(ctx->gpr[1] + 0x260); /* " + MARK + " ld r31,0x260(r1) */\n"
       "        ctx->gpr[11] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[5], 6, 0, 25);\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e419b_cs_multi_offset_290d44.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E419b: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    for p in chunks:
        s = open(p, errors="surrogateescape").read()
        i = s.find(HEAD)
        if i < 0:
            continue
        end = s.find("\n}\n", i) + 3
        body = s[i:end]
        if MARK in body:
            print("E419b: ALREADY")
            return 0
        if body.count(OLD) != 1:
            print(f"E419b: needle count {body.count(OLD)} (expected 1)", file=sys.stderr)
            return 3
        open(p, "w", errors="surrogateescape").write(s[:i] + body.replace(OLD, NEW) + s[end:])
        print(f"E419b: {os.path.basename(p)}: APPLIED")
        return 0
    print("E419b: func_00290D44 not found", file=sys.stderr)
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
