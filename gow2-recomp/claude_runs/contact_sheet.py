#!/usr/bin/env python3
"""contact_sheet.py OUT_NAME PREFIX [COLS] -- grid of the full presented frames
claude_runs/frames/<PREFIX>_frame_gp*_f*.png into claude_runs/frames/<OUT_NAME>.png,
each labelled by file order. Read-only on inputs."""
import glob
import os
import re
import sys

from PIL import Image, ImageDraw


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out, prefix = sys.argv[1], sys.argv[2]
    cols = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    if not re.fullmatch(r"[a-z0-9_]+", out) or not re.fullmatch(r"[a-z0-9_]+", prefix):
        sys.exit("bad names")
    if not 1 <= cols <= 6:
        sys.exit("bad cols")
    base = os.path.join(os.path.dirname(os.path.abspath(__file__)), "frames")
    files = sorted(glob.glob(os.path.join(base, prefix + "_frame_gp[0-9]*_f[0-9]*.png")))
    if not files:
        sys.exit("no frames")
    w, h = 640, 360
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new("RGB", (w * cols, h * rows), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)
    for i, f in enumerate(files):
        im = Image.open(f).convert("RGB").resize((w, h))
        x, y = (i % cols) * w, (i // cols) * h
        sheet.paste(im, (x, y))
        draw.text((x + 6, y + 6), os.path.basename(f).replace(prefix + "_frame_", ""), fill=(255, 255, 0))
    dst = os.path.join(base, out + ".png")
    sheet.save(dst)
    print(dst, len(files))


if __name__ == "__main__":
    main()
