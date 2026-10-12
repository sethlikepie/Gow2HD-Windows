#!/usr/bin/env python3
"""Rewrite vm_write64 → vm_write64_hot only inside func_00159EE4.

8 r1 stack/callee-save spills on the sampled gameplay path (63 exclusive
hits). Does not touch the 14 vm_read32. Idempotent.
"""
from __future__ import annotations

import argparse
from pathlib import Path

DRAIN = (
    "#define DRAIN_TRAMPOLINE(ctx) do { \\\n"
    "    while (g_trampoline_fn) { \\\n"
    "        void(*_tf)(void*) = g_trampoline_fn; \\\n"
    "        g_trampoline_fn = 0; \\\n"
    "        _tf((void*)(ctx)); \\\n"
    "    } \\\n"
    "} while(0)\n"
)
DECL = 'extern "C" void vm_write64_hot(uint64_t addr, uint64_t val);\n'
INCLUDE = DRAIN + "\n" + DECL
START = "void func_00159EE4("


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    if "vm_write64_hot(" in body and "vm_write64_hot(uint64_t" in text:
        return text, "ALREADY"
    if "vm_write64(" not in body:
        return text, "NO-EFFECT"
    new_body = body.replace("vm_write64(", "vm_write64_hot(")
    out = text[:i] + new_body + (text[j:] if j >= 0 else "")
    if "void vm_write64_hot(uint64_t addr, uint64_t val);" not in out:
        if DRAIN not in out:
            return text, "NO-EFFECT"
        out = out.replace(DRAIN, INCLUDE, 1)
    return out, "APPLIED"


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
