"""oracle_slotidx.py - *(0x53AAD4) manager and +0x3C (slot index) of every TABLE manager."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import guest_ok, TABLE
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--at", type=float, default=8.0); a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0)
    try:
        time.sleep(a.at); rsp.connect(retries=120, delay=0.5); rsp.handshake()
        g = rsp.read_u32(0x0053AAD4); print(f"[slot] *(0x53AAD4)=0x{g:08X} +0x3C={rsp.read_u32(g+0x3C) if guest_ok(g) else None}", flush=True)
        tab = rsp.read_mem(TABLE, 40*4)
        for i in range(40):
            m = int.from_bytes(tab[i*4:i*4+4], "big")
            if guest_ok(m): print(f"[slot] TABLE[{i:2d}] mgr=0x{m:08X} +0x3C={rsp.read_u32(m+0x3C)} +0x44={rsp.read_u32(m+0x44)} +0x40={rsp.read_u32(m+0x40)}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__ == "__main__": main()
