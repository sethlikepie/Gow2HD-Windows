#!/usr/bin/env python3
"""patch_spu1_391c.py -- add the missing SPU entry 0x391C to GoW2's spu1 lift.

The spu1 image (decoder task, fp 0x2A5C4E67A14505B8, entry 0x3050) branches
indirectly to LS 0x391C, which the lift only has as the middle of
spu1_spu_func_00003914. The branch found no registered entry and the job
aborted there tens of times per second (``[SPU] indirect branch to unknown LS
address 0x0391C (image 2)``), leaving its output half written -- the particle
vertices the game draws as screen-sized red/grey planes.

The new function is the exact suffix of 0x3914 from its third instruction
(0x391C = 0x3914 + 2 * 4), in the file's own style; a fresh lift with
``tools/spu_lifter.py --extra-funcs <all spu1_v2 entries>,0x391C`` produces the
same body and no other new or lost entry. Idempotent.

Usage: patch_spu1_391c.py <gow2-recomp>/spu_lifted/spu1_v2
"""
import sys
from pathlib import Path

NAME = "spu1_spu_func_0000391C"


def main(d: str) -> int:
    src = Path(d) / "spu_recomp.c"
    hdr = Path(d) / "spu_recomp.h"
    c = src.read_text()
    h = hdr.read_text()
    if f"void {NAME}(spu_context* ctx) {{" in c:
        print("patch_spu1_391c: already applied")
        return 0

    start = c.index("void spu1_spu_func_00003914(spu_context* ctx) {\n")
    end = c.index("\n}\n", start) + 3
    body = c[start:end].split("\n")
    # line 0 = signature; lines 1-2 = the two instructions at 0x3914/0x3918.
    head = body[0]
    tail = body[3:]
    if not tail or "spu_rotqmbyi(ctx->gpr[82], 124)" not in tail[0]:
        print("patch_spu1_391c: unexpected 0x3914 body, not patching", file=sys.stderr)
        return 1
    fn = "\n".join([head.replace("00003914", "0000391C")] + tail)
    note = ("/* Indirect-branch entry inside 0x3914 (tools/patch_spu1_391c.py):\n"
            " * the same instructions from 0x391C on. */\n")
    c = c[:end] + "\n" + note + fn + c[end:]

    row = '    { 0x00003914u, spu1_spu_func_00003914, "spu1_spu_func_00003914" },\n'
    if row not in c:
        print("patch_spu1_391c: table row for 0x3914 not found", file=sys.stderr)
        return 1
    c = c.replace(row, row + f'    {{ 0x0000391Cu, {NAME}, "{NAME}" }},\n', 1)

    proto = "void spu1_spu_func_00003914(spu_context* ctx);\n"
    if proto not in h:
        print("patch_spu1_391c: prototype for 0x3914 not found", file=sys.stderr)
        return 1
    h = h.replace(proto, proto + f"void {NAME}(spu_context* ctx);\n", 1)

    src.write_text(c)
    hdr.write_text(h)
    print("patch_spu1_391c: added", NAME)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
