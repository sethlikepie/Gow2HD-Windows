"""oracle_dae20.py - FUN_002dae20(cache, obj, p3, p4) entry on the oracle: regs + *(FUN_00283164()+0x68) pool words."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--at", type=float, default=7.8); ap.add_argument("--hits", type=int, default=12); a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0 = time.time(); k = 0
    try:
        time.sleep(a.at); rsp.connect(); rsp.handshake(); rsp.add_breakpoint(0x002DAE20); dl = time.time() + 600
        while k < a.hits and time.time() < dl:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print("[dae20] no stop", flush=True); break
            select_main(rsp); regs = rsp.read_registers(); k += 1
            r = {i: regs.get(f"r{i}", 0) & 0xFFFFFFFF for i in (3, 4, 5, 6)}
            c = r[3]; words = [f"0x{rsp.read_u32(c+o):08X}" for o in range(0, 0x28, 4)] if guest_ok(c) else []
            st = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 6) if f["pc"]][1:5]
            g = rsp.read_u32(0x0053F58C); m = rsp.read_u32(g + 0x3C) if guest_ok(g) else 0
            tab = rsp.read_mem(0x00868D48, 40 * 4); idx = [i for i in range(40) if int.from_bytes(tab[i*4:i*4+4], "big") == m]
            cur = rsp.read_mem(m + 0xC8, 1)[0] if guest_ok(m) else None; cur = cur - 256 if cur is not None and cur > 127 else cur
            st0 = f"0x{rsp.read_u32(m + 0x48):08X}" if guest_ok(m) else None
            print(f"[dae20]    mgr(*(*0x53F58C)+0x3C)=0x{m:08X} TABLE[{idx}] cursor={cur} stack0={st0}", flush=True)
            print(f"[dae20] #{k} t={time.time()-t0:.1f} cache=0x{r[3]:08X} obj=0x{r[4]:08X} p3=0x{r[5]:08X} p4=0x{r[6]:08X} cache[0..9]={words} ra={st}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__ == "__main__": main()
