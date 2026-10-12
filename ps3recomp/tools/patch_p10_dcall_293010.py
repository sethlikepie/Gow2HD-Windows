#!/usr/bin/env python3
"""Specialize func_00293010's 4 bctrl to sampled gameplay CTR targets.

Sampled (e435p10c, PS3_TRACE_P10_CTR=1, 130s, 1 unique/site, PADMENU=12):
  site0 0x00297310  site1 0x002BED34  site2 0x0029731C  site3 0x002BED40
Fallback remains ps3_indirect_call. Idempotent.
"""
from __future__ import annotations

import argparse
from pathlib import Path

START = "void func_00293010("
ICALL = "ps3_indirect_call(ctx)"
TARGETS = (0x00297310, 0x002BED34, 0x0029731C, 0x002BED40)


def _repl(ea: int) -> str:
    return (
        f"if ((uint32_t)ctx->ctr == 0x{ea:08X}u) {{ func_{ea:08X}(ctx); }} "
        f"else {{ ps3_indirect_call(ctx); }}"
    )


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    if all(f"func_{ea:08X}(ctx)" in body for ea in TARGETS) and ICALL in body:
        return text, "ALREADY"
    n = body.count(ICALL)
    if n != 4:
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
        out_parts.append(_repl(TARGETS[site]))
        site += 1
        k = p + len(ICALL)
    if site != 4:
        return text, "NO-EFFECT"
    new_body = "".join(out_parts)
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
        print(f"APPLIED {args.file.name}")
    else:
        print(f"{state} {args.file.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
