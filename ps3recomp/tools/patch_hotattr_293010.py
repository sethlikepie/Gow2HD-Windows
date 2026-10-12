#!/usr/bin/env python3
"""Mark func_00293010 with __attribute__((hot)) only.

The ~51% inclusive gameplay function. Not an accessor rewrite. Idempotent.
"""
from __future__ import annotations

import argparse
from pathlib import Path

START = "void func_00293010("
HOT = "void __attribute__((hot)) func_00293010("


def transform(text: str) -> tuple[str, str]:
    if HOT in text:
        return text, "ALREADY"
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    out = text[:i] + HOT + text[i + len(START) :]
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
