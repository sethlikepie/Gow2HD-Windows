#!/usr/bin/env python3
"""Extrator de PKG retail de PS3 (AES-128-CTR) -- extração COMPLETA com streaming.

A camada externa do PKG retail/finalized do PS3 usa AES-128-CTR com a chave
pública retail e o contador iniciado pelo data_riv do header -- extrai a árvore
inteira (PARAM.SFO, USRDIR/..., assets) SEM chave de console. Arquivos grandes
são decifrados em streaming (16 MiB/bloco), sem carregar tudo na RAM.

ATENÇÃO: NÃO decifra o EBOOT.BIN/.edat internos -- esses saem como SELF NPDRM
cifrado (a camada SELF é tratada por decrypt_self.py). Assets normais saem em claro.

Uso:
  python extract_pkg.py PKG                 # parse + lista (valida)
  python extract_pkg.py PKG --out DIR       # extrai tudo (streaming)
  python extract_pkg.py PKG --out DIR --only SUBSTR   # só caminhos contendo SUBSTR
"""
import argparse, os, struct, sys
from Crypto.Cipher import AES
from Crypto.Util import Counter

PS3_RETAIL_KEY = bytes.fromhex("2e7b71d7c9c9a14ea3221f188828b8f8")
CHUNK = 1 << 24  # 16 MiB (múltiplo de 16)


def dec_bytes(fin, data_offset, rel, size, iv_int, key):
    """Decifra [rel, rel+size) da seção de dados (tabela e nomes -- coisas pequenas)."""
    base = (rel // 16) * 16
    skip = rel - base
    nblocks = (skip + size + 15) // 16
    fin.seek(data_offset + base)
    ct = fin.read(nblocks * 16)
    ctr = Counter.new(128, initial_value=iv_int + base // 16)
    pt = AES.new(key, AES.MODE_CTR, counter=ctr).decrypt(ct)
    return pt[skip:skip + size]


def extract_file(fin, data_offset, rel, size, iv_int, key, out_path):
    """Decifra um arquivo em streaming, alinhado a 16 bytes (CTR contínuo)."""
    base = (rel // 16) * 16
    skip = rel - base
    ctr = Counter.new(128, initial_value=iv_int + base // 16)
    cipher = AES.new(key, AES.MODE_CTR, counter=ctr)
    fin.seek(data_offset + base)
    to_read = skip + size          # bytes do stream a produzir a partir de 'base'
    out_left = size
    first = True
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as o:
        while to_read > 0 and out_left > 0:
            want = min(CHUNK, to_read)
            if want < to_read:                  # não é o último bloco -> 16-alinha
                want -= want % 16
                if want == 0:
                    want = 16
            ct = fin.read(want)
            if not ct:
                break
            pt = cipher.decrypt(ct)
            if first and skip:
                pt = pt[skip:]
                first = False
            if len(pt) > out_left:
                pt = pt[:out_left]
            o.write(pt)
            out_left -= len(pt)
            to_read -= want


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pkg")
    ap.add_argument("--out", default=None)
    ap.add_argument("--only", default=None)
    args = ap.parse_args()

    with open(args.pkg, "rb") as fin:
        hdr = fin.read(0x80)
        magic = struct.unpack(">I", hdr[0:4])[0]
        if magic != 0x7F504B47:
            sys.exit(f"magic inesperado: 0x{magic:08X} (esperado 0x7F504B47)")
        revision = struct.unpack(">H", hdr[0x04:0x06])[0]
        ptype    = struct.unpack(">H", hdr[0x06:0x08])[0]
        item_count = struct.unpack(">I", hdr[0x14:0x18])[0]
        data_offset = struct.unpack(">Q", hdr[0x20:0x28])[0]
        data_size = struct.unpack(">Q", hdr[0x28:0x30])[0]
        content_id = hdr[0x30:0x54].split(b"\x00")[0].decode("ascii", "replace")
        iv = hdr[0x70:0x80]
        iv_int = int.from_bytes(iv, "big")

        print(f"magic        OK (.PKG)")
        print(f"revision     0x{revision:04X} ({'retail/finalized' if revision & 0x8000 else 'debug'})")
        print(f"type         0x{ptype:04X} ({'PS3' if ptype == 1 else 'PSP/Vita' if ptype == 2 else '?'})")
        print(f"content_id   {content_id}")
        print(f"item_count   {item_count}")
        print(f"data_offset  0x{data_offset:X}")
        print(f"data_size    {data_size} bytes ({data_size/2**30:.2f} GiB)")
        print(f"iv           {iv.hex()}")

        if not (revision & 0x8000) or ptype != 1:
            sys.exit("Não é PKG retail de PS3 (este extrator só trata esse caso).")
        key = PS3_RETAIL_KEY

        table = dec_bytes(fin, data_offset, 0, item_count * 0x20, iv_int, key)
        entries = []
        for i in range(item_count):
            e = table[i * 0x20:(i + 1) * 0x20]
            name_off = struct.unpack(">I", e[0x00:0x04])[0]
            name_sz  = struct.unpack(">I", e[0x04:0x08])[0]
            file_off = struct.unpack(">Q", e[0x08:0x10])[0]
            file_sz  = struct.unpack(">Q", e[0x10:0x18])[0]
            flags    = struct.unpack(">I", e[0x18:0x1C])[0]
            name = dec_bytes(fin, data_offset, name_off, name_sz, iv_int, key)
            name = name.decode("ascii", "replace").rstrip("\x00")
            entries.append((name, file_off, file_sz, flags & 0xFF))

        is_dir = lambda t: t == 4
        nfiles = sum(1 for _, _, _, t in entries if not is_dir(t))
        tot = sum(sz for _, _, sz, t in entries if not is_dir(t))
        print(f"\narquivos: {nfiles}  | soma de tamanhos: {tot/2**30:.2f} GiB")
        big = sorted((e for e in entries if not is_dir(e[3])), key=lambda x: -x[2])[:10]
        print("10 maiores:")
        for name, _, sz, _ in big:
            print(f"  {sz/2**20:9.1f} MB  {name}")

        if not args.out:
            print("\n(modo lista -- use --out DIR para extrair)")
            return

        done = 0
        bytes_done = 0
        for name, off, sz, t in entries:
            if args.only and args.only not in name:
                continue
            out_path = os.path.join(args.out, name.replace("/", os.sep))
            if is_dir(t):
                os.makedirs(out_path, exist_ok=True)
                continue
            extract_file(fin, data_offset, off, sz, iv_int, key, out_path)
            done += 1
            bytes_done += sz
            if done % 50 == 0 or sz > 50 * 2**20:
                print(f"  [{done}/{nfiles}] {bytes_done/2**30:.2f} GiB  <- {name}", flush=True)
        print(f"\nOK: {done} arquivos, {bytes_done/2**30:.2f} GiB extraídos para {args.out}")


if __name__ == "__main__":
    main()
