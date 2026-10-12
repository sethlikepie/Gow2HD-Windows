"""unself.py -- decrypt a PS3 NPDRM SELF (EBOOT.BIN) into a plain ELF, with the owner's .rap licence.

Python port of the kit's ps3recomp/tools/unself/ps3_unself.c (which uses macOS CommonCrypto + zlib),
same layers and the same key table (ps3_unself's npdrm_keys.inc is parsed at run time). AES comes from
Windows CNG (bcrypt.dll, via ctypes), inflate from Python's zlib: no extra packages, no compiler.

    unself(eboot_bin, out_elf, rap=<path>, keys_inc=<path to npdrm_keys.inc>)
"""
import ctypes
import re
import struct
import zlib
from ctypes import wintypes

NP_KLIC_KEY = bytes([0xF2, 0xFB, 0xCA, 0x7A, 0x75, 0xB0, 0x4E, 0xDC, 0x13, 0x90, 0x63, 0x8C, 0xCD, 0xFD, 0xD1, 0xEE])
NP_KLIC_FREE = bytes([0x72, 0xF9, 0x90, 0x78, 0x8F, 0x9C, 0xFF, 0x74, 0x57, 0x25, 0xF0, 0x8E, 0x4C, 0x12, 0x83, 0x87])
RAP_KEY = bytes([0x86, 0x9F, 0x77, 0x45, 0xC1, 0x3F, 0xD8, 0x90, 0xCC, 0xF2, 0x91, 0x88, 0xE3, 0xCC, 0x3E, 0xDF])
RAP_PBOX = [0x0C, 0x03, 0x06, 0x04, 0x01, 0x0B, 0x0F, 0x08, 0x02, 0x07, 0x00, 0x05, 0x0A, 0x0E, 0x0D, 0x09]
RAP_E1 = [0xA9, 0x3E, 0x1F, 0xD6, 0x7C, 0x55, 0xA3, 0x29, 0xB7, 0x5F, 0xDD, 0xA6, 0x2A, 0x95, 0xC7, 0xA5]
RAP_E2 = [0x67, 0xD4, 0x5D, 0xA3, 0x29, 0x6D, 0x00, 0x6A, 0x4E, 0x7C, 0x53, 0x7B, 0xF5, 0x53, 0x8C, 0x74]


class UnselfError(Exception):
    pass


# ---- AES through Windows CNG ------------------------------------------------------------------
_bc = ctypes.WinDLL("bcrypt.dll")
_HANDLE = ctypes.c_void_p
_bc.BCryptOpenAlgorithmProvider.argtypes = [ctypes.POINTER(_HANDLE), wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.ULONG]
_bc.BCryptSetProperty.argtypes = [_HANDLE, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.ULONG, wintypes.ULONG]
_bc.BCryptGenerateSymmetricKey.argtypes = [_HANDLE, ctypes.POINTER(_HANDLE), ctypes.c_void_p, wintypes.ULONG,
                                           ctypes.c_char_p, wintypes.ULONG, wintypes.ULONG]
_bc.BCryptEncrypt.argtypes = _bc.BCryptDecrypt.argtypes = [
    _HANDLE, ctypes.c_char_p, wintypes.ULONG, ctypes.c_void_p, ctypes.c_char_p, wintypes.ULONG,
    ctypes.c_char_p, wintypes.ULONG, ctypes.POINTER(wintypes.ULONG), wintypes.ULONG]
_bc.BCryptDestroyKey.argtypes = [_HANDLE]
_bc.BCryptCloseAlgorithmProvider.argtypes = [_HANDLE, wintypes.ULONG]


def _aes(key, data, iv=None, encrypt=False):
    """AES-128/256, ECB (iv None) or CBC, no padding."""
    if len(data) % 16:
        raise UnselfError("AES input not a multiple of 16 bytes")
    alg = _HANDLE()
    if _bc.BCryptOpenAlgorithmProvider(ctypes.byref(alg), "AES", None, 0):
        raise UnselfError("BCryptOpenAlgorithmProvider(AES) failed")
    hkey = _HANDLE()
    try:
        mode = ctypes.create_unicode_buffer("ChainingModeCBC" if iv is not None else "ChainingModeECB")
        if _bc.BCryptSetProperty(alg, "ChainingMode", mode, ctypes.sizeof(mode), 0):
            raise UnselfError("BCryptSetProperty(ChainingMode) failed")
        if _bc.BCryptGenerateSymmetricKey(alg, ctypes.byref(hkey), None, 0, bytes(key), len(key), 0):
            raise UnselfError("BCryptGenerateSymmetricKey failed")
        out = ctypes.create_string_buffer(len(data))
        n = wintypes.ULONG(0)
        ivb = ctypes.create_string_buffer(bytes(iv), 16) if iv is not None else None
        fn = _bc.BCryptEncrypt if encrypt else _bc.BCryptDecrypt
        st = fn(hkey, bytes(data), len(data), None, ivb, 16 if iv is not None else 0, out, len(data), ctypes.byref(n), 0)
        if st or n.value != len(data):
            raise UnselfError("AES %s failed (status 0x%08X)" % ("encrypt" if encrypt else "decrypt", st & 0xFFFFFFFF))
        return out.raw
    finally:
        if hkey:
            _bc.BCryptDestroyKey(hkey)
        _bc.BCryptCloseAlgorithmProvider(alg, 0)


