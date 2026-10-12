#!/usr/bin/env python3
"""E406 -- 441C C-returns to BAD10 after-DRAIN; do not trampoline BADF4.

441C is 4340's poll+epilogue. A trampoline-return from 4340/43DC/43C0 into
441C is drained by BAD10's DRAIN after `bl 4340`. If 441C then sets
g_trampoline_fn=BADF4, DRAIN runs the tick loop (and BAE20 pops BAD10's
frame) *before* BAD10's after-DRAIN, so the live r3!=0 -> BADF4 path never
sees the open result.

Fix:
  1. Drop the BADF4 handoff inside 441C (BAD10 after-DRAIN owns it).
  2. Turn the three trampoline-to-441C sites into call+DRAIN+return so a
     leftover trampoline inside 441C cannot steal BAD10's DRAIN.

Always-on. Markers E406-441C-NO-BADF4, E406-441C-CALL.
rc: 0 applied/already; 2 needle 0; 3 needle != expected unique count.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARK_NO = "E406-441C-NO-BADF4"
MARK_CALL = "E406-441C-CALL"

NEEDLE_BADF4 = (
    "            if (ctx->gpr[3]) {\n"
    "                /* 441C is 4340's epilogue via trampoline; BAD10's after-DRAIN\n"
    "                 * (r3!=0 -> BADF4 sync ticks) does not run. Hand off to BADF4. */\n"
    "                g_trampoline_fn = (void(*)(void*))func_002BADF4;\n"
    "            }\n"
)
REPL_BADF4 = (
    "            /* E406-441C-NO-BADF4: C-return to BAD10 after-DRAIN; that site\n"
    "             * trampolines BADF4 with live frame/r31 when r3!=0. */\n"
)

NEEDLE_4340 = (
    "        if ((!((ctx->cr >> 0) & 2))) { g_trampoline_fn = (void(*)(void*))func_002B441C; return; }\n"
)
REPL_4340 = (
    "        if ((!((ctx->cr >> 0) & 2))) { /* E406-441C-CALL */ func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx); return; }\n"
)

NEEDLE_BARE = (
    "        { g_trampoline_fn = (void(*)(void*))func_002B441C; return; }\n"
)
REPL_BARE = (
    "        { /* E406-441C-CALL */ func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx); return; }\n"
)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    texts: dict[Path, str] = {}
    for p in paths:
        if p.is_file():
            texts[p] = p.read_text(errors="replace")
    if not texts:
        print("E406: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2

    already_call = sum(s.count(MARK_CALL) for s in texts.values())
    already_no = sum(s.count(MARK_NO) for s in texts.values())
    if already_call and already_no:
        print("E406: ALREADY")
        return 0

    n4340 = sum(s.count(NEEDLE_4340) for s in texts.values())
    nbare = sum(s.count(NEEDLE_BARE) for s in texts.values())
    nbadf4 = sum(s.count(NEEDLE_BADF4) for s in texts.values())

    if already_call == 0:
        if n4340 != 1:
            print(f"E406: 4340-call needle {n4340}x (esperado 1)", file=sys.stderr)
            return 3 if n4340 else 2
        if nbare != 2:
            print(f"E406: 43C0/43DC needle {nbare}x (esperado 2)", file=sys.stderr)
            return 3 if nbare else 2
        for p, s in list(texts.items()):
            if NEEDLE_4340 in s or NEEDLE_BARE in s:
                s = s.replace(NEEDLE_4340, REPL_4340, 1)
                s = s.replace(NEEDLE_BARE, REPL_BARE)
                texts[p] = s
                print(f"E406: {p.name}: CALL APPLIED")

    if already_no == 0:
        if nbadf4 == 0:
            # e405-only lift never had the handoff; still stamp the marker
            # next to the force-boolean so a later re-apply is ALREADY.
            stamped = False
            needle_bool = (
                "            ctx->gpr[3] = (_p4 != 0u || _p10 != 0u) ? 1 : 0;\n"
            )
            repl_bool = needle_bool + REPL_BADF4
            nbool = sum(s.count(needle_bool) for s in texts.values())
            if nbool != 1:
                print(f"E406: BADF4 handoff 0x and boolean {nbool}x (esperado 1)", file=sys.stderr)
                return 3 if nbool else 2
            for p, s in list(texts.items()):
                if needle_bool in s:
                    texts[p] = s.replace(needle_bool, repl_bool, 1)
                    print(f"E406: {p.name}: NO-BADF4 MARKED (handoff ausente)")
                    stamped = True
                    break
            if not stamped:
                print("E406: FAILED to stamp NO-BADF4", file=sys.stderr)
                return 2
        elif nbadf4 != 1:
            print(f"E406: BADF4 handoff {nbadf4}x (esperado 1)", file=sys.stderr)
            return 3
        else:
            for p, s in list(texts.items()):
                if NEEDLE_BADF4 in s:
                    texts[p] = s.replace(NEEDLE_BADF4, REPL_BADF4, 1)
                    print(f"E406: {p.name}: NO-BADF4 APPLIED")
                    break

    for p, s in texts.items():
        orig = p.read_text(errors="replace")
        if s != orig:
            p.write_text(s)
    print("E406: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
