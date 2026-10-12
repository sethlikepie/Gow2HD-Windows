#!/usr/bin/env python3
"""Per-site icall cache in func_00293010 (4 bctrl, 4 last-hit slots).

Shared ps3_indirect_call_mono is one thread_local; these 4 vtable slots
alternate (OPD+0 then OPD+0xC on two objects). Idempotent.
"""
from __future__ import annotations

import argparse
from pathlib import Path

DECL = 'extern "C" void ps3_indirect_call(ppu_context* ctx);\n'
DECL_SITE = (
    'extern "C" void ps3_indirect_call(ppu_context* ctx);\n'
    'extern "C" void ps3_indirect_call_site(ppu_context* ctx, unsigned site);\n'
)
START = "void func_00293010(ppu_context* ctx) {"
ICALL = "ps3_indirect_call(ctx)"
SITE_PREFIX = "ps3_indirect_call_site(ctx, "


def _rewrite_body(body: str) -> str:
    out = []
    i = 0
    n = 0
    while True:
        k = body.find(ICALL, i)
        if k < 0:
            out.append(body[i:])
            break
        # already a site call? ICALL is a prefix of site? no, site is different
        out.append(body[i:k])
        out.append(f"ps3_indirect_call_site(ctx, {n})")
        n += 1
        i = k + len(ICALL)
    return "".join(out)


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    site_n = body.count(SITE_PREFIX)
    fat_n = body.count(ICALL)
    if site_n == 4 and fat_n == 0:
        out = text if "void ps3_indirect_call_site" in text else text.replace(DECL, DECL_SITE, 1)
        return out, "ALREADY"
    if fat_n == 0:
        return text, "NO-EFFECT"
    new_body = _rewrite_body(body)
    if new_body.count(SITE_PREFIX) != 4 or ICALL in new_body:
        return text, "NO-EFFECT"
    out = text[:i] + new_body + (text[j:] if j >= 0 else "")
    if "void ps3_indirect_call_site" not in out:
        if DECL not in out:
            return text, "NO-EFFECT"
        out = out.replace(DECL, DECL_SITE, 1)
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
