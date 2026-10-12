#!/usr/bin/env python3
"""E419 -- fix the r27 loads of func_00286D44 that the lifter merged into one callee-save snapshot.

Lifter defect (tools/ppu_lifter.py, memory snapshot of a pure tail-entry fragment): `_mem_snap.setdefault(reg,
off)` keys the snapshot by REGISTER, so every `ld rN,X(r1)` in the fragment becomes `ctx->gpr[N] = _cs_N` with
the offset of the FIRST such load. In func_00286D44 (fragment of 0x286BE8) r27 is loaded from three slots:
  0x286DCC  ld r27,0x1B0(r1)   array B base (r1+0xD8)     -> _cs_27 = *(r1+0x1B0)   correct
  0x286DF4  ld r27,0x1C8(r1)   inner-loop end pointer     -> _cs_27                 WRONG (reads 0x1B0)
  0x2873E0  ld r27,0x250(r1)   epilogue callee-save restore -> _cs_27               WRONG (reads 0x1B0)
Measured (E418f): begin=r1+0xC0, lifted end=r1+0xD8, (end-begin) mod 0x60 = 0x18, so `p != end` never holds
and the first menu frame writes vertex data through all guest memory (0x51000000+ = first uncommitted byte).

This script restores the two wrong loads to plain reads of their own slots (the faithful translation).
Idempotent (marker E419-CS27). rc 0 ok / 2 no lift / 3 needle count != 1.
Usage: patch_e419_cs_multi_offset_286d44.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import sys

MARK = "E419-CS27"
HEAD = "void func_00286D44(ppu_context* ctx) {\n"
LOOP_OLD = ("loc_00286DF0:\n        ctx->gpr[8] = ctx->gpr[5] + (int64_t)(0x3C);\n"
            "        ctx->gpr[27] = _cs_27;\n")
LOOP_NEW = ("loc_00286DF0:\n        ctx->gpr[8] = ctx->gpr[5] + (int64_t)(0x3C);\n"
            "        ctx->gpr[27] = vm_read64(ctx->gpr[1] + 0x1C8); /* " + MARK + " ld r27,0x1C8(r1) @0x286DF4 */\n")
EPI_OLD = ("        ctx->gpr[26] = _cs_26;\n        ctx->gpr[27] = _cs_27;\n        ctx->gpr[28] = _cs_28;\n")
EPI_NEW = ("        ctx->gpr[26] = _cs_26;\n"
           "        ctx->gpr[27] = vm_read64(ctx->gpr[1] + 0x250); /* " + MARK + " ld r27,0x250(r1) @0x2873E0 */\n"
           "        ctx->gpr[28] = _cs_28;\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e419_cs_multi_offset_286d44.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E419: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    for p in chunks:
        s = open(p, errors="surrogateescape").read()
        i = s.find(HEAD)
        if i < 0:
            continue
        end = s.find("\n}\n", i) + 3
        body = s[i:end]
        if MARK in body:
            print("E419: ALREADY")
            return 0
        n_loop, n_epi = body.count(LOOP_OLD), body.count(EPI_OLD)
        if n_loop != 1 or n_epi != 1:
            print(f"E419: needle counts loop={n_loop} epilogue={n_epi} (expected 1 each)", file=sys.stderr)
            return 3
        body = body.replace(LOOP_OLD, LOOP_NEW).replace(EPI_OLD, EPI_NEW)
        open(p, "w", errors="surrogateescape").write(s[:i] + body + s[end:])
        print(f"E419: {os.path.basename(p)}: APPLIED (2 loads)")
        return 0
    print("E419: func_00286D44 not found", file=sys.stderr)
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
