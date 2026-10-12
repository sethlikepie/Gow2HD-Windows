"""oracle_ldrsh.py -- CONSOLE (E417): does the level shader loader (ICGLdrShader body, func_0032109C = our [LDRSH])
run in the front-end phase, and after which container loads?

Ours (E416b + PS3_LWMUTEX_REAL=1): in ~20 s the driver runs func_002BAD10 with lr 0x41F6C (R_LglScA sync),
0x41FCC (R_PermA), 0x42F0C (state 13), 0x42AA4 (state 21), builds 1465 type-15 shells, then the loader idles
(state 2, rem 0) and [LDRSH]=0. This probe records, on the skip-intro ELF, the order and time of:
  func_002BAD10 entry (container loads; lr tells the driver case), func_003CC208 ([SHADERSRC] defs),
  func_0032109C ([LDRSH]).
Stop reply of this stub carries no thread id: the parked thread is found by scanning qfThreadInfo."""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
from oracle_sample import thread_ids

BAD10, SHADERSRC, LDRSH = 0x002BAD10, 0x003CC208, 0x0032109C
NAMES = {BAD10: "BAD10", SHADERSRC: "SHADERSRC", LDRSH: "LDRSH"}


def parked(rsp, bps):
    r = rsp.read_registers(); pc = probe._pc(r) & 0xFFFFFFFF
    if pc in bps:
        return r, pc
    for t in thread_ids(rsp):
        rsp._send_packet(f"Hg{t}"); rsp._recv_packet()
        try:
            rr = rsp.read_registers(); pp = probe._pc(rr) & 0xFFFFFFFF
        except Exception:
            continue
        if pp in bps:
            return rr, pp
    return r, pc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", required=True); ap.add_argument("--at", type=float, default=3.0)
    ap.add_argument("--deadline", type=float, default=300); ap.add_argument("--ldrsh-hits", type=int, default=6)
    a = ap.parse_args()
    proc = probe.launch_rpcs3(None, a.title, headless=True)
    rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0)
    counts = {k: 0 for k in NAMES}
    try:
        time.sleep(a.at); rsp.connect(retries=240, delay=0.5); rsp.handshake(); select_main(rsp)
        for ea in NAMES:
            rsp.add_breakpoint(ea)
        t0 = time.time(); dl = t0 + a.deadline
        while time.time() < dl and counts[LDRSH] < a.ldrsh_hits:
            rsp.cont(); st = rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S", b"T"):
                print(f"[ldrsh] end stop={st[:40]!r}", flush=True); break
            r, pc = parked(rsp, set(NAMES))
            g = lambda k: r.get(k, 0) & 0xFFFFFFFF
            name = NAMES.get(pc, f"other@0x{pc:08X}")
            if pc in counts:
                counts[pc] += 1
            n = counts.get(pc, 0)
            if pc == BAD10 or n <= 8 or (n % 50) == 0:
                print(f"[ldrsh] t={time.time()-t0:6.1f}s {name} #{n} lr=0x{g('lr'):08X} r3=0x{g('r3'):08X} r4=0x{g('r4'):08X}", flush=True)
            select_main(rsp)
        print(f"[ldrsh] DONE t={time.time()-t0:.1f}s counts: " + ", ".join(f"{NAMES[k]}={v}" for k, v in counts.items()), flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()


if __name__ == "__main__":
    main()
