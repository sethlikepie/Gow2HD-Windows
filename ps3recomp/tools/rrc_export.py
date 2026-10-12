#!/usr/bin/env python3
"""
ps3recomp - rrc_export.py: RPCS3 RSX capture (.rrc/.rrc.gz) -> our .rxs replay
stream (RXS1 v2, see libs/video/tests/replay_main.c:rxs_load() /
replay_mac.m:rxs_load() and gen_synthetic_rxs.py).

Usage:
    python3 rrc_export.py <in.rrc> <out.rxs>

Format spec source: gow2-recomp/notes/2026-07-21-rpcs3-improvement-backlog.md,
"Area 1 - formato .rrc -> spec para rrc_export.py". That note pins the wire
format from static reading of the RPCS3 source (read-only oracle, GPLv2,
never copied):

  - Emu/RSX/Capture/rsx_replay.h   (frame_capture_data, memory_block,
    replay_command, tile_state/display_buffers_state, magic "RRC", version 6)
  - Emu/RSX/RSXThread.cpp:53-93    (top-level field order, reg_state layout,
    the transform_constants inclusion guard)
  - Emu/RSX/RSXFIFO.cpp:786-847   (how one FIFO command becomes a
    replay_command; how the 3 "batch" register ranges - NV308A_COLOR,
    SET_TRANSFORM_PROGRAM, SET_TRANSFORM_CONSTANT - turn into a run of
    multiple replay_command entries sharing one PFIFO header)
  - Emu/RSX/Capture/rsx_replay.cpp:59-102 (alloc_write_fifo: how a run of
    replay_command entries reassembles into raw (header, arg...) FIFO words)
  - util/serialization.hpp        (VLE = unsigned LEB128; vector/set/map get
    a VLE size prefix, std::array/pair/tuple never do; bitwise structs are a
    raw memcpy)
  - util/serialization_ext.cpp    (compressed_serialization_file_handler:
    deflateInit2/inflateInit2 with windowBits=16+15=31, i.e. a *standard*
    RFC1952 gzip stream with nothing else wrapped around it)
  - Emu/RSX/gcm_enums.h:827-864   (the classic NV_PFIFO method-header bit
    layout: count at [28:18], method at [15:2], non-increment flag at bit
    30 - this is public GPU hardware documentation, e.g. envytools' rnndb /
    psdevwiki, not RPCS3-proprietary; our own libs/video/rsx_dispatch.c
    already leans on the same public sources)

Everything here is a clean-room re-derivation of that wire format in Python;
no RPCS3 source was copied. Only the *addresses* of two of our own registers
(M_VP_UPLOAD_CONST_ID / M_VP_UPLOAD_CONST, used to synthesize records for the
one .rxs format gap noted below) are taken from ../libs/video/rsx_dispatch.c,
exactly like gen_synthetic_rxs.py already does for its M_* table.

Known .rxs format gaps (see the note's Area 1 section C) and how this
exporter handles each one:

  - reg_state.transform_constants is *conditionally* present in a real .rrc
    (RSXThread.cpp:57's `GET_SERIALIZATION_VERSION(global_version) ||
    is_savestate_capture` guard). Since every field before reg_state is
    self-delimited (VLE-prefixed containers), presence is fully
    deterministic from the byte count left after replay_commands: exactly
    74240B (transform_program 8704B + registers 65536B) means "absent",
    exactly 82432B (+8192B) means "present". Anything else is a hard error
    (we refuse to guess - see parse_rrc()).
  - .rxs has no constants[] field. When transform_constants is present, we
    synthesize VP_UPLOAD_CONST_ID/VP_UPLOAD_CONST records (see
    synthesize_constant_records()) so replaying the .rxs through
    rsx_dispatch_method() reconstructs the same constants[] table our own
    dispatcher would have seeded directly, instead of silently dropping it.
  - .rxs has no tile_state (tiling/zcull) field at all, and only a single
    static display-buffer table in its header (not RPCS3's per-command
    hashed display_buffers_state). We drop tile_state, warning once per
    distinct *non-default* tile/zcull state instead of doing it silently;
    we pick the *last* non-zero display_buffer_state referenced by the
    command stream to populate the .rxs header's disp[]/disp_w/disp_h
    (closest to "the display config active at the final flip").
"""