def _aes_ctr(key, iv, data):
    """AES-128-CTR, big-endian 128-bit counter (CommonCrypto kCCModeOptionCTR_BE)."""
    if not data:
        return b""
    nblk = (len(data) + 15) // 16
    c0 = int.from_bytes(iv, "big")
    mask = (1 << 128) - 1
    ctr = b"".join(((c0 + i) & mask).to_bytes(16, "big") for i in range(nblk))
    ks = _aes(key, ctr, encrypt=True)[:len(data)]
    return (int.from_bytes(data, "big") ^ int.from_bytes(ks, "big")).to_bytes(len(data), "big")


def rap_to_klicensee(rap):
    key = bytearray(_aes(RAP_KEY, rap, iv=bytes(16)))
    for _ in range(5):
        for i in range(16):
            key[RAP_PBOX[i]] ^= RAP_E1[RAP_PBOX[i]]
        for i in range(15, 0, -1):
            key[RAP_PBOX[i]] ^= key[RAP_PBOX[i - 1]]
        o = 0
        for i in range(16):
            p = RAP_PBOX[i]
            kc = (key[p] - o) & 0xFF
            ec2 = RAP_E2[p]
            if o != 1 or kc != 0xFF:
                o = 1 if kc < ec2 else 0
                key[p] = (kc - ec2) & 0xFF
            else:
                key[p] = (kc - ec2) & 0xFF
    return bytes(key)


def load_keys(inc_path):
    """npdrm_keys.inc lines: { 0xREV, { 32 bytes erk }, { 16 bytes riv } },"""
    keys = {}
    txt = open(inc_path, encoding="utf-8").read()
    for m in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*\{([^}]*)\}\s*,\s*\{([^}]*)\}\s*\}", txt):
        erk = bytes(int(x, 16) for x in m.group(2).replace(" ", "").split(",") if x)
        riv = bytes(int(x, 16) for x in m.group(3).replace(" ", "").split(",") if x)
        if len(erk) == 32 and len(riv) == 16:
            keys[int(m.group(1), 16)] = (erk, riv)
    if not keys:
        raise UnselfError("no keys parsed from %s" % inc_path)
    return keys


