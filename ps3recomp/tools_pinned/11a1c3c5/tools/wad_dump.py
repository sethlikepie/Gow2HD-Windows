#!/usr/bin/env python3
"""
wad_dump.py - parser + edgezlib-decoder para os WADs do GoW2 (.wad_ps3).

Contexto (LEVA 20h-20k, ps3recomp-feasibility): o registry de shaders do GoW2
(0x4306C780) fica vazio -> todas as 648 combinacoes "Invalid" -> set_shader
nunca dispara. Os WADs r_perma/r_shella tem uma secao SHGX (SHader Group). O
teste decisivo: a SHGX contem MICROCODIGO FP compilado (=> bug de carregamento
corrigivel) ou so REFERENCIAS ao $/enginesupport/ global (=> dados ausentes)?

Formato do WAD (.wad_ps3, ja descomprimido do psarc):
  - Arvore de records de 0x20 bytes: [u32 type][u32 size][24B name]
    type: 0x15 root, 0x01 node (+ data-info record 0x20B logo apos),
          0x02 GroupStart, 0x03 GroupEnd.
  - Secoes: WAD/TXRX/LGTX/CXT/ANMX/BHVX/PRTX/EEPR/COLX/FX/GFXX/MATX/MDLX/
            SCRX/SNDX/CAMX/WYPX/SHGX (nome "XXXX_R_<lvl>").
  - data-info do node: [flags u32][?0][usz u32][csz u32]; usz==csz==0x20000
    uniforme = provavel BUFFER de LS do descompressor, NAO o tamanho real.
    OFFSET real dos dados: AINDA NAO DECODIFICADO -> a regiao de dados vem
    apos o TOC; as streams sao edgezlib.

Compressao (libs/spurs/lfq_host_inflate.c): edgezlib = RAW DEFLATE, window 4KB
  (wbits=-12), via stbi_zlib_decode_noheader. Decode: zlib.decompressobj(-12).
  IMPORTANTE: passar slice LIMITADO (d[off:off+CAP]) e NAO d[off:] -- o tail de
  20MB por tentativa foi o que causou timeouts nas LEVAS 20j-k.

Uso:
  wad_dump.py <file.wad_ps3> --toc            # lista a arvore de secoes
  wad_dump.py <file.wad_ps3> --streams        # lista streams edgezlib (walk sequencial)
  wad_dump.py <file.wad_ps3> --shgx out.bin   # tenta isolar+dumpar a SHGX
"""
import sys, zlib, struct, re

CAP = 400_000  # limite do slice por tentativa de decode (evita timeout)

def rd_be32(b, o): return struct.unpack_from('>I', b, o)[0]
def rd_le32(b, o): return struct.unpack_from('<I', b, o)[0]

def _align(x, a=0x10):
    return (x + a - 1) & ~(a - 1)

def parse_toc(d, limit=0x8000):
    """Anda a arvore de records; retorna [(name, type, flags, usz, csz, toc_off)].

    Formato do record (decodificado LEVA 20k):
      header = 32B: [u32-LE type][u32-LE size][24B name]   (type/size LITTLE-endian!)
        type: 0x15 root, 0x01 node, 0x02 GroupStart, 0x03 GroupEnd
      node/root: seguido de 'size' bytes de data-info, e o proximo record comeca
        em align16(off + 32 + size). Group: so os 32B (sem data-info).
      data-info (primeiros 16B): [u32 flags][u32 ?][u32 usz][u32 csz] (BE p/ usz/csz).
    """
    secs = []
    off = 0
    while off + 0x20 <= len(d) and off < limit:
        typ = d[off]
        size = rd_le32(d, off + 4)
        name = d[off+8:off+32].split(b'\x00')[0].decode('latin1', 'replace')
        if typ in (0x01, 0x15):  # node/root -> tem data-info de 'size' bytes
            di = off + 0x20
            flags = rd_be32(d, di) if di + 16 <= len(d) else 0
            usz = rd_be32(d, di + 8) if di + 16 <= len(d) else 0
            csz = rd_be32(d, di + 12) if di + 16 <= len(d) else 0
            if typ == 0x01 and name:
                secs.append((name, typ, flags, usz, csz, off))
            off = _align(off + 0x20 + size)
        elif typ in (0x02, 0x03):  # GroupStart/End: so o header
            off += 0x20
        else:
            off += 0x20  # desconhecido: avanca conservador
    return secs

def edge_decode(d, off):
    """Tenta raw-deflate wbits=-12 num slice limitado. Retorna (out, consumido) ou None."""
    try:
        do = zlib.decompressobj(-12)
        out = do.decompress(d[off:off+CAP], 8_000_000)
        if len(out) < 64:
            return None
        consumed = min(CAP, len(d) - off) - len(do.unused_data)
        return out, max(1, consumed)
    except Exception:
        return None

def find_first_stream(d, limit=0x8000):
    for i in range(limit):
        if (d[i] & 7) <= 5:
            r = edge_decode(d, i)
            if r and len(r[0]) > 2000:
                return i
    return None

def walk_streams(d):
    """Walk sequencial usando 'consumido' p/ pular stream-a-stream (eficiente)."""
    start = find_first_stream(d)
    if start is None:
        return []
    streams = []
    off = start
    while off < len(d) - 8 and len(streams) < 8000:
        r = None
        # tolerar padding/alinhamento ate 0x40 bytes ate o proximo stream
        for probe in range(off, min(off + 0x40, len(d) - 4)):
            if (d[probe] & 7) <= 5:
                r = edge_decode(d, probe)
                if r:
                    off = probe
                    break
        if not r:
            break
        out, consumed = r
        streams.append((off, len(out), out))
        off += consumed
    return streams

def classify(out):
    txt = len(re.findall(rb'[ -~]{4,}', out[:8192]))
    low = out.lower()
    shader = any(m in low for m in (b'ps3fx', b'shader', b'texture=', b'constcolor',
                                    b'.cfx', b'enginesupport', b'!!fp', b'default'))
    return txt, shader

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 1
    path, mode = sys.argv[1], sys.argv[2]
    d = open(path, 'rb').read()
    if mode == '--toc':
        for name, typ, flags, usz, csz, o in parse_toc(d):
            print(f"  {name:16s} flags=0x{flags:08X} usz=0x{usz:X} csz=0x{csz:X} @toc0x{o:X}")
    elif mode == '--streams':
        ss = walk_streams(d)
        print(f"streams={len(ss)} total_usz={sum(l for _,l,_ in ss)}")
        for off, ln, out in ss:
            txt, shd = classify(out)
            tag = ' [SHADER]' if shd else ''
            if shd or ln > 0x8000:
                frags = re.findall(rb'[ -~]{5,}', out[:512])[:5]
                print(f" @0x{off:X} usz={ln}(0x{ln:X}) txt={txt}{tag} {b'|'.join(frags)[:120]}")
    elif mode == '--shgx':
        out_path = sys.argv[3] if len(sys.argv) > 3 else 'shgx.bin'
        # TODO(prox sessao): resolver o offset REAL da secao SHGX no data-region.
        # Por ora: walk + reportar streams que casam com o tamanho/posicao da SHGX.
        ss = walk_streams(d)
        for off, ln, out in ss:
            txt, shd = classify(out)
            if shd:
                open(out_path, 'wb').write(out)
                print(f"stream com shader-marker dumpado -> {out_path} (@0x{off:X} usz={ln})")
                return 0
        print("nenhum stream com shader-marker (txt); SHGX pode ser microcodigo binario -- "
              "resolver offset da SHGX e dumpar por posicao")
    else:
        print(__doc__); return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
