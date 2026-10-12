"""oracle_frontend_rem.py -- DECISIVE (E353): does the CONSOLE body-load R_PermA in the FRONT-END?
Boots EBOOT_intro.BIN (front-end regime, NOT skip-intro), breaks at func_002BA76C (0x002BA76C,
the loader inner step). On each hit reads type_sys (fixed global 0x0053FF4C = TOC 0x00541178 - 0x122C),
then rem=ts+0x1AC, state=ts+0x1CC, body1D4=ts+0x1D4. If rem DESCENDS across hits during the
front-end -> console body-loads in front-end (needs-level FALSE; our front-end has a bug).
If rem stays ~0x133C280 (20MB) and hits stop -> TOC-only (needs-level plausible)."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
TS_PTR = 0x0053FF4C  # *(TOC 0x00541178 - 0x122C) -> type_sys base
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", required=True)
    ap.add_argument("--bp", default="0x002BA76C")
    ap.add_argument("--at", type=float, default=6.0)
    ap.add_argument("--hits", type=int, default=120)
    ap.add_argument("--deadline", type=float, default=300)
    a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True)
    rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0 = time.time(); k = 0
    try:
        print(f"[fe] launched pid={proc.pid}; sleeping {a.at}s", flush=True)
        time.sleep(a.at)
        print("[fe] connecting to stub...", flush=True)
        rsp.connect(retries=240, delay=0.5); rsp.handshake()
        print("[fe] connected + handshake OK", flush=True)
        select_main(rsp)
        ts0 = rsp.read_u32(TS_PTR)
        print(f"[fe] ATTACH read: ts=0x{ts0:08X}" + (f" rem=0x{rsp.read_u32(ts0+0x1AC):08X} state={rsp.read_u32(ts0+0x1CC)}" if guest_ok(ts0) else " (loader not init)"), flush=True)
        rsp.add_breakpoint(int(a.bp, 0)); print(f"[fe] bp set at {a.bp}; entering hit loop (deadline {a.deadline}s)", flush=True); dl = time.time() + a.deadline
        prev_rem = None
        while k < a.hits and time.time() < dl:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print("[fe] no stop / boot ended", flush=True); break
            select_main(rsp); k += 1
            try: ts = rsp.read_u32(TS_PTR)
            except Exception: ts = 0
            if guest_ok(ts):
                rem = rsp.read_u32(ts + 0x1AC); state = rsp.read_u32(ts + 0x1CC)
                b1d4 = rsp.read_u32(ts + 0x1D4); f1b0 = rsp.read_u32(ts + 0x1B0)
                f1bc = rsp.read_u32(ts + 0x1BC); f1c4 = rsp.read_u32(ts + 0x1C4); f1c8 = rsp.read_u32(ts + 0x1C8)
                ring = rsp.read_u32(ts + 0x1A8); avail = rsp.read_u32(ring + 0x10) if guest_ok(ring) else 0
                d = "" if prev_rem is None else (f" drem={(rem-prev_rem):+d}")
                print(f"[fe] #{k} t={time.time()-t0:.1f} state={state} rem=0x{rem:08X} 1BC=0x{f1bc:08X} 1C4=0x{f1c4:08X} 1C8=0x{f1c8:08X} avail=0x{avail:08X} body1D4=0x{b1d4:08X}{d}", flush=True)
                prev_rem = rem
            else:
                print(f"[fe] #{k} t={time.time()-t0:.1f} ts=0x{ts:08X} (loader not init yet)", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__ == "__main__": main()
