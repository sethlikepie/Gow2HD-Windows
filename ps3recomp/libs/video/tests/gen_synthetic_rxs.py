#!/usr/bin/env python3
"""
ps3recomp - synthetic .rxs generator for the Track B replay harnesses

Emits a minimal but valid RXS1 v2 capture (see rxs_load() in replay_main.c /
replay_mac.m): one guest-memory block holding an interleaved position+color
vertex stream for a single triangle, and a record list that seeds the NV4097
registers this replay needs (vertex attribute layout, viewport, clear color)
purely through ordinary dispatch(method, arg) records -- no seed-registers
header block required, since rsx_dispatch_method() updates the register file
unconditionally for every write.

No real capture / game data involved: every byte here is synthesized.

This is a *test fixture generator*, not the real exporter (tools/rrc_export.py).
Method addresses below are copied from the M_* constants in ../rsx_dispatch.c;
keep them in sync if that file's register map changes.

Usage:
    python3 gen_synthetic_rxs.py [out.rxs]   (default: tri.rxs)
"""

import struct
import sys

# --------------------------------------------------------------------------
# NV4097 method addresses (must match libs/video/rsx_dispatch.c's M_* table)
# --------------------------------------------------------------------------
M_DMA_COLOR0 = 0x0194
M_COLOR0_OFFSET = 0x0210
M_VIEWPORT_HORIZ = 0x0A00
M_VIEWPORT_VERT = 0x0A04
M_VTXBUF_OFFSET = 0x1680
M_VERTEX_DATA_BASE = 0x1738
M_VTXFMT = 0x1740
M_VERTEX_BEGIN_END = 0x1808
M_VB_VERTEX_BATCH = 0x1814
M_CLEAR_COLOR_VALUE = 0x1D90
M_CLEAR_BUFFERS = 0x1D94
M_GCM_DRIVER_FLIP = 0xE944

DMA_HANDLE_LOCAL = 0xFEED0000
RSX_LOCATION_LOCAL = 0

RSX_VTX_TYPE_FLOAT = 2

RSX_CLEAR_COLOR_R = 0x10
RSX_CLEAR_COLOR_G = 0x20
RSX_CLEAR_COLOR_B = 0x40
RSX_CLEAR_COLOR_A = 0x80

PRIM_TRIANGLES = 5

APPLY_BLOCK_FLAG = 0x80000000

# --------------------------------------------------------------------------
# Scene: one triangle, apex at top (red), base corners bottom-left (green)
# and bottom-right (blue). Positions are in RSX *window/viewport* pixel
# space -- this replay never runs a vertex program (stage 4 is out of
# scope), so the fallback path treats ATTR0 as already-viewport-space and
# applies the INVERSE of the viewport scale/translate to reach clip space
# (see replay_main.c's sink_end()/fetch_draw-comment "stage 2" fallback,
# mirrored in replay_mac.m's sink_end()).
# --------------------------------------------------------------------------
WIDTH = 256
HEIGHT = 256

VERTICES = [
    # x,     y,     z,   r,   g,   b,   a
    (128.0,  40.0, 0.0, 1.0, 0.0, 0.0, 1.0),  # top, red
    (40.0,  216.0, 0.0, 0.0, 1.0, 0.0, 1.0),  # bottom-left, green
    (216.0, 216.0, 0.0, 0.0, 0.0, 1.0, 1.0),  # bottom-right, blue
]
VERT_STRIDE = 28  # 3 floats position + 4 floats color, tightly packed
VERT_BASE_ADDR = 0x1000  # guest LOCAL-memory offset for the vertex block

CLEAR_ARGB = 0xFF102040  # opaque dark navy -- clearly not any vertex color


def u32(v):
    return struct.pack('<I', v & 0xFFFFFFFF)


def be_f32(v):
    return struct.pack('>f', v)


def vtxfmt(vtype, size, stride, freq=0):
    return (vtype & 0x7) | ((size & 0xF) << 4) | ((stride & 0xFF) << 8) | ((freq & 0xFFFF) << 16)


def vtxbuf_offset(location, offset):
    return ((location & 1) << 31) | (offset & 0x7FFFFFFF)


def horiz_vert(pos, size):
    return (pos & 0xFFFF) | ((size & 0xFFFF) << 16)