import gzip
import struct
import sys

# --------------------------------------------------------------------------
# .rrc (RPCS3 frame_capture_data) wire-format constants
# --------------------------------------------------------------------------

RRC_MAGIC = b"RRC\x00"   # "RRC"_u32 literal: 3 chars + the string literal's
                          # implicit NUL, bit_cast to a *native* u32 - on any
                          # real-world (little-endian) RPCS3 host that is the
                          # 4 raw bytes 'R','R','C',0x00 on disk.
RRC_VERSION = 6

# Classic NV_PFIFO method-submission header (gcm_enums.h's Method enum;
# public GPU hardware fact, not RPCS3-proprietary):
RSX_METHOD_METHOD_MASK = 0x0000FFFC        # method byte-address, bits [15:2]
RSX_METHOD_COUNT_SHIFT = 18
RSX_METHOD_COUNT_MASK = 0x7FF              # 11 bits, bits [28:18]
RSX_METHOD_NON_INCREMENT_BIT = 0x40000000  # bit 30

# --------------------------------------------------------------------------
# .rxs (RXS1 v2) wire-format constants (see replay_main.c:rxs_load() /
# gen_synthetic_rxs.py)
# --------------------------------------------------------------------------

RXS_MAGIC = b"RXS1"
RXS_VERSION = 2
APPLY_BLOCK_FLAG = 0x80000000

# --------------------------------------------------------------------------
# Sizes shared with our own dispatcher (libs/video/rsx_dispatch.h); reg_state
# in a .rrc always carries the first two at fixed size, and this is also
# where the note's Area 1 section D deterministic byte-count check comes
# from.
# --------------------------------------------------------------------------

RSX_DSP_NUM_REGS = 0x10000 // 4     # 16384 words  (rsx_dispatch.h RSX_DSP_NUM_REGS)
RSX_DSP_VP_INSTR = 544
RSX_DSP_VP_WORDS = RSX_DSP_VP_INSTR * 4   # 2176 words (RSX_DSP_VP_WORDS)
RSX_DSP_NUM_CONSTANTS = 512               # vec4 slots (RSX_DSP_NUM_CONSTANTS)

TRANSFORM_PROGRAM_BYTES = RSX_DSP_VP_WORDS * 4          # 8704
TRANSFORM_CONSTANTS_BYTES = RSX_DSP_NUM_CONSTANTS * 4 * 4  # 8192
REGISTERS_BYTES = RSX_DSP_NUM_REGS * 4                   # 65536

REG_STATE_SIZE_NO_CONST = TRANSFORM_PROGRAM_BYTES + REGISTERS_BYTES               # 74240
REG_STATE_SIZE_WITH_CONST = TRANSFORM_PROGRAM_BYTES + TRANSFORM_CONSTANTS_BYTES + REGISTERS_BYTES  # 82432

# M_VP_UPLOAD_CONST_ID / M_VP_UPLOAD_CONST addresses + window shape, copied
# from ../libs/video/rsx_dispatch.c's M_* table (read-only cross-reference,
# same convention gen_synthetic_rxs.py already uses). The load pointer does
# NOT auto-advance (rsx_dispatch.c:180-189); each burst covers 8 vec4 slots
# (32 words / 4 lanes), matching the method's classified EXEC window there
# and RPCS3's own capture-side batching range size for this method (both
# independently land on 32 words - a hardware fact, not a coincidence).
M_VP_UPLOAD_CONST_ID = 0x1EFC
M_VP_UPLOAD_CONST = 0x1F00
VP_CONST_BURST_SLOTS = 8
VP_CONST_BURST_WORDS = VP_CONST_BURST_SLOTS * 4  # 32


