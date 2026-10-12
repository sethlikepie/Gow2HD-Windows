#!/usr/bin/env python3
"""Insert #include "ppu_icall_hot.h" after ppu_recomp.h in one lift TU.

Idempotent. Used so only ppu_recomp_000.cpp (func_001569E4 vtable bctrl)
gets the one-entry CTR cache, not a global ppu_lookup change.
"""
from __future__ import annotations

import argparse
from pathlib import Path

NEEDLE = (
    "#define DRAIN_TRAMPOLINE(ctx) do { \\\n"
    "    while (g_trampoline_fn) { \\\n"
    "        void(*_tf)(void*) = g_trampoline_fn; \\\n"
    "        g_trampoline_fn = 0; \\\n"
    "        _tf((void*)(ctx)); \\\n"
    "    } \\\n"
    "} while(0)\n"
)
INSERT = NEEDLE + '\n#include "ppu_icall_hot.h"\n'


def transform(text: str) -> tuple[str, str]:
    if '#include "ppu_icall_hot.h"' in text:
        return text, "ALREADY"
    if NEEDLE not in text:
        return text, "NO-EFFECT"
    return text.replace(NEEDLE, INSERT, 1), "APPLIED"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("file", type=Path)
    args = ap.parse_args()
    original = args.file.read_text(encoding="utf-8", errors="replace")
    updated, state = transform(original)
    if state == "NO-EFFECT":
        print(f"NO-EFFECT {args.file}")
        return 1
    if state == "APPLIED":
        args.file.write_text(updated, encoding="utf-8")
    print(f"{state} {args.file.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
