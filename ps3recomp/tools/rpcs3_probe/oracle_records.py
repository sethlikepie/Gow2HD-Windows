"""oracle_records.py - the WAD record sequence the oracle's dispatcher sees.

Breaks on FUN_002b9c80 (record dispatcher: r3=type_sys+0x7c hdr, r4=record ptr),
FUN_002b0db0 (push handler) and FUN_002b0d60 (pop handler) and logs, per stop,
the record word0 / +6 count (big-endian, read from guest memory) plus the loader
state (rem/avail) and the class-1/22 cursors. Compare with our [DESTROY] lines.
"""
import argparse, json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, cursors, s8, guest_ok

BPS = {0x002B9C80: "disp", 0x002B0DB0: "PUSHH", 0x002B0D60: "POPH", 0x000B70B0: "b71_054"}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", required=True); ap.add_argument("--out", default="oracle_records.json")
    ap.add_argument("--boot-wait", type=float, default=1.5); ap.add_argument("--deadline", type=float, default=1800)
    ap.add_argument("--max-events", type=int, default=6000)
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
            if not stop or stop[:1] not in (b"S", b"T"): print(f"[rec] no stop {stop!r}", flush=True); break
            select_main(rsp); regs = rsp.read_registers(); pc = probe._pc(regs); n += 1
            tag = BPS.get(pc, f"pc_{pc:08X}")
            r3 = regs.get("r3", 0) & 0xFFFFFFFF; r4 = regs.get("r4", 0) & 0xFFFFFFFF
            # r3 = record header (type_sys+0x7c: u16 type @+0, u16 flags @+2, u32 size @+4);
            # r4 = payload (u16 class @+2 for the pop handler)
            h = rsp.read_mem(r3, 8) if guest_ok(r3) else None
            hdr = (int.from_bytes(h[0:2], "big"), int.from_bytes(h[2:4], "big"), int.from_bytes(h[4:8], "big")) if h else None
            pl = rsp.read_mem(r4, 8) if guest_ok(r4) else None
            pcls = int.from_bytes(pl[2:4], "big") if pl else None
            cs = cursors(rsp)
            ev = {"n": n, "t": round(time.time() - t0, 1), "tag": tag, "r3": f"0x{r3:08X}", "r4": f"0x{r4:08X}",
                  "hdr": hdr, "pcls": pcls, "c1": cs[1], "c22": cs[22], "ld": cs.get("ld")}
            res["events"].append(ev)
            ht = f"type=0x{hdr[0]:02X} fl=0x{hdr[1]:04X} size={hdr[2]}" if hdr else "hdr=?"
            print(f"[rec] {n:5d} t={ev['t']:6.1f} {tag:7s} {ht} pcls={pcls} r4={ev['r4']} c1={cs[1]} c22={cs[22]} {cs.get('ld')}", flush=True)
            if tag == "b71_054": break
    except Exception as e:
        res["error"] = f"{type(e).__name__}: {e}"; print(f"[rec] ERROR: {res['error']}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        try: proc.kill()
        except Exception: pass
    json.dump(res, open(a.out, "w"), indent=1); print(f"[rec] wrote {a.out}", flush=True)

if __name__ == "__main__": main()
