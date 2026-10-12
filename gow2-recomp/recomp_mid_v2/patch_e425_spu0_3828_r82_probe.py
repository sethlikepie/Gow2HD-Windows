#!/usr/bin/env python3
"""E425 -- where does r82 (spu0 0x3828's descriptor pointer P) become 0?

E424: at the failing flush call (spu0 0xD9DC) the caller's saved r82 is 0, so 0x3828's body reads its
descriptor fields *(r82+8) / *(r82+28) from LS 8 / LS 28 (zeros) -> count 0 -> the {begin,end,current,callback}
object overflows at once and calls a stack-leftover "callback" (0x47CE9A10), and the EA-0 DMA is refused.
Static reading clears every callee of that body (0x3608, 0x36F0/0x3750/0x3820, 0x5A18 restore r82). This probe
measures instead of reading:
  A  entry of spu0 0x3828: r3 (= P), lr, sp            first 40 calls, then every P < 0x100 (cap 40)
  B  0x3AC8 right after the call to 0x36F0: r82 if < 0x100 (cap 40)
  C  0x3A78 right after the call to 0x5A18: r82 if < 0x100 (cap 40)
Gated PS3_TRACE_SPU3828 (OFF). Reads registers only. Target is the TRACKED spu_lifted/spu0_v2/spu_recomp.c:
apply, build, then `git checkout` the file (the compiled object stays in the lift dir).
Idempotent (marker E425-SPU3828). rc 0 ok / 2 no file / 3 a needle count != 1.
Usage: patch_e425_spu0_3828_r82_probe.py <path/to/spu0_v2/spu_recomp.c>
"""
from __future__ import annotations

import sys

MARK = "E425-SPU3828"
GATE = ('static int _on=-1; if(_on<0){ const char* _e=getenv("PS3_TRACE_SPU3828"); '
        '_on=(_e&&*_e&&*_e!=\'0\')?1:0; } ')

HEAD_A = "void spu0_spu_func_00003828(spu_context* ctx) {\n"
PROBE_A = ("        /* " + MARK + " A */ { " + GATE + "if(_on){ static int _c=0, _z=0; "
           "uint32_t _p=ctx->gpr[3]._u32[0]; "
           "if(_c<40 || (_p<0x100u && _z<40)){ if(_c<40) _c++; else _z++; "
           "fprintf(stderr,\"[SPU3828] entry P=0x%08X lr=0x%05X sp=0x%05X\\n\",_p,ctx->gpr[0]._u32[0]&0x3FFFFu,"
           "ctx->gpr[1]._u32[0]&0x3FFFFu); fflush(stderr);} } }\n")

SITE_B = "ctx->gpr[0] = spu_splat_u32(0x3AD0); spu0_spu_func_000036F0(ctx); SPU_DRAIN(ctx);\n"
SITE_C = "ctx->gpr[0] = spu_splat_u32(0x3A90); spu0_spu_func_00005A18(ctx); SPU_DRAIN(ctx);\n"


def after(tag: str, where: str) -> str:
    return ("        /* " + MARK + " " + tag + " */ { " + GATE + "if(_on){ static int _c=0; "
            "uint32_t _r=ctx->gpr[82]._u32[0]; if(_r<0x100u && _c<40){ _c++; "
            "fprintf(stderr,\"[SPU3828] r82=0x%08X after " + where + " sp=0x%05X\\n\",_r,"
            "ctx->gpr[1]._u32[0]&0x3FFFFu); fflush(stderr);} } }\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e425_spu0_3828_r82_probe.py <spu0_v2/spu_recomp.c>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    try:
        s = open(path, errors="surrogateescape").read()
    except OSError as e:
        print(f"E425: {e}", file=sys.stderr)
        return 2
    if MARK in s:
        print("E425: ALREADY")
        return 0
    counts = {"A": s.count(HEAD_A), "B": s.count(SITE_B), "C": s.count(SITE_C)}
    if any(n != 1 for n in counts.values()):
        print(f"E425: needle counts {counts} (expected 1 each)", file=sys.stderr)
        return 3
    s = s.replace(HEAD_A, HEAD_A + PROBE_A)
    s = s.replace(SITE_B, SITE_B + after("B", "0x36F0 (in 0x3AC8)"))
    s = s.replace(SITE_C, SITE_C + after("C", "0x5A18 (in 0x3A78)"))
    if "#include <stdio.h>" not in s:
        s = s.replace('#include "spu_recomp.h"\n', '#include "spu_recomp.h"\n#include <stdio.h>\n#include <stdlib.h>\n', 1)
    open(path, "w", errors="surrogateescape").write(s)
    print("E425: APPLIED (3 sites)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
