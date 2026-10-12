#!/usr/bin/env python3
"""
Generate runtime/spu/spu_interp_gen.h -- the decode table and the ALU cases of
the SPU interpreter (runtime/spu/spu_interp.c) -- from the SAME sources the
static SPU lift uses, so the interpreter and lifted code cannot diverge:

  * decode:    tools/spu_disasm.py spu_decode(). The mnemonic of an SPU
               instruction depends only on its top 11 bits (every opcode
               width -- 4/7/8/9/10/11 -- is a prefix of those), so the
               2048-entry table below is spu_decode() evaluated on each prefix.
  * semantics: tools/spu_lifter.py SPULifter._translate(). For every mnemonic
               that is not control flow / memory / channel (those live in
               spu_interp.c, because an interpreter does them differently from
               a lift), the lifter is asked to translate a synthetic
               instruction whose register fields are placeholders; its C
               statement is kept verbatim with the placeholders turned into
               the instruction's field extractors.
  * immediates: which bits hold the immediate and how they are extended is
               NOT restated by hand: every candidate extractor is tested
               against spu_decode() on random encodings of the opcode and the
               single one that reproduces the disassembler's immediate is used.

Usage:
    python3 tools/gen_spu_interp.py            # (re)write the header
    python3 tools/gen_spu_interp.py --check    # exit 1 if the header is stale
"""

import os
import random
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from spu_disasm import spu_decode, sign_extend          # noqa: E402
from spu_lifter import SPULifter, LiftedFunction        # noqa: E402
from spu_disasm import SPUInstruction                   # noqa: E402

OUT = os.path.join(ROOT, "runtime", "spu", "spu_interp_gen.h")
HELPERS = os.path.join(ROOT, "runtime", "spu", "spu_helpers.h")

# Executed by hand-written code in spu_interp.c (control flow, local store,
# channels, stop, hints, no-ops). Everything else must come from the lifter.
HANDWRITTEN = {
    "stop", "stopd", "lnop", "nop", "sync", "dsync", "fscrwr", "mtspr",
    "lqd", "stqd", "lqa", "stqa", "lqr", "stqr", "lqx", "stqx",
    "rdch", "wrch", "rchcnt",
    "br", "bra", "brsl", "brasl", "brz", "brnz", "brhz", "brhnz",
    "bi", "bisl", "bisled", "iret", "biz", "binz", "bihz", "bihnz",
    "hbr", "hbra", "hbrr",
}

# Placeholder register numbers -> C field extractors (see spu_interp_gen.h).
F_RT, F_RA, F_RB, F_RT3 = 101, 102, 103, 104
REGMAP = {F_RT: "RT", F_RA: "RA", F_RB: "RB", F_RT3: "RT3"}
IMM_TOKEN = "__IMM__"
PC_SENTINEL = 0xABCDC     # 4-aligned, printed by the halt ops as 0x%05Xu

# Candidate immediate extractors: (name, python fn(insn), C expression).
IMM_CANDIDATES = [
    ("i7u",  lambda x: (x >> 14) & 0x7F,                   "(int32_t)((insn >> 14) & 0x7Fu)"),
    ("i7s",  lambda x: sign_extend((x >> 14) & 0x7F, 7),   "((int32_t)(insn << 11) >> 25)"),
    ("i8u",  lambda x: (x >> 14) & 0xFF,                   "(int32_t)((insn >> 14) & 0xFFu)"),
    ("i10s", lambda x: sign_extend((x >> 14) & 0x3FF, 10), "((int32_t)(insn << 8) >> 22)"),
    ("i16s", lambda x: sign_extend((x >> 7) & 0xFFFF, 16), "((int32_t)(insn << 9) >> 16)"),
    ("i16u", lambda x: (x >> 7) & 0xFFFF,                  "(int32_t)((insn >> 7) & 0xFFFFu)"),
    ("i18u", lambda x: (x >> 7) & 0x3FFFF,                 "(int32_t)((insn >> 7) & 0x3FFFFu)"),
]

_REG_RE = re.compile(r"^\$r(\d+)$")


def _tok_is_reg(tok):
    return _REG_RE.match(tok.strip()) is not None


def _parse_imm(tok):
    tok = tok.strip()
    return int(tok, 0)


def decode_table():
    """prefix (insn >> 21) -> mnemonic, plus one canonical prefix per mnemonic."""
    table = []
    canon = {}
    for p in range(2048):
        mn = spu_decode(p << 21).mnemonic
        table.append(mn)
        canon.setdefault(mn, []).append(p)
    return table, canon


