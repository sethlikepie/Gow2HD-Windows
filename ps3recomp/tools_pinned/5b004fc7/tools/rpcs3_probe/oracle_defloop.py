"""oracle_defloop.py - every iteration of the default-children loop (0x256D70: r30=node) on the
oracle, after the N-th root registration; prints node, class, list head (r27), mgr (r24)."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--max", type=int, default=80); a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0 = time.time(); k = 0
    try:
        time.sleep(1.5); rsp.connect(); rsp.handshake(); rsp.add_breakpoint(0x00256D70); rsp.add_breakpoint(0x00256B64); dl = time.time() + 900
        while k < a.max and time.time() < dl:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print("[defloop] no stop", flush=True); break
            select_main(rsp); regs = rsp.read_registers(); pc = probe._pc(regs); k += 1
            if pc == 0x00256B64:
                print(f"[defloop] --- entrada FUN_00256B1C mgr=0x{regs.get('r3',0)&0xFFFFFFFF:08X} desc=0x{regs.get('r4',0)&0xFFFFFFFF:08X} r5=0x{regs.get('r5',0)&0xFFFFFFFF:08X} t={time.time()-t0:.1f}", flush=True); continue
            r9 = regs.get("r9", 0) & 0xFFFFFFFF; r27 = regs.get("r27", 0) & 0xFFFFFFFF; r24 = regs.get("r24", 0) & 0xFFFFFFFF
            d = rsp.read_u32(r9 + 8) if guest_ok(r9) else 0; c = int.from_bytes(rsp.read_mem(d + 2, 2), "big") if guest_ok(d) else None
            nx = rsp.read_u32(r9) if guest_ok(r9) else 0
            print(f"[defloop] node=0x{r9:08X} next=0x{nx:08X} desc=0x{d:08X} cls={c} head=0x{r27:08X} mgr=0x{r24:08X}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        try: proc.kill()
        except Exception: pass
if __name__ == "__main__": main()
