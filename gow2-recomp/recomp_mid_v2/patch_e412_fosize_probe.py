#!/usr/bin/env python3
"""E412 -- probe the FIOS op-completion handler (func_0030C7F8) where the console fills FO+0x48 (file size).

Console (oracle_fosize.py, 2026-09-14): on the FIOS scheduler thread, func_0030C7F8(sched, op, result) switches on
op+0xC8 (jump table @0x30C8F8); case 7 (0x30CA88) copies the stat into the file object and does
FO=*(op+0x98); FO+0x48 = *(op+0x80) (std @0x30CAE8): 0xC00 for R_LglScA (FO=0x33018528), 0x133C280 for R_PermA.
Ours (E411): the loader's sync open sees no size. This probe says, per dispatch of that switch, which case runs,
with which op state / status (r10 = *(result+4); nonzero -> func_0030CD1C) / FO / op+0x80, and prints the case-7
store itself.

The switch exists in three fragment copies (func_0030C828 in chunk 001, func_0030C87C and func_0030C8D0 in chunk
002); every copy is instrumented, each tagged with its function name.

DIAGNOSTIC ONLY: gated PS3_TRACE_FOSIZE (OFF by default), cap PS3_TRACE_FOSIZE_CAP (default 400). Reads guest memory,
writes nothing, touches no register. Idempotent (marker E412-FOSIZE). rc 0 ok / 2 no lift / 3 shape mismatch.
Usage: patch_e412_fosize_probe.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import re
import sys

MARK = "E412-FOSIZE"
SWITCH_HEAD = "        switch ((uint32_t)ctx->ctr) { case 0x0030C924u: goto loc_0030C924;"
CASE7 = "loc_0030CA88:\n"
STORE = "        vm_write64(ctx->gpr[4] + 0x48, ctx->gpr[0]);\n"

GATE = ('{ static int _on=-1, _cap=400, _n=0; if(_on<0){ extern char* getenv(const char*); '
        'const char* _e=getenv("PS3_TRACE_FOSIZE"); _on=(_e&&*_e&&*_e!=\'0\')?1:0; '
        'const char* _c=getenv("PS3_TRACE_FOSIZE_CAP"); if(_c&&*_c) _cap=atoi(_c); } '
        'if(_on && _n++<_cap){ ')


def disp_probe(fn: str) -> str:
    return ("        /* " + MARK + " dispatch */ " + GATE +
            'uint32_t _op=(uint32_t)ctx->gpr[31]; uint32_t _fo=_op?vm_read32(_op+0x98u):0u; '
            'fprintf(stderr,"[FOSIZE] disp fn=' + fn + ' tgt=0x%08X op=0x%08X st=%u status=0x%08X FO=0x%08X '
            'op80=0x%llX FO48=0x%llX\\n",(uint32_t)ctx->ctr,_op,_op?vm_read32(_op+0xC8u):0u,'
            '(uint32_t)ctx->gpr[10],_fo,_op?(unsigned long long)vm_read64(_op+0x80u):0ull,'
            '(_fo>=0x10000u&&_fo<0xF0000000u)?(unsigned long long)vm_read64(_fo+0x48u):0ull); fflush(stderr);} }\n')


def store_probe(fn: str) -> str:
    return ("        /* " + MARK + " case7-store */ " + GATE +
            'fprintf(stderr,"[FOSIZE] CASE7 fn=' + fn + ' FO=0x%08X FO+0x48 <- 0x%llX op=0x%08X\\n",'
            '(uint32_t)ctx->gpr[4],(unsigned long long)ctx->gpr[0],(uint32_t)ctx->gpr[31]); fflush(stderr);} }\n')


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e412_fosize_probe.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E412: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    texts = {p: open(p, errors="surrogateescape").read() for p in chunks}
    if any(MARK in s for s in texts.values()):
        print("E412: ALREADY")
        return 0
    n_disp = n_store = 0
    fn_rx = re.compile(r"^void (func_[0-9A-F]{8})\(ppu_context\* ctx\) \{$", re.M)
    for p, s in texts.items():
        if SWITCH_HEAD not in s:
            continue
        out, pos = [], 0
        for m in re.finditer(re.escape(SWITCH_HEAD), s):
            fns = fn_rx.findall(s, 0, m.start())
            fn = fns[-1] if fns else "?"
            out.append(s[pos:m.start()]); out.append(disp_probe(fn)); pos = m.start(); n_disp += 1
        s2 = "".join(out) + s[pos:]
        # case-7 store: the first STORE after each CASE7 label
        out, pos = [], 0
        for m in re.finditer(re.escape(CASE7), s2):
            k = s2.find(STORE, m.end())
            nxt = s2.find("\nloc_", m.end())
            if k < 0 or (0 <= nxt < k):
                print(f"E412: {os.path.basename(p)}: case-7 store not found after label", file=sys.stderr)
                return 3
            fns = fn_rx.findall(s2, 0, m.start())
            fn = fns[-1] if fns else "?"
            out.append(s2[pos:k]); out.append(store_probe(fn)); pos = k; n_store += 1
        s3 = "".join(out) + s2[pos:]
        open(p, "w", errors="surrogateescape").write(s3)
        print(f"E412: {os.path.basename(p)}: APPLIED")
    print(f"E412: dispatch probes={n_disp} case7-store probes={n_store} (expected 3/3)")
    return 0 if (n_disp == 3 and n_store == 3) else 3


if __name__ == "__main__":
    raise SystemExit(main())
