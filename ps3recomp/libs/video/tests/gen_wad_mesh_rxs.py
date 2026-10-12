#!/usr/bin/env python3
"""
Extrai a malha 3D REAL de um WAD do GoW2 (chunk MDLX/MODL) e emite .rxs para o
replay_mac (Metal) ou .mesh (KMSH/KMH2) para os viewers -- offline, sem emulador.

Formato de malha (RE clean-room):
  MODL -> directoria de atributos de 16B (TAG|type|offset|size), tags
  POS0/NRM0/COL0/TEX0/BONI, offsets relativos ao MODL. TODOS os streams de
  vertice comecam 12 bytes DEPOIS do offset da directoria (header/sentinela de
  12B; no POS0 e ffffffff x3). Confirmado empiricamente: fase +12 zera spikes,
  NaN e UV esticadas (fases 0/4/8 dao 88+ spikes / 116 UVs rasgadas).
  POS0 = (x,y,z,w) float32 BE stride 16, layout natural (y = vertical, w = peso
  de skin 0..1). TEX0 = (u,v) half BE stride 4. A entrada MODL aponta p/ um
  descritor: index_count u32 BE em +12 seguido do index buffer u32 BE (lista de
  triangulos alinhada, confirmado: repeticoes (a,a) caem na fronteira das
  janelas de 3). Apos os indices: 5 segmentos de skinning (paleta de bones +
  bbox), nao usados aqui. PS3 = big-endian.

Uso: python3 gen_wad_mesh_rxs.py <wad> <out> [modl_hex] [solid|points|kmesh|kmh2]
  solid  -> .rxs (flat shading + painter sort) p/ replay_mac
  kmesh  -> KMSH LE (pos+indices) p/ mesh_view / mesh_spin
  kmh2   -> KMH2 LE (pos+uv+indices) p/ mesh_spin_tex
"""
import struct, sys, math
import gen_synthetic_rxs as g

STREAM_HDR = 12   # bytes de header/sentinela no inicio de cada stream de vertice

def parse_modl(d, m):
    a = {}; o = m
    for _ in range(8):
        t = d[o:o+4]
        if not t.isalnum(): break
        typ, off, sz = struct.unpack('>III', d[o+4:o+16]); a[t.decode('latin1')] = (off, sz); o += 16
        if t == b'BONI': break
    return a

def half(h):
    s, e, m = (h >> 15) & 1, (h >> 10) & 0x1f, h & 0x3ff
    if e == 0: v = m / 1024.0 * 2**-14
    elif e == 31: v = float('nan')
    else: v = (1 + m / 1024.0) * 2**(e - 15)
    return -v if s else v

def extract_mesh(d, modl):
    a = parse_modl(d, modl)
    off, sz = a['POS0']; base = modl + off + STREAM_HDR; n = sz // 16
    P = []
    for i in range(n):
        x, y, z = struct.unpack('>fff', d[base+i*16:base+i*16+12])
        valid = x == x and y == y and z == z and max(abs(x), abs(y), abs(z)) < 1e4
        P.append((x, y, z) if valid else None)
    # index buffer (descritor MODL): lista de triangulos u32 BE
    moff, _ = a['MODL']; b = modl + moff
    cnt = struct.unpack('>I', d[b+12:b+16])[0]; io = b + 16
    idx = struct.unpack('>%dI' % cnt, d[io:io+cnt*4])
    tris = [(idx[i], idx[i+1], idx[i+2]) for i in range(0, cnt - 2, 3)
            if len({idx[i], idx[i+1], idx[i+2]}) == 3
            and idx[i] < n and idx[i+1] < n and idx[i+2] < n
            and P[idx[i]] and P[idx[i+1]] and P[idx[i+2]]]
    return P, tris

def extract_uvs(d, modl, n):
    a = parse_modl(d, modl)
    off, sz = a['TEX0']; base = modl + off + STREAM_HDR
    uvs = []
    for i in range(n):
        hu, hv = struct.unpack('>HH', d[base+i*4:base+i*4+4])
        uvs.append((half(hu), half(hv)))
    return uvs

