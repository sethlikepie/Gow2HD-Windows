#!/usr/bin/env python3
"""
decrypt_self.py -- Decripta um SELF/SELF-NPDRM do PS3 para ELF.

Reimplementacao em Python (auditavel, sem binarios externos) da rotina de
decriptacao do RPCS3 (rpcs3/Crypto/unself.cpp, unedat.cpp, key_vault.cpp).
Usa apenas constantes publicas. Suporta apps NPDRM (program_type=8) com
licenca local/network via arquivo RAP, e licenca free via klicensee.

Fluxo:
  RAP -> rap_to_rif -> rifkey
  npdrm_key = AES-128-ECB-dec(NP_KLIC_KEY, rifkey)
  metadata_info(0x40) = AES-128-CBC-dec(npdrm_key, iv=0)        # camada NPDRM
  metadata_info       = AES-256-CBC-dec(erk, riv)               # camada SCE
  metadata_headers    = AES-128-CTR(meta_info.key, meta_info.iv)
  cada secao (encrypted==3) = AES-128-CTR(data_key, data_iv)
  reconstroi ELF: copia ehdr/phdr/shdr em claro + cola dados das secoes em p_offset

Uso:
  python decrypt_self.py <EBOOT.BIN> <saida.elf> [--rap <arquivo.rap>] [--klic <hex32>]
"""
import argparse, struct, sys, zlib
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

# --- constantes publicas (de RPCS3 key_vault.h) ---
NP_KLIC_KEY  = bytes.fromhex("F2FBCA7A75B04EDC1390638CCDFDD1EE")
NP_KLIC_FREE = bytes.fromhex("72F990788F9CFF745725F08E4C128387")
RAP_KEY      = bytes.fromhex("869F7745C13FD890CCF29188E3CC3EDF")
RAP_PBOX     = [0x0C,0x03,0x06,0x04,0x01,0x0B,0x0F,0x08,0x02,0x07,0x00,0x05,0x0A,0x0E,0x0D,0x09]
RAP_E1       = bytes.fromhex("A93E1FD67C55A329B75FDDA62A95C7A5")
RAP_E2       = bytes.fromhex("67D45DA3296D006A4E7C537BF5538C74")

# NPDRM erk/riv por revisao (se_flags). De LoadSelfNPDRMKeys().
NPDRM_KEYS = {
    0x0001: ("F9EDD0301F770FABBA8863D9897F0FEA6551B09431F61312654E28F43533EA6B","A551CCB4A42C37A734A2B4F9657D5540"),
    0x0002: ("8E737230C80E66AD0162EDDD32F1F774EE5E4E187449F19079437A508FCF9C86","7AAECC60AD12AED90C348D8C11D2BED5"),
    0x0003: ("1B715B0C3E8DC4C1A5772EBA9C5D34F7CCFE5B82025D453F3167566497239664","E31E206FBB8AEA27FAB0D9A2FFB6B62F"),
    0x0004: ("BB4DBF66B744A33934172D9F8379A7A5EA74CB0F559BB95D0E7AECE91702B706","ADF7B207A15AC601110E61DDFC210AF6"),
    0x0006: ("8B4C52849765D2B5FA3D5628AFB17644D52B9FFEE235B4C0DB72A62867EAA020","05719DF1B1D0306C03910ADDCE4AF887"),
    0x0007: ("3946DFAA141718C7BE339A0D6C26301C76B568AEBC5CD52652F2E2E0297437C3","E4897BE553AE025CDCBF2B15D1C9234E"),
    0x0009: ("0786F4B0CA5937F515BDCE188F569B2EF3109A4DA0780A7AA07BD89C3350810A","04AD3C2F122A3B35E804850CAD142C6D"),
    0x000A: ("03C21AD78FBB6A3D425E9AAB1298F9FD70E29FD4E6E3A3C151205DA50C413DE4","0A99D4D4F8301A88052D714AD2FB565E"),
    0x000C: ("357EBBEA265FAEC271182D571C6CD2F62CFA04D325588F213DB6B2E0ED166D92","D26E6DD2B74CD78E866E742E5571B84F"),
    0x000D: ("337A51416105B56E40D7CAF1B954CDAF4E7645F28379904F35F27E81CA7B6957","8405C88E042280DBD794EC7E22B74002"),
    0x000F: ("135C098CBE6A3E037EBE9F2BB9B30218DDE8D68217346F9AD33203352FBB3291","4070C898C2EAAD1634A288AA547A35A8"),
    0x0010: ("4B3CD10F6A6AA7D99F9B3A660C35ADE08EF01C2C336B9E46D1BB5678B4261A61","C0F2AB86E6E0457552DB50D7219371C5"),
    0x0013: ("265C93CF48562EC5D18773BEB7689B8AD10C5EB6D21421455DEBC4FB128CBF46","8DEA5FF959682A9B98B688CEA1EF4A1D"),
    0x0016: ("7910340483E419E55F0D33E4EA5410EEEC3AF47814667ECA2AA9D75602B14D4B","4AD981431B98DFD39B6388EDAD742A8E"),
    0x0019: ("FBDA75963FE690CFF35B7AA7B408CF631744EDEF5F7931A04D58FD6A921FFDB3","F72C1D80FFDA2E3BF085F4133E6D2805"),
    0x001C: ("8103EA9DB790578219C4CEDF0592B43064A7D98B601B6C7BC45108C4047AA80F","246F4B8328BE6A2D394EDE20479247C5"),
}

