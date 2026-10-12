"""Draws the launcher icon from simple shapes (our own artwork, nothing from the game).

A dark tile with a thin bronze frame and a crimson omega built from an arc and two feet.
Usage: python make_icon.py <out.ico> [<preview.png>]
"""
import sys
from PIL import Image, ImageDraw

SS = 4          # supersampling
N = 256 * SS

BG = (18, 15, 13, 255)
BRONZE = (176, 141, 87, 255)
RED = (176, 36, 28, 255)


def draw():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = 36 * SS
    d.rounded_rectangle([0, 0, N - 1, N - 1], radius=r, fill=BG)
    inset = 14 * SS
    d.rounded_rectangle([inset, inset, N - 1 - inset, N - 1 - inset], radius=r - inset // 2,
                        outline=BRONZE, width=3 * SS)
    # omega: thick arc open at the bottom, then two horizontal feet
    cx, cy = N // 2, int(N * 0.47)
    R = int(N * 0.27)
    w = int(N * 0.085)
    d.arc([cx - R, cy - R, cx + R, cy + R], start=125, end=415, fill=RED, width=w)
    import math
    foot_y = int(cy + R * math.sin(math.radians(55)) + w * 0.15)
    for side in (-1, 1):
        ex = cx + side * int(R * math.cos(math.radians(55)))
        x0 = ex - side * int(w * 0.35)
        x1 = ex + side * int(R * 0.62)
        d.rectangle([min(x0, x1), foot_y - w // 2 + w // 6, max(x0, x1), foot_y + w // 2 - w // 6 + w // 3], fill=RED)
    return im


def main():
    out = sys.argv[1]
    im = draw()
    sizes = [256, 64, 48, 32, 24, 16]
    frames = [im.resize((s, s), Image.LANCZOS) for s in sizes]
    frames[0].save(out, format="ICO", sizes=[(s, s) for s in sizes], append_images=frames[1:])
    if len(sys.argv) > 2:
        frames[0].save(sys.argv[2])


if __name__ == "__main__":
    main()
