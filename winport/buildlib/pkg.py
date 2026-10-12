"""pkg.py -- read a PS3 PSN package (.pkg) and extract the files the build needs, with no external tools.

    python pkg.py list <file.pkg>
    python pkg.py extract <file.pkg> <dest dir> [name ...]      (default: every file)

Format and decryption follow RPCS3's package reader (rpcs3/Crypto/unpkg.cpp + unpkg.h, key from
rpcs3/Crypto/key_vault.h) and pkg2zip (lusid1/pkg2zip, pkg2zip.c), which agree on the PS3 layout:

  header (big-endian, 0xC0 bytes)
    0x00 u32 magic 0x7F504B47 ("\\x7FPKG")      0x04 u16 revision: 0x8000 retail (finalized), 0x0000 debug
    0x06 u16 type: 1 = PS3, 2 = PSP/PS Vita     0x08 u32 metadata offset   0x0C u32 metadata count
    0x10 u32 header size                        0x14 u32 item count
    0x18 u64 total package size                 0x20 u64 data offset       0x28 u64 data size
    0x30 char[0x30] content id ("UP9000-NPUA80491_00-GODOFWARIIHDUS00")
    0x60 u8[16] digest (QA digest)              0x70 u8[16] data IV ("klicensee" in RPCS3's struct)
  data area (encrypted, AES-128-CTR for retail packages)
    item table: item count x 32 bytes { u32 name offset, u32 name size, u64 data offset, u64 data size,
                                        u32 flags, u32 pad }   (offsets relative to the data offset)
    then the names and the file contents, each at its own offset.
  Retail: keystream block i = AES-128-ECB(PS3 package key, IV + i), the IV read as a big-endian 128-bit
          number, i = (position - data offset) / 16. Items flagged as PSP content (flags >> 24 == 0x90) use
          another key; this game has none, and they are refused.
  Debug:  keystream block i = SHA-1(digest[0:8] x2 + digest[8:16] x2 + 24 zero bytes + u64 BE i)[:16].
  Flags & 0xFF: 4 = folder, 1/2/3/... = file.

The PS3 package key below is a platform constant (the same for every PS3 retail package; published in RPCS3's
key_vault.h as PKG_AES_KEY and in pkg2zip), not data of any game. AES comes from Windows CNG (bcrypt.dll via
ctypes, AES-NI): the counter blocks of a chunk are encrypted in one ECB call. A pure-Python AES is the fallback
when bcrypt.dll is unavailable; it is about 10,000x slower (minutes per MB), fine for the header and item table.
"""
import array
import hashlib
import os
import struct
import sys
import time

PKG_MAGIC = 0x7F504B47
PS3_PKG_KEY = bytes.fromhex("2E7B71D7C9C9A14EA3221F188828B8F8")   # RPCS3 key_vault.h PKG_AES_KEY
CHUNK = 16 << 20   # bytes per AES/XOR/write step (a multiple of 16)


class PkgError(Exception):
    pass


