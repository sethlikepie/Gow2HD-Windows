#!/usr/bin/env python3
"""Replace every ps3_indirect_call in func_00293010 with ps3_indirect_call_mono.

00293010 dominates gameplay samples (~51%). Four vtable bctrl; not TU-wide.
Idempotent. NO-EFFECT if the function/icall is missing.
"""
from __future__ import annotations

import argparse
from pathlib import Path

DECL = 'extern "C" void ps3_indirect_call(ppu_context* ctx);\n'
DECL_MONO = (
    'extern "C" void ps3_indirect_call(ppu_context* ctx);\n'
    'extern "C" void ps3_indirect_call_mono(ppu_context* ctx);\n'
)
START = "void func_00293010(ppu_context* ctx) {"
ICALL = "ps3_indirect_call(ctx)"
MONO = "ps3_indirect_call_mono(ctx)"


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    if MONO in body and ICALL not in body:
        out = text if "void ps3_indirect_call_mono" in text else text.replace(DECL, DECL_MONO, 1)
        return out, "ALREADY"
    n = body.count(ICALL)
    if n == 0:
        return text, "NO-EFFECT"
    new_body = body.replace(ICALL, MONO)
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
        i = original.find(START)
        j = original.find("\nvoid func_", i + len(START))
        body = original[i:j] if j >= 0 else original[i:]
        print(f"APPLIED {args.file.name} {body.count(ICALL)}")
    else:
        print(f"{state} {args.file.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
