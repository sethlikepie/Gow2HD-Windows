#!/usr/bin/env python3
"""Promote observed spu0 indirect targets without replacing its stable lift.

The production spu0_v2 lift is kept stable because a whole-image re-lift can
change gameplay timing.  The captured workload ELF is re-lifted in a temporary
directory with every observed ``bi``/``bisl`` destination, then only those
entries (and their direct dependencies) are imported into spu0_v2.  The static
manifest is always included; ``SPU0_OBSERVED_LOG`` adds fresh runtime evidence
to the same generation step.  This makes promotion part of the build pipeline
instead of a hand-maintained list of one-off addresses.
"""
from __future__ import annotations

import pathlib
import os
import re
import subprocess
import sys
import tempfile


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
ENGINE = ROOT.parent / "ps3recomp"
DEFAULT = ROOT / "spu_lifted" / "spu0_v2"
FRESH = ROOT / "spu_lifted" / "spu0_v3" / "spu_recomp.c"
INPUT = ROOT / "spu_hit_de6dc3a5ea2be487.bin"
OBSERVED = HERE / "spu0_observed_indirect_targets.lst"


def observed_paths() -> list[pathlib.Path]:
    paths = [OBSERVED]
    runtime_log = os.environ.get("SPU0_OBSERVED_LOG")
    if runtime_log:
        paths.append(pathlib.Path(runtime_log).resolve())
    return paths


def observed_targets(paths: list[pathlib.Path]) -> list[str]:
    targets: set[int] = set()
    pattern = re.compile(
        r"(?:unknown\s+LS\s+address|\btarget)\s*(?:[=:]\s*)?"
        r"0x([0-9a-fA-F]+)\b", re.IGNORECASE)
    for path in paths:
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            lower = line.lower()
            if ("indirect branch" not in lower and "spuabort" not in lower
                    and not lower.lstrip().startswith("target")):
                continue
            targets.update(int(value, 16) & ~0x3 for value in pattern.findall(line))
    if not targets:
        raise ValueError("no observed spu0 indirect targets")
    return [f"{target:X}" for target in sorted(targets)]


def main() -> int:
    target = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT
    if not INPUT.is_file() and not FRESH.is_file():
        print(f"missing captured spu0 image and reference lift: {INPUT}", file=sys.stderr)
        return 2
    try:
        paths = observed_paths()
        targets = observed_targets(paths)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="spu0-observed-") as fresh:
        fresh_c = pathlib.Path(fresh) / "spu_recomp.c"
        if INPUT.is_file():
            lift = subprocess.run([
                sys.executable, str(ENGINE / "tools" / "spu_lifter.py"),
                "--auto-functions", str(INPUT),
                *sum((["--observed-indirect-targets", str(path)] for path in paths), []),
                "--symbol-prefix", "spu0_", "-o", fresh,
            ], check=False)
            if lift.returncode:
                return lift.returncode
        else:
            # Keep the existing reference-lift fallback for checkouts that do
            # not carry the captured image, while still importing every target
            # found in the manifest/log.
            fresh_c = FRESH
        return subprocess.run([
            sys.executable, str(ENGINE / "tools" / "patch_spu_add_entries.py"),
            str(target), str(fresh_c), "spu0_", ",".join(targets),
        ], check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
