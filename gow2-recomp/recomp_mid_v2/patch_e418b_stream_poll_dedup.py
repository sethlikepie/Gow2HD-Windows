#!/usr/bin/env python3
"""E418b -- change-log probe of the loader's stream poll (func_002B45D0) and every submit (func_002B3D1C).

E418's AT-EOF cap (64) was used up by the R_LglScA sync loop at the start (op 0x008695D4, size 0xC00 = pos),
so the plateau state (driver case 22, loader state 2, rem 0) stayed invisible -- same trap as [AREAD]'s 256.
This probe prints a poll line only when the tuple (op, FO, io, io+0x90, flags, size, cursor) CHANGES
(cap 4000) plus a heartbeat every 10000 polls, and every submit (cap 4000). The last change line is what the
loader is waiting on at the plateau.

Gated PS3_TRACE_EOFD (OFF by default). Reads guest memory only. Apply on a lift WITHOUT patch_e418 (both
instrument the same entries). Idempotent (marker E418b-EOFD). rc 0 ok / 2 no lift / 3 needle count != 1.
Usage: patch_e418b_stream_poll_dedup.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import sys

MARK = "E418b-EOFD"
SUBMIT = "void func_002B3D1C(ppu_context* ctx) {\n"
POLL = "void func_002B45D0(ppu_context* ctx) {\n"

GATE = ('static int _on=-1; if(_on<0){ extern char* getenv(const char*); '
        'const char* _e=getenv("PS3_TRACE_EOFD"); _on=(_e&&*_e&&*_e!=\'0\')?1:0; } ')
FIELDS = ('uint32_t _op=(uint32_t)ctx->gpr[3]; '
          'uint32_t _fo=_op?vm_read32(_op+4u):0u, _io=_op?vm_read32(_op+8u):0u, _fl=_op?vm_read32(_op+0xCu):0u; '
          'uint32_t _sz=_op?vm_read32(_op+0x10u):0u, _pos=_op?vm_read32(_op+0x14u):0u; '
          'uint32_t _dn=(_io>=0x10000u&&_io<0xF0000000u)?vm_read32(_io+0x90u):0xFFFFFFFFu; ')

SUBMIT_PROBE = ("        /* " + MARK + " submit */ { " + GATE + "if(_on){ " + FIELDS +
                "static int _k=0; if(_k++<4000){ fprintf(stderr,"
                "\"[EOFD] submit #%d op=0x%08X fo=0x%08X io=0x%08X fl=0x%08X size=0x%X pos=0x%X n=%u\\n\","
                "_k,_op,_fo,_io,_fl,_sz,_pos,(uint32_t)ctx->gpr[5]); fflush(stderr);} } }\n")
POLL_PROBE = ("        /* " + MARK + " poll */ { " + GATE + "if(_on){ " + FIELDS +
              "static unsigned long _n=0; static int _c=0; static uint32_t _l[7]={0,0,0,0,0,0,0}; _n++; "
              "int _ch=(_l[0]!=_op||_l[1]!=_fo||_l[2]!=_io||_l[3]!=_dn||_l[4]!=_fl||_l[5]!=_sz||_l[6]!=_pos); "
              "if((_ch && _c++<4000) || (_n%10000)==0){ fprintf(stderr,"
              "\"[EOFD] poll #%lu%s op=0x%08X fo=0x%08X io=0x%08X done=0x%08X fl=0x%08X size=0x%X pos=0x%X\\n\","
              "_n,_ch?\" CHANGE\":\" beat\",_op,_fo,_io,_dn,_fl,_sz,_pos); fflush(stderr);} "
              "_l[0]=_op;_l[1]=_fo;_l[2]=_io;_l[3]=_dn;_l[4]=_fl;_l[5]=_sz;_l[6]=_pos; } }\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e418b_stream_poll_dedup.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E418b: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    texts = {p: open(p, errors="surrogateescape").read() for p in chunks}
    if any(MARK in s for s in texts.values()):
        print("E418b: ALREADY")
        return 0
    if any("E418-EOF" in s for s in texts.values()):
        print("E418b: lift already has patch_e418 -- use a lift without it", file=sys.stderr)
        return 3
    ns = sum(s.count(SUBMIT) for s in texts.values()); npl = sum(s.count(POLL) for s in texts.values())
    if ns != 1 or npl != 1:
        print(f"E418b: needle counts submit={ns} poll={npl} (expected 1 each)", file=sys.stderr)
        return 3
    for p, s in texts.items():
        t = s.replace(SUBMIT, SUBMIT + SUBMIT_PROBE, 1).replace(POLL, POLL + POLL_PROBE, 1)
        if t != s:
            open(p, "w", errors="surrogateescape").write(t)
            print(f"E418b: {os.path.basename(p)}: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
