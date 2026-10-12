#!/usr/bin/env python3
"""E426 -- which spu0 call returns with a different stack pointer?

E425: spu0 0x3828 is entered with sp=0x3FE60 (frame -> 0x3FD50) and P=0x3FEC4, but right after its calls to
0x5A18 and 0x36F0 the probe sees sp=0x2FD50 -- exactly 0x10000 lower -- and r82=0: the callee-save restores read
`lq(sp-0x30)` from the wrong, zero-filled LS region. Every direct call in the SPU ABI must return with the same sp,
so this probe wraps EVERY `ctx->gpr[0] = spu_splat_u32(RET); spu0_spu_func_X(ctx); SPU_DRAIN(ctx);` line and prints
the callee, the return address and sp before/after when they differ (first 40, gated PS3_TRACE_SPUSP, OFF).
Target is the TRACKED spu_lifted/spu0_v2/spu_recomp.c: apply, build, then `git checkout` the file.
Idempotent (marker E426-SPUSP). rc 0 ok / 2 no file / 3 no call site matched.
Usage: patch_e426_spu0_sp_balance_probe.py <path/to/spu0_v2/spu_recomp.c>
"""
from __future__ import annotations

import re
import sys

MARK = "E426-SPUSP"
CALL_RX = re.compile(
    r"^(?P<ind>[ \t]*)(?P<stmt>ctx->gpr\[0\] = spu_splat_u32\((?P<ret>0x[0-9A-F]+)\); "
    r"(?P<fn>spu0_spu_func_[0-9A-F]{8})\(ctx\); SPU_DRAIN\(ctx\);)[ \t]*$", re.M)

HELPER = ("\n/* " + MARK + " */\n"
          "static void e426_sp_check(spu_context* ctx, const char* fn, uint32_t ret, uint32_t before)\n"
          "{\n"
          "    static int on = -1, n = 0;\n"
          "    if (on < 0) { const char* e = getenv(\"PS3_TRACE_SPUSP\"); on = (e && *e && *e != '0') ? 1 : 0; }\n"
          "    uint32_t after = ctx->gpr[1]._u32[0];\n"
          "    if (on && after != before && n < 40) {\n"
          "        n++;\n"
          "        fprintf(stderr, \"[SPUSP] %s returned to 0x%05X with sp 0x%05X -> 0x%05X (delta %d)\\n\",\n"
          "                fn, ret & 0x3FFFFu, before & 0x3FFFFu, after & 0x3FFFFu, (int)(after - before));\n"
          "        fflush(stderr);\n"
          "    }\n"
          "}\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e426_spu0_sp_balance_probe.py <spu0_v2/spu_recomp.c>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    try:
        s = open(path, errors="surrogateescape").read()
    except OSError as e:
        print(f"E426: {e}", file=sys.stderr)
        return 2
    if MARK in s:
        print("E426: ALREADY")
        return 0
    n = 0

    def wrap(m: re.Match) -> str:
        nonlocal n
        n += 1
        ind = m.group("ind")
        return (f"{ind}{{ const uint32_t _e426sp = ctx->gpr[1]._u32[0]; {m.group('stmt')} "
                f"e426_sp_check(ctx, \"{m.group('fn')}\", {m.group('ret')}u, _e426sp); }}")

    s = CALL_RX.sub(wrap, s)
    if n == 0:
        print("E426: no call site matched", file=sys.stderr)
        return 3
    head = '#include "spu_recomp.h"\n'
    if head not in s:
        print("E426: include anchor not found", file=sys.stderr)
        return 3
    extra = "" if "#include <stdio.h>" in s else "#include <stdio.h>\n#include <stdlib.h>\n"
    s = s.replace(head, head + extra + HELPER, 1)
    open(path, "w", errors="surrogateescape").write(s)
    print(f"E426: APPLIED ({n} call sites)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
