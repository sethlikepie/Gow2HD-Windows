#!/usr/bin/env python3
"""gen_scream_native.py -- lift SCREAM's two mixer jobs (SPU ELF #2 and #3 of the EBOOT) for the
hybrid interpreter/native mode of engine_fixes_v29_scream_native.py (PS3_SCREAM_NATIVE=1).

The SCREAM policy module (image 7) stays in the SPU interpreter. The jobs it DMAs to LS 0x4000
(job A = ELF #2, EBOOT 0x4EF400; job B = ELF #3, EBOOT 0x500980) are lifted here with the kit's
spu_lifter.py, but every instruction whose interpreter semantics differ from the lifted runtime
is emitted as a call into the hybrid glue (winport/scream_native_rt.c), which can hand the
architectural state back to the interpreter at an exact PC (scr_bail):
  * channel ops      -> the interpreter's own channel code (time-based decrementer, blocking
                        RdTagStat/RdInMbox: a read that would stall bails AT the rdch)
  * wrch MFC_Cmd ... -> bails right after the write if it raised an event while interrupts are on
  * bi/bisl/biz...   -> scr_ib: return to the C caller / dispatch inside the job / bail (PM code,
                        kernel, unknown); E/D flags update ctx->interp_ie like the interpreter
  * bi $r0 return    -> scr_ret0: checks the C frame matches $r0, else bails
  * stop, iret, bisled, `br .`, TODO ops -> bail AT the instruction (the interpreter runs it)
Re-entry points ("labels"): every observed branch target (scream_tgt.bin), every call return
site, every bail-capable channel site and every constant bi target get a resumable label: the
function begins with a switch on g_scr_resume that jumps to it.

Usage: gen_scream_native.py <EBOOT.ELF> <kit ps3recomp dir> <out dir> [scream_tgt.bin]
Output is GAME-DERIVED (lifted game code): keep it on this PC, never publish it."""
import os, struct, subprocess, sys, pathlib, re

EBOOT, PS3, OUT = (pathlib.Path(a) for a in sys.argv[1:4])
TGT = pathlib.Path(sys.argv[4]) if len(sys.argv) > 4 else None
TOOLS = PS3 / "tools"
sys.path.insert(0, str(TOOLS))
import spu_lifter as L                                   # noqa: E402
from spu_disasm import disassemble_spu                   # noqa: E402
from find_spu_functions import detect_functions          # noqa: E402

JOBS = [("A", 0xABCD0BA4D18DED49, "0x4080,0x92F8,0x91C0"), ("B", 0xED6A0C318DEB46C6, "0x4080")]
FAST_WR = {16, 17, 18, 19, 20}        # MFC_LSA..MFC_TagID: plain stores in both worlds


