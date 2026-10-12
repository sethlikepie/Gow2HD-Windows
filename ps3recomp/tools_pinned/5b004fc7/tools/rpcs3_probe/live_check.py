"""live_check.py - capability census against a RUNNING RPCS3 GDB stub.

Answers "what can the oracle actually do right now", so a failed capture is
attributed to the emulator's state instead of to the client. Verified against
RPCS3 v0.0.42-19734 (macOS arm64) on 2026-08-12.

    python tools/rpcs3_probe/live_check.py [--port 2345] [--text-ea 0x10000]

HAZARD -- do NOT port-scan the stub. RPCS3's gdb_thread calls read_char() on an
accepted connection and *terminates the whole GDB Server thread* if no byte
arrives (Emu/GDB.cpp:240, errno=60). A bare TCP connect-and-close -- exactly what
`nc -z` does -- therefore kills the stub for the rest of the session:

    RPCS3: GDB Server: SIG: Thread terminated due to fatal error:
    Tried to read char, but no data was available

Wait a fixed interval and speak RSP immediately instead. `RSPClient.connect()` is
safe because handshake() sends a packet right away.

HAZARD 2 -- `D` (detach) kills the stub the same way. After detaching, the stub
accepts no further connections for that emulator run. Budget ONE attach per
emulator launch; sampling a value over time needs one launch per sample.

What the census establishes on a stub with no emulation running (no firmware):
    ?                       -> S05        stub alive, target stopped
    m<text_ea>,10           -> ELF magic  guest memory reads WORK
    g                       -> (empty)    register reads DO NOT (no PPU thread)
    qXfer:features:read     -> (empty)    unsupported; rsp.py uses its fallback
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient, RSPError    # noqa: E402


def probe_one(c, pkt, label):
    try:
        c._send_packet(pkt)
        r = c._recv_packet()
        return {"packet": pkt, "label": label, "len": len(r),
                "reply": r[:96].decode("latin-1", "replace"), "error": None}
    except Exception as e:
        return {"packet": pkt, "label": label, "len": -1, "reply": None,
                "error": f"{type(e).__name__}: {e}"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=None)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--text-ea", default="0x10000",
                    help="a guest EA expected to be mapped (ELF text base)")
    ap.add_argument("--timeout", type=float, default=6.0)
    a = ap.parse_args()

    port = a.port
    if port is None:
        try:
            import rpcs3_host
            port = rpcs3_host.stub_port()
        except Exception:
            port = 2345

    c = RSPClient(host=a.host, port=port, timeout=a.timeout)
    try:
        c.connect(retries=8, delay=1.0)
    except RSPError as e:
        print(f"[live] cannot reach the stub on {a.host}:{port}: {e}")
        print("[live] is emulation started? did something port-scan the stub "
              "(see HAZARD in this file's docstring)?")
        return 2
    print(f"[live] connected to {a.host}:{port}")

    feats = c.handshake()
    print(f"[live] qSupported: {feats or '(empty)'}")
    caps = {
        "no_ack_mode": c.no_ack,
        "qXfer_features": "qXfer:features:read+" in (feats or ""),
        "reg_layout_from_xml": not hasattr(c, "_reg_err"),
        "reg_layout_size": len(c.reg_layout),
    }
    for k, v in caps.items():
        print(f"[live]   {k:<22} {v}")

    ea = int(a.text_ea, 0)
    results = [
        probe_one(c, "?", "stop reason"),
        probe_one(c, "g", "register block"),
        probe_one(c, f"m{ea:x},10", f"memory read @0x{ea:08X}"),
        probe_one(c, "qXfer:features:read:target.xml:0,1000", "target description"),
    ]
    print("\n[live] capability census")
    for r in results:
        state = ("ERROR" if r["error"] else
                 "empty" if r["len"] == 0 else f"{r['len']} bytes")
        print(f"  {r['label']:<26} {r['packet']:<14} -> {state}")
        if r["reply"]:
            print(f"      {r['reply']}")
        if r["error"]:
            print(f"      {r['error']}")

    mem_ok = next(r["len"] > 0 for r in results if r["label"].startswith("memory"))
    reg_ok = next(r["len"] > 0 for r in results if r["label"] == "register block")

    print("\n[live] verdict")
    print(f"  memory reads   : {'WORK' if mem_ok else 'FAIL'}")
    print(f"  register reads : {'WORK' if reg_ok else 'FAIL'}")
    if mem_ok and not reg_ok:
        print("  -> stub is alive and memory-capable, but no PPU thread exists.")
        print("     `mem` recipe steps work; `breakpoint`/`snapshot` steps need a")
        print("     RUNNING title, which needs PS3 firmware installed in RPCS3")
        print("     (Missing Firmware in RPCS3's log confirms this case).")
    elif mem_ok and reg_ok:
        print("  -> full capture available: breakpoints and snapshots will work.")
    else:
        print("  -> stub not serving; check RPCS3's log for a GDB Server error.")
    c.close()
    return 0 if mem_ok else 1


if __name__ == "__main__":
    sys.exit(main())
