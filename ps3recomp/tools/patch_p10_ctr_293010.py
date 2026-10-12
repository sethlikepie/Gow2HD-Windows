#!/usr/bin/env python3
"""Insert ps3_p10_ctr_note(site, ctx->ctr) before each bctrl in func_00293010.

Captures the live CTR (already OPD-resolved). Gated in the helper (OFF default).
Idempotent. Numbers sites 0..3 in source order.
"""
from __future__ import annotations

import argparse
from pathlib import Path

START = "void func_00293010("
ICALL = "ps3_indirect_call(ctx)"
NOTE = "ps3_p10_ctr_note("
DECL = 'extern "C" void ps3_indirect_call(ppu_context* ctx);\n'
DECL_NOTE = (
    'extern "C" void ps3_indirect_call(ppu_context* ctx);\n'
    'extern "C" void ps3_p10_ctr_note(unsigned site, uint32_t ctr);\n'
)


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    if NOTE in body and ICALL in body:
        n = 0
        k = 0
        while True:
            p = body.find(ICALL, k)
            if p < 0:
                break
            n += 1
            k = p + len(ICALL)
        if n == 4 and body.count(NOTE) == 4:
            out = text if "void ps3_p10_ctr_note" in text else text.replace(DECL, DECL_NOTE, 1)
            return out, "ALREADY"
    if ICALL not in body:
        return text, "NO-EFFECT"
    out_parts: list[str] = []
    k = 0
    site = 0
    while True:
        p = body.find(ICALL, k)
        if p < 0:
            out_parts.append(body[k:])
            break
        out_parts.append(body[k:p])
        out_parts.append(f"ps3_p10_ctr_note({site}, (uint32_t)ctx->ctr); ")
        out_parts.append(ICALL)
        site += 1
        k = p + len(ICALL)
    if site != 4:
        return text, "NO-EFFECT"
    new_body = "".join(out_parts)
    out = text[:i] + new_body + (text[j:] if j >= 0 else "")
    if "void ps3_p10_ctr_note" not in out:
        if DECL not in out:
            return text, "NO-EFFECT"
        out = out.replace(DECL, DECL_NOTE, 1)
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
        print(f"APPLIED {args.file.name}")
    else:
        print(f"{state} {args.file.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
