"""oracle_list70.py - at FUN_002a466c entry (r3=obj) walk the circular list at obj+0x70 (bounded)."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--hits", type=int, default=3); a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0 = time.time(); k = 0
    try:
        time.sleep(1.5); rsp.connect(); rsp.handshake(); rsp.add_breakpoint(0x002A466C); dl = time.time() + 900
        while k < a.hits and time.time() < dl:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print("[l70] no stop", flush=True); break
            select_main(rsp); regs = rsp.read_registers(); k += 1; r3 = regs.get("r3", 0) & 0xFFFFFFFF
            st = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 6) if f["pc"]][1:5]
            sent = r3 + 0x70; n = rsp.read_u32(sent) if guest_ok(sent) else 0; j = 0; out = []
            while guest_ok(n) and n != sent and j < 100:
                out.append(f"0x{n:08X}(st={rsp.read_mem(n+10,1)[0]},f=0x{int.from_bytes(rsp.read_mem(n+8,2),'big'):04X})"); n = rsp.read_u32(n); j += 1
            print(f"[l70] #{k} t={time.time()-t0:.1f} obj=0x{r3:08X} sent=0x{sent:08X} ra={st} nos={j} fim={'cabeca' if n==sent else f'0x{n:08X}'} {out[:8]}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__ == "__main__": main()
