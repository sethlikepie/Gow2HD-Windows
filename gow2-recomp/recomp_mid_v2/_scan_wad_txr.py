#!/usr/bin/env python3
"""Offline scan of GoW2 WAD for TXR/GFX/PAL package layout."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
paths = [
    ROOT / "movie_cache" / "R_LglScA.wad_ps3",
    ROOT / "movie_cache" / "r_perma.wad_ps3",
]


def dump_at(b: bytes, i: int, before: int = 16, after: int = 96) -> None:
    start = max(0, i - before)
    chunk = b[start : i + after]
    asc = "".join(chr(x) if 32 <= x < 127 else "." for x in chunk)
    hx = " ".join(f"{x:02X}" for x in chunk)
    print(f"  @{i} (rel -{i-start}): {asc}")
    print(f"    {hx}")


def main() -> None:
    tags = [
        b"TXR_",
        b"GFX_",
        b"PAL_",
        b"SBI_",
        b"SBP_",
        b"SHGX",
        b"TXRX",
        b"SCREEN",
        b"comicsmoke",
        b"chestTexture",
        b"MDL_",
        b"FLP_",
    ]
    for path in paths:
        if not path.exists():
            print("MISSING", path)
            continue
        b = path.read_bytes()
        print(f"=== {path.name} len={len(b)} ===")
        print("head32:", b[:32].hex())
        for t in tags:
            idx = 0
            n = 0
            while n < 6:
                i = b.find(t, idx)
                if i < 0:
                    break
                print(f"-- {t!r} hit#{n+1}")
                dump_at(b, i)
                # If looks like a package header (common 4-byte BE size nearby), try parse
                for off in (-8, -4, 0, 4, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 80):
                    p = i + off
                    if 0 <= p + 4 <= len(b):
                        w = struct.unpack_from(">I", b, p)[0]
                        if off in (0, 4) or (0x20 <= w <= 0x200000) or (w & 0xFFFF0000) == 0x80000000:
                            print(f"    BE+{off:+d}=0x{w:08X} ({w})")
                idx = i + 4
                n += 1
        print()


if __name__ == "__main__":
    main()
