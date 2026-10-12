#!/usr/bin/env python3
"""E413 -- 2B9F00: if inflight==0 but ring avail>=0x20, return (skip AREAD).

BA990 SKIP-POLL reaches state 2. 2B9F00 then 2B9F54 issues AREAD-HLE on a
container whose OPEN op was already 42B4-deleted; that call never returns.
RING-FILL already placed 3072 bytes (LglScA). Console consumes avail then
AREAD. If avail>=one header, skip 2B9F54 so BA9BC can parse.

Marker E413-SKIP-AREAD. Gate log PS3_TRACE_BAD10. rc 0/2/3.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARK = "E413-SKIP-AREAD"
NEEDLE = (
    "        ctx->gpr[4] = vm_read32(ctx->gpr[31] + 0x1A4);\n"
    "        { int64_t a = (int32_t)ctx->gpr[4]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_002B9F54; return; }\n"
)
REPL = (
    "        ctx->gpr[4] = vm_read32(ctx->gpr[31] + 0x1A4);\n"
    "        { int64_t a = (int32_t)ctx->gpr[4]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if (((ctx->cr >> 0) & 2)) {\n"
    "          /* E413-SKIP-AREAD: ring already has a header; do not AREAD-HLE */\n"
    "          uint32_t _st=vm_read32((uint32_t)ctx->gpr[31]+0x1A8u);\n"
    "          uint32_t _av=(_st>=0x10000u&&_st<0x4F000000u)?vm_read32(_st+0x10u):0u;\n"
    "          { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_BAD10\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "            if(_on){ fprintf(stderr,\"[2B9F00] inflight=0 av=%u%s\\n\", _av,\n"
    "              _av>=0x20u?\" SKIP-AREAD\":\" -> 2B9F54\"); fflush(stderr);} }\n"
    "          if(_av>=0x20u){\n"
    "            ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x90);\n"
    "            ctx->gpr[30] = _cs_30;\n"
    "            ctx->gpr[31] = _cs_31;\n"
    "            ctx->gpr[3] = (int64_t)(int32_t)(0);\n"
    "            ctx->lr = ctx->gpr[0];\n"
    "            ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x80);\n"
    "            return;\n"
    "          }\n"
    "          g_trampoline_fn = (void(*)(void*))func_002B9F54; return; }\n"
)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    texts: dict[Path, str] = {}
    for p in paths:
        if p.is_file():
            texts[p] = p.read_text(errors="replace")
    if not texts:
        print("E413: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    if sum(s.count(MARK) for s in texts.values()):
        print("E413: ALREADY")
        return 0
    n = sum(s.count(NEEDLE) for s in texts.values())
    if n != 1:
        print(f"E413: needle {n}x (esperado 1)", file=sys.stderr)
        return 3 if n else 2
    for p, s in list(texts.items()):
        if NEEDLE in s:
            texts[p] = s.replace(NEEDLE, REPL, 1)
            print(f"E413: {p.name}: APPLIED")
    for p, s in texts.items():
        if s != p.read_text(errors="replace"):
            p.write_text(s)
    print("E413: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