# ------------------------------------------------------------------ AES-128 encrypt (ECB): Windows CNG
class _CngAesEcb:
    def __init__(self, key):
        import ctypes
        from ctypes import wintypes
        self.ct = ctypes
        bc = ctypes.WinDLL("bcrypt.dll")
        H = ctypes.c_void_p
        bc.BCryptOpenAlgorithmProvider.argtypes = [ctypes.POINTER(H), wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.ULONG]
        bc.BCryptSetProperty.argtypes = [H, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.ULONG, wintypes.ULONG]
        bc.BCryptGenerateSymmetricKey.argtypes = [H, ctypes.POINTER(H), ctypes.c_void_p, wintypes.ULONG,
                                                  ctypes.c_char_p, wintypes.ULONG, wintypes.ULONG]
        bc.BCryptEncrypt.argtypes = [H, ctypes.c_void_p, wintypes.ULONG, ctypes.c_void_p, ctypes.c_void_p,
                                     wintypes.ULONG, ctypes.c_void_p, wintypes.ULONG, ctypes.POINTER(wintypes.ULONG),
                                     wintypes.ULONG]
        bc.BCryptDestroyKey.argtypes = [H]
        bc.BCryptCloseAlgorithmProvider.argtypes = [H, wintypes.ULONG]
        self.bc, self.alg, self.key = bc, H(), H()
        if bc.BCryptOpenAlgorithmProvider(ctypes.byref(self.alg), "AES", None, 0):
            raise OSError("BCryptOpenAlgorithmProvider(AES) failed")
        mode = ctypes.create_unicode_buffer("ChainingModeECB")
        if bc.BCryptSetProperty(self.alg, "ChainingMode", mode, ctypes.sizeof(mode), 0):
            raise OSError("BCryptSetProperty(ChainingModeECB) failed")
        if bc.BCryptGenerateSymmetricKey(self.alg, ctypes.byref(self.key), None, 0, bytes(key), len(key), 0):
            raise OSError("BCryptGenerateSymmetricKey failed")
        self.n = wintypes.ULONG(0)

    def encrypt_inplace(self, buf):
        """buf: bytearray, length a multiple of 16; encrypted in place (CNG allows in == out for ECB)."""
        ct = self.ct
        c = (ct.c_char * len(buf)).from_buffer(buf)
        st = self.bc.BCryptEncrypt(self.key, c, len(buf), None, None, 0, c, len(buf), ct.byref(self.n), 0)
        if st or self.n.value != len(buf):
            raise PkgError("AES (Windows CNG) failed, status 0x%08X" % (st & 0xFFFFFFFF))

    def close(self):
        if self.key:
            self.bc.BCryptDestroyKey(self.key)
            self.key = self.ct.c_void_p()
        if self.alg:
            self.bc.BCryptCloseAlgorithmProvider(self.alg, 0)
            self.alg = self.ct.c_void_p()


# ------------------------------------------------------------------ AES-128 encrypt (ECB): pure Python fallback
def _xt(a):
    return ((a << 1) ^ 0x1B) & 0xFF if a & 0x80 else a << 1


def _make_sbox():
    sbox = [0] * 256
    p = q = 1
    while True:   # multiplicative inverse via generator 3, then the affine map (FIPS-197 5.1.1)
        p = p ^ _xt(p)
        q ^= q << 1
        q ^= q << 2
        q ^= q << 4
        q &= 0xFF
        if q & 0x80:
            q ^= 0x09
        x = q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4))
        sbox[p] = (x ^ 0x63) & 0xFF
        if p == 1:
            break
    sbox[0] = 0x63
    return sbox


class _PyAesEcb:
    def __init__(self, key):
        s = self.sbox = _make_sbox()
        w = [list(key[i:i + 4]) for i in range(0, 16, 4)]
        rc = 1
        for i in range(4, 44):
            t = list(w[i - 1])
            if i % 4 == 0:
                t = [s[t[1]] ^ rc, s[t[2]], s[t[3]], s[t[0]]]
                rc = _xt(rc)
            w.append([w[i - 4][j] ^ t[j] for j in range(4)])
        self.rk = [sum(w[r * 4:r * 4 + 4], []) for r in range(11)]

    def _block(self, b):
        s, rk = self.sbox, self.rk
        st = [b[i] ^ rk[0][i] for i in range(16)]
        for r in range(1, 11):
            st = [s[x] for x in st]
            st = [st[(i + 4 * (i % 4)) % 16] for i in range(16)]   # ShiftRows (column-major state)
            if r != 10:
                o = []
                for c in range(4):
                    a = st[4 * c:4 * c + 4]
                    t = a[0] ^ a[1] ^ a[2] ^ a[3]
                    o += [a[0] ^ t ^ _xt(a[0] ^ a[1]), a[1] ^ t ^ _xt(a[1] ^ a[2]),
                          a[2] ^ t ^ _xt(a[2] ^ a[3]), a[3] ^ t ^ _xt(a[3] ^ a[0])]
                st = o
            st = [st[i] ^ rk[r][i] for i in range(16)]
        return bytes(st)

    def encrypt_inplace(self, buf):
        for i in range(0, len(buf), 16):
            buf[i:i + 16] = self._block(buf[i:i + 16])

    def close(self):
        pass


def aes_ecb(key):
    if os.name == "nt" and not os.environ.get("GOW2_PKG_PURE_AES"):
        try:
            return _CngAesEcb(key)
        except OSError:
            pass
    return _PyAesEcb(key)


