#!/usr/bin/env python3
"""Decode EBOOT legalscreen720.ctxr (gzip + DXT1) to PNG for content-pixel proof."""
from pathlib import Path
import gzip
import struct
import zlib
import sys

ROOT = Path(__file__).resolve().parent.parent
OUT = Path(__file__).resolve().parent / "legal_screen720.png"


def skip_gzip(data: bytes, off: int) -> tuple[bytes, str]:
    assert data[off : off + 3] == b"\x1f\x8b\x08"
    flg = data[off + 3]
    p = off + 10
    fname = ""
    if flg & 4:
        xlen = struct.unpack_from("<H", data, p)[0]
        p += 2 + xlen
    if flg & 8:
        s = p
        while data[p]:
            p += 1
        fname = data[s:p].decode("latin1", "replace")
        p += 1
    if flg & 16:
        while data[p]:
            p += 1
        p += 1
    if flg & 2:
        p += 2
    raw = data[p:]
    # drop 8-byte trailer preference handled by zlib tolerating trail
    return zlib.decompress(raw, -15), fname


def dxt1_decode_block(block: bytes) -> list[tuple[int, int, int, int]]:
    c0, c1 = struct.unpack_from("<HH", block, 0)
    bits = struct.unpack_from("<I", block, 4)[0]

    def rgb565(c: int) -> tuple[int, int, int]:
        r = ((c >> 11) & 0x1F) * 255 // 31
        g = ((c >> 5) & 0x3F) * 255 // 63
        b = (c & 0x1F) * 255 // 31
        return r, g, b

    r0, g0, b0 = rgb565(c0)
    r1, g1, b1 = rgb565(c1)
    if c0 > c1:
        palette = [
            (r0, g0, b0, 255),
            (r1, g1, b1, 255),
            ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255),
            ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255),
        ]
    else:
        palette = [
            (r0, g0, b0, 255),
            (r1, g1, b1, 255),
            ((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255),
            (0, 0, 0, 0),
        ]
    pixels = []
    for i in range(16):
        idx = (bits >> (2 * i)) & 3
        pixels.append(palette[idx])
    return pixels


def decode_dxt1(data: bytes, w: int, h: int, header: int = 128) -> bytes:
    body = data[header:]
    expected = ((w + 3) // 4) * ((h + 3) // 4) * 8
    print(f"body={len(body)} expected={expected} header={header}")
    if len(body) < expected:
        raise SystemExit(f"short body {len(body)} < {expected}")
    rgba = bytearray(w * h * 4)
    bw = (w + 3) // 4
    bh = (h + 3) // 4
    o = 0
    for by in range(bh):
        for bx in range(bw):
            block = body[o : o + 8]
            o += 8
            pix = dxt1_decode_block(block)
            for py in range(4):
                for px in range(4):
                    x, y = bx * 4 + px, by * 4 + py
                    if x >= w or y >= h:
                        continue
                    r, g, b, a = pix[py * 4 + px]
                    i = (y * w + x) * 4
                    rgba[i : i + 4] = bytes((r, g, b, a))
    return bytes(rgba)


def write_png(path: Path, w: int, h: int, rgba: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(
            ">I", zlib.crc32(tag + data) & 0xFFFFFFFF
        )

    raw = b"".join(b"\x00" + rgba[y * w * 4 : (y + 1) * w * 4] for y in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(
        b"IDAT", zlib.compress(raw, 9)
    ) + chunk(b"IEND", b"")
    path.write_bytes(png)
    print(f"wrote {path} ({path.stat().st_size} bytes) {w}x{h}")


def main() -> None:
    eboot = (ROOT / "EBOOT.ELF").read_bytes()
    # find gzip with fname legalscreen720.ctxr
    off = None
    i = 0
    while i < len(eboot) - 20:
        if eboot[i] == 0x1F and eboot[i + 1] == 0x8B and eboot[i + 2] == 8:
            flg = eboot[i + 3]
            p = i + 10
            try:
                if flg & 4:
                    xlen = struct.unpack_from("<H", eboot, p)[0]
                    p += 2 + xlen
                if flg & 8:
                    s = p
                    while eboot[p]:
                        p += 1
                    name = eboot[s:p].decode("latin1", "replace")
                    if name == "legalscreen720.ctxr":
                        off = i
                        break
            except Exception:
                pass
        i += 1
    if off is None:
        raise SystemExit("legalscreen720.ctxr gzip not found")
    raw, fname = skip_gzip(eboot, off)
    print(f"off={off:#x} name={fname} raw_len={len(raw)}")
    print("head32:", raw[:32].hex())
    # try header sizes 0, 16, 32, 64, 128
    for hdr in (0, 16, 32, 64, 80, 96, 112, 128, 160, 192, 256):
        body = len(raw) - hdr
        if body == 1280 * 720 // 2:
            print(f"perfect DXT1 fit header={hdr}")
            rgba = decode_dxt1(raw, 1280, 720, header=hdr)
            write_png(OUT, 1280, 720, rgba)
            # also sample nonzero
            nz = sum(1 for i in range(0, len(rgba), 4) if rgba[i] | rgba[i + 1] | rgba[i + 2])
            print(f"nonzero_pixels={nz}/{1280*720}")
            return
    print("no perfect fit; dumping first guess hdr=128")
    rgba = decode_dxt1(raw, 1280, 720, header=128)
    write_png(OUT, 1280, 720, rgba)


if __name__ == "__main__":
    main()