def vb_batch(first, count):
    return (first & 0xFFFFFF) | (((count - 1) & 0xFF) << 24)


def build(path):
    vert_data = b''.join(be_f32(c) for v in VERTICES for c in v)
    assert len(vert_data) == VERT_STRIDE * len(VERTICES)

    # one guest-memory block: the vertex stream, placed at VERT_BASE_ADDR in
    # LOCAL memory (arena index RSX_LOCATION_LOCAL)
    blocks = [(RSX_LOCATION_LOCAL, VERT_BASE_ADDR, len(vert_data), 0)]

    fmt_pos = vtxfmt(RSX_VTX_TYPE_FLOAT, 3, VERT_STRIDE)   # ATTR0: x,y,z
    fmt_col = vtxfmt(RSX_VTX_TYPE_FLOAT, 4, VERT_STRIDE)   # ATTR3: r,g,b,a

    records = []

    def method(m, arg):
        records.append((m, arg))

    def apply_block(index):
        records.append((APPLY_BLOCK_FLAG, index))

    # vertex attribute layout: ATTR0 = position (offset 0), ATTR3 = color
    # (offset 12), both strided over the same interleaved 28-byte vertex
    method(M_VTXFMT + 0 * 4, fmt_pos)
    method(M_VTXBUF_OFFSET + 0 * 4, vtxbuf_offset(RSX_LOCATION_LOCAL, VERT_BASE_ADDR))
    method(M_VTXFMT + 3 * 4, fmt_col)
    method(M_VTXBUF_OFFSET + 3 * 4, vtxbuf_offset(RSX_LOCATION_LOCAL, VERT_BASE_ADDR + 12))
    method(M_VERTEX_DATA_BASE, 0)

    # push the vertex bytes into the LOCAL arena before they're fetched
    apply_block(0)

    # render target identity (used only as a (location, offset) cache key
    # by this replay's simplified single-surface model) + viewport
    method(M_DMA_COLOR0, DMA_HANDLE_LOCAL)
    method(M_COLOR0_OFFSET, 0)
    method(M_VIEWPORT_HORIZ, horiz_vert(0, WIDTH))
    method(M_VIEWPORT_VERT, horiz_vert(0, HEIGHT))

    # stage 1: clear
    method(M_CLEAR_COLOR_VALUE, CLEAR_ARGB)
    method(M_CLEAR_BUFFERS,
           RSX_CLEAR_COLOR_R | RSX_CLEAR_COLOR_G | RSX_CLEAR_COLOR_B | RSX_CLEAR_COLOR_A)

    # stage 2: one triangle via DRAW_ARRAYS
    method(M_VERTEX_BEGIN_END, PRIM_TRIANGLES)
    method(M_VB_VERTEX_BATCH, vb_batch(0, len(VERTICES)))
    method(M_VERTEX_BEGIN_END, 0)

    # present
    method(M_GCM_DRIVER_FLIP, 0)

    with open(path, 'wb') as f:
        f.write(b'RXS1')
        f.write(u32(2))                # version
        f.write(u32(len(blocks)))      # n_blocks
        f.write(u32(len(records)))     # n_records
        f.write(u32(0))                # reg_words (all state set via records)
        f.write(u32(0))                # vp_words  (no transform program: stage 4 n/a)
        f.write(u32(WIDTH))            # disp_w
        f.write(u32(HEIGHT))           # disp_h

        disp = [(WIDTH, HEIGHT, WIDTH * 4, 0)]  # buffer 0 == COLOR0_OFFSET's surface
        f.write(u32(len(disp)))        # disp_count
        for i in range(8):
            w, h, pitch, offset = disp[i] if i < len(disp) else (0, 0, 0, 0)
            f.write(u32(w)); f.write(u32(h)); f.write(u32(pitch)); f.write(u32(offset))

        # regs[reg_words], vp[vp_words]: both empty

        for (location, offset, size, data_off) in blocks:
            f.write(u32(location)); f.write(u32(offset)); f.write(u32(size)); f.write(u32(data_off))

        f.write(vert_data)

        for (a, b) in records:
            f.write(u32(a)); f.write(u32(b))

    print(f"wrote {path}: {len(records)} records, {len(blocks)} block(s), "
          f"display {WIDTH}x{HEIGHT}, triangle @ {VERT_BASE_ADDR:#x}")


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'tri.rxs'
    build(out)
