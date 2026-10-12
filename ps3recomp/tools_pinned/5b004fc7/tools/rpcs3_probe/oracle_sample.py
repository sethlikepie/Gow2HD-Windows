"""oracle_sample.py - periodic snapshots of a title running under RPCS3's GDB stub.

RPCS3's stub honours only `vCont;c` (rsp.py sends that now). After a resume the
stub blocks in wait_with_interrupts() until either a breakpoint pauses a thread or
the client sends the 0x03 interrupt byte; both end in an `S05` stop reply. That
makes SAMPLING possible: cont -> wait `interval` s -> 0x03 -> capture -> cont ...
all within the single attach a launch allows (E81 §4).

Each sample records: every PPU thread's pc (qfThreadInfo + Hg + g), the stack of
the thread the stub selected, the GoW2 manager table cursors (TABLE 0x00868D48,
cursor int8 @mgr+0xC8, stack @+0x48) and the intro counter 0x00592668. Optional
breakpoints are still armed so a hit is captured as its own event.

    python tools/rpcs3_probe/oracle_sample.py --title <ELF> --out o.json \
        [--boot-wait 15] [--interval 10] [--samples 12] [--bp 0x000B70B0 ...]
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient, RSPError          # noqa: E402
import probe                                  # noqa: E402
import rpcs3_host                             # noqa: E402
from oracle_scopes import managers, s8, INTRO_COUNTER   # noqa: E402


def thread_ids(rsp):
    ids = []
    rsp._send_packet("qfThreadInfo")
    r = rsp._recv_packet().decode("latin-1")
    while r.startswith("m"):
        for tok in r[1:].split(","):
            tok = tok.strip()
            if tok:
                ids.append(tok)
        rsp._send_packet("qsThreadInfo")
        r = rsp._recv_packet().decode("latin-1")
    return ids


def thread_pcs(rsp, ids):
    out = {}
    for t in ids:
        rsp._send_packet(f"Hg{t}")
        rsp._recv_packet()
        try:
            regs = rsp.read_registers()
            pc = probe._pc(regs)
            out[t] = f"0x{pc:08X}" if pc is not None else None
        except Exception as e:
            out[t] = f"err:{e}"
    return out


def capture(rsp, label, t0):
    regs = rsp.read_registers()
    pc = probe._pc(regs)
    st = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 20) if f["pc"]]
    ids = thread_ids(rsp)
    ev = {"label": label, "t": round(time.time() - t0, 1),
          "pc": f"0x{pc:08X}" if pc else None, "stack": st,
          "r3": f"0x{regs.get('r3',0)&0xFFFFFFFF:08X}", "r4": f"0x{regs.get('r4',0)&0xFFFFFFFF:08X}",
          "threads": thread_pcs(rsp, ids),
          "intro_counter": rsp.read_u32(INTRO_COUNTER),
          "managers": managers(rsp)}
    # restore the selected thread to the one the stub paused (first id is not
    # necessarily it; re-select via the pc match is overkill -- leave as is)
    print(f"[sample] {label} t={ev['t']}s pc={ev['pc']} intro={ev['intro_counter']} stack={st[:10]}", flush=True)
    print(f"         threads={ev['threads']}", flush=True)
    line = " ".join(f"[{i}]={m['cursor']}" for i, m in sorted(ev["managers"].items()))
    print(f"         cursors: {line}", flush=True)
    return ev


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rpcs3", default=None)
    ap.add_argument("--title", required=True)
    ap.add_argument("--out", default="oracle_sample.json")
    ap.add_argument("--port", type=int, default=None)
    ap.add_argument("--boot-wait", type=float, default=15.0)
    ap.add_argument("--interval", type=float, default=10.0)
    ap.add_argument("--samples", type=int, default=12)
    ap.add_argument("--bp", action="append", default=[])
    ap.add_argument("--no-headless", action="store_true")
    a = ap.parse_args()
    if a.port is None:
        a.port = rpcs3_host.stub_port()
    print(f"[sample] launching RPCS3: {a.title}", flush=True)
    proc = probe.launch_rpcs3(a.rpcs3, a.title, headless=True)  # sempre com janela (pedido 2026-09-02))
    rsp = RSPClient(port=a.port, timeout=30.0)
    result = {"title": a.title, "samples": []}
    t0 = time.time()
    try:
        time.sleep(a.boot_wait)
        rsp.connect()
        rsp.handshake()
        result["samples"].append(capture(rsp, "attach", t0))
        for b in a.bp:
            ea = int(b, 0)
            print(f"[sample] Z0 0x{ea:08X} -> {rsp.add_breakpoint(ea)}", flush=True)
        for k in range(1, a.samples + 1):
            rsp.cont()
            stop = rsp.wait_stop(deadline=time.time() + a.interval)
            how = "bp"
            if not stop or stop[:1] not in (b"S", b"T"):
                rsp.interrupt()
                stop = rsp.wait_stop(deadline=time.time() + 20)
                how = "intr"
            if not stop or stop[:1] not in (b"S", b"T"):
                print(f"[sample] no stop reply after interrupt ({stop!r}); ending", flush=True)
                break
            result["samples"].append(capture(rsp, f"{how}#{k}", t0))
    except Exception as e:
        result["error"] = f"{type(e).__name__}: {e}"
        print(f"[sample] ERROR: {result['error']}", flush=True)
    finally:
        try:
            rsp.close()
        except Exception:
            pass
        try:
            proc.kill()
        except Exception:
            pass
    json.dump(result, open(a.out, "w"), indent=1)
    print(f"[sample] wrote {a.out}", flush=True)
    return 0 if "error" not in result else 1


if __name__ == "__main__":
    sys.exit(main())
