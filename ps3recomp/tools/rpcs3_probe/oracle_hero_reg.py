"""oracle_hero_reg.py - registrations (FUN_0039e794: r3=manager, r4=desc) right after the
3rd WAD open (R_Hero01) on the oracle; class = u16 at desc+2, subtag = (w0>>16)&0xFFF."""
import argparse, json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok, TABLE

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--title", required=True); ap.add_argument("--out", default="oracle_hero_reg.json")
    ap.add_argument("--nth-open", type=int, default=3); ap.add_argument("--max-reg", type=int, default=80); ap.add_argument("--deadline", type=float, default=1500)
    a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True); rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=30.0)
    res = {"events": []}; t0 = time.time(); opens = 0; armed = False; nreg = 0
    try:
        time.sleep(1.5); rsp.connect(); rsp.handshake()
        rsp.add_breakpoint(0x002B4340); rsp.add_breakpoint(0x0041FA30)
        tab = rsp.read_mem(TABLE, 40 * 4); idx = {int.from_bytes(tab[i*4:i*4+4], "big"): i for i in range(40)}
        dl = time.time() + a.deadline
        while time.time() < dl and nreg < a.max_reg:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print("[reg] no stop", flush=True); break
            select_main(rsp); regs = rsp.read_registers(); pc = probe._pc(regs)
            r3 = regs.get("r3", 0) & 0xFFFFFFFF; r4 = regs.get("r4", 0) & 0xFFFFFFFF
            if pc == 0x002B4340:
                opens += 1
                nm = None
                for rr in (4, 6):
                    ea = regs.get(f"r{rr}", 0) & 0xFFFFFFFF
                    if guest_ok(ea):
                        try: s = rsp.read_mem(ea, 24).split(b"\0")[0]
                        except Exception: s = b""
                        if 2 <= len(s) < 24 and all(32 <= c < 127 for c in s): nm = s.decode(); break
                print(f"[reg] open #{opens} {nm} t={time.time()-t0:.1f}", flush=True)
                if opens == a.nth_open and not armed:
                    rsp.add_breakpoint(0x0039E794); armed = True
                    tab = rsp.read_mem(TABLE, 40 * 4); idx = {int.from_bytes(tab[i*4:i*4+4], "big"): i for i in range(40)}
                continue
            if pc == 0x0041FA30:
                t4 = int.from_bytes(tab[16:20], "big"); t3 = int.from_bytes(tab[12:16], "big")
                info = ""
                for nm, m in (("T4", t4), ("T3", t3)):
                    if guest_ok(m):
                        c = rsp.read_mem(m + 0xC8, 1)[0]; c = c - 256 if c > 127 else c
                        info += f" {nm}: cur={c} d4=0x{rsp.read_u32(m+0xD4):08X} st0=0x{rsp.read_u32(m+0x48):08X} st1=0x{rsp.read_u32(m+0x4C):08X} st2=0x{rsp.read_u32(m+0x50):08X};"
                print(f"[reg] root_new r4=0x{r4:08X} t={time.time()-t0:.1f}{info}", flush=True); continue
            if pc == 0x0039E794:
                nreg += 1
                cls = int.from_bytes(rsp.read_mem(r4 + 2, 2), "big") if guest_ok(r4) else None
                w0 = rsp.read_u32(r4) if guest_ok(r4) else None
                st = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 5) if f["pc"]][1:4]
                d4 = rsp.read_u32(r3 + 0xD4) if guest_ok(r3) else None
                cur = rsp.read_mem(r3 + 0xC8, 1)[0] if guest_ok(r3) else None
                cur = cur - 256 if cur is not None and cur > 127 else cur
                ev = {"n": nreg, "mgr": f"0x{r3:08X}", "tidx": idx.get(r3), "desc": f"0x{r4:08X}", "cls": cls, "w0": f"0x{w0:08X}" if w0 is not None else None, "ra": st,
                      "d4": f"0x{d4:08X}" if d4 is not None else None, "cur": cur}
                res["events"].append(ev)
                print(f"[reg] {nreg:3d} T[{ev['tidx']}] mgr={ev['mgr']} desc={ev['desc']} cls={cls} w0={ev['w0']} d4={ev['d4']} cur={cur} ra={st}", flush=True)
    except Exception as e:
        res["error"] = f"{type(e).__name__}: {e}"; print(f"[reg] ERROR: {res['error']}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        try: proc.kill()
        except Exception: pass
    json.dump(res, open(a.out, "w"), indent=1); print(f"[reg] wrote {a.out}", flush=True)

if __name__ == "__main__": main()
