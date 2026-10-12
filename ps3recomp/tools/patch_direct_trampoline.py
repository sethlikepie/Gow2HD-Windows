#!/usr/bin/env python3
"""Rewrite trampoline-only lift fragments to a direct call.

Lifted `b` between fragments of the same function becomes:
    { g_trampoline_fn = (void(*)(void*))func_BBB; return; }
The caller then DRAIN_TRAMPOLINE-runs BBB. A fragment whose entire body is
that store+return can call BBB itself; the caller's drain still picks up a
nested trampoline. Idempotent.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

TRAMPOLINE_ONLY = re.compile(
    r"(void (func_[0-9A-Fa-f]+)\(ppu_context\* ctx\) \{\n)"
    r"(        \{ g_trampoline_fn = \(void\(\*\)\(void\*\)\)(func_[0-9A-Fa-f]+); return; \}\n)"
    r"(\})",
    re.M,
)

DIRECT = r"\1        \4(ctx);\n\5"


def transform(text: str) -> tuple[str, int]:
    out, n = TRAMPOLINE_ONLY.subn(DIRECT, text)
    return out, n


def patch_file(path: Path) -> int:
    original = path.read_text(encoding="utf-8", errors="replace")
    updated, n = transform(original)
    if n and updated != original:
        path.write_text(updated, encoding="utf-8")
    return n


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("root", type=Path, help="lift directory (e.g. recomp_macos_e435)")
    args = ap.parse_args()
    total = 0
    for p in sorted(args.root.glob("ppu_recomp_*.cpp")):
        n = patch_file(p)
        if n:
            print(f"{p.name}: {n}")
            total += n
    print(f"total {total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