def warn(msg):
    print(f"[rrc_export] WARNING: {msg}", file=sys.stderr)


def info(msg):
    print(f"[rrc_export] {msg}", file=sys.stderr)


# --------------------------------------------------------------------------
# Wire-format reader: VLE = unsigned LEB128 (util/serialization.hpp's
# serialize_vle/deserialize_vle - 7 bits/byte, bit 0x80 = "more bytes
# follow"); fixed-size containers (arrays/pairs) have no length prefix;
# bitwise structs are a raw byte copy in declaration order.
# --------------------------------------------------------------------------

class Reader:
    __slots__ = ("buf", "pos")

    def __init__(self, buf):
        self.buf = buf
        self.pos = 0

    def bytes_(self, n):
        if self.pos + n > len(self.buf):
            raise ValueError(
                f".rrc truncated: need {n} bytes at offset {self.pos}, "
                f"only {len(self.buf) - self.pos} left"
            )
        b = self.buf[self.pos:self.pos + n]
        self.pos += n
        return b

    def u8(self):
        return self.bytes_(1)[0]

    def u32(self):
        return struct.unpack_from("<I", self.bytes_(4))[0]

    def u64(self):
        return struct.unpack_from("<Q", self.bytes_(8))[0]

    def vle(self):
        value = 0
        shift = 0
        while True:
            b = self.u8()
            value |= (b & 0x7F) << shift
            if not (b & 0x80):
                return value
            shift += 7

    def remaining(self):
        return len(self.buf) - self.pos


# --------------------------------------------------------------------------
# .rrc parsing
# --------------------------------------------------------------------------

def parse_rrc(buf):
    """Parse a decompressed frame_capture_data payload into plain dicts."""
    r = Reader(buf)

    magic = r.bytes_(4)
    version = r.u32()
    le_format = r.u32()
    if magic != RRC_MAGIC:
        raise ValueError(f".rrc bad magic {magic!r}, expected {RRC_MAGIC!r}")
    if version != RRC_VERSION:
        raise ValueError(f".rrc version {version}, expected {RRC_VERSION}")
    if le_format != 1:
        # RPCS3 itself refuses to load a capture whose LE_format does not
        # match the running host (Emu/System.cpp); we have no host to match
        # against here, but a big-endian-recorded capture would need every
        # scalar in this file re-read big-endian, which this exporter does
        # not implement (no real-world RPCS3 build records this way).
        raise ValueError(
            ".rrc LE_format=0 (big-endian-host capture) is not supported by "
            "this exporter"
        )

    tile_map = {}
    for _ in range(r.vle()):
        key = r.bytes_(432)   # tile_state: 15*tile_info(16B) + 8*zcull_info(24B)
        val = r.u64()
        tile_map[val] = key

    memory_map = {}
    for _ in range(r.vle()):
        offset = r.u32()
        location = r.u32()
        data_state = r.u64()
        val = r.u64()
        memory_map[val] = (offset, location, data_state)

    memory_data_map = {}
    for _ in range(r.vle()):
        data = r.bytes_(r.vle())
        val = r.u64()
        memory_data_map[val] = data

    display_buffers_map = {}
    for _ in range(r.vle()):
        buffers = tuple(
            (r.u32(), r.u32(), r.u32(), r.u32())  # width, height, pitch, offset
            for _ in range(8)
        )
        count = r.u32()
        val = r.u64()
        display_buffers_map[val] = (count, buffers)

    replay_commands = []
    for _ in range(r.vle()):
        first = r.u32()
        second = r.u32()
        memory_state = [r.u64() for _ in range(r.vle())]
        tile_state = r.u64()
        display_buffer_state = r.u64()
        replay_commands.append({
            "first": first,
            "second": second,
            "memory_state": memory_state,
            "tile_state": tile_state,
            "display_buffer_state": display_buffer_state,
        })

    # reg_state: transform_program (fixed) [+ transform_constants
    # (conditional)] + registers (fixed), nothing else follows it. Since
    # everything above is self-delimited, the remaining byte count
    # deterministically tells us whether transform_constants is present -
    # this is the note's Area 1 section D resolution of the one open
    # sub-question, not a guess.
    reg_state_bytes = r.remaining()
    if reg_state_bytes == REG_STATE_SIZE_WITH_CONST:
        has_constants = True
    elif reg_state_bytes == REG_STATE_SIZE_NO_CONST:
        has_constants = False
    else:
        raise ValueError(
            f"reg_state is {reg_state_bytes} bytes, which matches neither "
            f"the no-transform_constants layout ({REG_STATE_SIZE_NO_CONST}B) "
            f"nor the with-transform_constants layout "
            f"({REG_STATE_SIZE_WITH_CONST}B) predicted from source. Refusing "
            "to guess: either an earlier section was misparsed, or this is "
            "an unsupported capture version."
        )
    info(
        f"reg_state: {'WITH' if has_constants else 'without'} "
        f"transform_constants ({reg_state_bytes} bytes)"
    )

    transform_program = r.bytes_(TRANSFORM_PROGRAM_BYTES)
    transform_constants = r.bytes_(TRANSFORM_CONSTANTS_BYTES) if has_constants else None
    registers = r.bytes_(REGISTERS_BYTES)

    if r.remaining() != 0:
        warn(f"{r.remaining()} unexpected trailing byte(s) after reg_state; ignoring")

    return {
        "tile_map": tile_map,
        "memory_map": memory_map,
        "memory_data_map": memory_data_map,
        "display_buffers_map": display_buffers_map,
        "replay_commands": replay_commands,
        "transform_program": transform_program,
        "transform_constants": transform_constants,
        "registers": registers,
    }