def unself(src, dst, rap=None, keys_inc=None, klic=None):
    f = open(src, "rb").read()
    n = len(f)
    be16 = lambda o: struct.unpack_from(">H", f, o)[0]
    be32 = lambda o: struct.unpack_from(">I", f, o)[0]
    be64 = lambda o: struct.unpack_from(">Q", f, o)[0]
    if n < 0x80 or f[:4] != b"SCE\0":
        raise UnselfError("not a SELF (no SCE header)")
    key_rev, meta_off, hdr_len = be16(0x08), be32(0x0C), be64(0x10)
    app_off, elf_off, phdr_off, shdr_off = be64(0x28), be64(0x30), be64(0x38), be64(0x40)
    ctrl_off, ctrl_size = be64(0x58), be64(0x60)
    self_type = be32(app_off + 0x0C)
    if key_rev & 0x8000:
        raise UnselfError("debug SELF: not encrypted this way")
    if self_type != 8:
        raise UnselfError("not an NPDRM application SELF")
    npd = None
    o = ctrl_off
    while o + 0x40 <= ctrl_off + ctrl_size and o + 0x40 <= n:
        if f[o:o + 4] == b"NPD\0":
            npd = o
            break
        o += 4
    if npd is None:
        raise UnselfError("no NPDRM header")
    license_ = be32(npd + 8)
    content_id = f[npd + 0x10:npd + 0x40].split(b"\0")[0].decode("ascii", "replace")
    if license_ == 3:
        klic_b = NP_KLIC_FREE
    elif klic:
        klic_b = bytes.fromhex(klic)
    elif rap:
        r = open(rap, "rb").read()
        if len(r) != 16:
            raise UnselfError("the .rap file must be 16 bytes")
        klic_b = rap_to_klicensee(r)
    else:
        raise UnselfError("%s needs its licence: %s.rap" % (content_id, content_id))
    keys = load_keys(keys_inc)
    if key_rev not in keys:
        raise UnselfError("no NPDRM key for key revision %d" % key_rev)
    erk, riv = keys[key_rev]

    if meta_off + 0x20 + 0x40 > n:
        raise UnselfError("truncated metadata")
    info = f[meta_off + 0x20:meta_off + 0x60]
    npkey = _aes(NP_KLIC_KEY, klic_b)
    info = _aes(npkey, info, iv=bytes(16))
    info = _aes(erk, info, iv=riv)
    if any(info[0x10:0x20]) or any(info[0x30:0x40]):
        raise UnselfError("metadata did not decrypt (wrong licence or key)")

    mh_off = meta_off + 0x20 + 0x40
    if hdr_len <= mh_off or hdr_len > n:
        raise UnselfError("bad header length")
    mh = _aes_ctr(info[0x00:0x10], info[0x20:0x30], f[mh_off:hdr_len])
    sec_count, key_count = struct.unpack_from(">I", mh, 0x0C)[0], struct.unpack_from(">I", mh, 0x10)[0]
    if 0x20 + sec_count * 0x30 + key_count * 16 > len(mh):
        raise UnselfError("bad metadata counts")
    keys_off = 0x20 + sec_count * 0x30

    eh = f[elf_off:elf_off + 0x40]
    if eh[:4] != b"\x7fELF" or eh[4] != 2:
        raise UnselfError("only ELF64 is supported")
    e_phoff, e_shoff = struct.unpack_from(">Q", eh, 0x20)[0], struct.unpack_from(">Q", eh, 0x28)[0]
    e_phnum, e_shnum = struct.unpack_from(">H", eh, 0x38)[0], struct.unpack_from(">H", eh, 0x3C)[0]
    out_len = e_phoff + e_phnum * 0x38
    for i in range(e_phnum):
        ph = phdr_off + i * 0x38
        out_len = max(out_len, be64(ph + 0x08) + be64(ph + 0x20))
    if e_shoff and shdr_off and e_shoff + e_shnum * 0x40 > out_len:
        out_len = e_shoff + e_shnum * 0x40
    out = bytearray(out_len)
    out[0:0x40] = eh
    out[e_phoff:e_phoff + e_phnum * 0x38] = f[phdr_off:phdr_off + e_phnum * 0x38]
    if e_shoff and shdr_off:
        out[e_shoff:e_shoff + e_shnum * 0x40] = f[shdr_off:shdr_off + e_shnum * 0x40]

    written = 0
    for s in range(sec_count):
        sh = 0x20 + s * 0x30
        off, size = struct.unpack_from(">QQ", mh, sh)
        typ, prog = struct.unpack_from(">II", mh, sh + 0x10)
        enc, kidx, ividx, comp = struct.unpack_from(">IIII", mh, sh + 0x20)
        if typ != 2 or prog >= e_phnum or not size:
            continue
        if off + size > n:
            raise UnselfError("segment past end of file")
        ph = phdr_off + prog * 0x38
        p_offset, p_filesz = be64(ph + 0x08), be64(ph + 0x20)
        data = f[off:off + size]
        if enc == 3:
            if kidx >= key_count or ividx >= key_count:
                raise UnselfError("bad segment key index")
            data = _aes_ctr(mh[keys_off + kidx * 16:keys_off + kidx * 16 + 16],
                            mh[keys_off + ividx * 16:keys_off + ividx * 16 + 16], data)
        if comp == 2:
            d = zlib.decompress(data)
            if len(d) != p_filesz:
                raise UnselfError("segment did not inflate to p_filesz")
            out[p_offset:p_offset + p_filesz] = d
        else:
            if size > p_filesz:
                raise UnselfError("segment larger than p_filesz")
            out[p_offset:p_offset + size] = data
        written += 1
    with open(dst, "wb") as o:
        o.write(out)
    return "%s -> %d bytes, %d segments, key rev %d, licence %d" % (content_id, len(out), written, key_rev, license_)


if __name__ == "__main__":
    import sys
    print(unself(sys.argv[1], sys.argv[2], rap=sys.argv[3], keys_inc=sys.argv[4]))