def fnv(b):
    h = 1469598103934665603
    for x in b:
        h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def whash(b):  # scream_native_rt.c scr_hash(): FNV over little-endian u64 words
    h = 0xCBF29CE484222325
    for (w,) in struct.iter_unpack("<Q", b):
        h = ((h ^ w) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def text_range(elf):
    shoff = struct.unpack_from(">I", elf, 0x20)[0]; shn = struct.unpack_from(">H", elf, 0x30)[0]
    shstr = struct.unpack_from(">H", elf, 0x32)[0]
    sh = [struct.unpack_from(">10I", elf, shoff + 40 * i) for i in range(shn)]
    so = sh[shstr][4]
    for s in sh:
        if elf[so + s[0]:elf.index(b"\0", so + s[0])] == b".text":
            return s[3], s[3] + s[5]
    raise SystemExit("no .text")


class HybridLifter(L.SPULifter):
    def __init__(self, prefix, labels):
        super().__init__(trace=False, prefix=prefix)
        self.labels = labels          # resumable entry addresses
        self.entries = []             # (addr, fn name, is_label)

    def lift_function(self, insns, start, end):
        func = L.LiftedFunction(name=f"{self.prefix}spu_func_{start:08X}", start_addr=start, end_addr=end)
        internal = set()
        body = [i for i in insns if start <= i.addr < end]
        for insn in body:
            tgt = self._branch_target(insn)
            if tgt is not None and start <= tgt < end:
                internal.add(tgt)
        resume = sorted(a for a in self.labels if start < a < end)
        internal |= set(resume)
        # ila constants that are code addresses of THIS function: `bi rX` through such a register
        # is checked against them first and becomes a direct goto (job A's mixing loop 0x5300-0x5584
        # closes with `bi $r25`, r25 = 0x54A0 loaded long before).
        self._ila = {}
        addrs = {i.addr for i in body}
        for insn in body:
            if insn.mnemonic == "ila":
                o = L._ops(insn.operands)
                t = int(L._imm(o[1]), 0) & 0x3FFFC
                if start < t < end and t in addrs:
                    self._ila.setdefault("*", set()).add(t)   # any register: loop exits are often selb'd
                    internal.add(t)
        if resume:
            func.body_lines.append("if (__builtin_expect(g_scr_resume != 0, 0)) {")
            func.body_lines.append("    const uint32_t _r = g_scr_resume; g_scr_resume = 0;")
            func.body_lines.append("    switch (_r) {")
            for a in resume:
                func.body_lines.append(f"    case 0x{a:X}u: goto loc_{a:08X};")
            func.body_lines.append(f"    default: scr_bail(ctx, _r);")
            func.body_lines.append("    } }")
        last = None
        for insn in body:
            if insn.addr in internal:
                func.body_lines.append(f"loc_{insn.addr:08X}:")
            c = self._translate(insn, func)
            if c:
                func.body_lines.append(f"    {c}")
            last = insn
        term = {"br", "bra", "bi", "iret", "stop", "stopd", "bisled"}
        if last is not None and last.mnemonic not in term:
            if end in self.func_starts:
                func.body_lines.append(
                    f"    {{ ctx->pc = 0x{end:X}; SPU_TAILCALL({self.prefix}spu_func_{end:08X}(ctx)); }}")
            else:
                func.body_lines.append(f"    scr_bail(ctx, 0x{end:X}u);   /* falls off the lifted range */")
        if body and all(i.mnemonic in ("nop", "lnop", "br", "bra") for i in body):
            lt = self._branch_target(body[-1])
            if body[-1].mnemonic in ("br", "bra") and lt is not None and start <= lt < end:
                func.body_lines = [f"    scr_bail(ctx, 0x{start:X}u);   /* no-op self loop */"]
        self.functions.append(func)
        self.entries.append((start, func.name, 0))
        for a in resume:
            self.entries.append((a, func.name, 1))
        return func

    def _translate(self, insn, func):
        mn = insn.mnemonic; ops = L._ops(insn.operands); addr = insn.addr; raw = insn.raw
        g = lambda i: f"ctx->gpr[{i}]"
        ch = (raw >> 7) & 0x7F
        if mn == "rdch":
            return f"{g(raw & 0x7F)} = scr_rdch(ctx, {ch}u, 0x{addr:X}u);"
        if mn == "rchcnt":
            return f"{g(raw & 0x7F)} = spu_pref_u32(scr_rchcnt(ctx, {ch}u));"
        if mn == "wrch":
            if ch in FAST_WR:
                return f"spu_wrch(ctx, {ch}u, {g(raw & 0x7F)});"
            return f"scr_wrch(ctx, {ch}u, {g(raw & 0x7F)}, 0x{addr + 4:X}u);"
        if mn in ("stop", "stopd", "iret", "bisled"):
            return f"scr_bail(ctx, 0x{addr:X}u);   /* {mn}: the interpreter runs it */"
        if mn in ("br", "bra", "brsl", "brasl") and self._branch_target(insn) == addr:
            return f"scr_bail(ctx, 0x{addr:X}u);   /* {mn} . */"
        if mn in ("bi", "biz", "binz", "bihz", "bihnz", "bisl"):
            e = bool(raw & 0x00040000); d = bool(raw & 0x00080000)
            ie = "ctx->interp_ie = 1; " if (e and not d) else ("ctx->interp_ie = 0; " if (d and not e) else "")
            if mn == "bisl":
                link_rt = raw & 0x7F; tgt = L._reg(ops[-1])
                return (f"{{ const uint32_t _t = {g(tgt)}._u32[0]; {ie}{g(link_rt)} = spu_link(0x{addr + 4:X}); "
                        f"ctx->interp_link = 0x{addr + 4:X}u; spu_call_enter(ctx, 0x{addr + 4:X}u); "
                        f"ctx->pc = _t; scr_ib(ctx); SPU_DRAIN(ctx); spu_call_leave(ctx); }}")
            if mn == "bi":
                tgt = L._reg(ops[0]); cond = None
            else:
                tgt = L._reg(ops[1]); cond = self._cond(mn[1:], L._reg(ops[0]))
            if tgt == "0" and addr not in self.bi_r0_jump and not (e or d):
                act = "{ scr_ret0(ctx); return; }"
            else:
                fast = ""
                for t in sorted(getattr(self, "_ila", {}).get("*", ())):
                    fast += (f"if (_t == 0x{t:X}u) {{ if (ctx->interp_ie && (ctx->event_status & ctx->event_mask)) "
                             f"scr_bail(ctx, _t); goto loc_{t:08X}; }} ")
                if fast:
                    act = (f"{{ {ie}const uint32_t _t = {g(tgt)}._u32[0] & 0x3FFFCu; {fast}"
                           f"ctx->pc = {g(tgt)}._u32[0]; scr_ib(ctx); return; }}")
                else:
                    act = f"{{ {ie}ctx->pc = {g(tgt)}._u32[0]; scr_ib(ctx); return; }}"
            return act if cond is None else f"if ({cond}) {act}"
        c = super()._translate(insn, func)
        if c.startswith("/* TODO"):
            return f"scr_bail(ctx, 0x{addr:X}u);   {c[:-1]}"
        return c


def labels_for(insns, lo, hi, observed):
    lab = set(a for a in observed if lo <= a < hi)
    by = {i.addr: i for i in insns}
    for i in insns:
        mn = i.mnemonic
        if not (lo <= i.addr < hi):
            continue
        if mn in ("brsl", "brasl", "bisl"):
            lab.add(i.addr + 4)
        elif mn == "rdch":
            lab.add(i.addr)
        elif mn == "ila":
            t = int(L._imm(L._ops(i.operands)[1]), 0) & 0x3FFFC
            if lo <= t < hi:
                lab.add(t)
        elif mn == "wrch" and ((i.raw >> 7) & 0x7F) not in FAST_WR:
            lab.add(i.addr + 4)
        elif mn in ("bi", "biz", "binz", "bihz", "bihnz"):
            r = L._reg(L._ops(i.operands)[0 if mn == "bi" else 1])
            for k in range(1, 9):              # ila rX, const shortly before: a constant target
                p = by.get(i.addr - 4 * k)
                if p is None:
                    break
                if p.mnemonic == "ila" and L._reg(L._ops(p.operands)[0]) == r:
                    t = int(L._imm(L._ops(p.operands)[1]), 0) & 0x3FFFC
                    if lo <= t < hi:
                        lab.add(t)
                    break
                if (p.raw & 0x7F) == int(r) and p.mnemonic not in L._NO_RT_WRITE:
                    break
    return {a for a in lab if a in by and a % 4 == 0}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    tmp = OUT / "elf"; tmp.mkdir(exist_ok=True)
    subprocess.run([sys.executable, str(TOOLS / "extract_spu_images.py"), str(EBOOT), "-o", str(tmp)],
                   check=True, capture_output=True)
    by_fp = {fnv(p.read_bytes()): p.read_bytes() for p in tmp.glob("*.elf")}
    observed = set()
    if TGT and TGT.exists():
        b = TGT.read_bytes()
        observed = {w * 4 for w in range(len(b) * 8) if (b[w >> 3] >> (w & 7)) & 1}
    jobs_c = ['/* Generated by gen_scream_native.py -- GAME-DERIVED, do not publish. */',
              '#include "scr_native.h"', '']
    for name, fp, extra in JOBS:
        elf = by_fp.get(fp)
        if elf is None:
            raise SystemExit(f"EBOOT has no SPU ELF fp {fp:016X} (job {name})")
        lo, hi = text_range(elf)
        funcs, (toff, base, size) = detect_functions(elf)
        data = elf[toff:toff + size]
        code = data[lo - base:hi - base]
        bounds = sorted({(s, min(e, hi)) for s, e in funcs if lo <= s < hi})
        for a in sorted(int(x, 0) for x in extra.split(",")):
            nb = []
            for s, e in bounds:
                nb += [(s, a), (a, e)] if s < a < e else [(s, e)]
            if not any(s == a for s, _ in nb):
                nb.append((a, hi))
            bounds = sorted(set(nb))
        insns = disassemble_spu(data, base)
        lab = labels_for(insns, lo, hi, observed)
        lf = HybridLifter(f"scr{name}_", lab)
        lf.bi_r0_jump = L.compute_bi_r0_jumps(insns, bounds)
        lf.func_starts = {s for s, _ in bounds}
        for s, e in bounds:
            lf.lift_function(insns, s, e)
        hdr = lf.emit_header().replace('#include "../../runtime/spu/spu_context.h"', '#include "scr_native.h"')
        src = lf.emit_source()
        src = src.replace('#include "spu_recomp.h"', f'#include "scr{name}.h"')
        src = src.replace('#include "../../runtime/spu/spu_helpers.h"', '#include "spu_helpers.h"')
        src = src.replace("spu_indirect_branch(ctx)", "scr_ib(ctx)")
        src = src.replace("spu_shufb(", "scr_shufb(")   # SSE version (scr_native.h)
        # the kit's register function/table is not used (the hybrid has its own table)
        src = src[:src.index("/* Function table */")]
        for bad in ("spu_irq_window", "spu_stop(", "spu_halt(ctx)", "spu_indirect_branch"):
            if bad in src:
                raise SystemExit(f"job {name}: generated code still calls {bad}")
        if re.search(r"spu_il\(-(3276[9]|327[7-9]\d|32[8-9]\d\d|3[3-9]\d{3}|[4-9]\d{4}|\d{6,})\)", src):
            raise SystemExit(f"job {name}: out-of-range spu_il immediate (E427 bug class)")
        (OUT / f"scr{name}.h").write_text(hdr, newline="\n")
        (OUT / f"scr{name}.c").write_text("/* GAME-DERIVED (lifted SCREAM job), do not publish. */\n" + src,
                                          newline="\n")
        ents = sorted(lf.entries)
        jobs_c.append(f'#include "scr{name}.h"')
        jobs_c.append(f"static const scr_ent_t s_ents{name}[] = {{")
        jobs_c += [f"    {{ 0x{a:05X}u, {fn}, {lb} }}," for a, fn, lb in ents]
        jobs_c.append("};")
        jobs_c.append("")
        print(f"job {name}: .text 0x{lo:05X}-0x{hi:05X} funcs={len(bounds)} labels={len(lab)} "
              f"entries={len(ents)} hash=0x{whash(code):016X}")
        JOBS_META.append((name, lo, hi, whash(code), len(ents)))
    jobs_c.append(f"const scr_job_t g_scr_jobs[] = {{")
    for name, lo, hi, h, n in JOBS_META:
        jobs_c.append(f'    {{ "{name}", 0x{lo:05X}u, 0x{hi:05X}u, 0x{h:016X}ull, s_ents{name}, {n}u }},')
    jobs_c.append("};")
    jobs_c.append(f"const unsigned g_scr_njobs = {len(JOBS_META)}u;")
    (OUT / "scr_jobs.c").write_text("\n".join(jobs_c) + "\n", newline="\n")
    for p in tmp.glob("*.elf"):
        p.unlink()
    tmp.rmdir()


JOBS_META = []
if __name__ == "__main__":
    main()