# --------------------------------------------------------------------------
# replay_commands -> flat (method, arg) / (apply, block-hash) events
#
# Clean-room re-derivation of RSXFIFO.cpp's capture-side packing and
# rsx_replay.cpp:alloc_write_fifo's inverse: a plain method write becomes one
# replay_command with count=1 in `first`. The three "batch" ranges (blit
# engine NV308A_COLOR, SET_TRANSFORM_PROGRAM, SET_TRANSFORM_CONSTANT) can
# instead produce a *run* of several replay_command entries: the run's first
# entry carries the real PFIFO header (method/count/non-increment bit) in
# `first` and the first argument in `second`; every following entry in the
# same run carries `first=0` (unused) and just the next argument in
# `second`, with the method address implied by the header's auto-increment
# rule (or held fixed if the non-increment bit was set).
# --------------------------------------------------------------------------

def decode_replay_commands(parsed):
    replay_commands = parsed["replay_commands"]
    memory_map = parsed["memory_map"]
    tile_map = parsed["tile_map"]

    events = []
    count = 0          # words left in the current run
    cur_reg = 0         # word-register index for the *next* run word
    non_increment = False
    last_display_hash = 0
    warned_tile_hashes = set()
    unknown_mem_refs = 0

    for cmd in replay_commands:
        for h in cmd["memory_state"]:
            if h not in memory_map:
                unknown_mem_refs += 1
                continue
            events.append(("apply", h))

        if cmd["display_buffer_state"]:
            last_display_hash = cmd["display_buffer_state"]

        ts = cmd["tile_state"]
        if ts and ts not in warned_tile_hashes:
            warned_tile_hashes.add(ts)
            blob = tile_map.get(ts)
            if blob is not None and any(blob):
                warn(
                    f"capture references a non-default tile/zcull state "
                    f"(hash {ts:#x}) that .rxs has no field for; tiling/"
                    "zcull state is dropped for this replay"
                )

        first, second = cmd["first"], cmd["second"]
        if count == 0:
            reg = (first & RSX_METHOD_METHOD_MASK) >> 2
            run = (first >> RSX_METHOD_COUNT_SHIFT) & RSX_METHOD_COUNT_MASK
            non_increment = bool(first & RSX_METHOD_NON_INCREMENT_BIT)
            if run == 0:
                warn(f"replay_command header {first:#010x} has a zero count; skipping")
                continue
            events.append(("method", reg * 4, second))
            cur_reg = reg if non_increment else reg + 1
            count = run - 1
        else:
            events.append(("method", cur_reg * 4, second))
            if not non_increment:
                cur_reg += 1
            count -= 1

    if unknown_mem_refs:
        warn(
            f"{unknown_mem_refs} replay_command memory_state reference(s) "
            "pointed at a memory_map hash that was never defined; skipped"
        )

    last_display = None
    if last_display_hash:
        last_display = parsed["display_buffers_map"].get(last_display_hash)
        if last_display is None:
            warn(
                f"last display_buffer_state hash {last_display_hash:#x} not "
                "found in display_buffers_map; .rxs display header left zeroed"
            )

    return events, last_display


