"""oracle_wads.py - the order in which the console opens WADs/files (GoW2).

Breakpoints on func_002B4340 (WAD open by name; ours: '42A1D4-EMPTY skip -> 002B4340
nome=...') and on 0x0030D578 (FIOS open site; ours prints path there). At each stop
print any printable string reachable from r3/r4/r5 plus guest time.
"""
import argparse, json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok

BPS = {0x002B4340: "wad_open", 0x0030D578: "fios_open", 0x000B70B0: "b71_054"}

def cstr(rsp, ea):
    if not guest_ok(ea): return None
    try: b = rsp.read_mem(ea, 48)
    except Exception: return None
    s = b.split(b"\0")[0]
    if 2 <= len(s) <= 47 and all(32 <= c < 127 for c in s): return s.decode()
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", required=True); ap.add_argument("--out", default="oracle_wads.json")
    ap.add_argument("--boot-wait", type=float, default=1.5); ap.add_argument("--deadline", type=float, default=900)
    ap.add_argument("--max-events", type=int, default=80)
    a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True)
    rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=30.0)
    res = {"events": []}; t0 = time.time()
    try:
        time.sleep(a.boot_wait); rsp.connect(); rsp.handshake()
        for ea in BPS: rsp.add_breakpoint(ea)
        dl = time.time() + a.deadline; n = 0
        while n < a.max_events and time.time() < dl:
            rsp.cont(); stop = rsp.wait_stop(deadline=dl)
            if not stop or stop[:1] not in (b"S", b"T"): print(f"[wads] no stop {stop!r}", flush=True); break
            select_main(rsp); regs = rsp.read_registers(); pc = probe._pc(regs); n += 1
            tag = BPS.get(pc, f"pc_{pc:08X}")
            strs = {f"r{i}": cstr(rsp, regs.get(f"r{i}", 0) & 0xFFFFFFFF) for i in (3, 4, 5, 6)}
            strs = {k: v for k, v in strs.items() if v}
            # deref one level too (struct with name ptr)
            for i in (3, 4):
                ea = regs.get(f"r{i}", 0) & 0xFFFFFFFF
                if guest_ok(ea):
                    for off in (0, 4, 8):
                        try: p = rsp.read_u32(ea + off)
                        except Exception: break
                        v = cstr(rsp, p)
                        if v: strs[f"*r{i}+{off}"] = v
            ev = {"n": n, "t": round(time.time() - t0, 1), "tag": tag, "strs": strs,
                  "stack": [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 6) if f["pc"]][1:5]}
            res["events"].append(ev)
            print(f"[wads] {n:3d} t={ev['t']:6.1f} {tag:9s} {strs} ra={ev['stack']}", flush=True)
    except Exception as e:
        res["error"] = f"{type(e).__name__}: {e}"; print(f"[wads] ERROR: {res['error']}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        try: proc.kill()
        except Exception: pass
    json.dump(res, open(a.out, "w"), indent=1); print(f"[wads] wrote {a.out}", flush=True)

if __name__ == "__main__": main()
