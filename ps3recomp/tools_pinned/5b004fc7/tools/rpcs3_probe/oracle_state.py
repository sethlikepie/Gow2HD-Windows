"""oracle_state.py - the application state sequence on the oracle (GoW2 wall [G]).

Main loop FUN_00242c94: app = *PTR_DAT_0053ec34; per-frame handler OPD at app+0x460C
(code = *OPD). The game advances boot -> intro -> menu by rewriting that OPD.
Sample it (cont -> sleep -> 0x03) and print every change with the guest-side
time; also the intro counter 0x592668 and the main thread pc/stack at each change.
"""
import argparse, json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok, INTRO_COUNTER

APP_SLOT = 0x0053EC34

def state(rsp):
    app = rsp.read_u32(APP_SLOT)
    if not guest_ok(app): return app, None, None
    opd = rsp.read_u32(app + 0x460C)
    code = rsp.read_u32(opd) if guest_ok(opd) else None
    return app, opd, code

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", required=True); ap.add_argument("--out", default="oracle_state.json")
    ap.add_argument("--boot-wait", type=float, default=1.5); ap.add_argument("--interval", type=float, default=1.0)
    ap.add_argument("--samples", type=int, default=120)
    a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True)
    rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=30.0)
    res = {"samples": []}; t0 = time.time(); guest = a.boot_wait; prev = None
    try:
        time.sleep(a.boot_wait); rsp.connect(); rsp.handshake()
        for k in range(a.samples + 1):
            if k:
                rsp.cont(); time.sleep(a.interval); guest += a.interval; rsp.interrupt()
                stop = rsp.wait_stop(deadline=time.time() + 20)
                if not stop or stop[:1] not in (b"S", b"T"): print(f"[state] no stop {stop!r}", flush=True); break
            try:
                app, opd, code = state(rsp)
                ic = rsp.read_u32(INTRO_COUNTER)
                select_main(rsp); regs = rsp.read_registers(); pc = probe._pc(regs)
                st = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 10) if f["pc"]]
            except Exception as e:
                print(f"[state] s{k} err {e}", flush=True); continue
            cur = (opd, code)
            ev = {"k": k, "guest_s": round(guest, 1), "app": f"0x{app:08X}", "opd": f"0x{opd:08X}" if opd else None,
                  "code": f"0x{code:08X}" if code else None, "intro": ic, "pc": f"0x{pc:08X}" if pc else None, "stack": st}
            res["samples"].append(ev)
            mark = "CHANGE" if cur != prev else "      "
            print(f"[state] {mark} s{k:3d} guest~{guest:6.1f}s opd={ev['opd']} code={ev['code']} intro={ic} pc={ev['pc']} stack={st[:7]}", flush=True)
            prev = cur
    except Exception as e:
        res["error"] = f"{type(e).__name__}: {e}"; print(f"[state] ERROR: {res['error']}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        try: proc.kill()
        except Exception: pass
    json.dump(res, open(a.out, "w"), indent=1); print(f"[state] wrote {a.out}", flush=True)

if __name__ == "__main__": main()
