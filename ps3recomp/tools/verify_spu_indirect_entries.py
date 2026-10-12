#!/usr/bin/env python3
"""Fail when a lifted SPU indirect-branch target lacks a registered entry."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


_OBSERVED_TARGET_RE = re.compile(
    r"(?:unknown\s+LS\s+address|\btarget)\s*(?:[=:]\s*)?"
    r"0x([0-9a-fA-F]+)\b", re.IGNORECASE)


def addresses_from_observations(path: Path) -> set[int]:
    out: set[int] = set()
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        lower = line.lower()
        if ("indirect branch" in lower or "spuabort" in lower
                or lower.lstrip().startswith("target")):
            out.update(int(v, 16) & ~0x3 for v in _OBSERVED_TARGET_RE.findall(line))
    return out


def verify(source: str, prefix: str, observed: set[int]) -> list[int]:
    registered = {int(v, 16) for v in re.findall(
        r"\{\s*0x([0-9A-Fa-f]+)u?\s*,\s*" + re.escape(prefix) +
        r"spu_func_[0-9A-Fa-f]+", source)}
    emitted = {int(v, 16) for v in re.findall(
        r"ctx->pc\s*=\s*0x([0-9A-Fa-f]+)u?\s*;\s*spu_indirect_branch\(ctx\)", source)}
    return sorted((emitted | observed) - registered)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("lift_dir", type=Path)
    ap.add_argument("prefix", help="generated symbol prefix, e.g. spu0_")
    ap.add_argument("--observed", action="append", type=Path, default=[],
                    help="runtime log or tracked manifest of observed destinations")
    args = ap.parse_args(argv)
    source_path = args.lift_dir / "spu_recomp.c"
    source = source_path.read_text(encoding="utf-8")
    observed = set()
    for path in args.observed:
        observed.update(addresses_from_observations(path))
    missing = verify(source, args.prefix, observed)
    if missing:
        print("[spu-indirect-contract] FAIL %s: missing registrations: %s" %
              (source_path, ", ".join("0x%X" % a for a in missing)), file=sys.stderr)
        return 1
    print("[spu-indirect-contract] OK %s (%d observed)" %
          (source_path, len(observed)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
