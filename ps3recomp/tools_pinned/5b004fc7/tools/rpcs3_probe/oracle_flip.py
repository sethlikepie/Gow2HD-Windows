"""oracle_flip.py - when does a manager cursor first become >= 0 on the oracle?

Attach early, then interrupt-sample every `interval` s: main-thread pc + stack +
the cursors of WATCH managers. Stops once TABLE[key] cursor >= 0 (the flip) or
after max samples. Cheap capture (no thread census) so the bracket is tight.
"""
import argparse, json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import cursors, select_main, INTRO_COUNTER, managers

def snap(rsp, label, t0):
    select_main(rsp)
    regs = rsp.read_registers(); pc = probe._pc(regs)
    st = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 14) if f["pc"]]
    cs = cursors(rsp)
    ms = {}
    try:
        ms = {k: v for k, v in managers(rsp).items() if k in (1, 3, 4, 22)}
    except Exception:
        pass
    ev = {"label": label, "t": round(time.time()-t0, 2), "pc": f"0x{pc:08X}" if pc else None, "stack": st, "cursors": cs, "mgrs": ms}
    top = " ".join(f"[{k}]={v['cursor']}:{v['stack']}" for k, v in sorted(ms.items()))
    print(f"[flip] {label:8s} t={ev['t']:6.2f} pc={ev['pc']} c={cs} stack={st[:8]}\n        {top}", flush=True)
    return ev

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", required=True); ap.add_argument("--out", default="oracle_flip.json")
    ap.add_argument("--boot-wait", type=float, default=2.5); ap.add_argument("--interval", type=float, default=0.25)
    ap.add_argument("--samples", type=int, default=400); ap.add_argument("--key", type=int, default=4)
    ap.add_argument("--until", type=float, default=0.0, help="sample until this many seconds after launch (0 = stop at flip)")
    a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True)
    rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=30.0)
    res = {"samples": []}; t0 = time.time()
    try:
        time.sleep(a.boot_wait); rsp.connect(); rsp.handshake()
        try: res["samples"].append(snap(rsp, "attach", t0))
        except Exception as e: print(f"[flip] attach: {e}", flush=True)
        for k in range(1, a.samples+1):
            rsp.cont(); time.sleep(a.interval); rsp.interrupt()
            stop = rsp.wait_stop(deadline=time.time()+20)
            if not stop or stop[:1] not in (b"S", b"T"): print(f"[flip] no stop ({stop!r})", flush=True); break
            try: ev = snap(rsp, f"s{k}", t0)
            except Exception as e: print(f"[flip] s{k}: {e}", flush=True); continue
            res["samples"].append(ev)
            c = ev["cursors"].get(a.key)
            if a.until > 0 and ev["t"] >= a.until:
                break
            if a.until <= 0 and c is not None and c >= 0:
                print(f"[flip] FLIP: TABLE[{a.key}] cursor={c} at sample s{k}", flush=True); break
    except Exception as e:
        res["error"] = f"{type(e).__name__}: {e}"; print(f"[flip] ERROR: {res['error']}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        try: proc.kill()
        except Exception: pass
    json.dump(res, open(a.out, "w"), indent=1); print(f"[flip] wrote {a.out}", flush=True)

if __name__ == "__main__": main()
