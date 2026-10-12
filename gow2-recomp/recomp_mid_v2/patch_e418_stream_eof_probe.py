#!/usr/bin/env python3
"""E418 -- probe the loader's stream-read submit (func_002B3D1C) and poll (func_002B45D0) around end-of-stream.

E417: with PS3_LWMUTEX_REAL=1 the front-end loads in ~20 s, then the driver sits in case 22
(0x428D8: r3 = func_0003F2B0() = loader tick func_002BA76C; wait while r3 != 0) and the loader answers busy
from state 2 with rem = 0 and a pending record object (f1BC != 0). The existing [AREAD] entry probe of
func_002B3D1C is capped at 256 and runs out mid-container, so the end-of-stream read is never visible.

Op layout seen in [AREAD] dumps (loader op 0x008695D4): +04 FO, +08 in-flight FIOS op (0 at submit),
+0C flags, +10 file size, +14 cursor (file position), +18/+1C ring fields.

This probe prints (gated PS3_TRACE_EOF, OFF by default):
  * every submit whose cursor >= size (an EOF request), cap 64;
  * every poll: the first 24, then 1 in 20000 (rate-limited heartbeat), and ALWAYS (cap 64) when
    cursor >= size -- op, FO, io, io+0x90 (done word), flags, size, cursor.
Reads guest memory only. Idempotent (marker E418-EOF). rc 0 ok / 2 no lift / 3 needle count != 1 each.
Usage: patch_e418_stream_eof_probe.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import sys

MARK = "E418-EOF"
SUBMIT = "void func_002B3D1C(ppu_context* ctx) {\n"
POLL = "void func_002B45D0(ppu_context* ctx) {\n"

GATE = ('static int _on=-1; if(_on<0){ extern char* getenv(const char*); '
        'const char* _e=getenv("PS3_TRACE_EOF"); _on=(_e&&*_e&&*_e!=\'0\')?1:0; } ')
FIELDS = ('uint32_t _op=(uint32_t)ctx->gpr[3]; '
          'uint32_t _fo=_op?vm_read32(_op+4u):0u, _io=_op?vm_read32(_op+8u):0u, _fl=_op?vm_read32(_op+0xCu):0u; '
          'uint32_t _sz=_op?vm_read32(_op+0x10u):0u, _pos=_op?vm_read32(_op+0x14u):0u; '
          'uint32_t _dn=(_io>=0x10000u&&_io<0xF0000000u)?vm_read32(_io+0x90u):0xFFFFFFFFu; ')

SUBMIT_PROBE = ("        /* " + MARK + " submit */ { " + GATE + "if(_on){ " + FIELDS +
                "static int _k=0; if(_sz && _pos>=_sz && _k++<64){ fprintf(stderr,"
                "\"[EOF] submit-at-eof #%d op=0x%08X fo=0x%08X io=0x%08X fl=0x%08X size=0x%X pos=0x%X n=%u\\n\","
                "_k,_op,_fo,_io,_fl,_sz,_pos,(uint32_t)ctx->gpr[5]); fflush(stderr);} } }\n")
POLL_PROBE = ("        /* " + MARK + " poll */ { " + GATE + "if(_on){ " + FIELDS +
              "static unsigned long _n=0; static int _e=0; _n++; int _eof=(_sz && _pos>=_sz); "
              "if(_n<=24 || (_n%20000)==0 || (_eof && _e++<64)){ fprintf(stderr,"
              "\"[EOF] poll #%lu%s op=0x%08X fo=0x%08X io=0x%08X done=0x%08X fl=0x%08X size=0x%X pos=0x%X\\n\","
              "_n,_eof?\" AT-EOF\":\"\",_op,_fo,_io,_dn,_fl,_sz,_pos); fflush(stderr);} } }\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e418_stream_eof_probe.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E418: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    texts = {p: open(p, errors="surrogateescape").read() for p in chunks}
    if any(MARK in s for s in texts.values()):
        print("E418: ALREADY")
        return 0
    ns = sum(s.count(SUBMIT) for s in texts.values()); npl = sum(s.count(POLL) for s in texts.values())
    if ns != 1 or npl != 1:
        print(f"E418: needle counts submit={ns} poll={npl} (expected 1 each)", file=sys.stderr)
        return 3
    for p, s in texts.items():
        t = s.replace(SUBMIT, SUBMIT + SUBMIT_PROBE, 1).replace(POLL, POLL + POLL_PROBE, 1)
        if t != s:
            open(p, "w", errors="surrogateescape").write(t)
            print(f"E418: {os.path.basename(p)}: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
