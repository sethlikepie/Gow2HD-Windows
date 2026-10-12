#!/usr/bin/env python3
"""
psarc_list.py - list members of a PSARC v1.x archive (zlib) by decompressing
the manifest (entry 0 = newline-separated path list for entries 1..N-1).

Usage: python psarc_list.py <file.psarc> [--dump-entry N out.bin] [--grep SUBSTR]

Header (BE):
  0x00 'PSAR'  0x04 u16 vmaj,u16 vmin  0x08 compression ('zlib')
  0x0C u32 toc_length (includes 0x20 header)  0x10 u32 toc_entry_size (30)
  0x14 u32 toc_entries  0x18 u32 block_size  0x1C u32 archive_flags
TOC entry (30 bytes): 16B name-MD5, u32 first-block index, u40 uncompressed
size, u40 archive offset. After entries: block-size table, ceil(log256(
block_size)) bytes each (2 for 64K); value 0 = full block_size.
Block: if stored==block_size -> raw; else zlib (0x78 magic) else raw.
"""
import sys, struct, zlib, hashlib

def u40(b): return int.from_bytes(b, 'big')

def parse(path):
    f = open(path, 'rb')
    hdr = f.read(0x20)
    assert hdr[:4] == b'PSAR', 'not a PSARC'
    vmaj, vmin = struct.unpack('>HH', hdr[4:8])
    comp = hdr[8:12]
    toc_length, entry_size, n_entries, block_size, flags = struct.unpack('>IIIII', hdr[0xC:0x20])
    print(f'PSARC v{vmaj}.{vmin} comp={comp.decode()} toc_len=0x{toc_length:X} '
          f'entries={n_entries} block=0x{block_size:X} flags=0x{flags:X}')
    toc = f.read(toc_length - 0x20)
    entries = []
    for i in range(n_entries):
        e = toc[i*entry_size:(i+1)*entry_size]
        md5, blk, usz, off = e[:16], struct.unpack('>I', e[16:20])[0], u40(e[20:25]), u40(e[25:30])
        entries.append((md5, blk, usz, off))
    # block-size table
    bsz_bytes = 2 if block_size <= 0x10000 else 3
    btab_raw = toc[n_entries*entry_size:]
    n_blocks = len(btab_raw) // bsz_bytes
    btab = [int.from_bytes(btab_raw[i*bsz_bytes:(i+1)*bsz_bytes], 'big') for i in range(n_blocks)]
    print(f'block-table entries={n_blocks}')
    return f, entries, btab, block_size, flags

def read_entry(f, entries, btab, block_size, idx):
    md5, blk, usz, off = entries[idx]
    out = bytearray()
    f.seek(off)
    b = blk
    while len(out) < usz:
        stored = btab[b]
        if stored == 0:
            data = f.read(block_size)          # full raw block
        else:
            raw = f.read(stored)
            if raw[:1] == b'\x78':
                try: data = zlib.decompress(raw)
                except zlib.error: data = raw   # stored-as-is block
            else:
                data = raw
        out += data
        b += 1
    return bytes(out[:usz])

def main():
    path = sys.argv[1]
    f, entries, btab, block_size, flags = parse(path)
    manifest = read_entry(f, entries, btab, block_size, 0)
    names = manifest.decode('utf-8', 'replace').replace('\r\n', '\n').split('\n')
    print(f'manifest bytes={len(manifest)} names={len(names)} (entries-1={len(entries)-1})')
    grep = None
    if '--grep' in sys.argv:
        grep = sys.argv[sys.argv.index('--grep')+1].lower()
    shown = 0
    for i, nm in enumerate(names, start=1):
        if i >= len(entries): break
        md5, blk, usz, off = entries[i]
        if grep and grep not in nm.lower(): continue
        print(f'  [{i:4}] {nm}  usz={usz} off=0x{off:X}')
        shown += 1
        if not grep and shown >= 60:
            print('  ... (use --grep para filtrar)'); break
    if '--dump-entry' in sys.argv:
        k = sys.argv.index('--dump-entry')
        idx, outp = int(sys.argv[k+1]), sys.argv[k+2]
        data = read_entry(f, entries, btab, block_size, idx)
        open(outp, 'wb').write(data)
        print(f'dumped entry {idx} -> {outp} ({len(data)} bytes)')

if __name__ == '__main__':
    main()
