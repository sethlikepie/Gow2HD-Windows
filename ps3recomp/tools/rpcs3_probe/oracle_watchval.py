"""oracle_watchval.py - sample a guest u32 (default 0x00835A10) with interrupts; print changes with pc/stack."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--addr", default="0x00835A10"); ap.add_argument("--at", type=float, default=2.5)
    ap.add_argument("--interval", type=float, default=0.3); ap.add_argument("--samples", type=int, default=60); a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0 = time.time(); prev = None; ea = int(a.addr, 0)
    try:
        time.sleep(a.at); rsp.connect(retries=120, delay=0.5); rsp.handshake()
        for k in range(a.samples):
            if k: rsp.cont(); time.sleep(a.interval); rsp.interrupt(); rsp.wait_stop(deadline=time.time()+20)
            try:
                v = rsp.read_u32(ea); select_main(rsp); regs = rsp.read_registers(); pc = probe._pc(regs)
                st = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 8) if f["pc"]][:6]
            except Exception as e:
                print(f"[wv] s{k} err {e}", flush=True); continue
            if v != prev: print(f"[wv] s{k} t={time.time()-t0:.1f} *{a.addr}={v} pc=0x{pc:08X} stack={st}", flush=True)
            prev = v
        print("[wv] fim", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__ == "__main__": main()
