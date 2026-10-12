#!/usr/bin/env python3
"""E418f -- bounds of the runaway vertex loop in func_00286D44 (first menu frame, E418e).

E418e: after the loader closes, the main thread stays in func_00286D44 (a fragment of 0x286BE8) writing float
vertex data past the end of committed memory (0x51000000+). The inner loop (PPC 0x286DF0..0x286E88) is
`for (p = r18; p != end; p += 0x60)` with end = `ld r27,0x1B0(r1)` @0x286DCC -- and the lift turns that load
into `r27 = _cs_27`, a snapshot of *(r1+0x1B0) taken at FRAGMENT ENTRY (the callee-save copy). If the slot
changes after entry (outer loop back-edge to loc_00286D44 stays inside the fragment), the lifted end pointer
is stale and `p != end` never holds.

This probe prints, at the loop setup (right before loc_00286DF0), begin (r5), end as lifted (r27 = _cs_27),
end as the PPC would read it now (*(r1+0x1B0)), (end-begin) mod 0x60, r1: first 40 passes, then only passes
where lifted != current (cap 40), plus a 1/50000 heartbeat. Gated PS3_TRACE_MLOOP (OFF). Reads only.
Idempotent (marker E418f-MLOOP). rc 0 ok / 2 no lift / 3 needle count != 1.
Usage: patch_e418f_menu_loop_bounds_probe.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import sys

MARK = "E418f-MLOOP"
HEAD = "void func_00286D44(ppu_context* ctx) {\n"
NEEDLE = "        ctx->gpr[29] = (int64_t)(int32_t)(1);\nloc_00286DF0:\n"
PROBE = ("        ctx->gpr[29] = (int64_t)(int32_t)(1);\n"
         "        /* " + MARK + " */ { static int _on=-1; if(_on<0){ extern char* getenv(const char*); "
         "const char* _e=getenv(\"PS3_TRACE_MLOOP\"); _on=(_e&&*_e&&*_e!='0')?1:0; } "
         "if(_on){ static unsigned long _n=0; static int _c=0, _d=0; _n++; "
         "uint32_t _b=(uint32_t)ctx->gpr[5], _el=(uint32_t)ctx->gpr[27]; "
         "uint32_t _ec=(uint32_t)vm_read64((uint32_t)ctx->gpr[1]+0x1B0u); "
         "int _stale=(_el!=_ec); "
         "if(_c<40 || (_stale && _d<40) || (_n%50000)==0){ if(_c<40) _c++; else if(_stale) _d++; "
         "fprintf(stderr,\"[MLOOP] #%lu begin=0x%08X end_lifted=0x%08X end_now=0x%08X%s mod60(lifted)=0x%X mod60(now)=0x%X r1=0x%08X\\n\","
         "_n,_b,_el,_ec,_stale?\" STALE\":\"\",(unsigned)((_el-_b)%0x60u),(unsigned)((_ec-_b)%0x60u),(uint32_t)ctx->gpr[1]); fflush(stderr);} } }\n"
         "loc_00286DF0:\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e418f_menu_loop_bounds_probe.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E418f: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    texts = {p: open(p, errors="surrogateescape").read() for p in chunks}
    if any(MARK in s for s in texts.values()):
        print("E418f: ALREADY")
        return 0
    hits = []
    for p, s in texts.items():
        i = s.find(HEAD)
        if i < 0:
            continue
        end = s.find("\n}\n", i)
        if s.count(NEEDLE, i, end) == 1:
            hits.append((p, s.find(NEEDLE, i, end)))
    if len(hits) != 1:
        print(f"E418f: needle found {len(hits)} time(s) inside func_00286D44 (expected 1)", file=sys.stderr)
        return 3
    p, k = hits[0]
    s = texts[p]
    open(p, "w", errors="surrogateescape").write(s[:k] + PROBE + s[k + len(NEEDLE):])
    print(f"E418f: {os.path.basename(p)}: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
