#!/usr/bin/env python3
"""parse_gcm_stream.py FILE... : walk one frame of a PS3_GCM_STREAM_DUMP file
(prev flip offset -> this flip offset) like the FIFO walker and summarize it."""
import struct, sys
from collections import Counter

NAMES = {0x208: 'SURF_FMT', 0x210: 'AOFF', 0x220: 'COLOR_TGT', 0x1808: 'BEGIN_END',
         0x8E4: 'FP', 0x1D94: 'CLEAR', 0x194: 'DMA_COLOR_A'}

def walk(path):
    raw = open(path, 'rb').read()
    magic, n, prev_off, off, wrap_at, pend, beg, end = struct.unpack('<8I', raw[:32])
    words = struct.unpack('<%dI' % ((len(raw) - 32) // 4), raw[32:32 + ((len(raw) - 32) // 4) * 4])
    size = len(words) * 4
    get = prev_off
    wrapped = False
    methods = Counter()
    seq = []
    jumps = []
    steps = 0
    lap_limit = off if (off > prev_off) else None
    while steps < 2_000_000:
        steps += 1
        if get == off and (off > prev_off or wrapped):
            break
        if pend and not wrapped and get == wrap_at:
            get = beg; wrapped = True; continue
        if get >= size:
            seq.append(('OUT_OF_RING', get)); break
        cmd = words[get // 4]
        if (cmd & 0xE0000003) == 0x20000000:
            jumps.append(('old_jump', get, cmd & 0x1FFFFFFC)); get = cmd & 0x1FFFFFFC; continue
        if (cmd & 3) == 1:
            jumps.append(('jump', get, cmd & 0xFFFFFFFC)); get = cmd & 0xFFFFFFFC; continue
        if (cmd & 3) == 2:
            jumps.append(('call', get, cmd & 0xFFFFFFFC)); get += 4; continue
        if (cmd & 0xFFFF0003) == 0x00020000:
            jumps.append(('ret', get, 0)); get += 4; continue
        count = (cmd >> 18) & 0x7FF
        subch = (cmd >> 13) & 7
        method = cmd & 0x1FFC
        noinc = cmd & 0x40000000
        args = words[get // 4 + 1: get // 4 + 1 + count]
        for i, v in enumerate(args):
            m = method + (0 if noinc else i * 4)
            if subch == 0:
                methods[m] += 1
                if m in (0x210, 0x220, 0x8E4, 0x1D94, 0x208) or (m == 0x1808 and v != 0):
                    seq.append((get, NAMES.get(m, hex(m)), v))
            else:
                methods[('sub', subch, m)] += 1
        get += 4 + count * 4
    return dict(n=n, prev=prev_off, off=off, wrap_at=wrap_at, pend=pend, wrapped=wrapped,
                steps=steps, methods=methods, seq=seq, jumps=jumps, end_get=get)

for p in sys.argv[1:]:
    r = walk(p)
    mm = r['methods']
    print(f"== call {r['n']} prev=0x{r['prev']:X} off=0x{r['off']:X} wrap_at=0x{r['wrap_at']:X} pend={r['pend']} "
          f"wrapped={r['wrapped']} end_get=0x{r['end_get']:X} headers={r['steps']}")
    print(f"   AOFF={mm[0x210]} COLOR_TGT={mm[0x220]} BEGIN_END={mm[0x1808]} FP={mm[0x8E4]} CLEAR={mm[0x1D94]} "
          f"sub!=0={sum(v for k, v in mm.items() if isinstance(k, tuple))} jumps={len(r['jumps'])}")
    print('   jumps:', [(k, hex(a), hex(b)) for k, a, b in r['jumps'][:12]])
    surf = [s for s in r['seq'] if s[1] in ('AOFF', 'COLOR_TGT', 'SURF_FMT')]
    print('   surface cmds (first 40):', [(hex(g), nm, hex(v)) for g, nm, v in surf[:40]])
    if len(sys.argv) == 2:
        for s in r['seq'][-60:]:
            print('   ', hex(s[0]) if isinstance(s[0], int) else s[0], s[1], hex(s[2]) if len(s) > 2 else '')
