"""fake_rsp_server.py - an in-process GDB-RSP stub that mimics RPCS3's PPU server.

Exists so the oracle client (rsp.py) and the stack walker (probe.py/trace.py) can
be exercised WITHOUT RPCS3 installed: protocol framing, run-length decoding,
target.xml register-layout discovery, big-endian register decode, chunked memory
reads, breakpoints and stop replies.

Memory is served from a caller-supplied {addr: bytes} region list, so a test can
back the text region with a real EBOOT and hand-build a stack whose back chain
is the one a real stop would produce.
"""
import socket
import threading

# A reduced but structurally faithful PPU layout: 32 GPRs, 32 FPRs, then the
# specials -- same ordering RPCS3's target description uses, which is what makes
# the client's offset arithmetic meaningful to test.
REG_NAMES = ([f"r{i}" for i in range(32)] +
             [f"f{i}" for i in range(32)] +
             ["pc", "msr", "cr", "lr", "ctr", "xer", "fpscr"])

TARGET_XML = ('<?xml version="1.0"?><!DOCTYPE target SYSTEM "gdb-target.dtd">'
              '<target version="1.0"><architecture>powerpc:common64</architecture>'
              '<feature name="org.gnu.gdb.power.core">' +
              "".join(f'<reg name="{n}" bitsize="64" regnum="{i}"/>'
                      for i, n in enumerate(REG_NAMES)) +
              '</feature></target>')


def _cksum(b: bytes) -> int:
    return sum(b) & 0xFF


class FakeRSPServer(threading.Thread):
    def __init__(self, regions, regs, host="127.0.0.1", port=0, rpcs3_mode=False):
        """regions: list of (base, bytes). regs: {name: int}.

        rpcs3_mode reproduces what RPCS3 v0.0.42-19734 actually does, MEASURED
        2026-08-12: advertise only PacketSize (no qXfer, no QStartNoAckMode),
        serve a 556-byte `g` block with cr/xer/fpscr at 4 bytes, and mark
        everything after `pc` (bytes 520..555) unavailable with 'x'. Without this
        mode the fixture only exercises the target.xml path, which no live run
        takes."""
        super().__init__(daemon=True)
        self.rpcs3_mode = rpcs3_mode
        self.regions = sorted(regions, key=lambda r: r[0])
        self.regs = dict(regs)
        self.breakpoints = set()
        self.no_ack = False
        self.log = []
        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind((host, port))
        self._srv.listen(1)
        self.port = self._srv.getsockname()[1]
        self._stop = False

    # ---- memory -------------------------------------------------------------
    def read_mem(self, addr, length):
        out = bytearray()
        for i in range(length):
            a = addr + i
            b = None
            for base, data in self.regions:
                if base <= a < base + len(data):
                    b = data[a - base]
                    break
            if b is None:
                return None            # unmapped -> RSP error, like RPCS3
            out.append(b)
        return bytes(out)

    # The measured RPCS3 PPU layout (name, size). cr/xer/fpscr are 4 bytes.
    RPCS3_LAYOUT = ([(f"r{i}", 8) for i in range(32)] +
                    [(f"f{i}", 8) for i in range(32)] +
                    [("pc", 8), ("msr", 8), ("cr", 4),
                     ("lr", 8), ("ctr", 8), ("xer", 4), ("fpscr", 4)])
    RPCS3_AVAIL_BYTES = 520      # bytes >= this come back as 'x'

    def reg_block(self):
        out = bytearray()
        for n in REG_NAMES:
            out += int(self.regs.get(n, 0) & 0xFFFFFFFFFFFFFFFF).to_bytes(8, "big")
        return bytes(out)

    def reg_block_hex_rpcs3(self):
        """556-byte block, hex, with 'x' for the unavailable tail."""
        raw = bytearray()
        for n, size in self.RPCS3_LAYOUT:
            raw += int(self.regs.get(n, 0) & 0xFFFFFFFFFFFFFFFF).to_bytes(size, "big")
        hx = raw.hex()
        cut = self.RPCS3_AVAIL_BYTES * 2
        return hx[:cut] + "x" * (len(hx) - cut)

    # ---- protocol -----------------------------------------------------------
    def _send(self, sock, payload: str):
        body = payload.encode("latin-1")
        if not self.no_ack:
            sock.sendall(b"+")
        sock.sendall(b"$" + body + b"#" + f"{_cksum(body):02x}".encode())

    def _handle(self, sock, pkt):
        self.log.append(pkt)
        if pkt.startswith("qSupported"):
            if self.rpcs3_mode:
                return "PacketSize=1200"
            return "PacketSize=1000;qXfer:features:read+;QStartNoAckMode+"
        if pkt == "QStartNoAckMode":
            # reply first, THEN drop acks, so the client's own ack expectation
            # for this reply is still satisfied (mirrors real stubs).
            self._pending_no_ack = True
            return "OK"
        if pkt.startswith("qXfer:features:read:") and self.rpcs3_mode:
            return ""                   # unsupported, forcing the fallback layout
        if pkt.startswith("qXfer:features:read:"):
            rest = pkt[len("qXfer:features:read:"):]
            annex, _, where = rest.partition(":")
            off_s, _, len_s = where.partition(",")
            off, ln = int(off_s or "0", 16), int(len_s or "1000", 16)
            body = TARGET_XML if annex == "target.xml" else ""
            chunk = body[off:off + ln]
            return ("l" if off + len(chunk) >= len(body) else "m") + chunk
        if pkt == "g":
            return (self.reg_block_hex_rpcs3() if self.rpcs3_mode
                    else self.reg_block().hex())
        if pkt.startswith("m"):
            a_s, _, l_s = pkt[1:].partition(",")
            data = self.read_mem(int(a_s, 16), int(l_s, 16))
            return data.hex() if data is not None else "E01"
        if pkt.startswith("Z0,"):
            self.breakpoints.add(int(pkt[3:].split(",")[0], 16))
            return "OK"
        if pkt.startswith("z0,"):
            self.breakpoints.discard(int(pkt[3:].split(",")[0], 16))
            return "OK"
        if pkt in ("c", "s"):
            if pkt == "s":
                self.regs["pc"] = (self.regs.get("pc", 0) + 4) & 0xFFFFFFFF
            return "T05thread:01;"
        if pkt == "D":
            return "OK"
        if pkt.startswith("?"):
            return "T05thread:01;"
        return ""                       # unsupported, like a real stub

    def run(self):
        conn, _ = self._srv.accept()
        conn.settimeout(5.0)
        buf = b""
        self._pending_no_ack = False
        try:
            while not self._stop:
                try:
                    data = conn.recv(4096)
                except socket.timeout:
                    continue
                if not data:
                    break
                buf += data
                while True:
                    # drop acks and async interrupts
                    while buf[:1] in (b"+", b"-", b"\x03"):
                        buf = buf[1:]
                    s = buf.find(b"$")
                    e = buf.find(b"#")
                    if s < 0 or e < 0 or len(buf) < e + 3:
                        break
                    pkt = buf[s + 1:e].decode("latin-1")
                    buf = buf[e + 3:]
                    reply = self._handle(conn, pkt)
                    if reply is not None:
                        self._send(conn, reply)
                    if self._pending_no_ack:
                        self.no_ack = True
                        self._pending_no_ack = False
        finally:
            try:
                conn.close()
            finally:
                self._srv.close()

    def shutdown(self):
        self._stop = True
