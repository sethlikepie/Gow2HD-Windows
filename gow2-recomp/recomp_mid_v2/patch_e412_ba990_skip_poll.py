#!/usr/bin/env python3
"""E412 -- BA990: if open already consumed (+8=0) but req/size set, go state 2.

KEEP-OP left the OPEN op in container+8, so 2B9F54 AREAD-HLE used a DONE
open handle and hung. Console: 42EC→42B4 deletes +8, then BA990's 4224
sees no op. 4308 returns 0 (busy) and we never reached state 2.

Restore 42EC→42B4 (delete OPEN op). In BA990, if io==0 and ts+0x74!=0
the sync open already completed: write rem, state=2, trampoline BA9BC
without polling 4224.

Markers E412-BA990-SKIP-POLL, E412-42EC-RESTORE. rc 0/2/3.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARK = "E412-BA990-SKIP-POLL"

NEEDLE_BA990 = (
    "        ctx->gpr[3] = ctx->gpr[31] + (int64_t)(0x64);\n"
    "        ctx->lr = 0x002BA998; func_002B4224(ctx); DRAIN_TRAMPOLINE(ctx);\n"
)
REPL_BA990 = (
    "        ctx->gpr[3] = ctx->gpr[31] + (int64_t)(0x64);\n"
    "        /* E412-BA990-SKIP-POLL: open consumed (+8=0) but size at +0x74 */\n"
    "        { uint32_t _ts=(uint32_t)ctx->gpr[31];\n"
    "          uint32_t _io=vm_read32(_ts+0x6Cu);\n"
    "          uint32_t _req=vm_read32(_ts+0x74u);\n"
    "          if(!_io && _req){\n"
    "            vm_write32(_ts+0x1ACu, _req);\n"
    "            vm_write32(_ts+0x1CCu, 2u);\n"
    "            vm_write32(_ts+0x1D0u, _req);\n"
    "            { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "              const char* _e=getenv(\"PS3_TRACE_BAD10\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "              if(_on){ fprintf(stderr,\"[BA990] SKIP-POLL req=0x%08X -> state 2\\n\", _req); fflush(stderr);} }\n"
    "            g_trampoline_fn = (void(*)(void*))func_002BA9BC; return; } }\n"
    "        ctx->lr = 0x002BA998; func_002B4224(ctx); DRAIN_TRAMPOLINE(ctx);\n"
)

# Restore 42EC→42B4. E410 KEEP-OP block (unique).
NEEDLE_42EC = (
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
REPL_42EC = (
    "        /* E412-42EC-RESTORE: console deletes the OPEN op here (42B4) */\n"
    "        { g_trampoline_fn = (void(*)(void*))func_002B42B4; return; }\n"
)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    texts: dict[Path, str] = {}
    for p in paths:
        if p.is_file():
            texts[p] = p.read_text(errors="replace")
    if not texts:
        print("E412: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    if sum(s.count(MARK) for s in texts.values()):
        print("E412: ALREADY")
        return 0

    n1 = sum(s.count(NEEDLE_BA990) for s in texts.values())
    n2 = sum(s.count(NEEDLE_42EC) for s in texts.values())
    if n1 != 1:
        print(f"E412: BA990 needle {n1}x (esperado 1)", file=sys.stderr)
        return 3 if n1 else 2
    if n2 != 1:
        print(f"E412: 42EC restore needle {n2}x (esperado 1)", file=sys.stderr)
        return 3 if n2 else 2
    for p, s in list(texts.items()):
        ns = s.replace(NEEDLE_BA990, REPL_BA990, 1).replace(NEEDLE_42EC, REPL_42EC, 1)
        if ns != s:
            texts[p] = ns
            print(f"E412: {p.name}: APPLIED")
    for p, s in texts.items():
        if s != p.read_text(errors="replace"):
            p.write_text(s)
    print("E412: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
