#!/usr/bin/env python3
"""Specialize func_00156D78 vtable bctrl: CTR==0x001658A0 → direct call.

Sample: ~half of 00156D78's icalls land on func_001658A0. A direct call is
not the TLS 1-entry cache (mono) and not an accessor. Other CTRs still go
through ps3_indirect_call. Idempotent.
"""
from __future__ import annotations

import argparse
from pathlib import Path

START = "void func_00156D78(ppu_context* ctx) {"
OLD = "        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
NEW = (
    "        if ((uint32_t)ctx->ctr == 0x001658A0u) {"
    " func_001658A0(ctx); DRAIN_TRAMPOLINE(ctx); }\n"
    "        else { ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx); }\n"
)


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    if "0x001658A0u" in body:
        return text, "ALREADY"
    n = body.count(OLD)
    if n == 0:
        return text, "NO-EFFECT"
    new_body = body.replace(OLD, NEW)
    out = text[:i] + new_body + (text[j:] if j >= 0 else "")
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
        print(f"APPLIED {args.file.name} {body.count(OLD)}")
    else:
        print(f"{state} {args.file.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
