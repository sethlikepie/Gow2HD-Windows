#!/usr/bin/env python3
"""E410 -- 42EC must not trampoline 42B4 (that deletes the DONE op / zeros +8).

42EC writes rem from FO+0x48 then used to jump to 42B4, which calls 30AE58
(delete) and vm_write32(container+8, 0). BADF4 then sits in WADLD-SM state=1
(req=0xC00 inflight=0) because AREAD has no op. Keep the op: 42EC pops
4224's 0x90 frame and returns r3=1, same epilogue as 42B4 minus the delete.

Also stamp FO+0x48/+0x4C from movie_io_stat in 4274 *before* 42EC, because
natural file_new leaves that u64 at 0 (F2B-FO-SIZE only runs on reject).

Markers E410-42EC-KEEP-OP, E410-4274-FO-SIZE. rc 0/2/3.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARK = "E410-42EC-KEEP-OP"

NEEDLE_42EC = (
    "        ctx->gpr[11] = vm_read64(ctx->gpr[9] + 0x48);\n"
    "        vm_write32(ctx->gpr[31] + 0xC, ctx->gpr[0]);\n"
    "        vm_write32(ctx->gpr[31] + 0x10, ctx->gpr[11]);\n"
    "        { g_trampoline_fn = (void(*)(void*))func_002B42B4; return; }\n"
)
REPL_42EC = (
    "        ctx->gpr[11] = vm_read64(ctx->gpr[9] + 0x48);\n"
    "        vm_write32(ctx->gpr[31] + 0xC, ctx->gpr[0]);\n"
    "        vm_write32(ctx->gpr[31] + 0x10, ctx->gpr[11]);\n"
    "        /* E410-42EC-KEEP-OP: 42B4 deletes io; pop 4224 frame, keep +8 */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "          const char* _e=getenv(\"PS3_TRACE_BAD10\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          if(_on){ fprintf(stderr,\"[42EC] KEEP-OP rem=0x%08X io=0x%08X fo=0x%08X\\n\",\n"
    "            (uint32_t)ctx->gpr[11], vm_read32((uint32_t)ctx->gpr[31]+8u),\n"
    "            (uint32_t)ctx->gpr[9]); fflush(stderr);} }\n"
    "        { uint64_t _cs_31 = vm_read64(ctx->gpr[1] + 0x88);\n"
    "          ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xA0);\n"
    "          ctx->gpr[3] = (int64_t)(int32_t)(1);\n"
    "          ctx->gpr[31] = _cs_31;\n"
    "          ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x90);\n"
    "          ctx->gpr[3] = (int64_t)(int32_t)ctx->gpr[3];\n"
    "          ctx->lr = ctx->gpr[0];\n"
    "          return; }\n"
)

NEEDLE_FO = (
    "              fprintf(stderr,\"[FIOSOPEN] F2B-RESTATUS fo=0x%08X io=0x%08X was+44=0x%08X → 0\\n\",\n"
    "                _fo, _io, _was44);\n"
    "              fflush(stderr); } }\n"
    "          }\n"
    "        }\n"
)
REPL_FO = (
    "              fprintf(stderr,\"[FIOSOPEN] F2B-RESTATUS fo=0x%08X io=0x%08X was+44=0x%08X → 0\\n\",\n"
    "                _fo, _io, _was44);\n"
    "              fflush(stderr); } }\n"
    "          }\n"
    "        }\n"
    "        /* E410-4274-FO-SIZE: 42EC copies vm_read64(FO+0x48); fill if empty */\n"
    "        { uint32_t _c=(uint32_t)ctx->gpr[31]; uint32_t _fo=_c?vm_read32(_c+4u):0u;\n"
    "          if(_fo>=0x10000u && _fo<0x4F000000u && (uint32_t)vm_read64(_fo+0x48u)==0u){\n"
    "            uint32_t _p30=vm_read32(_fo+0x30u); char _pt[96]; _pt[0]=0;\n"
    "            if(_p30>=0x10000u && _p30<0x4F000000u){\n"
    "              for(int _i=0;_i<95;_i++){ unsigned char _ch=(unsigned char)vm_read8(_p30+_i);\n"
    "                _pt[_i]=(char)_ch; if(!_ch) break; }\n"
    "              _pt[95]=0; }\n"
    "            if(_pt[0]){\n"
    "              unsigned long long _st=movie_io_stat(_pt);\n"
    "              if(!_st){ const char* _b=_pt; for(const char* _s=_pt;*_s;_s++) if(*_s=='/') _b=_s+1;\n"
    "                _st=movie_io_stat(_b); }\n"
    "              if(_st && _st<0x10000000ull){\n"
    "                vm_write32(_fo+0x48u, 0u);\n"
    "                vm_write32(_fo+0x4Cu, (uint32_t)_st);\n"
    "              } } } }\n"
)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    texts: dict[Path, str] = {}
    for p in paths:
        if p.is_file():
            texts[p] = p.read_text(errors="replace")
    if not texts:
        print("E410: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    if sum(s.count(MARK) for s in texts.values()):
        print("E410: ALREADY")
        return 0

    n1 = sum(s.count(NEEDLE_42EC) for s in texts.values())
    n2 = sum(s.count(NEEDLE_FO) for s in texts.values())
    if n1 != 1:
        print(f"E410: 42EC needle {n1}x (esperado 1)", file=sys.stderr)
        return 3 if n1 else 2
    if n2 != 1:
        print(f"E410: 4274-FO needle {n2}x (esperado 1)", file=sys.stderr)
        return 3 if n2 else 2
    for p, s in list(texts.items()):
        ns = s.replace(NEEDLE_42EC, REPL_42EC, 1).replace(NEEDLE_FO, REPL_FO, 1)
        if ns != s:
            texts[p] = ns
            print(f"E410: {p.name}: APPLIED")
    for p, s in texts.items():
        if s != p.read_text(errors="replace"):
            p.write_text(s)
    print("E410: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
