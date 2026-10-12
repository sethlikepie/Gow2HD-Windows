#!/usr/bin/env python3
"""In func_00292164, call func_00292198 directly instead of trampoline.

The prologue must still run (callee-save frame). Callers keep DRAIN for
further hops from 00292198. Idempotent. NO-EFFECT if the trampoline is missing.
"""
from __future__ import annotations

import argparse
from pathlib import Path

START = "void func_00292164("
TRAMP = "{ g_trampoline_fn = (void(*)(void*))func_00292198; return; }"
DIRECT = "func_00292198(ctx); return;"


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    if DIRECT in body and TRAMP not in body:
        return text, "ALREADY"
    n = body.count(TRAMP)
    if n != 1:
        return text, "NO-EFFECT"
    new_body = body.replace(TRAMP, DIRECT, 1)
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
