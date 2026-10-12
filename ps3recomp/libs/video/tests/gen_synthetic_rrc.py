#!/usr/bin/env python3
"""
ps3recomp - synthetic .rrc(.gz) generator, for testing tools/rrc_export.py's
round-trip without a real RPCS3 capture in hand.

Hand-builds an RPCS3 frame_capture_data payload (gzip envelope + the wire
format documented in gow2-recomp/notes/2026-07-21-rpcs3-improvement-backlog.md
"Area 1") with plain struct.pack + gzip.compress -- clean-room, no RPCS3 code
involved (RPCS3 is GPLv2, read-only reference only).

The scene is the *same* single triangle gen_synthetic_rxs.py emits directly
(same vertices, same NV4097 method addresses, same display size), so that:

    gen_synthetic_rrc.py /tmp/t.rrc
    rrc_export.py /tmp/t.rrc /tmp/t2.rxs
    gen_synthetic_rxs.py /tmp/t_direct.rxs

produce blocks[]/data[]/records[]/disp[] that line up 1:1 between t2.rxs and
t_direct.rxs (regs[]/vp[] differ only in *length*: a real .rrc's reg_state
always carries the fixed-size transform_program+registers arrays, so the
exported .rxs always has reg_words=16384/vp_words=2176, all-zero here since
this scene seeds every register it needs through records anyway -- see
gen_synthetic_rxs.py's own header comment on why reg_words=0/vp_words=0 is
safe there).

The one replay_command that carries memory_state (the vertex block) and the
one that carries display_buffer_state (the flip) are placed so that
rrc_export.py's positional apply-block placement lands the resulting
APPLY_BLOCK record at the exact same index gen_synthetic_rxs.py puts it at
(index 5, right after VERTEX_DATA_BASE / before DMA_COLOR0) -- see build()
below for the exact placement.

No real capture / game data involved: every byte here is synthesized.

Usage:
    python3 gen_synthetic_rrc.py [out.rrc]   (default: tri.rrc)
"""

import gzip
import struct
import sys

# --------------------------------------------------------------------------
# NV4097 method addresses (must match libs/video/rsx_dispatch.c's M_* table
# and gen_synthetic_rxs.py's own copy of them)
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

# --------------------------------------------------------------------------
# Scene: identical to gen_synthetic_rxs.py's triangle (same vertices, same
# base address, same display size) so the two generators are comparable.
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

# --------------------------------------------------------------------------
# RPCS3 frame_capture_data wire-format constants (see rrc_export.py's header
# comment for the exact source citations; not repeated here)
# --------------------------------------------------------------------------
RRC_MAGIC = b"RRC\x00"
RRC_VERSION = 6

RSX_DSP_NUM_REGS = 0x10000 // 4      # 16384 words
RSX_DSP_VP_WORDS = 544 * 4           # 2176 words

# Opaque memory_indexer-style hash keys (RPCS3's own indexer starts at
# 0x1234 and pre-increments -- these are otherwise arbitrary, distinct,
# nonzero u64s from an exporter's point of view).
DATA_HASH = 0x1235
MEM_HASH = 0x1236
DISP_HASH = 0x1237


def u32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def u64(v):
    return struct.pack("<Q", v & 0xFFFFFFFFFFFFFFFF)


def be_f32(v):
    return struct.pack(">f", v)


def vle(v):
    """Unsigned LEB128: 7 bits/byte, high bit set while more bytes follow."""
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def vtxfmt(vtype, size, stride, freq=0):
    return (vtype & 0x7) | ((size & 0xF) << 4) | ((stride & 0xFF) << 8) | ((freq & 0xFFFF) << 16)


def vtxbuf_offset(location, offset):
    return ((location & 1) << 31) | (offset & 0x7FFFFFFF)


def horiz_vert(pos, size):
    return (pos & 0xFFFF) | ((size & 0xFFFF) << 16)


def vb_batch(first, count):
    return (first & 0xFFFFFF) | (((count - 1) & 0xFF) << 24)


def fifo_header(method_addr, count=1, non_increment=False):
    """Pack a classic NV_PFIFO method-submission header: method @ [15:2],
    count @ [28:18], non-increment flag @ bit 30 (public GPU hardware
    format; see rrc_export.py's RSX_METHOD_* constants for the same
    layout used on the decode side)."""
    hdr = (method_addr & 0xFFFC) | ((count & 0x7FF) << 18)
    if non_increment:
        hdr |= 0x40000000
    return hdr