# ---- exports binarios p/ os viewers Metal (little-endian nativo) ----

def export_kmesh(path, P, tris):
    with open(path, 'wb') as f:
        f.write(b'KMSH'); f.write(struct.pack('<I', len(P)))
        for p in P: f.write(struct.pack('<fff', *(p or (0.0, 0.0, 0.0))))
        f.write(struct.pack('<I', len(tris)))
        for t in tris: f.write(struct.pack('<III', *t))
    print(f'wrote {path}: {len(P)} verts, {len(tris)} tris (KMSH)')

def export_kmh2(path, P, uvs, tris):
    with open(path, 'wb') as f:
        f.write(b'KMH2'); f.write(struct.pack('<I', len(P)))
        for p, (u, v) in zip(P, uvs):
            x, y, z = p or (0.0, 0.0, 0.0)
            if u != u or v != v: u = v = 0.0
            f.write(struct.pack('<fffff', x, y, z, u, v))
        f.write(struct.pack('<I', len(tris)))
        for t in tris: f.write(struct.pack('<III', *t))
    print(f'wrote {path}: {len(P)} verts+uv, {len(tris)} tris (KMH2)')

# ---- projeccao/render 2D (modo solid -> .rxs) ----

def make_proj(P):
    pts = [p for p in P if p]
    cx = sum(p[0] for p in pts)/len(pts); cy = sum(p[1] for p in pts)/len(pts); cz = sum(p[2] for p in pts)/len(pts)
    ext = max(max(abs(p[k]-(cx, cy, cz)[k]) for p in pts) for k in range(3)) or 1.0
    ay, ax = math.radians(26), math.radians(10)
    W, H = g.WIDTH, g.HEIGHT
    def view(p):   # -> (x,y,z) em espaco de camera normalizado
        x, y, z = (p[0]-cx)/ext, (p[1]-cy)/ext, (p[2]-cz)/ext
        x, z = x*math.cos(ay)+z*math.sin(ay), -x*math.sin(ay)+z*math.cos(ay)
        y, z = y*math.cos(ax)-z*math.sin(ax), y*math.sin(ax)+z*math.cos(ax)
        return (x, y, z)
    def scr(v):
        zc = v[2] + 3.0
        return (W/2 + 105*v[0]/zc, H/2 - 105*v[1]/zc)
    return view, scr

