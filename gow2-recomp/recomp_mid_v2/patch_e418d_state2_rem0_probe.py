#!/usr/bin/env python3
"""E418d -- like E418c, but only records state-2 passes with rem == 0 (container end + the plateau).

E418c's change-log cap (3000) ran out mid-R_PermA (stream fields change every pass while streaming), so the
E417 plateau stayed invisible. Passes with rem (ts+0x1AC) == 0 happen only at the end of each container and
at the plateau, so a change-log restricted to them is small and reaches the state the driver waits on.

Stream object (E418c): st = *(ts+0x1A8) = 0x4007FCD0; +00 ring base, +04 container total bytes, +08 read
cursor, +0C ring size (0x40000), +10 bytes available. Done needs +10 == 0 with rem == 0 (R_LglScA: +10 goes
0xA0 -> 0x80 -> 0x40 -> 0 and the container closes).

Gated PS3_TRACE_ST2Z (OFF). Reads guest memory only. Apply on a lift WITHOUT patch_e418c (same call site).
Idempotent (marker E418d-ST2Z). rc 0 ok / 2 no lift / 3 no call site or e418c present.
Usage: patch_e418d_state2_rem0_probe.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import re
import sys

MARK = "E418d-ST2Z"
SITE = "ctx->lr = 0x002BA9C0; func_002B9F00(ctx); DRAIN_TRAMPOLINE(ctx);\n"
FN_RX = re.compile(r"^void (func_[0-9A-F]{8})\(ppu_context\* ctx\) \{$", re.M)


def probe(fn: str, indent: str) -> str:
    return (indent + "/* " + MARK + " */ { static int _on=-1; if(_on<0){ extern char* getenv(const char*); "
            "const char* _e=getenv(\"PS3_TRACE_ST2Z\"); _on=(_e&&*_e&&*_e!='0')?1:0; } "
            "if(_on){ const uint32_t _ts=0x00869570u; uint32_t _rem=vm_read32(_ts+0x1ACu); if(_rem==0u){ "
            "uint32_t _st=vm_read32(_ts+0x1A8u); uint32_t _v[12]; "
            "_v[0]=_st; for(int _i=0;_i<8;_i++) _v[1+_i]=(_st>=0x10000u&&_st<0xF0000000u)?vm_read32(_st+4u*_i):0u; "
            "_v[9]=_rem; _v[10]=vm_read32(_ts+0x1CCu); _v[11]=vm_read32(_ts+0x1C8u); "
            "static uint32_t _l[12]; static int _c=0; static unsigned long _n=0; _n++; int _ch=0; "
            "for(int _i=0;_i<12;_i++) if(_l[_i]!=_v[_i]){ _ch=1; _l[_i]=_v[_i]; } "
            "if((_ch && _c++<3000) || (_n%5000)==0){ fprintf(stderr,\"[ST2Z] " + fn +
            " #%lu%s st=0x%08X base=%08X total=%08X cursor=%08X ring=%08X avail=%08X +14=%08X +18=%08X +1C=%08X "
            "rem=0x%X state=%u f1C8=0x%X\\n\",_n,_ch?\" CHANGE\":\" beat\",_v[0],_v[1],_v[2],_v[3],_v[4],_v[5],_v[6],_v[7],_v[8],"
            "_v[9],_v[10],_v[11]); fflush(stderr);} } } }\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e418d_state2_rem0_probe.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E418d: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    texts = {p: open(p, errors="surrogateescape").read() for p in chunks}
    if any(MARK in s for s in texts.values()):
        print("E418d: ALREADY")
        return 0
    if any("E418c-ST2" in s for s in texts.values()):
        print("E418d: lift already has patch_e418c -- use a lift without it", file=sys.stderr)
        return 3
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
        print(f"E418d: {os.path.basename(p)}: APPLIED")
    if total == 0:
        print("E418d: call site not found", file=sys.stderr)
        return 3
    print(f"E418d: {total} call site(s) instrumented")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
