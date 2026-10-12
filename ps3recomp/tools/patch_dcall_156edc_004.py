#!/usr/bin/env python3
"""Replace func_00156EDC(ctx) call sites with func_001569E4(ctx) in one lift TU.

00248F3C's fragments in ppu_recomp_004.cpp make 262 hops through the
trampoline-only 00156EDC. Rewriting 00156EDC itself (all 45 trampoline
fragments) dropped fps 21→18. This only changes call sites in the hot TU.
Idempotent. NO-EFFECT if no call sites remain and none were converted.
"""
from __future__ import annotations

import argparse
from pathlib import Path

OLD = "func_00156EDC(ctx)"
NEW = "func_001569E4(ctx)"


def transform(text: str) -> tuple[str, str]:
    n_old = text.count(OLD)
    n_new = text.count(NEW)
    if n_old == 0 and n_new > 0:
        return text, "ALREADY"
    if n_old == 0:
        return text, "NO-EFFECT"
    out = text.replace(OLD, NEW)
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
        print(f"APPLIED {args.file.name} {original.count(OLD)}")
    else:
        print(f"{state} {args.file.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