# --------------------------------------------------------------------------
# .rxs has no constants[] field (Area 1 section C GAP). Reconstruct the
# initial transform_constants table by replaying the same
# VP_UPLOAD_CONST_ID/VP_UPLOAD_CONST writes our own rsx_dispatch_method()
# would consume (see rsx_dispatch.c:180-189, read-only cross-reference).
# All-zero bursts (the overwhelming majority on real content, since most of
# the 512 slots are unused by any given vertex program) are skipped.
# --------------------------------------------------------------------------

def synthesize_constant_records(constants_bytes):
    n_words = RSX_DSP_NUM_CONSTANTS * 4
    words = struct.unpack(f"<{n_words}I", constants_bytes)

    records = []
    n_bursts = RSX_DSP_NUM_CONSTANTS // VP_CONST_BURST_SLOTS
    for burst in range(n_bursts):
        w0 = burst * VP_CONST_BURST_WORDS
        chunk = words[w0:w0 + VP_CONST_BURST_WORDS]
        if not any(chunk):
            continue
        base_slot = burst * VP_CONST_BURST_SLOTS
        records.append((M_VP_UPLOAD_CONST_ID, base_slot))
        for i, w in enumerate(chunk):
            records.append((M_VP_UPLOAD_CONST + i * 4, w))
    return records


# --------------------------------------------------------------------------
# Assemble the .rxs sections
# --------------------------------------------------------------------------

def build_rxs(parsed):
    events, last_display = decode_replay_commands(parsed)

    blocks = []                # [(location, offset, size, data_off), ...]
    data = bytearray()
    block_index_by_hash = {}
    records = []

    def get_block_index(h):
        idx = block_index_by_hash.get(h)
        if idx is not None:
            return idx
        offset, location, data_state = parsed["memory_map"][h]
        payload = parsed["memory_data_map"].get(data_state)
        if payload is None:
            warn(
                f"memory_map entry {h:#x} references missing "
                f"memory_data_map hash {data_state:#x}; using an empty block"
            )
            payload = b""
        idx = len(blocks)
        data_off = len(data)
        blocks.append((location, offset, len(payload), data_off))
        data.extend(payload)
        block_index_by_hash[h] = idx
        return idx

    if parsed["transform_constants"] is not None:
        const_records = synthesize_constant_records(parsed["transform_constants"])
        if const_records:
            n_bursts = sum(1 for (a, _b) in const_records if a == M_VP_UPLOAD_CONST_ID)
            info(
                f"synthesized {len(const_records)} record(s) across "
                f"{n_bursts} burst(s) to reproduce transform_constants via "
                "VP_UPLOAD_CONST_ID/VP_UPLOAD_CONST (.rxs has no dedicated "
                "constants[] field)"
            )
        records.extend(const_records)

    for ev in events:
        if ev[0] == "apply":
            records.append((APPLY_BLOCK_FLAG, get_block_index(ev[1])))
        else:
            _, method, value = ev
            records.append((method, value))

    disp_count = 0
    disp = [(0, 0, 0, 0)] * 8
    disp_w = disp_h = 0
    if last_display is not None:
        count, buffers = last_display
        disp_count = count
        disp = list(buffers)
        if count > 0:
            disp_w, disp_h = buffers[0][0], buffers[0][1]
    elif parsed["replay_commands"]:
        warn("no display_buffer_state was ever referenced in this capture; "
             ".rxs display header (disp_w/disp_h/disp[]) left at zero")

    return {
        "regs": parsed["registers"],
        "vp": parsed["transform_program"],
        "blocks": blocks,
        "data": bytes(data),
        "records": records,
        "disp_count": disp_count,
        "disp": disp,
        "disp_w": disp_w,
        "disp_h": disp_h,
    }