def aes_ecb_dec(key, data): return Cipher(algorithms.AES(key), modes.ECB()).decryptor().update(data)
def aes_cbc_dec(key, iv, data):
    d=Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor(); return d.update(data)+d.finalize()
def aes_ctr(key, iv, data):
    d=Cipher(algorithms.AES(key), modes.CTR(iv)).decryptor(); return d.update(data)+d.finalize()

def rap_to_rif(rap):
    """RAP (16 bytes) -> rifkey/klicensee (16 bytes). Porte de rap_to_rif()."""
    key = bytearray(aes_cbc_dec(RAP_KEY, b"\x00"*16, rap))
    for _ in range(5):
        for i in range(16):
            p = RAP_PBOX[i]; key[p] ^= RAP_E1[p]
        for i in range(15, 0, -1):
            p = RAP_PBOX[i]; pp = RAP_PBOX[i-1]; key[p] ^= key[pp]
        o = 0
        for i in range(16):
            p = RAP_PBOX[i]
            kc = (key[p] - o) & 0xFF
            ec2 = RAP_E2[p]
            if o != 1 or kc != 0xFF:
                o = 1 if kc < ec2 else 0
                key[p] = (kc - ec2) & 0xFF
            elif kc == 0xFF:
                key[p] = (kc - ec2) & 0xFF
            else:
                key[p] = kc
    return bytes(key)

def u16(d,o): return struct.unpack_from(">H",d,o)[0]
def u32(d,o): return struct.unpack_from(">I",d,o)[0]
def u64(d,o): return struct.unpack_from(">Q",d,o)[0]

