"""oracle_fosize.py -- CONSOLE (E412): who writes the file size into FO+0x48 during the sync open of R_LglScA.

Ours (E411): the sync open's tail returns 1 but op+0x10=-1 / ts+0x74=0, i.e. FO+0x48 (u64 file size, FO=*(op+4))
is never filled on the natural path. The console delivers 0xC00 there (E398/E401, oracle_optail: FO=0x33018528).

The RPCS3 stub has no write watchpoints (Z0 only), so: arm Z0 on func_002BAD10's entry; at the first entry with
lr=0x41F6C (the sync LglScA open, flags 0x10000210) arm Z0 on every candidate store (--cands: `0xEA mn rS, d(rB)`
from scan_stores48.py; --cands-idx: `0xEA mn rS, rA, rB` indexed stores), on the DONE handler (0x2B4274) and on the
open tail (0x2B4434). The stop reply of this stub carries no thread id, so after each stop the thread parked on one
of our breakpoints is found by scanning qfThreadInfo (Hg + g per thread) -- a store may run on the FIOS thread and
reading the main thread's registers there would give a false EA. At the tail it prints FO=*(op+4) and *(FO+0x48);
the verdict filters every recorded store whose EA lands in [FO+0x48, FO+0x50).

Attach early (--at ~3 s): at 9 s the sync LglScA open has already happened on the skip-intro ELF (measured E412 r1:
first entries seen were lr=0x439AC/0x428C8/0x437AC/0x43B94)."""
import argparse, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsp import RSPClient
import probe, rpcs3_host
from oracle_scopes import select_main
from oracle_sample import thread_ids

ENT, DONE, TAIL, OP = 0x002BAD10, 0x002B4274, 0x002B4434, 0x008695D4
RX_D = re.compile(r"(r\d+),\s*(-?0x[0-9A-Fa-f]+|-?\d+)\((r\d+)\)")
RX_X = re.compile(r"(r\d+),\s*(r\d+),\s*(r\d+)")


def load_cands(path, indexed):
    out = {}
    if not path:
        return out
    for ln in open(path):
        p = ln.split(None, 2)
        if len(p) < 3 or not p[0].startswith("0x"):
            continue
        if indexed:
            m = RX_X.search(p[2])
            if m:
                out[int(p[0], 16)] = (p[1], m.group(1), None, m.group(2), m.group(3))
        else:
            m = RX_D.search(p[2])
            if m:
                out[int(p[0], 16)] = (p[1], m.group(1), int(m.group(2), 0), m.group(3), None)
    return out


