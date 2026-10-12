"""selftest.py - exercise the RPCS3 oracle client without RPCS3 installed.

Runs rsp.py + probe.walk_stack + trace.is_link_branch against
fake_rsp_server.FakeRSPServer, whose text region is the REAL GoW2 EBOOT and
whose stack is hand-built to be the back chain a stop inside func_002B2660
would actually produce:

    #0 0x002B2660   (the wall)
    #1 0x002B2DE0   return into func_002B2DD0, i.e. the insn after `bl 0x2B2660`
    #2 0x002B2E30   return into the poll loop, the insn after `bl 0x2B2DD0`

Both return addresses are preceded by a real linking branch in the EBOOT, so the
validated walk in trace.py must accept them -- that is the assertion that proves
the walker would give a truthful answer against a live RPCS3.

Usage:  python tools/rpcs3_probe/selftest.py [--eboot PATH]
Exit 0 = all pass. No RPCS3, no network beyond loopback, no game data written.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

from fake_rsp_server import FakeRSPServer          # noqa: E402
from rsp import RSPClient                          # noqa: E402
import probe                                       # noqa: E402
import trace as tracemod                           # noqa: E402

# The chain, from the static read of the EBOOT (see docs comment above).
WALL       = 0x002B2660     # func_002B2660 -- the tick that does not return
TICK       = 0x002B2DD0     # func_002B2DD0 -- calls WALL, then 0xB951C, 0x2B7188
RET_INTO_TICK = 0x002B2DE0  # insn after `bl 0x2B2660` at 0x002B2DDC
RET_INTO_POLL = 0x002B2E30  # insn after `bl 0x2B2DD0` at 0x002B2E2C
POLL_FLAG  = 0x008691A8     # *(TOC-0x1488) -> this EA; loop exits when != 0
TOC        = 0x00541178

# Synthetic stack, callee-first (stack grows down, so S0 < S1 < S2).
S0 = 0x28000F40             # func_002B2660's frame
S1 = 0x28000FB0             # func_002B2DD0's frame  (stdu r1,-0x70)
S2 = 0x28001020             # poll function's frame  (stdu r1,-0x80)
S3 = 0x280010B0             # its caller's frame (chain terminator)

FAILED = []


def check(name, got, want):
    ok = got == want
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}: got {got!r}"
          + ("" if ok else f", want {want!r}"))
    if not ok:
        FAILED.append(name)
    return ok


def check_true(name, cond, detail=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}" + (f": {detail}" if detail else ""))
    if not cond:
        FAILED.append(name)
    return cond


def load_eboot(path):
    """Regions as a real loader would map them: file bytes, then zero-fill out to
    p_memsz. The .bss tail matters here -- POLL_FLAG (0x008691A8) lives in
    segment 1's .bss (filesz ends 0x576EA4, memsz ends 0x9B9808), so without the
    zero-fill the fixture cannot serve the very word the wall polls."""
    import elf_parser as E
    elf = E.ELFFile(path)
    elf.load()
    regions = []
    for i, ph in enumerate(elf.program_headers):
        if ph.p_memsz and ph.p_vaddr:
            data = elf.get_segment_data(i) if ph.p_filesz else b""
            if ph.p_memsz > len(data):
                data = data + b"\x00" * (ph.p_memsz - len(data))
            regions.append((ph.p_vaddr, data))
    return regions


def build_stack():
    """ELFv1: a callee stores its return address at CALLER_sp+16, and *(sp) is
    the back chain to the caller's frame. Values are 64-bit BE; EAs are the low
    word."""
    mem = bytearray(0x200)
    base = S0

    def put64(ea, val):
        off = ea - base
        mem[off:off + 8] = int(val).to_bytes(8, "big")

    put64(S0, S1)                    # back chain: WALL frame -> TICK frame
    put64(S1 + 16, RET_INTO_TICK)    # WALL saved its LR in TICK's frame
    put64(S1, S2)                    # TICK frame -> poll frame
    put64(S2 + 16, RET_INTO_POLL)    # TICK saved its LR in the poll frame
    put64(S2, S3)                    # poll frame -> its caller
    put64(S3 + 16, 0)                # terminator: no return address
    put64(S3, 0)                     # terminator: no back chain
    return (base, bytes(mem))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--eboot",
                    default=os.path.join(os.path.dirname(ROOT), "gow2-recomp", "EBOOT.ELF"))
    a = ap.parse_args()

    print(f"[selftest] EBOOT: {a.eboot}")
    if not os.path.exists(a.eboot):
        print("[selftest] SKIP-ALL: EBOOT not present; cannot build a truthful fixture.")
        return 77
    regions = load_eboot(a.eboot) + [build_stack()]

    regs = {"pc": WALL, "r1": S0, "lr": RET_INTO_TICK, "r2": TOC,
            "r3": 0x1234, "ctr": TICK, "msr": 0x8000000000001000}
    srv = FakeRSPServer(regions, regs)
    srv.start()
    print(f"[selftest] fake RSP stub on 127.0.0.1:{srv.port}")

    c = RSPClient(port=srv.port, timeout=5.0)
    c.connect(retries=20, delay=0.1)
    feats = c.handshake()

    print("\n-- protocol / layout --")
    check_true("qSupported negotiated", "qXfer:features:read+" in feats, feats[:60])
    check_true("no-ack mode engaged", c.no_ack)
    check("register layout discovered from target.xml", len(c.reg_layout), 71)
    check_true("layout came from XML, not the fallback",
               not hasattr(c, "_reg_err"), getattr(c, "_reg_err", ""))

    print("\n-- register decode (big-endian) --")
    r = c.read_registers()
    check("pc", r.get("pc"), WALL)
    check("r1 (sp)", r.get("r1"), S0)
    check("lr", r.get("lr"), RET_INTO_TICK)
    check("r2 (TOC)", r.get("r2"), TOC)
    check("msr full 64-bit width", r.get("msr"), 0x8000000000001000)

    print("\n-- memory reads --")
    check("read_u32 at the wall entry decodes BE",
          c.read_u32(WALL) >> 26 in (14, 15, 31, 32, 36, 37, 38, 47, 58, 62, 18, 16),
          True)
    check("bl at 0x002B2DDC is the exact EBOOT word", c.read_u32(0x002B2DDC), 0x4BFFF885)
    long_read = c.read_mem(WALL, 1300)     # > the client's 512-byte chunk size
    check("chunked read length", len(long_read), 1300)
    check_true("chunked read is contiguous",
               long_read[:4] == c.read_mem(WALL, 4)
               and long_read[1024:1028] == c.read_mem(WALL + 1024, 4))
    try:
        c.read_mem(0x7F000000, 4)
        check_true("unmapped read raises", False, "no exception")
    except Exception as e:
        check_true("unmapped read raises RSPError", type(e).__name__ == "RSPError", str(e))

    print("\n-- stack walk (probe.walk_stack) --")
    frames = probe.walk_stack(c, r)
    pcs = [f["pc"] for f in frames]
    print(f"    frames: {[hex(p) for p in pcs]}")
    check("frame #0 is the wall", pcs[0], WALL)
    check("frame #1 is the return into the tick", pcs[1], RET_INTO_TICK)
    check("frame #2 is the return into the poll loop", pcs[2], RET_INTO_POLL)
    check_true("walk terminates (no runaway)", len(pcs) <= 4, f"{len(pcs)} frames")
    check_true("no duplicate lr-reg frame injected",
               [f["via"] for f in frames][:3] == ["pc", "lr-save", "lr-save"],
               str([f["via"] for f in frames]))

    print("\n-- validated walk (trace.is_link_branch against real EBOOT bytes) --")
    for ret, site in ((RET_INTO_TICK, 0x002B2DDC), (RET_INTO_POLL, 0x002B2E2C)):
        w = c.read_u32(ret - 4)
        check_true(f"0x{ret:08X} preceded by a linking branch at 0x{site:08X}",
                   tracemod.is_link_branch(w), f"word=0x{w:08X}")
    # negative control: a non-branch word must be rejected
    non_branch = c.read_u32(0x002B2DD0)      # stdu r1,-0x70(r1)
    check_true("negative control: stdu is NOT a linking branch",
               not tracemod.is_link_branch(non_branch), f"word=0x{non_branch:08X}")
    tframes = tracemod.walk(c, r)
    check_true("trace.walk validates both frames as 'ok'",
               [t[1] for t in tframes[1:3]] == ["ok", "ok"], str(tframes))

    print("\n-- breakpoints / execution control --")
    check_true("Z0 accepted", c.add_breakpoint(WALL))
    check_true("stub recorded the breakpoint", WALL in srv.breakpoints)
    c.cont()
    stop = c.wait_stop(deadline=time.time() + 3)
    check_true("stop reply after continue", stop[:1] == b"T", stop.decode("latin-1"))
    pc_before = c.read_registers()["pc"]
    c.step()
    c.wait_stop(deadline=time.time() + 3)
    check("single-step advanced pc by 4", c.read_registers()["pc"], pc_before + 4)
    check_true("z0 accepted", c.remove_breakpoint(WALL))
    check_true("stub dropped the breakpoint", WALL not in srv.breakpoints)

    print("\n-- fallback register layout (the path REAL RPCS3 uses) --")
    # Measured 2026-08-12 against RPCS3 v0.0.42-19734: the stub advertises only
    # "PacketSize=1200" -- no qXfer:features:read, no QStartNoAckMode -- so the
    # XML path above is NOT what a live run exercises. Assert the fallback that
    # actually gets used is well-formed.
    fb = RSPClient(port=srv.port)
    fb._fallback_registers()
    check("fallback layout size", len(fb.reg_layout), 71)
    check_true("fallback has pc and r1",
               "pc" in fb.reg_by_name and "r1" in fb.reg_by_name)
    check("fallback r0 at offset 0", fb.reg_by_name["r0"], (0, 8))
    check("fallback pc after 32 GPR + 32 FPR", fb.reg_by_name["pc"], (64 * 8, 8))
    check_true("fallback offsets are contiguous and non-overlapping",
               all(fb.reg_layout[i][1] + fb.reg_layout[i][2] == fb.reg_layout[i + 1][1]
                   for i in range(len(fb.reg_layout) - 1)))

    print("\n-- RPCS3-shaped stub (the real path: 556-byte g, no qXfer) --")
    # A second stub in rpcs3_mode, so the fallback layout, the 4-byte cr/xer/fpscr
    # sizes and the 'x' unavailable tail are all exercised the way a live capture
    # exercises them. Regression guard for the 568-vs-556 offset bug.
    srv2 = FakeRSPServer(regions, regs, rpcs3_mode=True)
    srv2.start()
    c2 = RSPClient(port=srv2.port, timeout=5.0)
    c2.connect(retries=20, delay=0.1)
    f2 = c2.handshake()
    check("qSupported is RPCS3's", f2, "PacketSize=1200")
    check_true("no-ack NOT engaged (RPCS3 lacks it)", not c2.no_ack)
    check_true("fell back to the built-in layout", hasattr(c2, "_reg_err"))
    check("fallback block size matches the measured 556",
          sum(sz for _, _, sz in c2.reg_layout), 556)
    r2 = c2.read_registers()
    check("pc decodes correctly at offset 512", r2.get("pc"), WALL)
    check("r1 decodes correctly", r2.get("r1"), S0)
    check("r2 (TOC) decodes correctly", r2.get("r2"), TOC)
    check_true("lr is OMITTED, not guessed (bytes 520+ unavailable)", "lr" not in r2)
    check_true("ctr is OMITTED, not guessed", "ctr" not in r2)
    check_true("msr/cr/xer/fpscr all omitted",
               not any(k in r2 for k in ("msr", "cr", "xer", "fpscr")),
               str([k for k in ("msr", "cr", "xer", "fpscr") if k in r2]))
    # The walk must still work with pc + r1 + memory alone, since lr is gone.
    fr2 = probe.walk_stack(c2, r2)
    check("walk still finds the wall chain without lr",
          [f["pc"] for f in fr2][:3], [WALL, RET_INTO_TICK, RET_INTO_POLL])
    c2.close()
    srv2.shutdown()

    print("\n-- config.yml shapes (rpcs3_host.gdb_stub_status) --")
    import tempfile
    import rpcs3_host
    shapes = [
        ("scalar/current", "Miscellaneous:\n  GDB Server: 127.0.0.1:2345\n", "on", 2345),
        ("scalar/other port", "Miscellaneous:\n  GDB Server: 127.0.0.1:9999\n", "on", 9999),
        ("scalar/empty = off", "Miscellaneous:\n  GDB Server: \n", "off", 2345),
        ("block/enabled", "GDB Server:\n  Enabled: true\n  Port: 2345\n", "on", 2345),
        ("block/disabled", "GDB Server:\n  Enabled: false\n  Port: 2345\n", "off", 2345),
        ("absent", "Miscellaneous:\n  Silence All Logs: false\n", "unknown", 2345),
    ]
    for label, body, want_state, want_port in shapes:
        with tempfile.NamedTemporaryFile("w", suffix=".yml", delete=False) as fh:
            fh.write(body)
            tmp = fh.name
        try:
            st, detail, _ = rpcs3_host.gdb_stub_status(tmp)
            check(f"config shape {label}: state", st, want_state)
            if want_state == "on":
                check(f"config shape {label}: port", rpcs3_host.stub_port(tmp), want_port)
        finally:
            os.unlink(tmp)

    print("\n-- recipe runner (probe.run_recipe) --")
    caps = probe.run_recipe(c, [
        {"op": "mem", "addr": f"0x{POLL_FLAG:08X}", "len": 4, "label": "poll-flag"},
    ], out=None)
    check("recipe produced one capture", len(caps), 1)
    check_true("poll-flag capture is labelled", caps[0]["label"] == "poll-flag")
    # The wall's exit predicate: the loop at 0x002B2E2C spins while this word is
    # zero. In a fresh image it IS zero -- that is the signature, not a bug.
    check("poll flag reads zero in a fresh image (.bss)",
          caps[0]["u32_be"], ["0x00000000"])

    c.close()
    srv.shutdown()

    print("\n" + "=" * 62)
    if FAILED:
        print(f"FAIL: {len(FAILED)} check(s) failed: {FAILED}")
        return 1
    print("PASS: every check green -- the oracle client is exercised end to end.")
    print(f"      wall chain proven walkable: 0x{WALL:08X} <- 0x{RET_INTO_TICK:08X}"
          f" <- 0x{RET_INTO_POLL:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
