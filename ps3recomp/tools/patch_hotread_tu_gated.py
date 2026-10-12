#!/usr/bin/env python3
"""Insert #include "ppu_vm_hot.h" after ppu_recomp.h in a lift TU.

The header rewrites vm_read32 → vm_read32_hot at preprocess time. The hot
path falls back to the fat accessor until g_ps3_vm_gameplay (draws>=400),
so boot stays on the full path. Writes stay fat. Idempotent. Not a
per-function accessor rewrite.
"""
from __future__ import annotations

import argparse
from pathlib import Path

NEEDLE = '#include "ppu_recomp.h"'
INSERT = '#include "ppu_vm_hot.h"\n'
MARKER = '#include "ppu_vm_hot.h"'


def transform(text: str) -> tuple[str, str]:
    if MARKER in text:
        return text, "ALREADY"
    i = text.find(NEEDLE)
    if i < 0:
        return text, "NO-EFFECT"
    j = i + len(NEEDLE)
    if j < len(text) and text[j] != "\n":
        # keep the rest of the line, insert after it
        nl = text.find("\n", j)
        if nl < 0:
            return text, "NO-EFFECT"
        j = nl
    out = text[:j] + "\n" + INSERT + text[j + 1 :]
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