def decrypt_self(self_bytes, rap_bytes=None, klic=None):
    d = self_bytes
    if d[:4] != b"SCE\x00":
        if d[:4] == b"\x7fELF":
            return d  # ja e um ELF
        raise ValueError("nao e SELF (magic != SCE\\0)")
    se_flags = u16(d, 8); se_meta = u32(d, 0xC); se_hsize = u64(d, 0x10)
    # ext_hdr @0x20
    prog_id_off = u64(d, 0x28); ehdr_off = u64(d, 0x30); phdr_off = u64(d, 0x38)
    shdr_off = u64(d, 0x40); supp_off = u64(d, 0x58); supp_size = u64(d, 0x60)
    program_type = u32(d, prog_id_off + 12)
    if program_type != 8:
        print(f"[aviso] program_type={program_type} (esperado 8=NPDRM). Tentando mesmo assim.", file=sys.stderr)

    # --- NPD: licenca + content_id ---
    npd_license = None
    o = supp_off; end = supp_off + supp_size
    while o < end:
        t = u32(d,o); sz = u32(d,o+4)
        if t == 3:
            npd_license = u32(d, o+0x10+8)
        if sz == 0: break
        o += sz

    # --- npdrm_key ---
    if klic is not None:
        npdrm_key = klic
    elif npd_license in (1, 2):
        if rap_bytes is None or len(rap_bytes) < 16:
            raise ValueError(f"licenca NPDRM {npd_license} exige RAP (--rap)")
        npdrm_key = rap_to_rif(rap_bytes[:16])
    elif npd_license == 3:
        npdrm_key = NP_KLIC_FREE
    else:
        raise ValueError(f"licenca NPDRM invalida: {npd_license}")
    npdrm_key = aes_ecb_dec(NP_KLIC_KEY, npdrm_key)  # decripta com NP_KLIC_KEY

    # --- erk/riv do key vault (NPDRM rev=se_flags) ---
    if se_flags not in NPDRM_KEYS:
        raise ValueError(f"sem chave NPDRM para revisao 0x{se_flags:04X}")
    erk = bytes.fromhex(NPDRM_KEYS[se_flags][0]); riv = bytes.fromhex(NPDRM_KEYS[se_flags][1])

    # --- metadata_info @ se_meta+0x20 ---
    mi_off = se_meta + 0x20
    meta_info = d[mi_off:mi_off+0x40]
    meta_info = aes_cbc_dec(npdrm_key, b"\x00"*16, meta_info)   # camada NPDRM
    meta_info = aes_cbc_dec(erk, riv, meta_info)                # camada SCE (AES-256)
    if meta_info[0x10] != 0 or meta_info[0x30] != 0:
        raise ValueError("falha ao decriptar metadata_info (padding != 0) -- RAP/chave errada?")
    mkey = meta_info[0x00:0x10]; miv = meta_info[0x20:0x30]

    # --- metadata_headers (AES-128-CTR) ---
    mh_off = se_meta + 0x20 + 0x40
    mh_size = se_hsize - (0x20 + se_meta + 0x40)
    mh = aes_ctr(mkey, miv, d[mh_off:mh_off+mh_size])
    section_count = u32(mh, 0xC); key_count = u32(mh, 0x10)
    sec = []
    base = 0x20
    for i in range(section_count):
        b = base + i*0x30
        sec.append(dict(
            data_offset=u64(mh,b), data_size=u64(mh,b+8), type=u32(mh,b+0x10),
            program_idx=u32(mh,b+0x14), encrypted=u32(mh,b+0x20),
            key_idx=u32(mh,b+0x24), iv_idx=u32(mh,b+0x28), compressed=u32(mh,b+0x2C)))
    dk_off = 0x20 + section_count*0x30
    data_keys = mh[dk_off:dk_off + key_count*0x10]

    # --- decripta dados de cada secao ---
    for s in sec:
        raw = d[s["data_offset"]:s["data_offset"]+s["data_size"]]
        if s["encrypted"] == 3 and s["key_idx"] <= key_count-1 and s["iv_idx"] <= key_count:
            dkey = data_keys[s["key_idx"]*0x10:s["key_idx"]*0x10+0x10]
            div  = data_keys[s["iv_idx"]*0x10:s["iv_idx"]*0x10+0x10]
            raw = aes_ctr(dkey, div, raw)
        s["plain"] = raw

    # --- reconstroi ELF ---
    ehdr = d[ehdr_off:ehdr_off+0x40]
    e_phoff=u64(ehdr,0x20); e_shoff=u64(ehdr,0x28)
    e_phentsize=u16(ehdr,0x36); e_phnum=u16(ehdr,0x38)
    e_shentsize=u16(ehdr,0x3A); e_shnum=u16(ehdr,0x3C)
    phdrs = d[phdr_off:phdr_off + e_phnum*e_phentsize]
    out = bytearray()
    def put(off, b):
        nonlocal out
        if off+len(b) > len(out): out.extend(b"\x00"*(off+len(b)-len(out)))
        out[off:off+len(b)] = b
    put(0, ehdr)
    put(e_phoff, phdrs)
    # p_offset/p_filesz por phdr
    poff=[]; pfsz=[]
    for i in range(e_phnum):
        pb = i*e_phentsize
        poff.append(u64(phdrs,pb+8)); pfsz.append(u64(phdrs,pb+32))
    for s in sec:
        if s["type"] == 2 and s["program_idx"] < e_phnum:
            data = s["plain"]
            if s["compressed"] == 2:
                data = zlib.decompress(data)
            put(poff[s["program_idx"]], data)
    if shdr_off != 0 and e_shoff != 0 and e_shnum:
        put(e_shoff, d[shdr_off:shdr_off + e_shnum*e_shentsize])
    return bytes(out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input"); ap.add_argument("output")
    ap.add_argument("--rap"); ap.add_argument("--klic")
    a = ap.parse_args()
    self_bytes = open(a.input,"rb").read()
    rap = open(a.rap,"rb").read() if a.rap else None
    klic = bytes.fromhex(a.klic) if a.klic else None
    elf = decrypt_self(self_bytes, rap, klic)
    open(a.output,"wb").write(elf)
    tag = "ELF" if elf[:4]==b"\x7fELF" else elf[:4].hex()
    print(f"OK -> {a.output} ({len(elf):,} bytes, magic={tag})")

if __name__ == "__main__":
    sys.exit(main())
