#!/usr/bin/env python3
"""
Gera fixtures SINTETICAS (zero dados de jogo) para test_native_loader.c:

  <dir>/fixture.psarc        PSARC v1.4 zlib (mesmo formato do gow2.psarc):
                             manifesto + 3 entradas que exercitam: bloco unico
                             comprimido, multi-bloco, bloco cheio armazenado
                             (incompressivel) e bloco parcial armazenado.
  <dir>/expected_<n>.bin     conteudo esperado de cada entrada (byte a byte)
  <dir>/fixture.wad          pacote estilo WAD GoW2 com um MODL (directoria de
                             atributos + streams com header de 12B + descritor
                             de indices) e um registo de textura ([type][sz LE]
                             [nome 24B][payload: header 152B + mip chain ARGB8
                             Morton-swizzled]).
  <dir>/expected_mesh.bin    KMH2 esperado (nverts, x,y,z,u,v f32 LE, ntris, idx)
  <dir>/expected_tex.bin     u32 w, u32 h (LE) + RGBA esperado
  <dir>/manifest.txt         paths do psarc (1/linha), p/ o teste iterar

O escritor PSARC segue a spec publica (mesma do leitor psarc_extract.py); o
WAD segue o RE clean-room de gen_wad_mesh_rxs.py / decode_hero_tex.py
(streams +12, POS0 (x,y,z,w) f32 BE, TEX0 half BE, cnt do descritor em +12,
textura com header derivado = payload_len - mip_chain).

Uso: python3 gen_native_fixtures.py <dir>
"""
import math
import os
import struct
import sys
import zlib

BLK = 65536


# ---------------------------------------------------------------- PSARC ----
def make_psarc(out_path, files):
    """files: [(path_str, bytes)] -> escreve PSARC v1.4 zlib."""
    manifest = '\n'.join(p for p, _ in files).encode('latin1')
    entries = [manifest] + [c for _, c in files]
    block_table = []
    blobs = []
    for content in entries:
        bidx = len(block_table)
        out = b''
        off = 0
        while off < len(content):
            raw = content[off:off + BLK]
            off += len(raw)
            comp = zlib.compress(raw, 9)
            if len(raw) == BLK and len(comp) >= BLK:
                block_table.append(0)                # bloco cheio armazenado
                out += raw
            elif len(comp) >= len(raw):
                block_table.append(len(raw))         # parcial armazenado
                out += raw
            else:
                block_table.append(len(comp))
                out += comp
        blobs.append((bidx, out, len(content)))
    n = len(entries)
    toc_len = 32 + n * 30 + 2 * len(block_table)
    ents = []
    off = toc_len
    for (bidx, blob, usize) in blobs:
        ents.append((bidx, usize, off))
        off += len(blob)
    with open(out_path, 'wb') as f:
        f.write(b'PSAR')
        f.write(struct.pack('>HH', 1, 4))
        f.write(b'zlib')
        f.write(struct.pack('>IIIII', toc_len, 30, n, BLK, 0))  # ..., flags (header=32B)
        for (bidx, usize, eoff) in ents:
            f.write(b'\x00' * 16)                    # md5 (ignorado pelo leitor)
            f.write(struct.pack('>I', bidx))
            f.write(usize.to_bytes(5, 'big'))
            f.write(eoff.to_bytes(5, 'big'))
        for b in block_table:
            f.write(struct.pack('>H', b))
        for (_, blob, _) in blobs:
            f.write(blob)
    print(f'wrote {out_path}: {n} entries, {len(block_table)} blocks')


def psarc_test_files():
    rng_state = [0x12345678]

    def rng():                                       # LCG deterministico
        rng_state[0] = (rng_state[0] * 1103515245 + 12345) & 0x7FFFFFFF
        return rng_state[0] & 0xFF

    small = b'ola do psarc nativo!\n' * 3
    zeros = b'\x00' * 70000                          # 2 blocos, muito compressivel
    rand = bytes(rng() for _ in range(150000))       # incompressivel
    # garante que os blocos armazenados nao parseiam como zlib (CMF=0 invalido)
    rand = b'\x00\x00' + rand[2:]
    rand = rand[:BLK] + b'\x00\x00' + rand[BLK + 2:]
    rand = rand[:2 * BLK] + b'\x00\x00' + rand[2 * BLK + 2:]
    return [('wad/a.txt', small), ('/wad/zeros.bin', zeros), ('data/rand.bin', rand)]


