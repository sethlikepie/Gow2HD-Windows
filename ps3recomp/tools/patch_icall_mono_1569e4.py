#!/usr/bin/env python3
"""Replace only func_001569E4's vtable bctrl with ps3_indirect_call_mono.

Not a TU-wide #define (that dropped fps 21→17). Idempotent.
ALREADY if the site is already mono; NO-EFFECT if the function/icall is missing.
"""
from __future__ import annotations

import argparse
from pathlib import Path

DECL = 'extern "C" void ps3_indirect_call(ppu_context* ctx);\n'
DECL_MONO = (
    'extern "C" void ps3_indirect_call(ppu_context* ctx);\n'
    'extern "C" void ps3_indirect_call_mono(ppu_context* ctx);\n'
)
START = "void func_001569E4(ppu_context* ctx) {"
ICALL = "ps3_indirect_call(ctx)"
MONO = "ps3_indirect_call_mono(ctx)"


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    if MONO in body:
        out = text if DECL_MONO in text or "ps3_indirect_call_mono" in text else text.replace(DECL, DECL_MONO, 1)
        return out, "ALREADY"
    if ICALL not in body:
        return text, "NO-EFFECT"
    new_body = body.replace(ICALL, MONO, 1)
    if body.count(ICALL) != 1:
        return text, "NO-EFFECT"
    out = text[:i] + new_body + (text[j:] if j >= 0 else "")
    if "void ps3_indirect_call_mono" not in out:
        if DECL not in out:
            return text, "NO-EFFECT"
        out = out.replace(DECL, DECL_MONO, 1)
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
