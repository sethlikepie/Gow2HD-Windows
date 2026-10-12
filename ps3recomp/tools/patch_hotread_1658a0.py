#!/usr/bin/env python3
"""Rewrite vm_read32 → vm_read32_hot only inside func_001658A0.

Sampled hot target of 00156D78's vtable icall (34 loads). Does not
#define vm_read32 for the whole TU and does not touch func_001569E4.
Idempotent. NO-EFFECT if missing.
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
INCLUDE = (
    DRAIN
    + "\n#define PPU_VM_HOT_NO_MACRO 1\n"
    + '#include "ppu_vm_hot.h"\n'
)
START = "void func_001658A0(ppu_context* ctx) {"


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    if "vm_read32_hot(" in body and "PPU_VM_HOT_NO_MACRO" in text:
        return text, "ALREADY"
    if "vm_read32(" not in body:
        return text, "NO-EFFECT"
    new_body = body.replace("vm_read32(", "vm_read32_hot(")
    out = text[:i] + new_body + (text[j:] if j >= 0 else "")
    if "PPU_VM_HOT_NO_MACRO" not in out:
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
