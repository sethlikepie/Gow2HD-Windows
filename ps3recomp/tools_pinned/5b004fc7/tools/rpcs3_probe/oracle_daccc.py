"""oracle_daccc.py - FUN_002daccc(head, key): list search; print head/key/list length (bounded) per hit."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--at", type=float, default=7.8); ap.add_argument("--hits", type=int, default=40); a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0); t0 = time.time(); k = 0
    try:
        time.sleep(a.at); rsp.connect(); rsp.handshake(); rsp.add_breakpoint(0x002DACCC); rsp.add_breakpoint(0x002B4340); dl = time.time() + 600
        while k < a.hits and time.time() < dl:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print("[daccc] no stop", flush=True); break
            select_main(rsp); regs = rsp.read_registers(); pc = probe._pc(regs); k += 1
            r3 = regs.get("r3", 0) & 0xFFFFFFFF; r4 = regs.get("r4", 0) & 0xFFFFFFFF
            if pc == 0x002B4340:
                nm = None
                for rr in (4, 6):
                    ea = regs.get(f"r{rr}", 0) & 0xFFFFFFFF
                    if guest_ok(ea):
                        s = rsp.read_mem(ea, 20).split(b"\0")[0]
                        if 2 <= len(s) < 20 and all(32 <= c < 127 for c in s): nm = s.decode(); break
                print(f"[daccc] wad_open {nm} t={time.time()-t0:.1f}", flush=True); continue
            n = rsp.read_u32(r3) if guest_ok(r3) else 0; j = 0; found = None
            while guest_ok(n) and n != r3 and j < 300:
                kk = rsp.read_u32(n + 8)
                if kk == r4 and found is None: found = j
                n = rsp.read_u32(n); j += 1
            st = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 5) if f["pc"]][1:4]
            print(f"[daccc] #{k} head=0x{r3:08X} key=0x{r4:08X} nos={j} circular={'sim' if n==r3 else 'NAO'} found_at={found} ra={st}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()
if __name__ == "__main__": main()
