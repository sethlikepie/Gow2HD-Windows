"""oracle_mgr_d4.py -- the "manager" func_002A43DC pops its pool from, on the console.

Ours (2026-09-22, PS3_TRACE_POOLCTOR): func_002A43DC runs with r30 = 0x407DB31C,
which is not a manager (word0 0xC0150015, floats around it), pool=[r30+0xD4]=0
then 0x80000000, and the manager constructor func_002A4120 never runs.
Chain: func_00029AF0 (script VM) -> func_000CD7B4 -> func_000CC094 -> 0x2A43DC.

Breakpoints:
  0x002A43E0  after `lwz r3,0xD4(r30)`: r30 = manager, r3 = pool
  0x002A4120  manager constructor (only reached through a vtable)
  0x000CC0C0  the call site in func_000CC094 that hands [r30+8] to 0x2A4354

Usage: python3 oracle_mgr_d4.py --title "<full path to EBOOT_skipintro.BIN>"
"""
import argparse, os, sys, time, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main, guest_ok

POP = 0x002A43E0
CTOR = 0x002A4120
CALL = 0x000CC0C0


def rd(rsp, a):
    return rsp.read_u32(a) if guest_ok(a) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", required=True)
    ap.add_argument("--at", type=float, default=2.0)
    ap.add_argument("--hits", type=int, default=60)
    ap.add_argument("--deadline", type=float, default=240)
    a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True)
    rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0)
    cnt = collections.Counter()
    t0 = time.time()
    try:
        time.sleep(a.at)
        rsp.connect(retries=240, delay=0.5)
        rsp.handshake()
        select_main(rsp)
        for bp in (POP, CTOR, CALL):
            rsp.add_breakpoint(bp)
        dl = time.time() + a.deadline
        k = 0
        while k < a.hits and time.time() < dl:
            rsp.cont()
            st = rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S", b"T"):
                print("[mgr] end", flush=True)
                break
            select_main(rsp)
            regs = rsp.read_registers()
            pc = probe._pc(regs) & 0xFFFFFFFF
            r = {i: regs.get(f"r{i}", 0) & 0xFFFFFFFF for i in (3, 4, 5, 26, 27, 29, 30, 31)}
            lr = regs.get("lr", 0) & 0xFFFFFFFF
            k += 1
            t = time.time() - t0
            if pc == POP:
                cnt["pop"] += 1
                m = r[30]
                print(f"[mgr] POP #{cnt['pop']} t={t:.1f} mgr=0x{m:08X} w0={rd(rsp, m)} "
                      f"pool=0x{r[3]:08X} [pool+4]={rd(rsp, r[3] + 4) if r[3] else None} "
                      f"node(r29)=0x{r[29]:08X} lr=0x{lr:08X}", flush=True)
            elif pc == CTOR:
                cnt["ctor"] += 1
                print(f"[mgr] CTOR #{cnt['ctor']} t={t:.1f} this=0x{r[3]:08X} pool(r5)=0x{r[5]:08X} "
                      f"lr=0x{lr:08X}", flush=True)
            elif pc == CALL:
                cnt["call"] += 1
                o = r[30]
                print(f"[mgr] CC094 #{cnt['call']} t={t:.1f} obj=0x{o:08X} [obj+8]=r3=0x{r[3]:08X} "
                      f"w0(obj)={rd(rsp, o)}", flush=True)
        print(f"[mgr] DONE {dict(cnt)}", flush=True)
    finally:
        try:
            rsp.close()
        except Exception:
            pass
        proc.kill()


if __name__ == "__main__":
    main()
