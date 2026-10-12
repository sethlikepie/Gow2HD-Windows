#!/usr/bin/env python3
"""psarc_extract.py - extractor do gow2.psarc (Sony PSARC v1.4, zlib).

Formato (read-only, clean-room a partir da spec publica do PSARC):
  header 32B: 'PSAR', ver(2+2 BE), 'zlib', toc_len, entry_size(30), n_entries,
  block_size(64K), flags. TOC: n x 30B (md5[16], block_idx(4 BE), usize(5 BE),
  offset(5 BE)). Depois a tabela de tamanhos de bloco (2B BE cada; 0 = bloco
  cheio block_size). Entry 0 = manifesto (lista de paths, 1 por linha); os paths
  mapeiam para as entries 1..N pela mesma ordem.

Uso:
  psarc_extract.py <psarc> --list
  psarc_extract.py <psarc> <path_no_manifesto> <out_file>
"""
import struct, zlib, sys

def _be(b):
    v = 0
    for x in b: v = (v << 8) | x
    return v

class Psarc:
    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.f.read(32)
        if h[:4] != b'PSAR': raise SystemExit('nao e PSARC')
        self.toc_len = struct.unpack('>I', h[12:16])[0]
        esz = struct.unpack('>I', h[16:20])[0]
        self.n = struct.unpack('>I', h[20:24])[0]
        self.blk = struct.unpack('>I', h[24:28])[0]
        self.f.seek(32); toc = self.f.read(self.n * esz)
        self.ent = []
        for i in range(self.n):
            e = toc[i*esz:(i+1)*esz]
            self.ent.append((struct.unpack('>I', e[16:20])[0], _be(e[20:25]), _be(e[25:30])))
        tbl_off = 32 + self.n * esz
        self.f.seek(tbl_off); tb = self.f.read(self.toc_len - tbl_off)
        self.bsz = [_be(tb[i*2:i*2+2]) for i in range(len(tb)//2)]
        # manifest (entry 0) -> paths
        man = self._read(0).decode('latin1', 'replace')
        self.paths = [p for p in man.replace('\r', '').split('\n') if p.strip()]
        self.idx = {p: i + 1 for i, p in enumerate(self.paths)}  # path -> entry index

    def _read(self, ei):
        bidx, usize, off = self.ent[ei]
        self.f.seek(off); out = b''; bi = bidx
        while len(out) < usize:
            cs = self.bsz[bi]; bi += 1
            raw = self.f.read(cs if cs else self.blk)
            if cs == 0: out += raw
            else:
                try: out += zlib.decompress(raw)
                except zlib.error: out += raw   # bloco armazenado
        return out[:usize]

    def extract(self, path):
        if path not in self.idx: raise SystemExit(f'path nao encontrado: {path}')
        return self._read(self.idx[path])

if __name__ == '__main__':
    p = Psarc(sys.argv[1])
    if len(sys.argv) >= 3 and sys.argv[2] == '--list':
        for pth in p.paths: print(pth)
    elif len(sys.argv) >= 4:
        data = p.extract(sys.argv[2])
        open(sys.argv[3], 'wb').write(data)
        print(f'wrote {sys.argv[3]}: {len(data)} bytes')
    else:
        print(__doc__)
