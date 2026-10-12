#!/usr/bin/env python3
"""E414 -- 2B9F00: when av<0x20 after RING-FILL of a small WAD (<=64KiB), EOF.

LglScA (0xC00) is fully prefilled. Parser drains av 3072→0 then 2B9F54 AREAD
hangs (OPEN op already deleted). rem 1AC stays 0xC00 so BADF4 never exits.
If req/size at ts+0x74 is a small WAD and the ring is empty, treat as EOF:
clear rem, return 0 so BA9BC/BADF4 can finish.

Marker E414-SMALLWAD-EOF. rc 0/2/3.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARK = "E414-SMALLWAD-EOF"
NEEDLE = (
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
REPL = (
    "          if(_av>=0x20u){\n"
    "            ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x90);\n"
    "            ctx->gpr[30] = _cs_30;\n"
    "            ctx->gpr[31] = _cs_31;\n"
    "            ctx->gpr[3] = (int64_t)(int32_t)(0);\n"
    "            ctx->lr = ctx->gpr[0];\n"
    "            ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x80);\n"
    "            return;\n"
    "          }\n"
    "          /* E414-SMALLWAD-EOF: whole LglScA was in the ring and parsed */\n"
    "          { uint32_t _req=vm_read32((uint32_t)ctx->gpr[31]+0x74u);\n"
    "            if(_req && _req<=65536u){\n"
    "              vm_write32((uint32_t)ctx->gpr[31]+0x1ACu, 0u);\n"
    "              { static int _on2=-1; if(_on2<0){ extern char* getenv(const char*);\n"
    "                const char* _e=getenv(\"PS3_TRACE_BAD10\"); _on2=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "                if(_on2){ fprintf(stderr,\"[2B9F00] SMALLWAD-EOF req=0x%08X av=%u\\n\", _req,_av); fflush(stderr);} }\n"
    "              ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x90);\n"
    "              ctx->gpr[30] = _cs_30;\n"
    "              ctx->gpr[31] = _cs_31;\n"
    "              ctx->gpr[3] = (int64_t)(int32_t)(0);\n"
    "              ctx->lr = ctx->gpr[0];\n"
    "              ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x80);\n"
    "              return; } }\n"
    "          g_trampoline_fn = (void(*)(void*))func_002B9F54; return; }\n"
)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    texts: dict[Path, str] = {}
    for p in paths:
        if p.is_file():
            texts[p] = p.read_text(errors="replace")
    if not texts:
        print("E414: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    if sum(s.count(MARK) for s in texts.values()):
        print("E414: ALREADY")
        return 0
    n = sum(s.count(NEEDLE) for s in texts.values())
    if n != 1:
        print(f"E414: needle {n}x (esperado 1)", file=sys.stderr)
        return 3 if n else 2
    for p, s in list(texts.items()):
        if NEEDLE in s:
            texts[p] = s.replace(NEEDLE, REPL, 1)
            print(f"E414: {p.name}: APPLIED")
    for p, s in texts.items():
        if s != p.read_text(errors="replace"):
            p.write_text(s)
    print("E414: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