# ------------------------------------------------------------------ WAD ----
def f32(v):
    """roundtrip por f32 BE p/ golden byte-exacto."""
    return struct.unpack('>f', struct.pack('>f', v))[0]


def half_bits(v):
    return struct.unpack('>H', struct.pack('>e', v))[0]


def half_val(v):
    return struct.unpack('>e', struct.pack('>e', v))[0]


def log2_ceil(n):
    l = 0
    while (1 << l) < n:
        l += 1
    return l


def morton_offset(x, y, lw, lh):
    off = shift = xi = yi = 0
    rx, ry = lw, lh
    while rx or ry:
        if rx:
            off |= ((x >> xi) & 1) << shift
            shift += 1; xi += 1; rx -= 1
        if ry:
            off |= ((y >> yi) & 1) << shift
            shift += 1; yi += 1; ry -= 1
    return off


def mip_chain_bytes(w, h, bpp):
    total = 0
    cw, ch = w, h
    while True:
        total += cw * ch * bpp
        if cw == 1 and ch == 1:
            break
        cw = max(1, cw // 2)
        ch = max(1, ch // 2)
    return total


def make_wad(out_path, mesh_golden_path, tex_golden_path):
    NV = 300
    verts = []                                       # (x,y,z,w) ja roundtripped
    for i in range(NV):
        a = 2.0 * math.pi * i / NV
        verts.append((f32(math.sin(a) * 5.0), f32(i * 0.1), f32(math.cos(a) * 3.0), f32((i % 5) / 4.0)))
    verts[7] = (float('nan'), verts[7][1], verts[7][2], verts[7][3])   # vertice invalido
    uvs = [(half_val((i % 256) / 256.0), half_val((i % 128) / 128.0)) for i in range(NV)]

    tris = []
    for i in range(0, NV - 2, 3):
        tris.append((i, i + 1, i + 2))
    # casos de filtragem: degenerado, fora de range, toca vertice invalido
    idx_flat = []
    for t in tris:
        idx_flat += list(t)
    idx_flat += [10, 10, 11]          # degenerado (2 iguais)
    idx_flat += [0, 1, NV + 50]       # fora de range
    idx_flat += [7, 8, 9]             # usa o vertice NaN -> filtrado
    valid_tris = [t for t in tris if 7 not in t]

    # skinning real-format: BONI = (b0,b1,0,0) por vertice; peso = w do POS0
    bones = [(i % 12, (i * 7) % 12) for i in range(NV)]
    pos_stream = b'\xff' * 12 + b''.join(struct.pack('>ffff', *v) for v in verts)
    tex_stream = b'\xff' * 12 + b''.join(struct.pack('>HH', half_bits(u), half_bits(v)) for (u, v) in uvs)
    nrm_stream = b'\xff' * 12 + b'\x11' * (NV * 4)
    col_stream = b'\xff' * 12 + b'\x22' * (NV * 8)
    bon_stream = b'\xff' * 12 + b''.join(bytes((b0, b1, 0, 0)) for (b0, b1) in bones)
    desc = b'\xee' * 12 + struct.pack('>I', len(idx_flat)) + b''.join(struct.pack('>I', i) for i in idx_flat)

    # directoria: MODL,POS0,NRM0,COL0,TEX0,BONI (offsets relativos ao MODL)
    dir_sz = 6 * 16
    off = dir_sz
    lay = []
    for tag, blob in (
            (b'POS0', pos_stream), (b'NRM0', nrm_stream), (b'COL0', col_stream),
            (b'TEX0', tex_stream), (b'BONI', bon_stream), (b'MODL', desc)):
        lay.append((tag, off, blob))
        off += len(blob)
    dire = b''
    order = {b'MODL': 0, b'POS0': 1, b'NRM0': 2, b'COL0': 3, b'TEX0': 4, b'BONI': 5}
    for tag in sorted((t for t, _, _ in lay), key=lambda t: order[t]):
        toff, blob = next((o, b) for t, o, b in lay if t == tag)
        dire += tag + struct.pack('>III', 0x20002, toff, len(blob))
    modl_block = dire + b''.join(b for _, _, b in lay)

    # ---- registo de textura (framing [type u32][sz u32 LE][nome 24B][corpo]) --
    TW = TH = 32
    TAG = b'~fixture_tex'
    name = (TAG + b'~pal_t').ljust(24, b'\x00')
    argb = bytearray(TW * TH * 4)
    for y in range(TH):
        for x in range(TW):
            i = (y * TW + x) * 4
            argb[i:i + 4] = bytes((255, x * 255 // (TW - 1), y * 255 // (TH - 1), 128))  # A,R,G,B
    lw, lh = log2_ceil(TW), log2_ceil(TH)
    swiz = bytearray(TW * TH * 4)
    for y in range(TH):
        for x in range(TW):
            s = morton_offset(x, y, lw, lh) * 4
            d = (y * TW + x) * 4
            swiz[s:s + 4] = argb[d:d + 4]
    mip_total = mip_chain_bytes(TW, TH, 4)
    hdr = bytearray(152)
    struct.pack_into('>I', hdr, 8, mip_total + 152)          # data_size
    struct.pack_into('>I', hdr, 24, mip_total)               # mip_size
    struct.pack_into('>I', hdr, 36, (TW << 16) | TH)         # dim
    payload = bytes(hdr) + bytes(swiz) + b'\x00' * (mip_total - TW * TH * 4)
    tex_rec = struct.pack('<I', 7) + struct.pack('<I', 24 + len(payload)) + name + payload
    decoy = struct.pack('<I', 7) + struct.pack('<I', 24) + name  # sz==24 (<64): ignorado

    junk0 = b'JUNKJUNK' * 16
    wad = junk0 + decoy + b'PAD.' * 8
    modl_off = len(wad)
    wad += modl_block + b'PAD!' * 8 + tex_rec + b'FIM.' * 4
    with open(out_path, 'wb') as f:
        f.write(wad)
    print(f'wrote {out_path}: modl_off={modl_off:#x} nverts={NV} tris_validos={len(valid_tris)}')

    # ---- goldens ----
    with open(mesh_golden_path, 'wb') as f:
        f.write(b'KMH2')
        f.write(struct.pack('<I', NV))
        for i, (x, y, z, _w) in enumerate(verts):
            u, v = uvs[i]
            if x != x:
                x = y = z = 0.0
            if u != u:
                u = 0.0
            if v != v:
                v = 0.0
            f.write(struct.pack('<fffff', x, y, z, u, v))
        f.write(struct.pack('<I', len(valid_tris)))
        for t in valid_tris:
            f.write(struct.pack('<III', *t))
    rgba = bytearray(TW * TH * 4)
    for i in range(TW * TH):
        a, r, g, b = argb[i * 4:i * 4 + 4]
        rgba[i * 4:i * 4 + 4] = bytes((r, g, b, a))
    with open(tex_golden_path, 'wb') as f:
        f.write(struct.pack('<II', TW, TH))
        f.write(bytes(rgba))

    # golden do skinning: NV x (b0,b1 u8) + NV x w f32 LE (NaN -> 1.0)
    skin_golden = tex_golden_path.replace('expected_tex', 'expected_skin')
    with open(skin_golden, 'wb') as f:
        f.write(struct.pack('<I', NV))
        for (b0, b1) in bones:
            f.write(bytes((b0, b1)))
        for (_x, _y, _z, w) in verts:
            f.write(struct.pack('<f', 1.0 if w != w else w))
    return modl_off


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else '/tmp/native_fixtures'
    os.makedirs(d, exist_ok=True)
    files = psarc_test_files()
    make_psarc(os.path.join(d, 'fixture.psarc'), files)
    for i, (p, content) in enumerate(files):
        with open(os.path.join(d, f'expected_{i}.bin'), 'wb') as f:
            f.write(content)
    with open(os.path.join(d, 'manifest.txt'), 'w') as f:
        for p, _ in files:
            f.write(p + '\n')
    modl_off = make_wad(os.path.join(d, 'fixture.wad'),
                        os.path.join(d, 'expected_mesh.bin'),
                        os.path.join(d, 'expected_tex.bin'))
    with open(os.path.join(d, 'modl_off.txt'), 'w') as f:
        f.write(f'{modl_off:#x}\n')
    print('fixtures OK')


if __name__ == '__main__':
    main()
