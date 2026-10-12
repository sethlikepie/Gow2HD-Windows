#!/usr/bin/env python3
"""
Decode the GoW2 R_Hero01 Kratos skin texture (asset "~000kratos01_1", paired
with palette name "~pal_krat") straight from the raw WAD package to a PNG.

Adapted from gow2-recomp/recomp_mid_v2/_decode_wad_tex.py (the R_PermA WAD
texture decoder: record framing [type u32][size u32 LE][name 24B][body], BE
header @ +8 data_size / +24 mip_size / +36 dim=(w<<16)|h) and from
ps3recomp/libs/video/rsx_texture_deswizzle.h (the Morton/NV40 deswizzle used
by the native host_wad_tex.c runtime decode path) -- both ported to Python
here since this is offline asset RE, not a live RSX bind. This is a NEW
file; neither of those two is edited.

Package-layout RE notes (r_hero01.wad_ps3 differs from the R_PermA reference):
  - The combined name field is "<texture_tag><palette_tag>" padded/terminated
    to 24 bytes, exactly like R_PermA's "~00ccomicsmoke~pal_c" -- here
    "~000kratos01_1" + "~pal_krat" + NUL = 24 bytes. Despite the palette name
    riding along, the payload is a BAKED, straight A8R8G8B8 (32bpp) mip chain,
    not 8bpp indices into a separate CLUT: mip_size (BE u32 @ payload+24) is
    349524, which equals mip_chain_bytes(256,256, bpp=4) BYTE-EXACT (an 8bpp
    indexed chain would be 87381, ~4x too small) -- so "~pal_krat" is
    provenance from the authoring tool, not a runtime indirection, same
    convention as the R_PermA sample this script is adapted from.
  - The sub-header between the 24-byte name and the pixel data is 152 bytes
    here (R_PermA used 48) -- recovered as payload_len - mip_size and then
    independently confirmed by the coherence search below (see
    decode_base_level): candidate hdr=152 scores far better than every
    R_PermA-style fallback offset (48/64/80/96/128/256).
  - The WAD also contains a second, zero-size occurrence of the same combined
    name (an asset-manifest/TOC entry, sibling to plain "GFX_kratos01_1" /
    "PAL_kratos01_1" name strings nearby) -- that one carries no payload
    (sz==0) and is skipped by find_record() below.
  - Base-level pixels are Morton-swizzled (RSX default when the LN format
    flag is clear, same as the R_PermA sample) -- confirmed empirically, not
    assumed: the coherence search tries BOTH the deswizzled and the
    linear-as-is interpretation for every header candidate and always prefers
    whichever scores as a smoother 2D image (see neighbor_score).

Usage:
    python3 decode_hero_tex.py [wad_path] [tag] [out.png]
Defaults: wad=/tmp/r_hero01.wad_ps3 tag=~000kratos01_1 out=/tmp/kratos_tex.png
"""
from pathlib import Path
import struct
import sys
import zlib

DEFAULT_WAD = "/tmp/r_hero01.wad_ps3"
DEFAULT_TAG = b"~000kratos01_1"
DEFAULT_OUT = "/tmp/kratos_tex.png"


# ---------------------------------------------------------------------------
# Morton (de)swizzle -- ported from ps3recomp/libs/video/rsx_texture_deswizzle.h
# (rsx_log2_ceil / rsx_morton_offset / rsx_deswizzle_texels). Same NV40-style
# addressing rule: starting from the LSB, emit one X bit then one Y bit
# alternately; once an axis runs out of bits, the remaining high bits of the
# longer axis are appended linearly on top. Golden-vector-tested there
# (libs/video/tests/test_texture_deswizzle.c); reimplemented in pure Python
# here (no C extension / FFI dependency for a one-off asset decode).
# ---------------------------------------------------------------------------

def log2_ceil(n: int) -> int:
    """ceil(log2(n)) -- address bits needed for n texels padded to pow2."""
    l = 0
    while (1 << l) < n:
        l += 1
    return l


def morton_offset(x: int, y: int, log2_w: int, log2_h: int) -> int:
    """Swizzled (Morton) texel index for linear coordinate (x, y)."""
    offset = 0
    shift = 0
    xi = yi = 0
    rx, ry = log2_w, log2_h
    while rx or ry:
        if rx:
            offset |= ((x >> xi) & 1) << shift
            shift += 1
            xi += 1
            rx -= 1
        if ry:
            offset |= ((y >> yi) & 1) << shift
            shift += 1
            yi += 1
            ry -= 1
    return offset