def emit(path, verts):   # verts: lista de (sx,sy, r,g,b,a); triangulos consecutivos
    g.VERTICES = None
    vert_data = b''.join(g.be_f32(sx) + g.be_f32(sy) + g.be_f32(0.0) + b''.join(g.be_f32(c) for c in col)
                         for (sx, sy, col) in verts)
    blocks = [(g.RSX_LOCATION_LOCAL, g.VERT_BASE_ADDR, len(vert_data), 0)]
    rec = []; m = lambda a, b: rec.append((a, b))
    m(g.M_VTXFMT+0, g.vtxfmt(g.RSX_VTX_TYPE_FLOAT, 3, g.VERT_STRIDE))
    m(g.M_VTXBUF_OFFSET+0, g.vtxbuf_offset(g.RSX_LOCATION_LOCAL, g.VERT_BASE_ADDR))
    m(g.M_VTXFMT+12, g.vtxfmt(g.RSX_VTX_TYPE_FLOAT, 4, g.VERT_STRIDE))
    m(g.M_VTXBUF_OFFSET+12, g.vtxbuf_offset(g.RSX_LOCATION_LOCAL, g.VERT_BASE_ADDR+12))
    m(g.M_VERTEX_DATA_BASE, 0); rec.append((g.APPLY_BLOCK_FLAG, 0))
    m(g.M_DMA_COLOR0, g.DMA_HANDLE_LOCAL); m(g.M_COLOR0_OFFSET, 0)
    m(g.M_VIEWPORT_HORIZ, g.horiz_vert(0, g.WIDTH)); m(g.M_VIEWPORT_VERT, g.horiz_vert(0, g.HEIGHT))
    m(g.M_CLEAR_COLOR_VALUE, g.CLEAR_ARGB)
    m(g.M_CLEAR_BUFFERS, g.RSX_CLEAR_COLOR_R|g.RSX_CLEAR_COLOR_G|g.RSX_CLEAR_COLOR_B|g.RSX_CLEAR_COLOR_A)
    nv = len(verts); B = 255 - 255 % 3; first = 0
    while first < nv:
        cnt = min(B, nv-first)
        m(g.M_VERTEX_BEGIN_END, g.PRIM_TRIANGLES); m(g.M_VB_VERTEX_BATCH, g.vb_batch(first, cnt)); m(g.M_VERTEX_BEGIN_END, 0)
        first += cnt
    m(g.M_GCM_DRIVER_FLIP, 0)
    with open(path, 'wb') as f:
        f.write(b'RXS1'); [f.write(g.u32(x)) for x in (2, len(blocks), len(rec), 0, 0, g.WIDTH, g.HEIGHT)]
        disp = [(g.WIDTH, g.HEIGHT, g.WIDTH*4, 0)]; f.write(g.u32(len(disp)))
        for i in range(8):
            w, h, p, o = disp[i] if i < len(disp) else (0, 0, 0, 0)
            [f.write(g.u32(x)) for x in (w, h, p, o)]
        for bl in blocks: [f.write(g.u32(x)) for x in bl]
        f.write(vert_data)
        for (a, b) in rec: f.write(g.u32(a)); f.write(g.u32(b))
    print(f"wrote {path}: {nv} verts ({nv//3} tri), {(nv+B-1)//B} batches")

def build_solid(path, P, tris):
    view, scr = make_proj(P)
    L = (0.30, 0.55, 0.76)  # direcao de luz (normalizada abaixo)
    ln = math.sqrt(sum(c*c for c in L)); L = tuple(c/ln for c in L)
    faces = []
    for (i0, i1, i2) in tris:
        a, b, c = view(P[i0]), view(P[i1]), view(P[i2])
        # normal de face (cross) p/ shading + depth do centroide p/ painter
        ux, uy, uz = (b[0]-a[0], b[1]-a[1], b[2]-a[2]); vx, vy, vz = (c[0]-a[0], c[1]-a[1], c[2]-a[2])
        nx, ny, nz = uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx
        nl = math.sqrt(nx*nx+ny*ny+nz*nz) or 1.0
        d = abs((nx*L[0]+ny*L[1]+nz*L[2])/nl)     # two-sided
        sh = 0.22 + 0.78*d
        depth = (a[2]+b[2]+c[2])/3.0
        faces.append((depth, (a, b, c), sh))
    faces.sort(key=lambda f: -f[0])   # back-to-front
    verts = []
    for _, (a, b, c), sh in faces:
        col = (0.62*sh+0.08, 0.60*sh+0.06, 0.58*sh+0.05, 1.0)   # cinza-quente (clay)
        for v in (a, b, c):
            sx, sy = scr(v); verts.append((sx, sy, col))
    emit(path, verts)

if __name__ == '__main__':
    wad, out = sys.argv[1], sys.argv[2]
    modl = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x2d29cb
    mode = sys.argv[4] if len(sys.argv) > 4 else 'solid'
    d = open(wad, 'rb').read()
    P, tris = extract_mesh(d, modl)
    print(f"malha: {sum(1 for p in P if p)} verts, {len(tris)} triangulos")
    if mode == 'kmesh':
        export_kmesh(out, P, tris)
    elif mode == 'kmh2':
        export_kmh2(out, P, extract_uvs(d, modl, len(P)), tris)
    else:
        build_solid(out, P, tris)
