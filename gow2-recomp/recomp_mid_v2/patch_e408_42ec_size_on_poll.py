#!/usr/bin/env python3
"""E408 -- after 441C poll, copy fo+0x48 to container+0x10 if rem is still 0.

42EC (func_002B42EC) is the guest write: rem = *(fo+0x48), flags |= 0x10000.
It only runs from 4224's DONE path (4274 -> 6610 -> r3==0). When the sync
open leaves container+8=0, 4224 trampolines 4308 and 42EC never runs, so
BADF4 sees ts+0x74=0 and takes BADC0 (r3=0, rem=0, FREELIST).

Live r31 in 441C is the container (4340). Same stores as 42EC, only when
rem==0 and fo+0x48 looks like a size. Also prints rem/io/fo48 at BAD78.

Always-on copy (guest 42EC stores). Log gated PS3_TRACE_BAD10.
Markers E408-42EC-SIZE, E408-BAD78-REM. rc 0/2/3.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARK = "E408-42EC-SIZE"

NEEDLE_441C = (
    "        /* E407-441C-POLL: no pop -- caller 4340/439C is the epilogue */\n"
    "        ctx->gpr[3] = (int64_t)(int32_t)ctx->gpr[3];\n"
    "        return;\n"
)
REPL_441C = (
    "        /* E407-441C-POLL: no pop -- caller 4340/439C is the epilogue */\n"
    "        /* E408-42EC-SIZE: 42EC skipped when io=0; copy fo+0x48 -> rem */\n"
    "        { uint32_t _c=(uint32_t)ctx->gpr[31];\n"
    "          uint32_t _rem=_c?vm_read32(_c+0x10u):0u;\n"
    "          uint32_t _io=_c?vm_read32(_c+0x8u):0u;\n"
    "          uint32_t _fo=_c?vm_read32(_c+0x4u):0u;\n"
    "          uint32_t _fo48=(_fo>=0x10000u&&_fo<0x4F000000u)?(uint32_t)vm_read64(_fo+0x48u):0u;\n"
    "          uint32_t _f2bsz=_fo?f2b_fo_sz_get(_fo):0u;\n"
    "          uint32_t _sz=_fo48?_fo48:_f2bsz;\n"
    "          { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_BAD10\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "            if(_on){ fprintf(stderr,\"[441C] 42EC c=0x%08X io=0x%08X rem=0x%08X fo=0x%08X fo48=0x%08X f2b=0x%08X\\n\",\n"
    "              _c,_io,_rem,_fo,_fo48,_f2bsz); fflush(stderr);} }\n"
    "          if(_rem==0u && _sz!=0u && _sz<0x10000000u){\n"
    "            vm_write32(_c+0x10u, _sz);\n"
    "            vm_write32(_c+0xCu, vm_read32(_c+0xCu)|0x10000u);\n"
    "          } }\n"
    "        ctx->gpr[3] = (int64_t)(int32_t)ctx->gpr[3];\n"
    "        return;\n"
)

NEEDLE_BAD78 = (
    "          if(_on){ fprintf(stderr,\"[BAD10] after-4340 r3=0x%08X r28=0x%08X via=BAD78\\n\",\n"
    "            (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[28]); fflush(stderr);} }\n"
)
REPL_BAD78 = (
    "          if(_on){ uint32_t _ts=(uint32_t)ctx->gpr[31]; uint32_t _c=_ts+0x64u;\n"
    "            uint32_t _fo=vm_read32(_c+4u);\n"
    "            fprintf(stderr,\"[BAD10] after-4340 r3=0x%08X r28=0x%08X via=BAD78 rem=0x%08X io=0x%08X fo=0x%08X fo48=0x%08X\\n\",\n"
    "              (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[28],\n"
    "              vm_read32(_ts+0x74u), vm_read32(_c+8u), _fo,\n"
    "              (_fo>=0x10000u&&_fo<0x4F000000u)?(uint32_t)vm_read64(_fo+0x48u):0u); fflush(stderr);} }\n"
)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    texts: dict[Path, str] = {}
    for p in paths:
        if p.is_file():
            texts[p] = p.read_text(errors="replace")
    if not texts:
        print("E408: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    if sum(s.count(MARK) for s in texts.values()):
        print("E408: ALREADY")
        return 0

    n1 = sum(s.count(NEEDLE_441C) for s in texts.values())
    n2 = sum(s.count(NEEDLE_BAD78) for s in texts.values())
    if n1 != 1:
        print(f"E408: 441C needle {n1}x (esperado 1)", file=sys.stderr)
        return 3 if n1 else 2
    if n2 != 1:
        print(f"E408: BAD78 needle {n2}x (esperado 1)", file=sys.stderr)
        return 3 if n2 else 2
    for p, s in list(texts.items()):
        ns = s.replace(NEEDLE_441C, REPL_441C, 1).replace(NEEDLE_BAD78, REPL_BAD78, 1)
        if ns != s:
            texts[p] = ns
            print(f"E408: {p.name}: APPLIED")
    for p, s in texts.items():
        if s != p.read_text(errors="replace"):
            p.write_text(s)
    print("E408: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