def write_rxs(path, rxs):
    regs = rxs["regs"]
    vp = rxs["vp"]
    blocks = rxs["blocks"]
    data = rxs["data"]
    records = rxs["records"]
    reg_words = len(regs) // 4
    vp_words = len(vp) // 4

    with open(path, "wb") as f:
        f.write(RXS_MAGIC)
        f.write(struct.pack("<I", RXS_VERSION))
        f.write(struct.pack("<I", len(blocks)))
        f.write(struct.pack("<I", len(records)))
        f.write(struct.pack("<I", reg_words))
        f.write(struct.pack("<I", vp_words))
        f.write(struct.pack("<I", rxs["disp_w"]))
        f.write(struct.pack("<I", rxs["disp_h"]))

        f.write(struct.pack("<I", rxs["disp_count"]))
        for i in range(8):
            w, h, pitch, offset = rxs["disp"][i] if i < len(rxs["disp"]) else (0, 0, 0, 0)
            f.write(struct.pack("<4I", w, h, pitch, offset))

        f.write(regs)
        f.write(vp)

        for (location, offset, size, data_off) in blocks:
            f.write(struct.pack("<4I", location, offset, size, data_off))

        f.write(data)

        for (a, b) in records:
            f.write(struct.pack("<2I", a & 0xFFFFFFFF, b & 0xFFFFFFFF))


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def export_rrc(in_path, out_path):
    with open(in_path, "rb") as f:
        raw = f.read()

    try:
        buf = gzip.decompress(raw)
    except OSError as e:
        raise ValueError(
            f"{in_path} is not a valid gzip stream (RPCS3 writes .rrc.gz "
            f"with a standard RFC1952 gzip envelope, windowBits=31): {e}"
        ) from e

    info(f"decompressed {len(raw)} -> {len(buf)} bytes")

    parsed = parse_rrc(buf)
    info(
        f"{len(parsed['replay_commands'])} replay_command(s), "
        f"{len(parsed['memory_map'])} memory block(s), "
        f"{len(parsed['display_buffers_map'])} display state(s), "
        f"{len(parsed['tile_map'])} tile state(s)"
    )

    rxs = build_rxs(parsed)
    write_rxs(out_path, rxs)

    info(
        f"wrote {out_path}: {len(rxs['blocks'])} block(s), "
        f"{len(rxs['records'])} record(s), "
        f"reg_words={len(rxs['regs']) // 4}, vp_words={len(rxs['vp']) // 4}, "
        f"display {rxs['disp_w']}x{rxs['disp_h']}"
    )
    return rxs


def main(argv):
    if len(argv) != 3:
        print(f"usage: {argv[0]} <in.rrc> <out.rxs>", file=sys.stderr)
        return 1
    try:
        export_rrc(argv[1], argv[2])
    except (ValueError, OSError) as e:
        print(f"[rrc_export] ERROR: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
