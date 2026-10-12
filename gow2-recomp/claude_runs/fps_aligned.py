#!/usr/bin/env python3
"""fps_aligned.py LOG [OFFSET_S LEN_S MIN_DRAWS] -- fps stats over a window ALIGNED to the
start of gameplay instead of to absolute log lines.

Why: PS3_PAD_AUTOSTART is wall-clock scripted, so a faster build reaches gameplay at a
different second and a fixed line window compares DIFFERENT SCENES. Two runs of the very
same binary measured 19.3 and 14.6 fps that way (2026-09-17). Gameplay starts at the first
second of a 3-second stretch with >= MIN_DRAWS draws; the window starts OFFSET_S after that.

Prints median/mean/stdev/p10/p90 of fps plus the median draw count -- compare the draw
medians first: if they differ by more than a few percent the runs are NOT the same scene and
the fps numbers are not comparable, whatever the harness did. Read-only.
"""
from __future__ import annotations

import re
import statistics
import sys


def rows(path):
    fps, draws = [], []
    for line in open(path, errors="replace"):
        if not line.startswith("[FPS]"):
            continue
        f = re.search(r"fps=(\d+)", line)
        d = re.search(r"draws=(\d+)", line)
        if f and d:
            fps.append(int(f.group(1)))
            draws.append(int(d.group(1)))
    return fps, draws


def gameplay_start(draws, min_draws):
    run = 0
    for i, d in enumerate(draws):
        run = run + 1 if d >= min_draws else 0
        if run == 3:
            return i - 2
    return None


def main() -> int:
    path = sys.argv[1]
    offset = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    length = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    min_draws = int(sys.argv[4]) if len(sys.argv) > 4 else 300

    fps, draws = rows(path)
    start = gameplay_start(draws, min_draws)
    if start is None:
        print(f"{path}: gameplay nunca comecou (nenhum trecho de 3 s com draws>={min_draws})")
        return 1
    lo = start + offset
    win = [(f, d) for f, d in zip(fps[lo:lo + length], draws[lo:lo + length]) if d >= min_draws]
    if len(win) < 5:
        print(f"{path}: janela curta demais ({len(win)} amostras)")
        return 1
    f = sorted(x[0] for x in win)
    d = sorted(x[1] for x in win)
    n = len(f)
    print(f"{path}: inicio_s={start} n={n} "
          f"fps mediana={statistics.median(f):.1f} media={statistics.mean(f):.2f} "
          f"dp={statistics.pstdev(f):.2f} p10={f[n // 10]} p90={f[(n * 9) // 10]} | "
          f"draws mediana={statistics.median(d):.0f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
