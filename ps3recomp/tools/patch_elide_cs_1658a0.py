#!/usr/bin/env python3
"""Drop dead guest callee-save spills inside func_001658A0.

The epilogue restores r14–r31 from host _cs_* (not from the stack).
Keep backchain (r1-0x110), lr at 0x120, and TOC saves at 0x28 (icall ABI).
Idempotent. NO-EFFECT if the function is missing.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

START = "void func_001658A0(ppu_context* ctx) {"
KEEP_OFF = {"-0x110", "0x120", "0x28"}
SPILL = re.compile(
    r"^[ \t]*vm_write64\(ctx->gpr\[1\] \+ (0x[0-9A-Fa-f]+|-0x[0-9A-Fa-f]+), "
    r"ctx->gpr\[(1[4-9]|2[0-9]|3[01])\]\);\n",
    re.M,
)
MARKER = "/* ELIDE-CS-1658A0 */\n"


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    if MARKER in body:
        return text, "ALREADY"

    def repl(m: re.Match[str]) -> str:
        off = m.group(1)
        if off in KEEP_OFF:
            return m.group(0)
        return ""

    new_body, n = SPILL.subn(repl, body)
    if n == 0 or new_body == body:
        return text, "NO-EFFECT"
    # marker after the opening brace line
    brace = new_body.find("{\n")
    if brace < 0:
        return text, "NO-EFFECT"
    new_body = new_body[: brace + 2] + "        " + MARKER + new_body[brace + 2 :]
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
    print(f"{state} {args.file.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
