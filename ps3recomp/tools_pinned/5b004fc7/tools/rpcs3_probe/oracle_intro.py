"""oracle_intro.py - the intro 'queue' on the oracle with the ORIGINAL EBOOT.BIN: samples the movie/demo-player
singleton (*(0x540054): state +0x620, +0x71C/+0x714/+0x718, bytes +0x744/+0x746), the intro counter 0x592668 and the
main-thread stack; prints every change (compare with our [MOVIEFSM] st620 a -> b sequence)."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--at", type=float, default=2.5)
    ap.add_argument("--interval", type=float, default=0.5); ap.add_argument("--samples", type=int, default=150); a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0 = time.time(); prev = None; guest = a.at
    try:
        time.sleep(a.at); rsp.connect(retries=120, delay=0.5); rsp.handshake()
        for k in range(a.samples):
            if k: rsp.cont(); time.sleep(a.interval); guest += a.interval; rsp.interrupt(); rsp.wait_stop(deadline=time.time()+20)
            try:
                obj = rsp.read_u32(0x00540054); ic = rsp.read_u32(0x00592668)
                st = rsp.read_u32(obj + 0x620) if guest_ok(obj) else None
                fl = (rsp.read_u32(obj + 0x71C), rsp.read_u32(obj + 0x714), rsp.read_u32(obj + 0x718), rsp.read_mem(obj + 0x744, 1)[0], rsp.read_mem(obj + 0x746, 1)[0]) if guest_ok(obj) else None
                select_main(rsp); regs = rsp.read_registers(); pc = probe._pc(regs)
                stk = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 8) if f["pc"]][:5]
            except Exception as e:
                print(f"[intro] s{k} err {e}", flush=True); continue
            cur = (obj, st, ic, fl)
            if cur != prev: print(f"[intro] s{k} guest~{guest:5.1f}s obj=0x{obj:08X} st620={st} intro=0x{ic:X} flags(71C,714,718,744,746)={fl} pc=0x{pc:08X} stack={stk}", flush=True)
            prev = cur
        print("[intro] fim", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__ == "__main__": main()
