#!/usr/bin/env python3
"""Decode GoW2 R_PermA ~texture packages (ARGB8 mip chain) to PNG."""
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parent.parent
WAD = ROOT / "movie_cache" / "r_perma.wad_ps3"
OUT = Path(__file__).resolve().parent


def chunk(tag: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + tag + data + struct.pack(
        ">I", zlib.crc32(tag + data) & 0xFFFFFFFF
    )


def write_png(path: Path, w: int, h: int, rgba: bytes) -> None:
    # rgba is already R,G,B,A
    raw = b"".join(b"\x00" + rgba[y * w * 4 : (y + 1) * w * 4] for y in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)
    print(f"wrote {path.name} {w}x{h} ({path.stat().st_size} bytes)")


def mip_chain_bytes(w: int, h: int) -> int:
    total = 0
    cw, ch = w, h
    while True:
        total += cw * ch * 4
        if cw == 1 and ch == 1:
            break
        cw = max(1, cw // 2)
        ch = max(1, ch // 2)
    return total


def decode_tilde(payload: bytes, name: str) -> None:
    # Header fields are big-endian (PPC guest stream style inside LE package framing).
    data_size = struct.unpack_from(">I", payload, 8)[0]
    mip_size = struct.unpack_from(">I", payload, 24)[0]
    dim_word = struct.unpack_from(">I", payload, 36)[0]
    w = (dim_word >> 16) & 0xFFFF
    h = dim_word & 0xFFFF
    if w == 0 or h == 0 or w > 4096 or h > 4096:
        # try LE halfwords at +36
        w = struct.unpack_from("<H", payload, 36)[0]
        h = struct.unpack_from("<H", payload, 38)[0]
    print(f"{name}: data_size={data_size} mip_size={mip_size} dim={w}x{h}")
    expect = mip_chain_bytes(w, h)
    print(f"  expect mip chain bytes={expect}")

    # Find pixel start: prefer header end where mip_size bytes remain, or fixed 48/256.
    pix = None
    for hdr in (48, 44, 40, 36, 32, 64, 128, 256):
        if len(payload) - hdr >= expect:
            # verify nonzero density in first 64 of base level
            cand = payload[hdr : hdr + expect]
            nz = sum(1 for i in range(0, min(256, len(cand)), 4) if cand[i + 1] | cand[i + 2] | cand[i + 3])
            print(f"  try hdr={hdr} nz_sample={nz}")
            if nz > 4:
                pix = cand
                break
    if pix is None:
        # scan for first ARGB-looking dense region
        for off in range(0, min(512, len(payload) - expect), 4):
            if len(payload) - off < expect:
                break
            cand = payload[off : off + expect]
            # base level first pixel alpha high?
            if cand[0] > 0x80:
                pix = cand
                print(f"  fallback pix@ {off}")
                break
    if pix is None:
        print("  FAIL no pixel start")
        return

    base = pix[: w * h * 4]
    # ARGB -> RGBA
    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        a, r, g, b = base[i * 4 : i * 4 + 4]
        rgba[i * 4 : i * 4 + 4] = bytes((r, g, b, a))
    nz = sum(1 for i in range(0, len(rgba), 4) if rgba[i] | rgba[i + 1] | rgba[i + 2])
    print(f"  nonzero={nz}/{w*h}")
    safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in name)[:40]
    write_png(OUT / f"wad_tex_{safe}.png", w, h, bytes(rgba))


def main() -> None:
    b = WAD.read_bytes()
    for tag in (b"~00ccomicsmoke~pal_c", b"~010decorchest01_gol"):
        i = b.find(tag)
        sz = struct.unpack_from("<I", b, i - 4)[0]
        # body after type+size+name(24)
        payload = b[i + 24 : i - 8 + 8 + sz]
        decode_tilde(payload, tag.decode("latin1", "replace"))


if __name__ == "__main__":
    main()