def pick_imm(mn, prefixes, rng):
    """Return (name, C expr) of the unique extractor matching the disassembler's
    immediate for `mn`, or None if the operand text has no immediate."""
    samples = []
    for _ in range(400):
        p = rng.choice(prefixes)
        insn = (p << 21) | rng.getrandbits(21)
        ins = spu_decode(insn)
        assert ins.mnemonic == mn
        toks = [t.strip() for t in ins.operands.split(",") if t.strip()]
        imm = [t for t in toks if not _tok_is_reg(t)]
        if not imm:
            return None
        assert len(imm) == 1, (mn, ins.operands)
        samples.append((insn, _parse_imm(imm[0])))
    ok = [(n, c) for (n, f, c) in IMM_CANDIDATES
          if all(f(x) == v for x, v in samples)]
    if not ok:
        raise SystemExit(f"gen_spu_interp: no extractor reproduces the immediate of '{mn}'")
    # Several can match only when they differ in bits that belong to this
    # opcode's fixed prefix (e.g. i7u vs i8u for cbd: bit 21 is opcode). The
    # samples randomise every non-prefix bit, so the matches are equivalent
    # for this opcode; take the first (narrowest) one.
    return ok[0]


def synth_operands(mn, prefixes):
    """Operand text for the lifter: the disassembler's own operand shape for a
    sample of `mn` whose register fields hold the placeholders, the immediate
    replaced by IMM_TOKEN."""
    # RRR ops carry their destination in the prefix (bits 21-27): pick the
    # prefix whose low 7 bits are F_RT3 when the opcode family has one.
    pref = [p for p in prefixes if (p & 0x7F) == F_RT3] or prefixes
    insn = (pref[0] << 21) | (F_RB << 14) | (F_RA << 7) | F_RT
    ins = spu_decode(insn)
    toks = [t.strip() for t in ins.operands.split(",") if t.strip()]
    out = []
    for t in toks:
        m = _REG_RE.match(t)
        if m:
            n = int(m.group(1))
            if n not in REGMAP:
                # Register field overlapping the immediate/opcode: this op has
                # no such register operand in the lifter's reading either.
                raise SystemExit(f"gen_spu_interp: '{mn}' operand {t} is not a "
                                 f"placeholder field (operands '{ins.operands}')")
            out.append(t)
        else:
            out.append(IMM_TOKEN)
    return ", ".join(out)


def helper_exists(src, name):
    return re.search(r"\b" + re.escape(name) + r"\s*\(", src) is not None and \
        re.search(r"static\s+inline\s+[\w\s\*]*\b" + re.escape(name) + r"\s*\(", src) is not None


def translate(mn, operands, imm_c):
    lf = SPULifter()
    fn = LiftedFunction(name="gen", start_addr=0, end_addr=0)
    ins = SPUInstruction(addr=PC_SENTINEL, raw=0, mnemonic=mn, operands=operands)
    c = lf._translate(ins, fn)
    if "TODO" in c or lf.unsupported:
        return None
    if "$r" in c:
        # The lifter's operand reading disagrees with the decoder's (e.g.
        # dftsv: RR in the decoder, RI in the lifter) -- a lift of it would
        # not compile either. Unsupported, not guessed.
        return None
    if "return" in c or "goto" in c:
        raise SystemExit(f"gen_spu_interp: unexpected lifter output for '{mn}': {c}")

    def reg(m):
        n = int(m.group(1))
        if n not in REGMAP:
            raise SystemExit(f"gen_spu_interp: '{mn}' uses gpr[{n}]: {c}")
        return f"ctx->gpr[{REGMAP[n]}]"
    c = re.sub(r"ctx->gpr\[(\d+)\]", reg, c)
    c = c.replace(f"0x{PC_SENTINEL:05X}u", "pc")
    if IMM_TOKEN in c:
        if imm_c is None:
            raise SystemExit(f"gen_spu_interp: '{mn}' has an immediate the disassembler lacks")
        c = c.replace(IMM_TOKEN, f"({imm_c})")
    if "__IMM" in c or "ABCDC" in c:
        raise SystemExit(f"gen_spu_interp: leftover placeholder in '{mn}': {c}")
    return c


def c_ident(mn):
    return "SPUI_" + re.sub(r"[^0-9A-Za-z_]", "_", mn)


