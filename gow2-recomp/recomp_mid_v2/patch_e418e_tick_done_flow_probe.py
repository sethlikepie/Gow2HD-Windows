#!/usr/bin/env python3
"""E418e -- trace the loader tick's "done" flow at the E417 plateau.

E418d: at the plateau the state-2 handler sees every done condition true (rem 0, stream avail 0, state 2,
f1C8 0), yet the loader stays in state 2 and driver case 22 keeps waiting. Static reading of the lifted
fragments (func_002BA9BC sets r29=1 and trampolines to func_002BA824; func_002BA824/808/7F0 test r29 at
loc_002BA854 before any _cs_ restore; the done path closes the container and writes state 0) says it should
work. This probe measures, rate-limited and change-based:
  A  `li r29,1` in func_002BA9BC executes           [TICK] set-r29 (cap 64, then 1/5000)
  B  r29/r26/r27 at loc_002BA854 in each fragment    [TICK] test-r29 fn=... (on change, cap 400)
  C  the done path writes state 0 (after func_002B3F78)  [TICK] done fn=... (cap 64)
  D  r3 returned by the tick to func_0003F2B0        [TICK] ret r3=... (on change, cap 400)
Gated PS3_TRACE_TICK2 (OFF). Reads registers/guest memory only. Idempotent (marker E418e-TICK).
rc 0 ok / 2 no lift / 3 a needle is missing.
Usage: patch_e418e_tick_done_flow_probe.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import re
import sys

MARK = "E418e-TICK"
GATE = ('static int _on=-1; if(_on<0){ extern char* getenv(const char*); '
        'const char* _e=getenv("PS3_TRACE_TICK2"); _on=(_e&&*_e&&*_e!=\'0\')?1:0; } ')
FN_RX = re.compile(r"^void (func_[0-9A-F]{8})\(ppu_context\* ctx\) \{$", re.M)

SET_R29 = "        ctx->gpr[29] = (int64_t)(int32_t)(1);\n"
TEST_R29 = "loc_002BA854:\n        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[29], 0, 24, 31);\n"
DONE = ("        ctx->lr = 0x002BA888; func_002B3F78(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n"
        "        ctx->gpr[0] = (int64_t)(int32_t)(0);\n        vm_write32(ctx->gpr[31] + 0x1CC, ctx->gpr[0]);\n")
RET = "ctx->lr = 0x0003F2C0; func_002BA76C(ctx); DRAIN_TRAMPOLINE(ctx);\n"


def p_set() -> str:
    return ("        /* " + MARK + " A */ { " + GATE + "if(_on){ static unsigned long _n=0; _n++; "
            "if(_n<=64 || (_n%5000)==0){ fprintf(stderr,\"[TICK] set-r29 #%lu state=%u avail=0x%X rem=0x%X\\n\",_n,"
            "vm_read32((uint32_t)ctx->gpr[31]+0x1CCu),"
            "vm_read32(vm_read32((uint32_t)ctx->gpr[31]+0x1A8u)+0x10u),vm_read32((uint32_t)ctx->gpr[31]+0x1ACu)); fflush(stderr);} } }\n")


def p_test(fn: str) -> str:
    return ("        /* " + MARK + " B */ { " + GATE + "if(_on){ static uint64_t _l29=~0ull,_l27=~0ull; static int _c=0; static unsigned long _n=0; _n++; "
            "if((ctx->gpr[29]!=_l29 || ctx->gpr[27]!=_l27) && _c++<400){ _l29=ctx->gpr[29]; _l27=ctx->gpr[27]; "
            "fprintf(stderr,\"[TICK] test-r29 fn=" + fn + " #%lu r29=0x%llX r27=0x%llX r26=0x%llX state=%u\\n\",_n,"
            "(unsigned long long)ctx->gpr[29],(unsigned long long)ctx->gpr[27],(unsigned long long)ctx->gpr[26],"
            "vm_read32((uint32_t)ctx->gpr[31]+0x1CCu)); fflush(stderr);} } }\n")


def p_done(fn: str) -> str:
    return ("        /* " + MARK + " C */ { " + GATE + "if(_on){ static int _c=0; if(_c++<64){ "
            "fprintf(stderr,\"[TICK] done fn=" + fn + " #%d -> state 0, return 0\\n\",_c); fflush(stderr);} } }\n")


def p_ret(indent: str) -> str:
    return (indent + "/* " + MARK + " D */ { " + GATE + "if(_on){ static uint64_t _l=~0ull; static int _c=0; static unsigned long _n=0; _n++; "
            "if((ctx->gpr[3]!=_l && _c++<400) || (_n%20000)==0){ _l=ctx->gpr[3]; "
            "fprintf(stderr,\"[TICK] ret #%lu r3=0x%llX state=%u\\n\",_n,(unsigned long long)ctx->gpr[3],"
            "vm_read32(0x00869570u+0x1CCu)); fflush(stderr);} } }\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e418e_tick_done_flow_probe.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E418e: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    texts = {p: open(p, errors="surrogateescape").read() for p in chunks}
    if any(MARK in s for s in texts.values()):
        print("E418e: ALREADY")
        return 0
    counts = {"A": 0, "B": 0, "C": 0, "D": 0}
    for p in list(texts):
        s = texts[p]
        # A: the first `li r29,1` inside func_002BA9BC's body
        head = "void func_002BA9BC(ppu_context* ctx) {\n"
        i = s.find(head)
        if i >= 0:
            end = s.find("\n}\n", i)
            k = s.find(SET_R29, i, end)
            if k >= 0:
                s = s[:k + len(SET_R29)] + p_set() + s[k + len(SET_R29):]
                counts["A"] += 1
        # B and C: every fragment copy, tagged with its function
        for needle, maker, key in ((TEST_R29, p_test, "B"), (DONE, p_done, "C")):
            out, pos = [], 0
            for m in re.finditer(re.escape(needle), s):
                fns = FN_RX.findall(s, 0, m.start())
                fn = fns[-1] if fns else "?"
                out.append(s[pos:m.end()]); out.append(maker(fn)); pos = m.end(); counts[key] += 1
            s = "".join(out) + s[pos:]
        # D: after the tick call in func_0003F2B0
        out, pos = [], 0
        for m in re.finditer(r"([ \t]*)" + re.escape(RET), s):
            out.append(s[pos:m.end()]); out.append(p_ret(m.group(1))); pos = m.end(); counts["D"] += 1
        s = "".join(out) + s[pos:]
        if s != texts[p]:
            open(p, "w", errors="surrogateescape").write(s)
            print(f"E418e: {os.path.basename(p)}: APPLIED")
    print(f"E418e: sites A={counts['A']} B={counts['B']} C={counts['C']} D={counts['D']}")
    if counts["A"] != 1 or counts["B"] < 1 or counts["C"] < 1 or counts["D"] != 1:
        print("E418e: a needle is missing or ambiguous", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
