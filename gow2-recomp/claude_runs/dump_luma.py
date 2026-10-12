#!/usr/bin/env python3
"""dump_luma.py REF_FILE -- mean luma of every presented-frame dump (gow2-recomp/frame_<n>.bmp)
newer than REF_FILE, in frame order, one "frame=luma" per line. Read-only."""
import glob
import os
import re
import sys

from PIL import Image, ImageStat


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ref = os.path.abspath(sys.argv[1])
    if not ref.startswith(root + os.sep) or not os.path.isfile(ref):
        sys.exit("bad ref")
    t0 = os.path.getmtime(ref)
    rows = []
    for f in glob.glob(os.path.join(root, "frame_[0-9]*.bmp")):
        m = re.fullmatch(r"frame_([0-9]+)\.bmp", os.path.basename(f))
        if not m or os.path.getmtime(f) <= t0:
            continue
        im = Image.open(f).convert("L").resize((320, 180))
        rows.append((int(m.group(1)), ImageStat.Stat(im).mean[0]))
    print(" ".join(f"{n}={v:.0f}" for n, v in sorted(rows)))


if __name__ == "__main__":
    main()