def generate():
    rng = random.Random(0x5EED)
    table, canon = decode_table()
    helpers_src = open(HELPERS, encoding="utf-8").read()

    mnemonics = sorted(m for m in canon if m != ".word")
    cases = []
    missing_helper = []
    for mn in mnemonics:
        if mn in HANDWRITTEN:
            continue
        imm = pick_imm(mn, canon[mn], rng)
        imm_c = imm[1] if imm else None
        ops = synth_operands(mn, canon[mn])
        c = translate(mn, ops, imm_c)
        if c is None:
            missing_helper.append(mn)
            continue
        helpers = set(re.findall(r"\b(spu_[a-z0-9_]+)\s*\(", c)) - {"spu_halt_if"}
        absent = [h for h in helpers if not helper_exists(helpers_src, h)]
        if absent:
            missing_helper.append(mn)
            continue
        cases.append((mn, imm[0] if imm else "-", c))

    lines = []
    w = lines.append
    w("/* Generated by tools/gen_spu_interp.py -- DO NOT EDIT.")
    w(" *")
    w(" * Decode table and ALU semantics of the SPU interpreter (spu_interp.c),")
    w(" * derived from tools/spu_disasm.py (decode, immediates) and")
    w(" * tools/spu_lifter.py (per-instruction C, the same helpers lifted code")
    w(" * calls). Regenerate after touching either tool:")
    w(" *     python3 tools/gen_spu_interp.py")
    w(" */")
    w("#ifndef SPU_INTERP_GEN_H")
    w("#define SPU_INTERP_GEN_H")
    w("")
    w("enum spui_op {")
    w("    SPUI_UNKNOWN = 0,")
    for i, mn in enumerate(mnemonics):
        w(f"    {c_ident(mn)} = {i + 1},")
    w(f"    SPUI_NUM_OPS = {len(mnemonics) + 1}")
    w("};")
    w("")
    w("static const char* const spui_op_name[SPUI_NUM_OPS] = {")
    w('    ".word",')
    for mn in mnemonics:
        w(f'    "{mn}",')
    w("};")
    w("")
    w("/* insn >> 21 -> enum spui_op (spu_decode() on each 11-bit prefix). */")
    w("static const unsigned char spui_decode11[2048] = {")
    idx = {mn: i + 1 for i, mn in enumerate(mnemonics)}
    assert len(mnemonics) + 1 < 256
    row = []
    for p, mn in enumerate(table):
        row.append(str(idx.get(mn, 0)))
        if len(row) == 16:
            w("    " + ", ".join(row) + ",")
            row = []
    w("};")
    w("")
    w("/* Lifter-derived semantics. Returns 1 when `op` was executed here, 0 when")
    w(" * it is control flow / memory / channel (spu_interp.c) or unsupported. */")
    w("static inline int spui_exec_gen(spu_context* ctx, unsigned op, uint32_t insn, uint32_t pc)")
    w("{")
    w("    const uint32_t RT  = insn & 0x7Fu;          /* rt (RR/RI*), rc of RRR */")
    w("    const uint32_t RA  = (insn >> 7) & 0x7Fu;")
    w("    const uint32_t RB  = (insn >> 14) & 0x7Fu;")
    w("    const uint32_t RT3 = (insn >> 21) & 0x7Fu;  /* RRR destination */")
    w("    (void)RT; (void)RA; (void)RB; (void)RT3; (void)pc;")
    w("    switch (op) {")
    for mn, immn, c in cases:
        w(f"    case {c_ident(mn)}: /* imm {immn} */")
        w(f"        {c}")
        w("        return 1;")
    w("    default:")
    w("        return 0;")
    w("    }")
    w("}")
    w("")
    w("/* Mnemonics the disassembler decodes but neither the lifter nor the")
    w(" * helpers implement (the interpreter stops the job on them):")
    for i in range(0, len(missing_helper), 8):
        w(" *   " + " ".join(missing_helper[i:i + 8]))
    w(" */")
    w("")
    w("#endif /* SPU_INTERP_GEN_H */")
    return "\n".join(lines) + "\n", len(cases), missing_helper


def main():
    text, n, missing = generate()
    if "--check" in sys.argv[1:]:
        cur = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if cur != text:
            print(f"FAIL: {OUT} is stale; run python3 tools/gen_spu_interp.py")
            sys.exit(1)
        print(f"OK: {OUT} up to date ({n} generated cases)")
        return
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"wrote {OUT}: {n} generated cases; unsupported: {' '.join(missing) or '-'}")


if __name__ == "__main__":
    main()
