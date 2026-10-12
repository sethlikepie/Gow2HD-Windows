#!/usr/bin/env python3
"""fps_window.py LOG [FIRST_LINE LAST_LINE MIN_DRAWS] -- from [FPS] lines: the first second of a
3-second run with >= MIN_DRAWS draws, and the fps/draws medians over [FPS] lines FIRST..LAST
(1-based) that have >= MIN_DRAWS draws. Read-only."""
import re
import statistics
import sys


def main():
    path = sys.argv[1]
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 70
    last = int(sys.argv[3]) if len(sys.argv) > 3 else 100
    min_draws = int(sys.argv[4]) if len(sys.argv) > 4 else 300
    rows = [l for l in open(path, errors="replace") if l.startswith('[FPS]')]
    fps = [int(re.search(r'fps=(\d+)', l).group(1)) for l in rows]
    draws = [int(re.search(r'draws=(\d+)', l).group(1)) for l in rows]
    start = None
    run = 0
    for i, d in enumerate(draws):
        run = run + 1 if d >= min_draws else 0
        if run == 3:
            start = i - 1  # 1-based index of the first second of the run
            break
    win = [(f, d) for f, d in zip(fps[first - 1:last], draws[first - 1:last]) if d >= min_draws]
    fmed = statistics.median([f for f, _ in win]) if win else None
    dmed = statistics.median([d for _, d in win]) if win else None
    print(f"lines={len(rows)} gameplay_from_fps#={start} window_n={len(win)} "
          f"fps_median={fmed} draws_median={dmed}")


if __name__ == "__main__":
    main()
