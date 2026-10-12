"""oracle_9ec0c.py - FUN_0009ec0c(obj, p2, p3) entry: obj, obj[1] (parent), parent w0, parent+0xB8.. slot words."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--at", type=float, default=8.5); ap.add_argument("--hits", type=int, default=6); a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0 = time.time(); k = 0
    try:
        time.sleep(a.at); rsp.connect(); rsp.handshake(); rsp.add_breakpoint(0x0009EC0C); dl = time.time() + 600
        while k < a.hits and time.time() < dl:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print("[9ec0c] no stop", flush=True); break
            select_main(rsp); regs = rsp.read_registers(); k += 1
            o = regs.get("r3", 0) & 0xFFFFFFFF; p2 = regs.get("r4", 0) & 0xFFFFFFFF; p3 = regs.get("r5", 0) & 0xFFFFFFFF
            w0 = rsp.read_u32(o) if guest_ok(o) else None; par = rsp.read_u32(o + 4) if guest_ok(o) else 0
            pw0 = rsp.read_u32(par) if guest_ok(par) else None
            slots = [f"0x{rsp.read_u32(par + 0xB8 + i*4):08X}" for i in range(0, 40)] if guest_ok(par) else []
            st = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 6) if f["pc"]][1:5]
            print(f"[9ec0c] #{k} t={time.time()-t0:.1f} obj=0x{o:08X} w0=0x{(w0 or 0):08X} parent=0x{par:08X} parent_w0=0x{(pw0 or 0):08X} p2=0x{p2:08X} p3=0x{p3:08X} ra={st}", flush=True)
            print(f"[9ec0c]    parent+0xB8 slots: {slots}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__ == "__main__": main()
