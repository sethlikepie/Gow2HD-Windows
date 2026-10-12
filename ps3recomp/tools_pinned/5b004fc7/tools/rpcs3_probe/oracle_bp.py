"""oracle_bp.py - generic: break at ADDR (repeat --bp), print r3..r6 with one-level derefs (+0, +4) and the stack."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--bp", action="append", required=True)
    ap.add_argument("--at", type=float, default=8.5); ap.add_argument("--hits", type=int, default=8); ap.add_argument("--deadline", type=float, default=600); ap.add_argument("--extra", action="append", default=[], help="rN:+off -> print u32 at rN+off, its w0 and slots +0xB8.."); a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0 = time.time(); k = 0
    try:
        time.sleep(a.at); rsp.connect(retries=120, delay=0.5); rsp.handshake()
        for b in a.bp: rsp.add_breakpoint(int(b, 0))
        dl = time.time() + a.deadline
        while k < a.hits and time.time() < dl:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print("[bp] no stop", flush=True); break
            select_main(rsp); regs = rsp.read_registers(); pc = probe._pc(regs); k += 1
            parts = []
            for i in (3, 4, 5, 6):
                v = regs.get(f"r{i}", 0) & 0xFFFFFFFF; d0 = d4 = None
                if guest_ok(v):
                    try: d0 = rsp.read_u32(v); d4 = rsp.read_u32(v + 4)
                    except Exception: pass
                parts.append(f"r{i}=0x{v:08X}" + (f"[+0=0x{d0:08X},+4=0x{d4:08X}]" if d0 is not None else ""))
            st = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 12) if f["pc"]][1:11]
            print(f"[bp] #{k} t={time.time()-t0:.1f} pc=0x{pc:08X} " + " ".join(parts) + f" ra={st}", flush=True)
            for x in a.extra:
                rn, off = x.split(":"); base = regs.get(rn, 0) & 0xFFFFFFFF; ea = (base + int(off, 0)) & 0xFFFFFFFF
                if guest_ok(ea):
                    v = rsp.read_u32(ea); w0 = rsp.read_u32(v) if guest_ok(v) else None
                    f9c = rsp.read_u32(v + 0x9C) if guest_ok(v) else None
                    print(f"[bp]    *({rn}{off})=0x{v:08X} w0=0x{(w0 or 0):08X} +0x9c=0x{(f9c or 0):08X}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__ == "__main__": main()