def build(path):
    vert_data = b"".join(be_f32(c) for v in VERTICES for c in v)
    assert len(vert_data) == VERT_STRIDE * len(VERTICES)

    fmt_pos = vtxfmt(RSX_VTX_TYPE_FLOAT, 3, VERT_STRIDE)   # ATTR0: x,y,z
    fmt_col = vtxfmt(RSX_VTX_TYPE_FLOAT, 4, VERT_STRIDE)   # ATTR3: r,g,b,a

    # -- replay_commands: one entry per FIFO write, count=1 each (this fixture
    # never exercises the multi-arg "batch" ranges - NV308A_COLOR /
    # SET_TRANSFORM_PROGRAM / SET_TRANSFORM_CONSTANT - so rrc_export.py's
    # run-continuation decode path isn't exercised by this round-trip; see
    # the report for how that path was checked separately.)
    #
    # memory_state (the vertex block) is attached to the DMA_COLOR0 entry and
    # display_buffer_state (the display table) to the final FLIP entry so
    # that, after rrc_export.py's positional apply-block placement, the
    # resulting .rxs records land in the exact same order/positions
    # gen_synthetic_rxs.py's own writer uses (APPLY_BLOCK right after
    # VERTEX_DATA_BASE, before DMA_COLOR0).
    cmds = []

    def cmd(method_addr, value, memory_state=(), display_buffer_state=0, tile_state=0):
        cmds.append({
            "first": fifo_header(method_addr, count=1),
            "second": value & 0xFFFFFFFF,
            "memory_state": list(memory_state),
            "tile_state": tile_state,
            "display_buffer_state": display_buffer_state,
        })

    cmd(M_VTXFMT + 0 * 4, fmt_pos)
    cmd(M_VTXBUF_OFFSET + 0 * 4, vtxbuf_offset(RSX_LOCATION_LOCAL, VERT_BASE_ADDR))
    cmd(M_VTXFMT + 3 * 4, fmt_col)
    cmd(M_VTXBUF_OFFSET + 3 * 4, vtxbuf_offset(RSX_LOCATION_LOCAL, VERT_BASE_ADDR + 12))
    cmd(M_VERTEX_DATA_BASE, 0)
    cmd(M_DMA_COLOR0, DMA_HANDLE_LOCAL, memory_state=[MEM_HASH])
    cmd(M_COLOR0_OFFSET, 0)
    cmd(M_VIEWPORT_HORIZ, horiz_vert(0, WIDTH))
    cmd(M_VIEWPORT_VERT, horiz_vert(0, HEIGHT))
    cmd(M_CLEAR_COLOR_VALUE, CLEAR_ARGB)
    cmd(M_CLEAR_BUFFERS, RSX_CLEAR_COLOR_R | RSX_CLEAR_COLOR_G | RSX_CLEAR_COLOR_B | RSX_CLEAR_COLOR_A)
    cmd(M_VERTEX_BEGIN_END, PRIM_TRIANGLES)
    cmd(M_VB_VERTEX_BATCH, vb_batch(0, len(VERTICES)))
    cmd(M_VERTEX_BEGIN_END, 0)
    cmd(M_GCM_DRIVER_FLIP, 0, display_buffer_state=DISP_HASH)

    payload = bytearray()
    payload += RRC_MAGIC
    payload += u32(RRC_VERSION)
    payload += u32(1)  # LE_format = true

    payload += vle(0)  # tile_map: empty

    # memory_map: 1 entry -- key=memory_block{offset,location,data_state}(16B), value=hash(8B)
    payload += vle(1)
    payload += u32(VERT_BASE_ADDR)       # offset
    payload += u32(RSX_LOCATION_LOCAL)   # location
    payload += u64(DATA_HASH)            # data_state (-> memory_data_map key)
    payload += u64(MEM_HASH)             # map value (referenced by memory_state)

    # memory_data_map: 1 entry -- key=memory_block_data{VLE(len)+bytes}, value=hash(8B)
    payload += vle(1)
    payload += vle(len(vert_data))
    payload += vert_data
    payload += u64(DATA_HASH)

    # display_buffers_map: 1 entry -- key=display_buffers_state{buffers[8](128B)+count(4B)=132B}, value=hash(8B)
    payload += vle(1)
    payload += struct.pack("<4I", WIDTH, HEIGHT, WIDTH * 4, 0)   # buffers[0]
    payload += struct.pack("<4I", 0, 0, 0, 0) * 7                # buffers[1..7]
    payload += u32(1)          # count
    payload += u64(DISP_HASH)

    # replay_commands: VLE(count) + count * (first,second (8B) + memory_state
    # (VLE(n)+8n) + tile_state (8B) + display_buffer_state (8B))
    payload += vle(len(cmds))
    for c in cmds:
        payload += u32(c["first"])
        payload += u32(c["second"])
        payload += vle(len(c["memory_state"]))
        for h in c["memory_state"]:
            payload += u64(h)
        payload += u64(c["tile_state"])
        payload += u64(c["display_buffer_state"])

    # reg_state: transform_program + registers, all zero (no vertex program /
    # no seeded register state needed -- every register this scene touches
    # is set through records above, mirroring gen_synthetic_rxs.py's own
    # reg_words=0/vp_words=0 choice). transform_constants is omitted
    # entirely, which exercises rrc_export.py's "no transform_constants"
    # branch of its deterministic byte-count check.
    payload += bytes(RSX_DSP_VP_WORDS * 4)
    payload += bytes(RSX_DSP_NUM_REGS * 4)

    compressed = gzip.compress(bytes(payload), compresslevel=9)
    with open(path, "wb") as f:
        f.write(compressed)

    print(
        f"wrote {path}: {len(cmds)} replay_command(s), 1 memory block, "
        f"display {WIDTH}x{HEIGHT}, triangle @ {VERT_BASE_ADDR:#x} "
        f"(payload {len(payload)}B -> gzip {len(compressed)}B)"
    )


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "tri.rrc"
    build(out)