def deswizzle_texels(src: bytes, width: int, height: int, bpp_bytes: int) -> bytes:
    """Morton-order src -> row-major linear dst. Mirrors rsx_deswizzle_texels()."""
    log2w = log2_ceil(width)
    log2h = log2_ceil(height)
    out = bytearray(width * height * bpp_bytes)
    for y in range(height):
        row = y * width
        for x in range(width):
            s = morton_offset(x, y, log2w, log2h) * bpp_bytes
            d = (row + x) * bpp_bytes
            out[d:d + bpp_bytes] = src[s:s + bpp_bytes]
    return bytes(out)


def mip_chain_bytes(w: int, h: int, bpp_bytes: int) -> int:
    """Total bytes of a full mip chain w x h -> 1x1 at bpp_bytes/texel
    (same recurrence as _decode_wad_tex.py's mip_chain_bytes, generalized
    to arbitrary bytes-per-texel instead of a hardcoded ARGB8 *4)."""
    total = 0
    cw, ch = w, h
    while True:
        total += cw * ch * bpp_bytes
        if cw == 1 and ch == 1:
            break
        cw = max(1, cw // 2)
        ch = max(1, ch // 2)
    return total


def argb_to_rgba(buf: bytes) -> bytes:
    """(A,R,G,B) byte quads -> (R,G,B,A), same convention as
    _decode_wad_tex.py's decode_tilde() / host_wad_tex.c's argb_to_rgba()."""
    n = len(buf) // 4
    out = bytearray(n * 4)
    for i in range(n):
        a, r, g, b = buf[i * 4:i * 4 + 4]
        out[i * 4:i * 4 + 4] = bytes((r, g, b, a))
    return bytes(out)


# ---------------------------------------------------------------------------
# Coherence scoring -- extends host_wad_tex.c's decode_base_rgba() neighbor
# heuristic (horizontal deltas only) with a vertical pass. A still-swizzled
# ("linear-as-is") buffer can read plausibly smooth left-to-right in patches
# by coincidence, but jumps wildly top-to-bottom (adjacent output rows are
# scattered across the swizzled buffer) -- the combined score is decisive
# where the horizontal-only score alone was close to a tie (measured on this
# asset: swizzled h+v=110.5 vs linear-as-is h+v=229.0, a ~2x gap, vs. h-only
# 36.0 vs 34.9 which was ambiguous).
# ---------------------------------------------------------------------------

def neighbor_score(rgba: bytes, w: int, h: int) -> float:
    step_x = max(1, w // 64)
    sh = 0.0
    nh = 0
    for y in range(0, h, step_x):
        row = y * w
        for x in range(0, w - step_x, step_x):
            i0 = (row + x) * 4
            j0 = (row + x + step_x) * 4
            sh += (abs(rgba[i0] - rgba[j0]) + abs(rgba[i0 + 1] - rgba[j0 + 1])
                   + abs(rgba[i0 + 2] - rgba[j0 + 2]))
            nh += 1

    step_y = max(1, h // 64)
    sv = 0.0
    nv = 0
    for x in range(0, w, step_y):
        for y in range(0, h - step_y, step_y):
            i0 = (y * w + x) * 4
            j0 = ((y + step_y) * w + x) * 4
            sv += (abs(rgba[i0] - rgba[j0]) + abs(rgba[i0 + 1] - rgba[j0 + 1])
                   + abs(rgba[i0 + 2] - rgba[j0 + 2]))
            nv += 1

    return (sh / nh if nh else 1e9) + (sv / nv if nv else 1e9)


def nonzero_count(rgba: bytes, w: int, h: int) -> int:
    n = w * h
    return sum(1 for i in range(n) if rgba[i * 4] or rgba[i * 4 + 1] or rgba[i * 4 + 2])


# ---------------------------------------------------------------------------
# WAD record framing -- same convention as _decode_wad_tex.py's main():
#   [type u32][size u32 LE, sz][name 24B][body, sz-24 bytes]
# The tag is searched as the START of the 24-byte name field; sz sits 4
# bytes before it. This WAD also has a directory/manifest section that
# repeats some names with sz==0 (no payload, just a name listing) --
# find_record() skips those and returns the first occurrence with real data.
# ---------------------------------------------------------------------------

def find_record(data: bytes, tag: bytes):
    start = 0
    while True:
        i = data.find(tag, start)
        if i < 0:
            return None
        if i >= 4:
            sz = struct.unpack_from("<I", data, i - 4)[0]
            if sz >= 64:
                return i, sz
        start = i + 1


def decode_base_level(payload: bytes, w: int, h: int):
    """Search header-offset candidates x {swizzled, linear}; return the
    best-scoring (hdr, swizzled, rgba, score, nz), or None if nothing in the
    payload looks like a coherent w x h ARGB8 image."""
    base_bytes = w * h * 4

    candidates = []
    derived = len(payload) - mip_chain_bytes(w, h, 4)
    if 0 <= derived <= 4096:
        candidates.append(derived)
    for hdr in (48, 64, 80, 96, 128, 152, 256):  # R_PermA default + RE'd fallbacks
        if hdr not in candidates:
            candidates.append(hdr)

    best = None
    for hdr in candidates:
        if hdr < 0 or hdr + base_bytes > len(payload):
            continue
        raw = payload[hdr:hdr + base_bytes]
        for swizzled in (True, False):
            linear = deswizzle_texels(raw, w, h, 4) if swizzled else raw
            rgba = argb_to_rgba(linear)
            nz = nonzero_count(rgba, w, h)
            if nz < (w * h) // 100:
                continue  # near-empty candidate, not worth scoring
            sc = neighbor_score(rgba, w, h)
            if best is None or sc < best[3]:
                best = (hdr, swizzled, rgba, sc, nz)
    return best


def _zlib_stored(raw: bytes) -> bytes:
    """A valid RFC1950 zlib stream using only STORED (uncompressed) DEFLATE
    blocks -- same technique as mesh_view.m's write_png() (see comment there:
    "IDAT as valid-but-uncompressed zlib 'stored' deflate blocks, no zlib/
    ImageIO dependency"). Used here (instead of zlib.compress) so that
    mesh_spin_tex.m -- which links only Metal/Foundation/QuartzCore/SDL2, no
    ImageIO/CoreGraphics and no -lz -- can load this PNG with a small
    hand-rolled reader that only needs to understand stored blocks, not real
    DEFLATE/Huffman decoding."""
    out = bytearray((0x78, 0x01))  # zlib header (matches mesh_view.m)
    n = len(raw)
    off = 0
    while True:
        block = min(65535, n - off)
        bfinal = 1 if (off + block >= n) else 0
        out.append(bfinal)  # BFINAL in bit0, BTYPE=00 (stored) in bits 1-2
        length = block
        nlen = (~length) & 0xFFFF
        out += struct.pack("<HH", length, nlen)
        out += raw[off:off + block]
        off += block
        if off >= n:
            break
    out += struct.pack(">I", zlib.adler32(raw) & 0xFFFFFFFF)
    return bytes(out)


def write_png(path: Path, w: int, h: int, rgba: bytes) -> None:
    def chunk(tag: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", _zlib_stored(raw)) + chunk(b"IEND", b""))
    path.write_bytes(png)
    print(f"[decode_hero_tex] wrote {path} {w}x{h} ({path.stat().st_size} bytes)")


def main() -> int:
    wad_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_WAD
    tag = sys.argv[2].encode("latin1") if len(sys.argv) > 2 else DEFAULT_TAG
    out_path = Path(sys.argv[3] if len(sys.argv) > 3 else DEFAULT_OUT)

    data = Path(wad_path).read_bytes()

    rec = find_record(data, tag)
    if rec is None:
        print(f"[decode_hero_tex] FAIL: tag {tag!r} not found in {wad_path}")
        return 1
    i, sz = rec
    name_field = data[i:i + 24]
    payload = data[i + 24:i + sz]
    print(f"[decode_hero_tex] tag={tag!r} record@0x{i:x} sz={sz} "
          f"name={name_field.rstrip(chr(0).encode())!r} payload={len(payload)}B")

    data_size = struct.unpack_from(">I", payload, 8)[0]
    mip_size = struct.unpack_from(">I", payload, 24)[0]
    dim_word = struct.unpack_from(">I", payload, 36)[0]
    w = (dim_word >> 16) & 0xFFFF
    h = dim_word & 0xFFFF
    print(f"[decode_hero_tex] header data_size={data_size} mip_size={mip_size} dim={w}x{h}")

    if w < 4 or h < 4 or w > 2048 or h > 2048 or mip_chain_bytes(w, h, 4) != mip_size:
        # RE fallback: recover a square POT size from the mip/data byte counts
        # (same idea as host_wad_tex.c's dim-recovery loop for RSX ARGB8 assets).
        recovered = False
        for side in (32, 64, 128, 256, 512, 1024):
            if mip_chain_bytes(side, side, 4) in (mip_size, data_size):
                w = h = side
                recovered = True
                break
        if not recovered:
            print("[decode_hero_tex] FAIL: could not determine texture dimensions")
            return 1
        print(f"[decode_hero_tex] dim field unusable, recovered square {w}x{h} from mip/data size")

    result = decode_base_level(payload, w, h)
    if result is None:
        print("[decode_hero_tex] FAIL: no coherent pixel data found among header candidates")
        return 1
    hdr, swizzled, rgba, score, nz = result
    total = w * h
    print(f"[decode_hero_tex] chosen hdr={hdr} swizzled={swizzled} "
          f"score={score:.2f} nz={nz}/{total} ({100.0 * nz / total:.1f}%)")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    write_png(out_path, w, h, rgba)
    print(f"[decode_hero_tex] OK {w}x{h}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
