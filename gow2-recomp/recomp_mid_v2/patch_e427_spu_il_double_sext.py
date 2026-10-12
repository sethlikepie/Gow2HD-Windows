#!/usr/bin/env python3
"""E427 -- undo the old SPU lifter's double sign-extension of `il` immediates in the tracked SPU lifts.

`il rt, i16` loads a 16-bit SIGNED immediate. Before the fix now in ps3recomp tools/spu_disasm.py (the `il`
branch: "i16 is ALREADY sign-extended ... Extending it again mapped every NEGATIVE il immediate to
(imm - 0x10000)"), every negative immediate was emitted as imm - 0x10000. spu0..spu3_v2 in spu_lifted/ were
generated before that fix (43/39/54/43 out-of-range `spu_il(...)`; spu4..6_v2 have none).

Measured damage (E425/E426): spu0 0x50B0 allocates its frame with `spu_il(-66880)` (real -1344, 0xFAC0) and
returns with sp 0x10000 lower (delta -65536, 20/20); callee-save restores then read zeroed LS, r82 = 0, and the
SPU task consuming LFQueue 0x47D59A80 calls a stack-leftover "callback" (0x47CE9A10) -> the main thread waits
forever for that queue to drain after the level's 14th container.

This script rewrites ONLY `spu_il(v)` with v < -32768 to `spu_il(v + 65536)`, after checking that the result is
a negative s16 with the same low 16 bits (the exact inverse of the old bug). Nothing else is touched.
Idempotent: a second run finds nothing to change (rc 0). rc 0 ok / 2 bad args or file / 3 a value outside the
expected bug pattern (nothing written).
Usage: patch_e427_spu_il_double_sext.py <spu_recomp.c> [<spu_recomp.c> ...]
"""
from __future__ import annotations

import re
import sys

IL_RX = re.compile(r"spu_il\((-\d+)\)")


def fix_text(s: str) -> tuple[str, int, list[int]]:
    bad: list[int] = []
    n = 0

    def repl(m: re.Match) -> str:
        nonlocal n
        v = int(m.group(1))
        if v >= -32768:
            return m.group(0)
        w = v + 65536
        if not (-32768 <= w <= -1) or (w & 0xFFFF) != (v & 0xFFFF):
            bad.append(v)
            return m.group(0)
        n += 1
        return f"spu_il({w})"

    return IL_RX.sub(repl, s), n, bad


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: patch_e427_spu_il_double_sext.py <spu_recomp.c> [...]", file=sys.stderr)
        return 2
    plans = []
    for path in sys.argv[1:]:
        try:
            s = open(path, errors="surrogateescape").read()
        except OSError as e:
            print(f"E427: {e}", file=sys.stderr)
            return 2
        t, n, bad = fix_text(s)
        if bad:
            print(f"E427: {path}: {len(bad)} value(s) outside the bug pattern, e.g. {bad[:4]} -- nothing written",
                  file=sys.stderr)
            return 3
        plans.append((path, t, n, s != t))
    for path, t, n, changed in plans:
        if changed:
            open(path, "w", errors="surrogateescape").write(t)
        print(f"E427: {path}: {n} il immediate(s) fixed" if changed else f"E427: {path}: ALREADY (0 to fix)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
