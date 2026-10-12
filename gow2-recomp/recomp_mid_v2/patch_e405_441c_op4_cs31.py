#!/usr/bin/env python3
"""E405 -- func_002B441C: snapshot live r31 (the FIOS op) at fragment entry;
tail reads op+4 from that snapshot.

4340 saves the *caller* r31 to 0x198, then sets r31=op, then trampolines to
441C. 441C's _cs_31 is therefore NOT the op. Live r31 at 441C entry is.
Poll/DONE fragments clobber r31 before the tail lwz r0,4(r31), so BAD10
returns 0 even when the op completed (E398/E403).

Always-on. Marker E405-441C-OP-SNAP. rc 0/2/3.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "E405-441C-OP-SNAP"
# v1 (wrong: used _cs_31) — rewrite if present
OLD_V1 = (
    "        /* E405-441C-OP4: r31 clobbered by 4224 fragments; op is _cs_31 */\n"
    "        ctx->gpr[31] = _cs_31;\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x4);\n"
)
NEEDLE_ENTRY = (
    "        uint64_t _cs_31 = vm_read64(ctx->gpr[1] + 0x198);\n"
    "        ctx->gpr[29] = ppc_rldicl(ctx->gpr[28], 0, 32);\n"
)
REPL_ENTRY = (
    "        uint64_t _cs_31 = vm_read64(ctx->gpr[1] + 0x198);\n"
    "        uint64_t _op = ctx->gpr[31]; /* E405-441C-OP-SNAP: live op from 4340 */\n"
    "        ctx->gpr[29] = ppc_rldicl(ctx->gpr[28], 0, 32);\n"
)
NEEDLE_TAIL = (
    "        if (((ctx->cr >> 0) & 2)) goto loc_002B4420;\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x4);\n"
)
REPL_TAIL = (
    "        if (((ctx->cr >> 0) & 2)) goto loc_002B4420;\n"
    "        ctx->gpr[0] = vm_read32((uint32_t)_op + 0x4);\n"
)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    hit = 0
    for p in paths:
        if not p.is_file():
            continue
        s = p.read_text(errors="replace")
        orig = s
        if OLD_V1 in s:
            s = s.replace(OLD_V1, "        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x4);\n", 1)
        if MARKER in s:
            if s != orig:
                p.write_text(s)
            print(f"E405: {p.name}: ALREADY")
            hit += 1
            continue
        ne, nt = s.count(NEEDLE_ENTRY), s.count(NEEDLE_TAIL)
        if ne == 0 and nt == 0:
            continue
        if ne != 1 or nt != 1:
            print(f"E405: {p.name}: FAILED entry={ne} tail={nt} (esperado 1/1)", file=sys.stderr)
            return 3
        s = s.replace(NEEDLE_ENTRY, REPL_ENTRY, 1).replace(NEEDLE_TAIL, REPL_TAIL, 1)
        p.write_text(s)
        print(f"E405: {p.name}: APPLIED")
        hit += 1
    if hit == 0:
        print("E405: FAILED 0x", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
