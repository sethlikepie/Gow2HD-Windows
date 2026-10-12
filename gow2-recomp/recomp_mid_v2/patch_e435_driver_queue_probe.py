#!/usr/bin/env python3
"""E435 -- dump the level driver's command queue at its loop head (func_00042294, EBOOT 0x422A0).

E431/E434: after the level's 14th container the driver runs case 17 (wait for the loader), the loader answers
done, case 17's done path writes state 0 and returns -- and [LDRSTATE] keeps showing 17 <-> 0. The loop head is
  if (*(o+0x54C)) return;              pause/busy flag
  if (*(o+0x550)) dispatch(state);     current state
  if (*(o+0x44) == 0) return;          pending command count
  state = *(u16*)(o + 0x4C + *(o+0x40)*32);   head command opcode, then dispatch
with o = *(r2-0x7558). Whether the plateau is "count 0, idle" or "a head entry nobody pops" is not decidable by
reading; this probe prints o, +0x40 (head idx), +0x44 (count), +0x54C, +0x550 and the 32 entry opcodes whenever
the first four change (cap 400) plus a heartbeat every 20000 calls.
Gated PS3_TRACE_DRVQ (OFF). Reads guest memory only. Idempotent (marker E435-DRVQ).
rc 0 ok / 2 no lift / 3 needle count != 1 inside func_00042294.
Usage: patch_e435_driver_queue_probe.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import sys

MARK = "E435-DRVQ"
HEAD = "void func_00042294(ppu_context* ctx) {\n"
NEEDLE = "        ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x7558);\n"
PROBE = (
    "        /* " + MARK + " */ { static int _on=-1; if(_on<0){ extern char* getenv(const char*); "
    "const char* _e=getenv(\"PS3_TRACE_DRVQ\"); _on=(_e&&*_e&&*_e!='0')?1:0; } "
    "if(_on){ uint32_t _o=(uint32_t)ctx->gpr[31]; if(_o>=0x10000u&&_o<0x4F000000u){ "
    "static uint32_t _l[4]={0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu}; static int _c=0; static unsigned long _n=0; _n++; "
    "uint32_t _v[4]; _v[0]=vm_read32(_o+0x40u); _v[1]=vm_read32(_o+0x44u); _v[2]=vm_read32(_o+0x54Cu); _v[3]=vm_read32(_o+0x550u); "
    "int _ch=(_v[0]!=_l[0]||_v[1]!=_l[1]||_v[2]!=_l[2]||_v[3]!=_l[3]); "
    "if((_ch && _c<400) || (_n%20000u)==0){ if(_ch) _c++; char _b[260]; int _w=0; _b[0]=0; "
    "for(int _i=0;_i<32;_i++) _w+=snprintf(_b+_w,sizeof(_b)-(size_t)_w,\"%02X\",(unsigned)(vm_read16(_o+0x4Cu+(uint32_t)_i*32u)&0xFFu)); "
    "fprintf(stderr,\"[DRVQ] #%lu%s o=0x%08X idx=%u count=%u pause=%u state=%u ops=%s\\n\",_n,_ch?\" CHANGE\":\" beat\","
    "_o,_v[0],_v[1],_v[2],_v[3],_b); fflush(stderr); } "
    "_l[0]=_v[0];_l[1]=_v[1];_l[2]=_v[2];_l[3]=_v[3]; } } }\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e435_driver_queue_probe.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E435: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    for p in chunks:
        s = open(p, errors="surrogateescape").read()
        i = s.find(HEAD)
        if i < 0:
            continue
        end = s.find("\n}\n", i) + 3
        body = s[i:end]
        if MARK in body:
            print("E435: ALREADY")
            return 0
        if body.count(NEEDLE) != 1:
            print(f"E435: needle count {body.count(NEEDLE)} in func_00042294 (expected 1)", file=sys.stderr)
            return 3
        body = body.replace(NEEDLE, NEEDLE + PROBE)
        open(p, "w", errors="surrogateescape").write(s[:i] + body + s[end:])
        print(f"E435: {os.path.basename(p)}: APPLIED")
        return 0
    print("E435: func_00042294 not found", file=sys.stderr)
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
