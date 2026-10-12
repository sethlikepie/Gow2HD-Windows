#!/usr/bin/env python3
"""E404 -- after CDE3C in func_000CE0A0, reload r30/r31 from TOC.

PPC: CE0A0 keeps r31 = *(TOC-0x5E70) (intro state S) and r30 = *(TOC-0x5E34)
across bl CDE3C, then lwz r0,0(r31); cmpwi cr7,r0,4; bne loop.
CDE3C fragments trampoline without restoring those callee-saves, so the
compare reads garbage and idx==4 never exits. Reload is the PPC invariant.

Always-on lift fix (not a probe). Idempotent. Marker E404-CE0A0-R31.
rc: 0 applied/already; 2 needle 0; 3 needle != 1.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "E404-CE0A0-R31"
NEEDLE = "        ctx->lr = 0x000CE0F4; func_000CDE3C(ctx); DRAIN_TRAMPOLINE(ctx);\n"
REPL = (
    NEEDLE
    + "        /* E404-CE0A0-R31: CDE3C fragments clobber callee-saves; restore S/guard */\n"
    "        ctx->gpr[30] = vm_read32(ctx->gpr[2] + -0x5E34);\n"
    "        ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x5E70);\n"
)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    total = 0
    already = 0
    target = None
    texts: dict[Path, str] = {}
    for p in paths:
        if not p.is_file():
            continue
        s = p.read_text(errors="replace")
        texts[p] = s
        n = s.count(NEEDLE)
        total += n
        if MARKER in s:
            already += 1
        if n:
            target = p
    if already:
        print("E404: ALREADY")
        return 0
    if total == 0:
        print("E404: FAILED 0x (esperado 1)", file=sys.stderr)
        return 2
    if total != 1:
        print(f"E404: FAILED {total}x (esperado 1)", file=sys.stderr)
        return 3
    assert target is not None
    texts[target] = texts[target].replace(NEEDLE, REPL, 1)
    target.write_text(texts[target])
    print(f"E404: {target.name}: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
