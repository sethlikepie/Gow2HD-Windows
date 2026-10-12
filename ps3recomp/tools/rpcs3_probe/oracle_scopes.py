"""oracle_scopes.py - GoW2 wall [F] oracle: what do push-all / pop-all see on RPCS3?

Question (E88, 2026-09-01): on our runtime the first push-all of a freshly created
WAD root iterates an EMPTY children list, while its pop-all iterates 12 children,
leaving 10 managers at cursor -1 before B71 #042 needs them >= 0. Same code on the
console => same -1, UNLESS the children already exist at the first push there.
Only the oracle can say. This rig boots the intro-skipping ELF (make_skipintro_elf.py)
in RPCS3 with the PPU *interpreter* (breakpoints do not fire under LLVM), breaks on

    0x0041F700  push-all class 1   (r4-4 = object)        [E88: 'push1']
    0x0041F924  pop-all  class 1   (r3 = manager)         ['pop1']
    0x0042AF18  push-all class 22  (r4-4 = object)        ['push22']
    0x0042A0C8  pop-all  class 22  (r3 = manager)         ['pop22']
    0x000B70B0  B71 #054 entry -> #042..#052 already passed on the oracle

and at each stop dumps the object's children subtags (list head at obj+0x80 /
obj+0x38: node -> next @+0, child @+8; child w0 @+4, subtag=(w0>>16)&0xFFF) plus
the cursor (int8 @mgr+0xC8) of every TABLE[idx] manager (TABLE = 0x00868D48+idx*4).

    python tools/rpcs3_probe/oracle_scopes.py --title <EBOOT_skipintro.ELF> \
        --out oracle.json [--boot-wait 20] [--deadline 900] [--max-events 600]

One attach per RPCS3 launch (E81 §4). Kill RPCS3 by PID at the end (done here).
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

TABLE = 0x00868D48
NTABLE = 40
BPS = {
    0x000B71B8: ("b71_entry", None, None),   # func_000B71B8 entry: base state BEFORE #038
    0x00041D5C: ("b71_038", None, None),     # func_00041D5C entry (#038 WAD loads)
    0x0041F700: ("push1", "obj_r4", 0x80),
    0x0041F924: ("pop1", "mgr_r3", 0x80),
    0x0042AF18: ("push22", "obj_r4", 0x38),
    0x0042A0C8: ("pop22", "mgr_r3", 0x38),
    0x0041FA30: ("root_new", "regs", None),   # func_0041FA30: creates a class-1 root (ours: B71 probe)
    0x00256B64: ("reg_push", "regs", None),   # func_00256B64: registration push (ours: SCOPE ra1, lr=0x256CE0)
    0x0042CD30: ("push_b1", "regs", None),    # func_0042CD30: set-B level-1 push (ours: SCOPE)
    0x002B6288: ("push_b0", "regs", None),    # func_002B6288: set-B level-0 push (ours: SCOPE)
    0x0039F0F8: ("push_x", "regs", None),     # func_0039F0F8: 2 pushes (ours: SCOPE)
    0x000B70B0: ("b71_054", None, None),     # reaching it => #042..#052 passed on the oracle
}
TOPS = (1, 3, 4, 22)


def tops(rsp):
    raw = rsp.read_mem(TABLE, NTABLE * 4)
    out = {}
    for i in TOPS:
        m = int.from_bytes(raw[i * 4:i * 4 + 4], "big")
        if not guest_ok(m):
            out[i] = None
            continue
        cur = s8(rsp.read_mem(m + 0xC8, 1)[0])
        out[i] = (cur, f"0x{rsp.read_u32(m + 0x48):08X}" if cur >= 0 else "-")
    return out
STOP_AT = "b71_054"
CHILDREN_READS_PER_OBJ = 4
STACK_EVENTS = 90   # record the caller chain of the first N push/pop events
INTRO_COUNTER = 0x00592668
MAIN_THREAD = "0000000001000000"   # qfThreadInfo id of GoW2's main_thread (measured, run 5)
WATCH = (1, 3, 4, 9, 22)


def select_main(rsp):
    rsp._send_packet(f"Hg{MAIN_THREAD}")
    rsp._recv_packet()


WADCTX = 0x00868C00   # WAD-context stack: +4 base, +8 index (E87)


def cursors(rsp):
    raw = rsp.read_mem(TABLE, NTABLE * 4)
    out = {}
    for i in WATCH:
        m = int.from_bytes(raw[i * 4:i * 4 + 4], "big")
        out[i] = s8(rsp.read_mem(m + 0xC8, 1)[0]) if guest_ok(m) else None
    out["wadctx"] = rsp.read_u32(WADCTX + 8)
    # WADLD loader state (FUN_002ba76c): type_sys = *PTR_DAT_0053ff4c; ring @+0x1a8 (avail @ring+0x10),
    # remaining-of-request @+0x1ac, request size @+0x74, state @+0x1cc, pending container @+0x1bc
    try:
        ts = rsp.read_u32(0x0053FF4C)
        ring = rsp.read_u32(ts + 0x1A8)
        out["ld"] = (f"avail={rsp.read_u32(ring + 0x10)}" if guest_ok(ring) else "avail=?") + \
                    f" rem={rsp.read_u32(ts + 0x1AC)} req={rsp.read_u32(ts + 0x74)} st={rsp.read_u32(ts + 0x1CC)} pend=0x{rsp.read_u32(ts + 0x1BC):08X}"
    except Exception as e:
        out["ld"] = f"err:{e}"
    return out


def s8(v):
    v &= 0xFF
    return v - 256 if v & 0x80 else v


def guest_ok(p):
    return 0x10000 <= p < 0x4F000000


def children(rsp, obj, listoff):
    if not guest_ok(obj):
        return None
    head = obj + listoff
    p = rsp.read_u32(head)
    out = []
    n = 0
    while p and p != head and n < 40 and guest_ok(p):
        c = rsp.read_u32(p + 8)
        w = rsp.read_u32(c + 4) if guest_ok(c) else 0
        out.append((w >> 16) & 0xFFF)
        p = rsp.read_u32(p)
        n += 1
    return out


def managers(rsp):
    raw = rsp.read_mem(TABLE, NTABLE * 4)
    res = {}
    for i in range(NTABLE):
        m = int.from_bytes(raw[i * 4:i * 4 + 4], "big")
        if not guest_ok(m):
            continue
        cur = s8(rsp.read_mem(m + 0xC8, 1)[0])
        st = []
        if cur >= 0:
            st = [rsp.read_u32(m + 0x48 + k * 4) for k in range(min(cur + 1, 4))]
        res[i] = {"mgr": f"0x{m:08X}", "cursor": cur, "stack": [f"0x{x:08X}" for x in st]}
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rpcs3", default=None)
    ap.add_argument("--title", required=True)
    ap.add_argument("--out", default="oracle_scopes.json")
    ap.add_argument("--port", type=int, default=None)
    ap.add_argument("--boot-wait", type=float, default=20.0)
    ap.add_argument("--deadline", type=float, default=900.0)
    ap.add_argument("--max-events", type=int, default=600)
    ap.add_argument("--no-headless", action="store_true")
    ap.add_argument("--stop-tag", default=STOP_AT)
    ap.add_argument("--extra-bp", action="append", default=[], help="addr:tag (regs dump)")
    a = ap.parse_args()
    for x in a.extra_bp:
        ea, tag = x.split(":"); BPS[int(ea, 0)] = (tag, "regs", None)

    if a.port is None:
        a.port = rpcs3_host.stub_port()
    print(f"[oracle] launching RPCS3: {a.title}", flush=True)
    proc = probe.launch_rpcs3(a.rpcs3, a.title, headless=True)  # sempre com janela (pedido 2026-09-02))
    rsp = RSPClient(port=a.port, timeout=30.0)
    result = {"title": a.title, "events": [], "final": None}
    t0 = time.time()
    try:
        time.sleep(a.boot_wait)
        rsp.connect()
        rsp.handshake()
        try:
            select_main(rsp)
            regs = rsp.read_registers()
            pc = probe._pc(regs)
        except Exception as e:
            pc = None
            print(f"[oracle] attach regs unavailable: {e}", flush=True)
        result["attach"] = {"pc": f"0x{pc:08X}" if pc else None,
                            "intro_counter": rsp.read_u32(INTRO_COUNTER)}
        print(f"[oracle] attached pc={result['attach']['pc']} intro_counter={result['attach']['intro_counter']}", flush=True)
        for ea in BPS:
            ok = rsp.add_breakpoint(ea)
            print(f"[oracle] Z0 0x{ea:08X} -> {ok}", flush=True)
        stop_deadline = time.time() + a.deadline
        n = 0
        seen = {}
        while n < a.max_events and time.time() < stop_deadline:
            rsp.cont()
            stop = rsp.wait_stop(deadline=stop_deadline)
            if not stop or stop[:1] not in (b"S", b"T"):
                print(f"[oracle] no stop reply ({stop!r}); ending", flush=True)
                break
            select_main(rsp)
            regs = rsp.read_registers()
            pc = probe._pc(regs)
            n += 1
            tag, src, listoff = BPS.get(pc, (f"pc_0x{pc:08X}", None, None))
            ev = {"n": n, "t": round(time.time() - t0, 1), "pc": f"0x{pc:08X}", "tag": tag}
            if src == "obj_r4":
                r4 = regs.get("r4", 0) & 0xFFFFFFFF
                obj = r4 - 4 if r4 else 0
                mgr = regs.get("r3", 0) & 0xFFFFFFFF
            elif src == "mgr_r3":
                mgr = regs.get("r3", 0) & 0xFFFFFFFF
                cur = s8(rsp.read_mem(mgr + 0xC8, 1)[0]) if guest_ok(mgr) else -1
                obj = rsp.read_u32(mgr + 0x48 + cur * 4) if cur >= 0 else 0
            elif src == "regs":
                ev["r3"] = f"0x{regs.get('r3',0)&0xFFFFFFFF:08X}"
                ev["r4"] = f"0x{regs.get('r4',0)&0xFFFFFFFF:08X}"
                ev["r5"] = f"0x{regs.get('r5',0)&0xFFFFFFFF:08X}"
                ev["cursors"] = cursors(rsp)
                ev["tops"] = tops(rsp)
                cs = " ".join(f"c{i}={v}" for i, v in ev["cursors"].items())
                tp = " ".join(f"[{i}]={v}" for i, v in ev["tops"].items())
                print(f"[oracle] {n:4d} t={ev['t']:6.1f} {tag:8s} r3={ev['r3']} r4={ev['r4']} r5={ev['r5']} | {cs} | {tp}", flush=True)
                mgr = obj = 0
                src = None
                result["events"].append(ev)
                continue
            else:
                mgr = obj = 0
            if src:
                ev["mgr"] = f"0x{mgr:08X}"
                ev["cursor"] = s8(rsp.read_mem(mgr + 0xC8, 1)[0]) if guest_ok(mgr) else None
                ev["obj"] = f"0x{obj:08X}"
                ev["w0"] = f"0x{rsp.read_u32(obj + 4):08X}" if guest_ok(obj) else None
                seen[obj] = seen.get(obj, 0) + 1
                ev["children"] = children(rsp, obj, listoff) if seen[obj] <= CHILDREN_READS_PER_OBJ else "(nao lido)"
                ev["cursors"] = cursors(rsp)
                if n <= STACK_EVENTS:
                    ev["stack"] = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 7) if f["pc"]][1:7]
                cs = " ".join(f"c{i}={v}" for i, v in ev["cursors"].items())
                print(f"[oracle] {n:4d} t={ev['t']:6.1f} {tag:7s} mgr={ev['mgr']} cursor={ev['cursor']} obj={ev['obj']} w0={ev['w0']} filhos={ev['children']} | {cs} | ra={ev.get('stack','')}", flush=True)
            else:
                ev["managers"] = managers(rsp)
                ev["stack"] = [f"0x{f['pc']:08X}" for f in probe.walk_stack(rsp, regs, 16) if f["pc"]]
                print(f"[oracle] {n:4d} {tag} stack={ev['stack'][:8]}", flush=True)
                for i, m in sorted(ev["managers"].items()):
                    print(f"          TABLE[{i:2d}] {m['mgr']} cursor={m['cursor']:3d} {m['stack']}", flush=True)
            result["events"].append(ev)
            if tag == a.stop_tag:
                break
        result["final"] = {"events": n, "managers": managers(rsp),
                           "intro_counter": rsp.read_u32(INTRO_COUNTER)}
    except Exception as e:
        result["error"] = f"{type(e).__name__}: {e}"
        print(f"[oracle] ERROR: {result['error']}", flush=True)
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
    print(f"[oracle] wrote {a.out}", flush=True)
    return 0 if "error" not in result else 1


if __name__ == "__main__":
    sys.exit(main())