def stopped_thread(rsp, bps):
    """Return (tid, regs, pc) of a thread parked on one of `bps`, or of the current thread if none is."""
    r = rsp.read_registers(); pc = probe._pc(r) & 0xFFFFFFFF
    if pc in bps:
        return None, r, pc
    for t in thread_ids(rsp):
        rsp._send_packet(f"Hg{t}"); rsp._recv_packet()
        try:
            rr = rsp.read_registers(); pp = probe._pc(rr) & 0xFFFFFFFF
        except Exception:
            continue
        if pp in bps:
            return t, rr, pp
    return None, r, pc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", required=True); ap.add_argument("--cands", required=True); ap.add_argument("--cands-idx")
    ap.add_argument("--at", type=float, default=3.0); ap.add_argument("--deadline", type=float, default=420)
    ap.add_argument("--maxstops", type=int, default=4000); ap.add_argument("--tails", type=int, default=2)
    a = ap.parse_args()
    cands = load_cands(a.cands, False); cands.update(load_cands(a.cands_idx, True))
    print(f"[fosize] {len(cands)} candidate stores loaded", flush=True)
    proc = probe.launch_rpcs3(None, a.title, headless=True)
    rsp = RSPClient(port=rpcs3_host.stub_port(), timeout=60.0)
    rec, armed, tails, stops = [], False, [], 0
    bps = {ENT}
    try:
        time.sleep(a.at); rsp.connect(retries=240, delay=0.5); rsp.handshake(); select_main(rsp)
        rsp.add_breakpoint(ENT); dl = time.time() + a.deadline
        while stops < a.maxstops and time.time() < dl and len(tails) < a.tails:
            rsp.cont(); st = rsp.wait_stop(deadline=dl)
            if not st or st[:1] not in (b"S", b"T"):
                print(f"[fosize] end stop={st[:40]!r}", flush=True); break
            stops += 1
            tid, r, pc = stopped_thread(rsp, bps)
            g = lambda k: r.get(k, 0) & 0xFFFFFFFF
            if pc == ENT:
                print(f"[fosize] BAD10 entry tid={tid} desc=0x{g('r3'):08X} flags=0x{g('r4'):08X} lr=0x{g('lr'):08X}", flush=True)
                if not armed and g("lr") == 0x00041F6C:
                    ok = sum(1 for ea in cands if rsp.add_breakpoint(ea))
                    rsp.add_breakpoint(DONE); rsp.add_breakpoint(TAIL); armed = True
                    bps |= set(cands) | {DONE, TAIL}
                    print(f"[fosize] ARMED {ok}/{len(cands)} stores + DONE + TAIL", flush=True)
            elif pc in cands:
                mn, src, disp, ra, rb = cands[pc]
                if disp is not None:
                    ea = (r.get(ra, 0) + disp) & 0xFFFFFFFF
                else:
                    ea = ((0 if ra == "r0" else r.get(ra, 0)) + r.get(rb, 0)) & 0xFFFFFFFF
                val = r.get(src, 0) & (0xFFFFFFFFFFFFFFFF if mn.startswith("std") else 0xFFFFFFFF)
                rec.append((pc, tid, ea, val, g("lr")))
                if len(rec) <= 400:
                    print(f"[fosize] ST pc=0x{pc:08X} {mn} tid={tid} ea=0x{ea:08X} val=0x{val:X} lr=0x{g('lr'):08X}", flush=True)
            elif pc == DONE:
                op = g("r3"); fo = rsp.read_u32(op + 4) if op else 0
                fo48 = rsp.read_u64(fo + 0x48) if 0x10000 <= fo < 0xF0000000 else -1
                print(f"[fosize] DONE tid={tid} op=0x{op:08X} FO=*(op+4)=0x{fo:08X} FO+0x48=0x{fo48:X} op+0x10=0x{rsp.read_u32(op + 0x10) if op else 0:08X}", flush=True)
            elif pc == TAIL:
                op = g("r31"); fo = rsp.read_u32(op + 4)
                fo48 = rsp.read_u64(fo + 0x48) if 0x10000 <= fo < 0xF0000000 else -1
                tails.append((op, fo, fo48))
                print(f"[fosize] TAIL tid={tid} op=0x{op:08X} FO=0x{fo:08X} FO+0x48=0x{fo48:X} loader-op+0x10=0x{rsp.read_u32(OP + 0x10):08X}", flush=True)
            else:
                print(f"[fosize] other pc=0x{pc:08X} (no thread parked on a breakpoint found)", flush=True)
            select_main(rsp)
        print(f"[fosize] stops={stops} stores_recorded={len(rec)} tails={len(tails)}", flush=True)
        for op, fo, fo48 in tails:
            hits = [x for x in rec if fo + 0x48 <= x[2] < fo + 0x50]
            print(f"[fosize] VERDICT FO=0x{fo:08X} FO+0x48=0x{fo48:X}: {len(hits)} candidate store(s) hit [FO+0x48,FO+0x50)", flush=True)
            for pc, tid, ea, val, lr in hits:
                print(f"[fosize]   WRITER pc=0x{pc:08X} tid={tid} ea=0x{ea:08X} val=0x{val:X} lr=0x{lr:08X}", flush=True)
            if not hits:
                near = sorted({x[0] for x in rec if abs(x[2] - fo) < 0x400})
                print(f"[fosize]   none; stores within 1 KB of FO came from: {[hex(p) for p in near]}", flush=True)
    finally:
        try: rsp.close()
        except Exception: pass
        proc.kill()


if __name__ == "__main__":
    main()
