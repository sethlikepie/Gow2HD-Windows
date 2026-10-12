#!/usr/bin/env python3
"""E407 -- 441C is poll-only; 4340/439C run the epilogue so BAD78 after-DRAIN sees r3.

E406 made 4340/43DC *call* 441C, but 441C still popped 4340's 0x1A0 frame and
C-returned. The LglScA path is BAD10 -> BAE48 -> BAD78 -> 4340, so BAD10's
after-4340 probe never runs. BAD78's after-DRAIN is the live r3.

PPC: 441C is the 0x200 poll loop; 439C (and 4340's own tail) is the shared
epilogue. Call 441C without popping, then continue the epilogue with r3 set.

Always-on. Markers E407-441C-POLL, E407-441C-THEN-439C, E407-BAD78-RET.
rc 0/2/3.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARK = "E407-441C-POLL"

NEEDLE_EPI = (
    "            /* E406-441C-NO-BADF4: C-return to BAD10 after-DRAIN; that site\n"
    "             * trampolines BADF4 with live frame/r31 when r3!=0. */\n"
    "        }\n"
    "        ctx->gpr[28] = _cs_28;\n"
    "        ctx->gpr[29] = _cs_29;\n"
    "        ctx->gpr[30] = _cs_30;\n"
    "        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x1B0);\n"
    "        ctx->gpr[31] = _cs_31;\n"
    "        ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x1A0);\n"
    "        ctx->lr = ctx->gpr[0];\n"
    "        ctx->gpr[3] = (int64_t)(int32_t)ctx->gpr[3];\n"
    "        return;\n"
)
REPL_EPI = (
    "            /* E406-441C-NO-BADF4: C-return to BAD10 after-DRAIN; that site\n"
    "             * trampolines BADF4 with live frame/r31 when r3!=0. */\n"
    "        }\n"
    "        /* E407-441C-POLL: no pop -- caller 4340/439C is the epilogue */\n"
    "        ctx->gpr[3] = (int64_t)(int32_t)ctx->gpr[3];\n"
    "        return;\n"
)

NEEDLE_4340 = (
    "        if ((!((ctx->cr >> 0) & 2))) { /* E406-441C-CALL */ func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx); return; }\n"
)
REPL_4340 = (
    "        if ((!((ctx->cr >> 0) & 2))) { /* E407-441C-THEN-EPILOGUE */ func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx); }\n"
)

NEEDLE_BARE = (
    "        { /* E406-441C-CALL */ func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx); return; }\n"
)
REPL_BARE = (
    "        { /* E407-441C-THEN-439C */ func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx);\n"
    "          g_trampoline_fn = (void(*)(void*))func_002B439C; return; }\n"
)

NEEDLE_BAD78 = (
    "        vm_write32(ctx->gpr[31] + 0x1CC, ctx->gpr[0]);\n"
    "        ctx->lr = 0x002BADA4; func_002B4340(ctx); DRAIN_TRAMPOLINE(ctx);\n"
    "        /* nop */;\n"
    "        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[28], 0, 22, 22);\n"
)
REPL_BAD78 = (
    "        vm_write32(ctx->gpr[31] + 0x1CC, ctx->gpr[0]);\n"
    "        ctx->lr = 0x002BADA4; func_002B4340(ctx); DRAIN_TRAMPOLINE(ctx);\n"
    "        /* nop */;\n"
    "        /* E407-BAD78-RET */ { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "          const char* _e=getenv(\"PS3_TRACE_BAD10\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          if(_on){ fprintf(stderr,\"[BAD10] after-4340 r3=0x%08X r28=0x%08X via=BAD78\\n\",\n"
    "            (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[28]); fflush(stderr);} }\n"
    "        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[28], 0, 22, 22);\n"
)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    texts: dict[Path, str] = {}
    for p in paths:
        if p.is_file():
            texts[p] = p.read_text(errors="replace")
    if not texts:
        print("E407: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2

    if sum(s.count(MARK) for s in texts.values()):
        print("E407: ALREADY")
        return 0

    def _one(needle: str, repl: str, label: str, expect: int) -> int:
        n = sum(s.count(needle) for s in texts.values())
        if n != expect:
            print(f"E407: {label} {n}x (esperado {expect})", file=sys.stderr)
            return 3 if n else 2
        for p, s in list(texts.items()):
            if needle in s:
                texts[p] = s.replace(needle, repl)
                print(f"E407: {p.name}: {label} APPLIED")
        return 0

    for needle, repl, label, exp in (
        (NEEDLE_EPI, REPL_EPI, "poll-only", 1),
        (NEEDLE_4340, REPL_4340, "4340-fallthrough", 1),
        (NEEDLE_BARE, REPL_BARE, "43C0/43DC-439C", 2),
        (NEEDLE_BAD78, REPL_BAD78, "BAD78-probe", 1),
    ):
        rc = _one(needle, repl, label, exp)
        if rc:
            return rc

    for p, s in texts.items():
        if s != p.read_text(errors="replace"):
            p.write_text(s)
    print("E407: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