# ------------------------------------------------------------------ keystream + XOR
def _xor(data, ks):
    n = len(data)
    return (int.from_bytes(data, "little") ^ int.from_bytes(memoryview(ks)[:n], "little")).to_bytes(n, "little")


class _RetailCipher:
    """AES-128-CTR over the data area: counter = IV + block index (big-endian 128-bit)."""

    UNIT = 1 << 16   # counter blocks whose low 16 bits run 0..65535 (only the last two bytes change)

    def __init__(self, iv):
        self.aes = aes_ecb(PS3_PKG_KEY)
        self.iv = int.from_bytes(iv, "big")
        self.col14 = bytes(j >> 8 for j in range(self.UNIT))
        self.col15 = bytes(range(256)) * 256

    def keystream(self, blk, nblk):
        """Keystream for data blocks [blk, blk + nblk). Whole aligned 64K-block units are built from constant
        byte columns (no per-block Python work); the unaligned head and tail go through _counters()."""
        U = self.UNIT
        c = (self.iv + blk) & ((1 << 128) - 1)
        head = min(nblk, (-c) % U)
        units = (nblk - head) // U
        if units == 0:
            return self._counters(blk, nblk)
        parts = []
        if head:
            parts.append(self._counters(blk, head))
        c0 = c + head
        buf = bytearray(b"".join(((c0 + u * U) & ((1 << 128) - 1)).to_bytes(16, "big") * U for u in range(units)))
        buf[14::16] = self.col14 * units
        buf[15::16] = self.col15 * units
        self.aes.encrypt_inplace(buf)
        parts.append(buf)
        tail = nblk - head - units * U
        if tail:
            parts.append(self._counters(blk + head + units * U, tail))
        return parts[0] if len(parts) == 1 else bytearray(b"".join(parts))

    def _counters(self, blk, nblk):
        c = (self.iv + blk) & ((1 << 128) - 1)
        hi, lo = c >> 64, c & 0xFFFFFFFFFFFFFFFF
        if lo + nblk > 1 << 64:   # the low half wraps inside this chunk: split it
            n1 = (1 << 64) - lo
            return self._counters(blk, n1) + self._counters(blk + n1, nblk - n1)
        q = array.array("Q", bytes(16 * nblk))
        hib = array.array("Q", hi.to_bytes(8, "big"))   # big-endian bytes in a native word
        q[0::2] = hib * nblk
        lows = array.array("Q", range(lo, lo + nblk))
        if sys.byteorder == "little":
            lows.byteswap()
        q[1::2] = lows
        buf = bytearray(q.tobytes())
        self.aes.encrypt_inplace(buf)
        return buf

    def close(self):
        self.aes.close()


class _DebugCipher:
    def __init__(self, digest):
        self.pre = digest[0:8] * 2 + digest[8:16] * 2 + bytes(24)

    def keystream(self, blk, nblk):
        pre, sha1 = self.pre, hashlib.sha1
        return bytearray(b"".join(sha1(pre + struct.pack(">Q", blk + i)).digest()[:16] for i in range(nblk)))

    def close(self):
        pass


def parse_sfo(data):
    """PARAM.SFO -> {key: str or int} (little-endian 'PSF' table)."""
    if len(data) < 20 or data[:4] != b"\0PSF":
        return {}
    _, _, kto, dto, n = struct.unpack("<4sIIII", data[:20])
    out = {}
    for i in range(n):
        e = 20 + i * 16
        if e + 16 > len(data):
            break
        ko, fmt, ln, _mx, do = struct.unpack("<HHIII", data[e:e + 16])
        key = data[kto + ko:data.index(b"\0", kto + ko)].decode("ascii", "replace")
        v = data[dto + do:dto + do + ln]
        out[key] = struct.unpack("<I", v[:4])[0] if fmt == 0x0404 and len(v) >= 4 else v.split(b"\0")[0].decode("utf-8", "replace")
    return out


# ------------------------------------------------------------------ package
class Item:
    __slots__ = ("name", "offset", "size", "flags")

    def __init__(self, name, offset, size, flags):
        self.name, self.offset, self.size, self.flags = name, offset, size, flags

    @property
    def is_dir(self):
        return (self.flags & 0xFF) == 4

    def __repr__(self):
        return "%s%s %d (flags %08X)" % (self.name, "/" if self.is_dir else "", self.size, self.flags)


