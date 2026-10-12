"""oracle_dump_ring.py - dump the WADLD ring after the N-th root_new (GoW2) to compare the
bytes the console reads with our extracted WAD. ring: type_sys=*0x0053FF4C, +0x1A8 -> ring
(base @+0, wp @+4, cur @+8, cap @+C, avail @+10)."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--out", required=True)
    ap.add_argument("--nth-root", type=int, default=4); ap.add_argument("--deadline", type=float, default=1200)
    a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0)
    t0 = time.time(); roots = 0
    try:
        time.sleep(1.5); rsp.connect(); rsp.handshake(); rsp.add_breakpoint(0x0041FA30)
        dl = time.time() + a.deadline
        while time.time() < dl:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print("[ring] no stop", flush=True); break
            roots += 1; print(f"[ring] root_new #{roots} t={time.time()-t0:.1f}", flush=True)
            if roots == a.nth_root:
                ts = rsp.read_u32(0x0053FF4C); ring = rsp.read_u32(ts + 0x1A8)
                base, wp, cur, cap, av = (rsp.read_u32(ring + o) for o in (0, 4, 8, 0xC, 0x10))
                req, rem = rsp.read_u32(ts + 0x74), rsp.read_u32(ts + 0x1AC)
                print(f"[ring] base=0x{base:08X} wp={wp} cur={cur} cap={cap} avail={av} req={req} rem={rem}", flush=True)
                data = rsp.read_mem(base, cap)
                open(a.out, "wb").write(data); print(f"[ring] wrote {a.out} ({len(data)} bytes)", flush=True)
                break
    finally:
        try: rsp.close()
        except Exception: pass
        try: proc.kill()
        except Exception: pass

if __name__ == "__main__": main()
