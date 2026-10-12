#!/usr/bin/env python3
"""E418c -- dump the loader's stream object right after the state-2 chunk poll (func_002BA76C @0x2BA9BC).

State-2 handler (static, 0x2BA9BC..0x2BAA38): after func_002B9F00, with st = *(ts+0x1A8):
  st+0x10 > 31          -> parse the next record header (needs 32 bytes)
  ts+0x1AC (rem) > 0    -> wait for more data
  st+0x10 != 0          -> wait (1..31 leftover bytes, nothing more to come)   <- suspected plateau cause
  state == 3 or ts+0x1C8 != 0 -> wait
  else r29 = 1 -> container done: close, state 0, tick returns 0 (driver case 22 can proceed).
E417 plateau: state 2, rem 0, f1C8 0, driver stuck in case 22 -> the only remaining gate is st+0x10 != 0.

Prints (gated PS3_TRACE_ST2, OFF) on every CHANGE of (st, st+0x00..0x1C, ts+0x1AC, ts+0x1CC, ts+0x1C8),
cap 3000, plus a heartbeat every 20000 passes. Reads guest memory only. Every copy of the call site in the
lift is instrumented (fragment duplicates), each tagged with its function. Idempotent (marker E418c-ST2).
rc 0 ok / 2 no lift / 3 no call site found.
Usage: patch_e418c_state2_stream_probe.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import re
import sys

MARK = "E418c-ST2"
SITE = "ctx->lr = 0x002BA9C0; func_002B9F00(ctx); DRAIN_TRAMPOLINE(ctx);\n"
FN_RX = re.compile(r"^void (func_[0-9A-F]{8})\(ppu_context\* ctx\) \{$", re.M)


def probe(fn: str, indent: str) -> str:
    return (indent + "/* " + MARK + " */ { static int _on=-1; if(_on<0){ extern char* getenv(const char*); "
            "const char* _e=getenv(\"PS3_TRACE_ST2\"); _on=(_e&&*_e&&*_e!='0')?1:0; } "
            "if(_on){ const uint32_t _ts=0x00869570u; uint32_t _st=vm_read32(_ts+0x1A8u); uint32_t _v[12]; "
            "_v[0]=_st; for(int _i=0;_i<8;_i++) _v[1+_i]=(_st>=0x10000u&&_st<0xF0000000u)?vm_read32(_st+4u*_i):0u; "
            "_v[9]=vm_read32(_ts+0x1ACu); _v[10]=vm_read32(_ts+0x1CCu); _v[11]=vm_read32(_ts+0x1C8u); "
            "static uint32_t _l[12]; static int _c=0; static unsigned long _n=0; _n++; int _ch=0; "
            "for(int _i=0;_i<12;_i++) if(_l[_i]!=_v[_i]){ _ch=1; _l[_i]=_v[_i]; } "
            "if((_ch && _c++<3000) || (_n%20000)==0){ fprintf(stderr,\"[ST2] " + fn +
            " #%lu%s st=0x%08X +00=%08X +04=%08X +08=%08X +0C=%08X avail(+10)=%08X +14=%08X +18=%08X +1C=%08X "
            "rem=0x%X state=%u f1C8=0x%X\\n\",_n,_ch?\" CHANGE\":\" beat\",_v[0],_v[1],_v[2],_v[3],_v[4],_v[5],_v[6],_v[7],_v[8],"
            "_v[9],_v[10],_v[11]); fflush(stderr);} } }\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e418c_state2_stream_probe.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E418c: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    texts = {p: open(p, errors="surrogateescape").read() for p in chunks}
    if any(MARK in s for s in texts.values()):
        print("E418c: ALREADY")
        return 0
    total = 0
    for p, s in texts.items():
        if SITE not in s:
            continue
        out, pos = [], 0
        for m in re.finditer(r"([ \t]*)" + re.escape(SITE), s):
            fns = FN_RX.findall(s, 0, m.start())
            fn = fns[-1] if fns else "?"
            out.append(s[pos:m.end()]); out.append(probe(fn, m.group(1))); pos = m.end(); total += 1
        open(p, "w", errors="surrogateescape").write("".join(out) + s[pos:])
        print(f"E418c: {os.path.basename(p)}: APPLIED")
    if total == 0:
        print("E418c: call site not found", file=sys.stderr)
        return 3
    print(f"E418c: {total} call site(s) instrumented")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