class Pkg:
    def __init__(self, path):
        self.path = path
        try:
            self.f = open(path, "rb")
        except OSError as e:
            raise PkgError("cannot open %s: %s" % (path, e))
        h = self.f.read(0xC0)
        if len(h) < 0xC0 or struct.unpack(">I", h[:4])[0] != PKG_MAGIC:
            raise PkgError("%s is not a PS3 package (.pkg): no \\x7FPKG header" % os.path.basename(path))
        (self.revision, self.type, self.meta_off, self.meta_count, self.header_size, self.item_count,
         self.total_size, self.data_off, self.data_size) = struct.unpack(">HHIIIIQQQ", h[4:0x30])
        self.content_id = h[0x30:0x60].split(b"\0")[0].decode("ascii", "replace")
        self.digest, self.iv = h[0x60:0x70], h[0x70:0x80]
        self.file_size = os.path.getsize(path)
        if self.type != 1:
            raise PkgError("%s is a PSP/PS Vita package, not a PS3 one (content %s)" % (os.path.basename(path), self.content_id))
        if self.revision == 0x8000:
            self.cipher = _RetailCipher(self.iv)
        elif self.revision == 0x0000:
            self.cipher = _DebugCipher(self.digest)
        else:
            raise PkgError("unknown package revision 0x%04X" % self.revision)
        self.retail = self.revision == 0x8000
        self.complete = self.file_size >= self.total_size
        self.meta = self._read_meta()
        self.items = []
        if self.file_size >= self.data_off + self.item_count * 32:
            self._read_items()

    def _read_meta(self):
        meta = {}
        self.f.seek(self.meta_off)
        for _ in range(min(self.meta_count, 64)):
            hd = self.f.read(8)
            if len(hd) < 8:
                break
            mid, size = struct.unpack(">II", hd)
            meta[mid] = self.f.read(size)
        return meta

    @property
    def content_type(self):   # metadata packet 2: 4/5 = PS3 game data/executable (RPCS3 PKG_CONTENT_TYPE_*)
        v = self.meta.get(2)
        return struct.unpack(">I", v[:4])[0] if v and len(v) >= 4 else None

    def read_data(self, rel, size):
        """Decrypt size bytes at data-area offset rel."""
        if size <= 0:
            return b""
        b0 = rel // 16
        skip = rel - b0 * 16
        nblk = (skip + size + 15) // 16
        self.f.seek(self.data_off + b0 * 16)
        enc = self.f.read(nblk * 16)
        if len(enc) < skip + size:
            raise PkgError("the package ends early (incomplete download)")
        return _xor(enc, self.cipher.keystream(b0, nblk))[skip:skip + size]

    def _read_items(self):
        tab = self.read_data(0, self.item_count * 32)
        for i in range(self.item_count):
            no, ns, do, ds, fl, _ = struct.unpack(">IIQQII", tab[i * 32:i * 32 + 32])
            if (fl >> 24) == 0x90:
                name = "<psp item %d>" % i   # PSP-key item (not used by PS3 games)
            else:
                name = self.read_data(no, ns).split(b"\0")[0].decode("utf-8", "replace")
            self.items.append(Item(name, do, ds, fl))

    def item(self, name):
        for it in self.items:
            if it.name.replace("\\", "/").lower() == name.lower():
                return it
        return None

    def extract(self, it, dst, progress=None, resume=True, want_sha256=None):
        """Stream item it into dst (via dst + '.part'); resumes a previous .part. Returns the sha256 hex."""
        if (it.flags >> 24) == 0x90:
            raise PkgError("%s uses the PSP key (not supported)" % it.name)
        if self.data_off + it.offset + it.size > self.file_size:
            raise PkgError("the package ends before %s (incomplete download: %d of %d bytes)"
                           % (it.name, self.file_size, self.total_size))
        os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
        part = dst + ".part"
        done = 0
        h = hashlib.sha256()
        if resume and os.path.isfile(part):
            done = min(os.path.getsize(part), it.size) // CHUNK * CHUNK
            with open(part, "rb") as p:   # re-hash what is there (the CTR stream is seekable, so just continue)
                left = done
                while left:
                    b = p.read(min(CHUNK, left))
                    if not b:
                        break
                    h.update(b)
                    left -= len(b)
        resumed = done > 0
        mode = "r+b" if done else "wb"
        with open(part, mode) as o:
            o.seek(done)
            o.truncate()
            self._pipeline(it, done, o, h, progress)
        sha = h.hexdigest()
        if want_sha256 and sha != want_sha256:
            os.remove(part)
            if resumed:   # the old .part may have come from another file: once more from the start
                return self.extract(it, dst, progress, resume=False, want_sha256=want_sha256)
            raise PkgError("%s from the package does not match the supported version (sha256)" % it.name)
        os.replace(part, dst)
        return sha

    def _pipeline(self, it, done, o, h, progress):
        """Reader thread -> decrypt (this thread) -> writer thread (write + sha256). File I/O, CNG and hashlib
        release the GIL, so reading, writing and hashing overlap the XOR."""
        import queue
        import threading
        rq, wq = queue.Queue(3), queue.Queue(3)
        err = []
        aligned = it.offset % 16 == 0   # true in every package seen; read_data handles the general case
        src = open(self.path, "rb")

        def reader():
            try:
                pos = done
                src.seek(self.data_off + it.offset + pos)
                while pos < it.size and not err:
                    n = min(CHUNK, it.size - pos)
                    b = src.read(n) if aligned else None
                    if aligned and len(b) < n:
                        raise PkgError("the package ends early (incomplete download)")
                    rq.put((pos, n, b))
                    pos += n
            except BaseException as e:
                err.append(e)
            rq.put(None)

        def writer():
            try:
                while True:
                    x = wq.get()
                    if x is None:
                        return
                    if not err:
                        o.write(x)
                        h.update(x)
            except BaseException as e:
                err.append(e)
                while wq.get() is not None:
                    pass

        tr, tw = threading.Thread(target=reader, daemon=True), threading.Thread(target=writer, daemon=True)
        tr.start()
        tw.start()
        t0, last, pos, x = time.time(), 0.0, done, ()
        try:
            while True:
                x = rq.get()
                if x is None or err:
                    break
                p, n, enc = x
                if aligned:
                    dec = _xor(enc, self.cipher.keystream((it.offset + p) // 16, (n + 15) // 16))
                else:
                    dec = self.read_data(it.offset + p, n)
                wq.put(dec)
                pos = p + n
                if progress and (time.time() - last > 0.5 or pos == it.size):
                    last = time.time()
                    progress(pos, it.size, time.time() - t0)
        except BaseException as e:
            err.append(e)
        finally:
            if x is not None:
                err.append(err[0] if err else PkgError("stopped"))   # tells the reader to stop
            while x is not None:   # drain until the reader has finished
                x = rq.get()
            wq.put(None)
            tw.join()
            tr.join()
            src.close()
        if err:
            raise err[0]

    def close(self):
        self.cipher.close()
        self.f.close()


def main(argv):
    if len(argv) < 2 or argv[0] not in ("list", "extract"):
        print(__doc__)
        return 2
    p = Pkg(argv[1])
    print("content %s  revision %04X  type %d  items %d  size %d/%d  content-type %s"
          % (p.content_id, p.revision, p.type, p.item_count, p.file_size, p.total_size, p.content_type))
    if argv[0] == "list":
        for it in p.items:
            print("  %12d  %08X  %s" % (it.size, it.flags, it.name))
        return 0
    dest, names = argv[2], set(n.lower() for n in argv[3:])
    for it in p.items:
        if it.is_dir or (names and it.name.lower() not in names):
            continue

        def prog(d, t, s, nm=it.name):
            sys.stdout.write("\r%s %5.1f%% %.0f MB/s   " % (nm, 100.0 * d / max(1, t), d / max(s, 1e-6) / 1e6))
            sys.stdout.flush()
        t0 = time.time()
        sha = p.extract(it, os.path.join(dest, *it.name.split("/")), prog)
        print("\r%s %d bytes %.1f s sha256 %s" % (it.name, it.size, time.time() - t0, sha))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
