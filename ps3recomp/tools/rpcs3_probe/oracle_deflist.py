"""oracle_deflist.py - FUN_00256B1C @0x256C6C: after `lwz r27,4(r27)` walk the circular list
(node: next @+0, desc @+8; class = u16 @desc+2) that yields a root's default children."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--hits", type=int, default=6)
    a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0 = time.time(); k = 0
    try:
        time.sleep(1.5); rsp.connect(); rsp.handshake(); rsp.add_breakpoint(0x00256C6C)
        dl = time.time() + 900
        while k < a.hits and time.time() < dl:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print("[deflist] no stop", flush=True); break
            select_main(rsp); regs = rsp.read_registers(); k += 1
            r24, r26, r27, r28 = (regs.get(f"r{i}", 0) & 0xFFFFFFFF for i in (24, 26, 27, 28))
            cls = []; n = rsp.read_u32(r27) if guest_ok(r27) else 0; steps = 0
            while guest_ok(n) and n != r27 and steps < 40:
                d = rsp.read_u32(n + 8); c = int.from_bytes(rsp.read_mem(d + 2, 2), "big") if guest_ok(d) else None
                cls.append(c); n = rsp.read_u32(n); steps += 1
            print(f"[deflist] #{k} t={time.time()-t0:.1f} mgr=0x{r24:08X} desc=0x{r26:08X} r5=0x{r28:08X} head=0x{r27:08X} classes={cls}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        try: proc.kill()
        except Exception: pass

if __name__ == "__main__": main()
