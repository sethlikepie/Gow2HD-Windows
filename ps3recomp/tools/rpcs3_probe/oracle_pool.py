"""oracle_pool.py - at FUN_00256B1C fragment 0x256B64 (r3=mgr, r4=desc, r5=src) print the pool
check that selects the path: p=*(mgr+0x30)+0x34 -> [+4] vs [+8] (0x256BA4..0x256BB8)."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--hits", type=int, default=6); a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0 = time.time(); k = 0
    try:
        time.sleep(1.5); rsp.connect(); rsp.handshake(); rsp.add_breakpoint(0x00256B64); dl = time.time() + 900
        while k < a.hits and time.time() < dl:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print("[pool] no stop", flush=True); break
            select_main(rsp); regs = rsp.read_registers(); k += 1
            r3, r4, r5 = (regs.get(f"r{i}", 0) & 0xFFFFFFFF for i in (3, 4, 5))
            m30 = rsp.read_u32(r3 + 0x30) if guest_ok(r3) else 0; p = m30 + 0x34
            a4 = rsp.read_u32(p + 4) if guest_ok(p) else None; a8 = rsp.read_u32(p + 8) if guest_ok(p) else None
            w0 = rsp.read_u32(r4) if guest_ok(r4) else None
            print(f"[pool] #{k} t={time.time()-t0:.1f} mgr=0x{r3:08X} desc=0x{r4:08X} w0=0x{(w0 or 0):08X} r5=0x{r5:08X} m30=0x{m30:08X} [+4]={a4} [+8]={a8} {'IGUAL(deflist)' if a4==a8 else 'DIFERENTE(0x256ECC)'}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        try: proc.kill()
        except Exception: pass
if __name__ == "__main__": main()
