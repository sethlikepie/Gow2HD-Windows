#!/usr/bin/env python3
"""tile_surfs.py LOG_NAME OUT_NAME SURF [SURF...] -- 2-column grid of the surfaces
of the first dumped frame in claude_runs/<LOG_NAME>.log (+1 = the second dump frame).
Run from gow2-recomp. Surface names like s4, s8, s1. Read-only on inputs."""
import glob
import os
import re
import sys

from PIL import Image


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    log, out = sys.argv[1], sys.argv[2]
    if not re.fullmatch(r"[a-z0-9_]+", log) or not re.fullmatch(r"[a-z0-9_]+", out):
        sys.exit("bad names")
    surfs = sys.argv[3:]
    for s in surfs:
        if not re.fullmatch(r"s[0-9]{1,2}", s):
            sys.exit("bad surface name")
    with open(os.path.join("claude_runs", log + ".log"), errors="replace") as fh:
        m = re.search(r"dumped surf_f([0-9]+)_", fh.read())
    if not m:
        sys.exit("no dump frame")
    frame = int(m.group(1)) + 1
    w, h = 640, 360
    rows = (len(surfs) + 1) // 2
    img = Image.new("RGB", (w * 2 + 4, h * rows + 4 * (rows - 1)), (255, 0, 255))
    for i, s in enumerate(surfs):
        files = glob.glob("surf_f%d_%s_*.bmp" % (frame, s))
        if files:
            img.paste(Image.open(files[0]).convert("RGB").resize((w, h)), ((i % 2) * (w + 4), (i // 2) * (h + 4)))
    img.save(os.path.join("claude_runs", "frames", out + ".png"))
    print("frame", frame, "->", out + ".png")


if __name__ == "__main__":
    main()
