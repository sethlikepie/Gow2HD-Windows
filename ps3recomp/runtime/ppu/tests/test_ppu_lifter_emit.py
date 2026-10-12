#!/usr/bin/env python3
"""
ps3recomp - PPU lifter EMISSION golden test (drives the real production path).

Unlike the hand-written .cpp fixtures next to it (which exercise the
runtime/ppu/ppu_ops.h *reference* helpers in isolation), this test proves the
thing that actually ships: the exact C string tools/ppu_lifter.py emits for
each instruction. It closes the gap where a green helper test could pass while
the lifter's own inline emission silently drifted or stayed wrong.

For every instruction under audit it:

  1. Encodes the REAL PowerPC instruction word, decodes it with the production
     decoder (ppu_disasm.decode) and translates it with the production emitter
     (PPULifter._translate_op) -- no hand transcription anywhere.
  2. Asserts the emitted C string EXACTLY matches a checked-in golden. A change
     to the lifter's output fails here loudly instead of silently.
  3. Drops those SAME emitted statements verbatim into a C++ harness, compiles
     it against the real runtime headers (ppu_context.h / ppu_memory.h, whose
     vm_read32/vm_write32 do the actual big-endian byte-swap) plus ppu_ops.h,
     runs it, and checks the runtime semantics:
       - lmw/stmw: big-endian word order, range-through-r31, and the emitted
         output is byte-for-byte identical to ppu_ops.h's ppu_lmw/ppu_stmw
         (so the two implementations cannot drift apart -- dual-maintenance
         guard).
       - fmadd family: the emitted fused multiply-add (base's ppu_fmadd_core,
         compiled here from the lifter's own SOURCE_PREAMBLE) is truly single-
         rounded (the classic 2^27+1 Dekker input where the naive `a*c+b`
         double-rounds to 0 but a fused multiply-add yields 1), AND -- on
         finite inputs -- bit-identical to the independent ppu_ops.h reference
         helper, so the two never silently diverge.
       - blrl: dispatches through ps3_indirect_call exactly once with the LR
         target, links LR<-CIA+4, and leaves CTR UNCHANGED (Book I 4.2.2 --
         blrl writes only LR; the dispatch's transient use of CTR is restored).

  No ELF and no boot are required; everything is offline. Run directly:
     python3 runtime/ppu/tests/test_ppu_lifter_emit.py [--cxx c++]
  Exit code 0 = all goldens matched and the compiled harness passed.
"""
from __future__ import annotations

import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RUNTIME_PPU = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(RUNTIME_PPU))
TOOLS = os.path.join(REPO, "tools")
sys.path.insert(0, TOOLS)

from ppu_disasm import decode                            # noqa: E402
from ppu_lifter import PPULifter, LiftedFunction, SOURCE_PREAMBLE  # noqa: E402


# ---------------------------------------------------------------------------
# 1) Golden emission: real word -> real decode -> real translate -> exact C.
# ---------------------------------------------------------------------------
# These goldens are the EXACT C the current lifter emits. lmw/stmw and the
# fmadd family are the base lifter's own emission (unrolled loads/stores; the
# fused ppu_fmadd_core / ppu_fp_single helpers defined in the lifter's
# SOURCE_PREAMBLE) -- this PR does NOT re-implement them, it only pins them
# under the golden. blrl is this PR's fix (Book I 4.2.2: link LR, dispatch via
# the CTR-keyed ps3_indirect_call, then RESTORE CTR so it is left unchanged).
# (word, decode_addr, golden_c). decode_addr matters for blrl (bakes CIA+4).
CASES = {
    "lmw": (
        0xBB810010, 0x1000,
        "{ uint64_t _ea = ctx->gpr[1] + 0x10; "
        "ctx->gpr[28] = vm_read32(_ea + 0); ctx->gpr[29] = vm_read32(_ea + 4); "
        "ctx->gpr[30] = vm_read32(_ea + 8); ctx->gpr[31] = vm_read32(_ea + 12); }",
    ),
    "stmw": (
        0xBF810010, 0x1000,
        "{ uint64_t _ea = ctx->gpr[1] + 0x10; "
        "vm_write32(_ea + 0, (uint32_t)ctx->gpr[28]); vm_write32(_ea + 4, (uint32_t)ctx->gpr[29]); "
        "vm_write32(_ea + 8, (uint32_t)ctx->gpr[30]); vm_write32(_ea + 12, (uint32_t)ctx->gpr[31]); }",
    ),
    # RA=0 forms: PowerISA Book I 3.3.5 says the base is a LITERAL zero, NOT
    # the contents of GPR[0]. Pinned here because the RA=1 goldens above cannot
    # tell the two apart (the lifter once emitted ctx->gpr[0] for both).
    "lmw_ra0": (
        0xBBC00300, 0x1000,
        "{ uint64_t _ea = 0 + 0x300; "
        "ctx->gpr[30] = vm_read32(_ea + 0); ctx->gpr[31] = vm_read32(_ea + 4); }",
    ),
    "stmw_ra0": (
        0xBFC00300, 0x1000,
        "{ uint64_t _ea = 0 + 0x300; "
        "vm_write32(_ea + 0, (uint32_t)ctx->gpr[30]); vm_write32(_ea + 4, (uint32_t)ctx->gpr[31]); }",
    ),
    # D-form (and DS-form lwa) load/store base, both RA cases. Book I 3.3.2/
    # 3.3.3/4.6.2: EA = (RA=0 ? 0 : GPR[RA]) + D, so RA=0 is a LITERAL zero
    # base. RA is a constant field of the instruction word, so the lifter
    # resolves the base at LIFT time ("0" vs "ctx->gpr[N]") instead of emitting
    # a ternary over a compile-time constant. These goldens pin BOTH halves of
    # that rule per family, so neither the zero-base semantics nor the emitted
    # spelling can drift unnoticed.
    "lwz":      (0x806400B8, 0x1000, "ctx->gpr[3] = vm_read32(ctx->gpr[4] + 0xB8);"),
    "lwz_ra0":  (0x806000B8, 0x1000, "ctx->gpr[3] = vm_read32(0 + 0xB8);"),
    "stw":      (0x906400B8, 0x1000, "vm_write32(ctx->gpr[4] + 0xB8, ctx->gpr[3]);"),
    "stw_ra0":  (0x906000B8, 0x1000, "vm_write32(0 + 0xB8, ctx->gpr[3]);"),
    "lwa":      (0xE86400BA, 0x1000, "ctx->gpr[3] = (int64_t)(int32_t)vm_read32(ctx->gpr[4] + 0xB8);"),
    "lwa_ra0":  (0xE86000BA, 0x1000, "ctx->gpr[3] = (int64_t)(int32_t)vm_read32(0 + 0xB8);"),
    "lfs": (
        0xC06400B8, 0x1000,
        "{ uint32_t tmp = vm_read32(ctx->gpr[4] + 0xB8); "
        "float ftmp; memcpy(&ftmp, &tmp, 4); ctx->fpr[3] = ftmp; }",
    ),
    "lfs_ra0": (
        0xC06000B8, 0x1000,
        "{ uint32_t tmp = vm_read32(0 + 0xB8); "
        "float ftmp; memcpy(&ftmp, &tmp, 4); ctx->fpr[3] = ftmp; }",
    ),
    "lfd": (
        0xC86400B8, 0x1000,
        "{ uint64_t tmp = vm_read64(ctx->gpr[4] + 0xB8); memcpy(&ctx->fpr[3], &tmp, 8); }",
    ),
    "lfd_ra0": (
        0xC86000B8, 0x1000,
        "{ uint64_t tmp = vm_read64(0 + 0xB8); memcpy(&ctx->fpr[3], &tmp, 8); }",
    ),
    "stfs": (
        0xD06400B8, 0x1000,
        "{ float ftmp = (float)ctx->fpr[3]; uint32_t tmp; memcpy(&tmp, &ftmp, 4); "
        "vm_write32(ctx->gpr[4] + 0xB8, tmp); }",
    ),
    "stfs_ra0": (
        0xD06000B8, 0x1000,
        "{ float ftmp = (float)ctx->fpr[3]; uint32_t tmp; memcpy(&tmp, &ftmp, 4); "
        "vm_write32(0 + 0xB8, tmp); }",
    ),
    "stfd": (
        0xD86400B8, 0x1000,
        "{ uint64_t tmp; memcpy(&tmp, &ctx->fpr[3], 8); vm_write64(ctx->gpr[4] + 0xB8, tmp); }",
    ),
    "stfd_ra0": (
        0xD86000B8, 0x1000,
        "{ uint64_t tmp; memcpy(&tmp, &ctx->fpr[3], 8); vm_write64(0 + 0xB8, tmp); }",
    ),
    "fmadd":   (0xFCA118BA, 0, "ctx->fpr[5] = ppu_fmadd_core(ctx->fpr[1], ctx->fpr[2], ctx->fpr[3], 0, 0);"),
    "fmadds":  (0xECA118BA, 0, "ctx->fpr[5] = ppu_fp_single(ppu_fmadd_core(ctx->fpr[1], ctx->fpr[2], ctx->fpr[3], 0, 0));"),
    "fmsub":   (0xFCA118B8, 0, "ctx->fpr[5] = ppu_fmadd_core(ctx->fpr[1], ctx->fpr[2], ctx->fpr[3], 1, 0);"),
    "fmsubs":  (0xECA118B8, 0, "ctx->fpr[5] = ppu_fp_single(ppu_fmadd_core(ctx->fpr[1], ctx->fpr[2], ctx->fpr[3], 1, 0));"),
    "fnmadd":  (0xFCA118BE, 0, "ctx->fpr[5] = ppu_fmadd_core(ctx->fpr[1], ctx->fpr[2], ctx->fpr[3], 0, 1);"),
    "fnmadds": (0xECA118BE, 0, "ctx->fpr[5] = ppu_fp_single(ppu_fmadd_core(ctx->fpr[1], ctx->fpr[2], ctx->fpr[3], 0, 1));"),
    "fnmsub":  (0xFCA118BC, 0, "ctx->fpr[5] = ppu_fmadd_core(ctx->fpr[1], ctx->fpr[2], ctx->fpr[3], 1, 1);"),
    "fnmsubs": (0xECA118BC, 0, "ctx->fpr[5] = ppu_fp_single(ppu_fmadd_core(ctx->fpr[1], ctx->fpr[2], ctx->fpr[3], 1, 1));"),
    "blrl": (
        0x4E800021, 0x402004,
        "{ uint64_t _blrl_tgt = ctx->lr; uint64_t _blrl_saved_ctr = ctx->ctr; "
        "ctx->lr = 0x402008ULL; ctx->ctr = _blrl_tgt; "
        "ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx); "
        "ctx->ctr = _blrl_saved_ctr; }",
    ),
    # Emissao 4 do criterio 1 (D-10): mftb/mftbu -> ppu_timebase_now(). Sem um
    # definidor versionado em runtime/syscalls/sys_timer.c um lift LIMPO nao
    # linka (commit e441f37: "Undefined symbols: _ppu_timebase_now", 7+
    # sitios). Word/golden reconfirmados nesta sessao rodando o decoder e o
    # tradutor de producao -- nao transcritos de cabeca.
    "mftb":  (0x7C6C42E6, 0x1000, "ctx->gpr[3] = ppu_timebase_now();"),
    "mftbu": (0x7C6D42E6, 0x1000, "ctx->gpr[3] = (ppu_timebase_now() >> 32);"),
}


def extract_fp_helpers() -> str:
    """Pull the fused-FP helper block (ppu_fp_quiet .. ppu_fp_single, incl.
    ppu_fmadd_core) VERBATIM out of the lifter's own SOURCE_PREAMBLE so the
    harness compiles the ACTUAL shipping helper definitions the emitted fmadd
    family calls -- not a re-typed copy that could drift from what ships."""
    start = SOURCE_PREAMBLE.index("static inline double ppu_fp_quiet")
    end = SOURCE_PREAMBLE.index("static inline double ppu_frsp")
    return SOURCE_PREAMBLE[start:end].strip()


def check_goldens() -> dict:
    lift = PPULifter()
    func = LiftedFunction(name="func", start_addr=0x0, end_addr=0x0100_0000)
    emitted = {}
    ok = True
    for name, (word, addr, golden) in CASES.items():
        insn = decode(word, addr)
        got = lift._translate_op(insn, func)
        emitted[name] = got
        if got != golden:
            ok = False
            print(f"[FAIL] golden {name}: emission diverged from checked-in golden")
            print(f"       decoded : {insn.mnemonic} {insn.operands}")
            print(f"       expected: {golden}")
            print(f"       got     : {got}")
        else:
            print(f"[PASS] golden {name}: {insn.mnemonic} {insn.operands!r} -> exact match")
    if not ok:
        raise SystemExit(
            "\nEmission goldens diverged. If tools/ppu_lifter.py intentionally "
            "changed, update the CASES table in this file to match.")
    return emitted


# ---------------------------------------------------------------------------
# 2) Compile + run the emitted C, cross-checked against ppu_ops.h helpers.
# ---------------------------------------------------------------------------
HARNESS_TEMPLATE = r"""
/* AUTO-GENERATED by test_ppu_lifter_emit.py -- do not edit. The bodies of the
 * emit_* functions below are the LITERAL strings tools/ppu_lifter.py produced
 * for each instruction, so what is compiled and run here is exactly what the
 * lifter ships into recompiled code. */
#pragma STDC FP_CONTRACT OFF

#include "{ppu_context_h}"
#include "{ppu_memory_h}"
#include "{ppu_ops_h}"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

/* The fused-FP helpers the emitted fmadd family calls (ppu_fmadd_core,
 * ppu_fp_single, ...) are lifted VERBATIM out of tools/ppu_lifter.py's
 * SOURCE_PREAMBLE, so what runs here is the exact helper source the lifter
 * emits into every generated .cpp -- no re-typed parallel copy. */
{fp_helpers}

/* Backing store for the guest address space (ppu_memory.h externs vm_base). */
static uint8_t g_guest_mem[4096];
extern "C" uint8_t* vm_base = g_guest_mem;

/* base's ppu_memory.h guards every vm_read/write with a commit check; our
 * flat backing buffer is fully addressable, so the whole range is committed. */
extern "C" int ppu_guest_range_committed(uint32_t /*addr*/, uint32_t /*n*/) {{ return 1; }}

/* Mocks standing in for the game project's indirect-call dispatcher + the
 * cross-fragment trampoline drain macro emitted in ppu_lifter's preamble. */
extern "C" __thread void (*g_trampoline_fn)(void*) = nullptr;
static int      g_ic_count   = 0;
static uint64_t g_ic_saw_ctr = 0;
static int      g_tramp_ran  = 0;
extern "C" void ps3_indirect_call(ppu_context* ctx) {{
    g_ic_count++;
    g_ic_saw_ctr = ctx->ctr;      /* what the dispatch was keyed on at call time */
    g_trampoline_fn = [](void*) {{ g_tramp_ran = 1; }};
}}
#define DRAIN_TRAMPOLINE(ctx) do {{ \
    while (g_trampoline_fn) {{ \
        void(*_tf)(void*) = g_trampoline_fn; \
        g_trampoline_fn = 0; \
        _tf((void*)(ctx)); \
    }} \
}} while(0)

static int fails = 0;
#define CHECK(c, msg) do {{ if (c) printf("[PASS] %s\n", msg); \
    else {{ printf("[FAIL] %s\n", msg); fails++; }} }} while (0)

/* ---- LITERAL lifter emission (bodies substituted from the goldens) ---- */
static void emit_lmw    (ppu_context* ctx) {{ {LMW} }}
static void emit_stmw   (ppu_context* ctx) {{ {STMW} }}
static void emit_lmw_ra0 (ppu_context* ctx) {{ {LMW_RA0} }}
static void emit_stmw_ra0(ppu_context* ctx) {{ {STMW_RA0} }}
static void emit_lwz     (ppu_context* ctx) {{ {LWZ} }}
static void emit_lwz_ra0 (ppu_context* ctx) {{ {LWZ_RA0} }}
static void emit_stw     (ppu_context* ctx) {{ {STW} }}
static void emit_stw_ra0 (ppu_context* ctx) {{ {STW_RA0} }}
static void emit_lwa     (ppu_context* ctx) {{ {LWA} }}
static void emit_lwa_ra0 (ppu_context* ctx) {{ {LWA_RA0} }}
static void emit_lfs     (ppu_context* ctx) {{ {LFS} }}
static void emit_lfs_ra0 (ppu_context* ctx) {{ {LFS_RA0} }}
static void emit_lfd     (ppu_context* ctx) {{ {LFD} }}
static void emit_lfd_ra0 (ppu_context* ctx) {{ {LFD_RA0} }}
static void emit_stfs    (ppu_context* ctx) {{ {STFS} }}
static void emit_stfs_ra0(ppu_context* ctx) {{ {STFS_RA0} }}
static void emit_stfd    (ppu_context* ctx) {{ {STFD} }}
static void emit_stfd_ra0(ppu_context* ctx) {{ {STFD_RA0} }}
static void emit_fmadd  (ppu_context* ctx) {{ {FMADD} }}
static void emit_fmadds (ppu_context* ctx) {{ {FMADDS} }}
static void emit_fmsub  (ppu_context* ctx) {{ {FMSUB} }}
static void emit_fmsubs (ppu_context* ctx) {{ {FMSUBS} }}
static void emit_fnmadd (ppu_context* ctx) {{ {FNMADD} }}
static void emit_fnmadds(ppu_context* ctx) {{ {FNMADDS} }}
static void emit_fnmsub (ppu_context* ctx) {{ {FNMSUB} }}
static void emit_fnmsubs(ppu_context* ctx) {{ {FNMSUBS} }}
static void emit_blrl   (ppu_context* ctx) {{ {BLRL} }}

/* ------------------------------------------------------------------ lmw/stmw */
static void test_lmw_stmw() {{
    printf("-- emitted lmw/stmw --\n");
    memset(g_guest_mem, 0, sizeof(g_guest_mem));

    ppu_context ce;  memset(&ce, 0, sizeof(ce));
    ce.gpr[1]  = 0x100;                       /* EA = r1 + 0x10 = 0x110 */
    ce.gpr[27] = 0xDEADDEADDEADDEADULL;       /* below range: must be untouched */
    ce.gpr[28] = 0x1122334455667788ULL;
    ce.gpr[29] = 0x00000000CAFEBABEULL;
    ce.gpr[30] = 0x0000000012345678ULL;
    ce.gpr[31] = 0x0000000000000001ULL;

    emit_stmw(&ce);                            /* lifter output writes to 0x110.. */

    uint32_t w0 = vm_read32(0x110), w1 = vm_read32(0x114),
             w2 = vm_read32(0x118), w3 = vm_read32(0x11C);
    CHECK(w0 == (uint32_t)ce.gpr[28], "emitted stmw: word[0] == r28 (ascending reg/addr)");
    CHECK(w1 == (uint32_t)ce.gpr[29], "emitted stmw: word[1] == r29");
    CHECK(w2 == (uint32_t)ce.gpr[30], "emitted stmw: word[2] == r30");
    CHECK(w3 == (uint32_t)ce.gpr[31], "emitted stmw: word[3] == r31 (range runs through r31)");
    CHECK(g_guest_mem[0x11C] == 0x00 && g_guest_mem[0x11D] == 0x00 &&
          g_guest_mem[0x11E] == 0x00 && g_guest_mem[0x11F] == 0x01,
          "emitted stmw: r31 word is big-endian on the wire (00 00 00 01)");

    /* Dual-maintenance guard: the ppu_ops.h helper, given the same registers,
     * must produce a byte-identical image at a different address. */
    ppu_stmw(&ce, 28, 0x200);
    CHECK(memcmp(&g_guest_mem[0x110], &g_guest_mem[0x200], 16) == 0,
          "emitted stmw is byte-identical to ppu_ops.h ppu_stmw (no drift)");

    /* lmw round-trip: emitted load must recover the low 32 bits, zero-extended,
     * and leave r27 (below range) untouched; also identical to ppu_lmw. */
    ppu_context cl;  memset(&cl, 0, sizeof(cl));
    cl.gpr[1]  = 0x100;
    cl.gpr[27] = 0xABCDABCDABCDABCDULL;
    emit_lmw(&cl);
    CHECK(cl.gpr[28] == (uint32_t)ce.gpr[28], "emitted lmw: r28 round-trips through stmw/lmw");
    CHECK(cl.gpr[31] == (uint32_t)ce.gpr[31], "emitted lmw: r31 round-trips (range runs through r31)");
    CHECK((cl.gpr[28] >> 32) == 0, "emitted lmw: zero-extends the upper 32 bits (no sign extension)");
    CHECK(cl.gpr[27] == 0xABCDABCDABCDABCDULL, "emitted lmw: r27 below the range is untouched");

    ppu_context ch;  memset(&ch, 0, sizeof(ch));
    ch.gpr[1] = 0x100;
    ppu_lmw(&ch, 28, 0x110);
    CHECK(memcmp(&cl.gpr[28], &ch.gpr[28], 4 * sizeof(uint64_t)) == 0,
          "emitted lmw is identical to ppu_ops.h ppu_lmw (no drift)");

    /* RA=0: Book I 3.3.5 -- base is a LITERAL zero, so r0's contents must be
     * ignored entirely. Poison r0: if the emission used ctx->gpr[0] the store
     * lands at 0x300+poison (out of the window) instead of at 0x300. */
    ppu_context cz;  memset(&cz, 0, sizeof(cz));
    cz.gpr[0]  = 0x400;                        /* poison: must NOT be added */
    cz.gpr[30] = 0x00000000A5A5A5A5ULL;
    cz.gpr[31] = 0x000000005A5A5A5AULL;
    memset(&g_guest_mem[0x300], 0, 32);
    memset(&g_guest_mem[0x700], 0, 32);        /* where a gpr[0] base would land */
    emit_stmw_ra0(&cz);
    CHECK(vm_read32(0x300) == 0xA5A5A5A5u && vm_read32(0x304) == 0x5A5A5A5Au,
          "emitted stmw RA=0: EA is the literal displacement (r0 contents ignored)");
    CHECK(vm_read32(0x700) == 0u && vm_read32(0x704) == 0u,
          "emitted stmw RA=0: nothing written at 0x300+r0 (no GPR[0] base)");

    ppu_context cz2; memset(&cz2, 0, sizeof(cz2));
    cz2.gpr[0] = 0x400;
    emit_lmw_ra0(&cz2);
    CHECK(cz2.gpr[30] == 0xA5A5A5A5ULL && cz2.gpr[31] == 0x5A5A5A5AULL,
          "emitted lmw RA=0: reads back from the literal displacement");

    ppu_context cz3; memset(&cz3, 0, sizeof(cz3));
    cz3.gpr[0] = 0x400;
    PPU_LMW(&cz3, 30, 0, 0x300);   /* ppu_ops.h's own RA=0 ternary */
    CHECK(cz3.gpr[30] == cz2.gpr[30] && cz3.gpr[31] == cz2.gpr[31],
          "emitted lmw RA=0 matches ppu_ops.h's RA=0 rule (no drift)");
}}

/* ------------------------------------------------- D-form base (RA=0 / RA!=0) */
/* Book I 3.3.2/3.3.3 (and 3.3.2 DS-form for lwa): EA = (RA=0 ? 0 : GPR[RA]) + D.
 * Every case below uses D = 0xB8 and poisons GPR[0] with 0x400, so a wrong RA=0
 * emission (one that used GPR[0] as the base) lands at 0x4B8 instead of 0xB8 --
 * a distinct, checkable address inside the window. The RA!=0 half uses r4=0x200
 * (EA 0x2B8) to prove the register base is still honoured. */
static const uint32_t EA_RA4 = 0x2B8;   /* r4(0x200) + 0xB8 */
static const uint32_t EA_RA0 = 0x0B8;   /* literal 0 + 0xB8 */
static const uint32_t EA_BAD = 0x4B8;   /* where an r0-poisoned base would land */

static void test_dform_base() {{
    printf("-- emitted D-form loads/stores: RA base resolution --\n");
    memset(g_guest_mem, 0, sizeof(g_guest_mem));

    /* ---- integer store/load (stw/lwz) ---- */
    ppu_context c; memset(&c, 0, sizeof(c));
    c.gpr[0] = 0x400;                       /* poison: only legal to use when RA=4 */
    c.gpr[4] = 0x200;
    c.gpr[3] = 0x00000000C0FFEE11ULL;
    emit_stw(&c);
    CHECK(vm_read32(EA_RA4) == 0xC0FFEE11u, "emitted stw RA=4: EA = GPR[4] + D");
    emit_stw_ra0(&c);
    CHECK(vm_read32(EA_RA0) == 0xC0FFEE11u, "emitted stw RA=0: EA is the literal D (r0 ignored)");
    CHECK(vm_read32(EA_BAD) == 0u, "emitted stw RA=0: nothing written at D + GPR[0]");

    ppu_context cl; memset(&cl, 0, sizeof(cl));
    cl.gpr[0] = 0x400; cl.gpr[4] = 0x200;
    emit_lwz(&cl);
    CHECK(cl.gpr[3] == 0xC0FFEE11ULL, "emitted lwz RA=4: reads from GPR[4] + D");
    cl.gpr[3] = 0;
    emit_lwz_ra0(&cl);
    CHECK(cl.gpr[3] == 0xC0FFEE11ULL, "emitted lwz RA=0: reads from the literal D (r0 ignored)");

    /* ---- lwa (DS-form): same base rule, plus sign extension ---- */
    vm_write32(EA_RA4, 0xFFFFFFFEu);
    vm_write32(EA_RA0, 0xFFFFFFFDu);
    vm_write32(EA_BAD, 0x00000007u);        /* a wrong base would read this instead */
    ppu_context ca; memset(&ca, 0, sizeof(ca));
    ca.gpr[0] = 0x400; ca.gpr[4] = 0x200;
    emit_lwa(&ca);
    CHECK(ca.gpr[3] == 0xFFFFFFFFFFFFFFFEULL, "emitted lwa RA=4: reads GPR[4] + D, sign-extended");
    emit_lwa_ra0(&ca);
    CHECK(ca.gpr[3] == 0xFFFFFFFFFFFFFFFDULL, "emitted lwa RA=0: reads the literal D (not D + GPR[0])");

    /* ---- FP single (stfs/lfs) ---- */
    memset(g_guest_mem, 0, sizeof(g_guest_mem));
    ppu_context cf; memset(&cf, 0, sizeof(cf));
    cf.gpr[0] = 0x400; cf.gpr[4] = 0x200;
    cf.fpr[3] = 0.5;                        /* exact in both float and double */
    emit_stfs(&cf);
    emit_stfs_ra0(&cf);
    uint32_t half_bits; float half = 0.5f; memcpy(&half_bits, &half, 4);
    CHECK(vm_read32(EA_RA4) == half_bits, "emitted stfs RA=4: EA = GPR[4] + D");
    CHECK(vm_read32(EA_RA0) == half_bits, "emitted stfs RA=0: EA is the literal D (r0 ignored)");
    CHECK(vm_read32(EA_BAD) == 0u, "emitted stfs RA=0: nothing written at D + GPR[0]");

    ppu_context cfl; memset(&cfl, 0, sizeof(cfl));
    cfl.gpr[0] = 0x400; cfl.gpr[4] = 0x200;
    emit_lfs(&cfl);
    CHECK(cfl.fpr[3] == 0.5, "emitted lfs RA=4: reads from GPR[4] + D");
    cfl.fpr[3] = 0.0;
    emit_lfs_ra0(&cfl);
    CHECK(cfl.fpr[3] == 0.5, "emitted lfs RA=0: reads from the literal D (r0 ignored)");

    /* ---- FP double (stfd/lfd) ---- */
    memset(g_guest_mem, 0, sizeof(g_guest_mem));
    ppu_context cd; memset(&cd, 0, sizeof(cd));
    cd.gpr[0] = 0x400; cd.gpr[4] = 0x200;
    cd.fpr[3] = -2.25;
    emit_stfd(&cd);
    emit_stfd_ra0(&cd);
    uint64_t dbits; double dv = -2.25; memcpy(&dbits, &dv, 8);
    CHECK(vm_read64(EA_RA4) == dbits, "emitted stfd RA=4: EA = GPR[4] + D");
    CHECK(vm_read64(EA_RA0) == dbits, "emitted stfd RA=0: EA is the literal D (r0 ignored)");
    CHECK(vm_read64(EA_BAD) == 0ull, "emitted stfd RA=0: nothing written at D + GPR[0]");

    ppu_context cdl; memset(&cdl, 0, sizeof(cdl));
    cdl.gpr[0] = 0x400; cdl.gpr[4] = 0x200;
    emit_lfd(&cdl);
    CHECK(cdl.fpr[3] == -2.25, "emitted lfd RA=4: reads from GPR[4] + D");
    cdl.fpr[3] = 0.0;
    emit_lfd_ra0(&cdl);
    CHECK(cdl.fpr[3] == -2.25, "emitted lfd RA=0: reads from the literal D (r0 ignored)");
}}

/* ------------------------------------------------------------- fmadd family */
static bool bit_eq(double x, double y) {{ return memcmp(&x, &y, sizeof(double)) == 0; }}

static void test_fma_family() {{
    printf("-- emitted fmadd/fmsub/fnmadd/fnmsub family --\n");

    /* Dekker input: a=c=2^27+1, so a*c needs 55 bits and the bare multiply
     * must round. b = -(rounded product). A truly fused multiply-add recovers
     * the exact remainder (=> 1.0); a double-rounded a*c+b collapses to 0. */
    double a = 134217729.0, c = 134217729.0;
    volatile double naive_mul  = a * c;
    double b = -naive_mul;
    volatile double naive_expr = a * c + b;    /* two roundings (contract off) */

    ppu_context cf; memset(&cf, 0, sizeof(cf));
    cf.fpr[1] = a; cf.fpr[2] = c; cf.fpr[3] = b;
    emit_fmadd(&cf);
    printf("   naive a*c+b = %.17g, emitted fmadd = %.17g\n", (double)naive_expr, cf.fpr[5]);
    CHECK((double)naive_expr == 0.0, "sanity: naive a*c+b double-rounds to 0");
    CHECK(cf.fpr[5] == 1.0, "emitted fmadd is truly single-rounded (fma), recovers the exact remainder");
    CHECK(cf.fpr[5] != (double)naive_expr, "emitted fmadd differs from the naive two-op formula");
    CHECK(bit_eq(cf.fpr[5], ppu_fmadd(a, c, b)), "emitted fmadd is bit-identical to ppu_ops.h ppu_fmadd");

    /* Every family member: emitted result must be bit-identical to its helper.
     * The helpers take (a,c,b) and apply the sign of b / result internally, so
     * fpr[1..3] map straight onto (a,c,b). */
    struct {{ const char* name; void(*emit)(ppu_context*); double(*ref)(double,double,double); }}
    fam[] = {{
        {{"fmadd",   emit_fmadd,   ppu_fmadd  }},
        {{"fmadds",  emit_fmadds,  ppu_fmadds }},
        {{"fmsub",   emit_fmsub,   ppu_fmsub  }},
        {{"fmsubs",  emit_fmsubs,  ppu_fmsubs }},
        {{"fnmadd",  emit_fnmadd,  ppu_fnmadd }},
        {{"fnmadds", emit_fnmadds, ppu_fnmadds}},
        {{"fnmsub",  emit_fnmsub,  ppu_fnmsub }},
        {{"fnmsubs", emit_fnmsubs, ppu_fnmsubs}},
    }};
    /* Inputs whose single-precision rounding is observable (da not float-exact). */
    double da = 1.0 + 0x1p-30, dc = 1.0, db = 0x1p-40;
    for (auto& f : fam) {{
        ppu_context cc; memset(&cc, 0, sizeof(cc));
        cc.fpr[1] = da; cc.fpr[2] = dc; cc.fpr[3] = db;
        f.emit(&cc);
        double ref = f.ref(da, dc, db);
        char msg[96];
        snprintf(msg, sizeof(msg), "emitted %s is bit-identical to ppu_ops.h ppu_%s", f.name, f.name);
        CHECK(bit_eq(cc.fpr[5], ref), msg);
    }}

    /* The "s" forms must actually apply the extra float rounding (differ from
     * the double-precision fused result on this input). */
    ppu_context cs; memset(&cs, 0, sizeof(cs));
    cs.fpr[1] = da; cs.fpr[2] = dc; cs.fpr[3] = db;
    emit_fmadds(&cs);
    CHECK(cs.fpr[5] == (double)(float)ppu_fmadd(da, dc, db),
          "emitted fmadds rounds to float precision");
    CHECK(cs.fpr[5] != ppu_fmadd(da, dc, db),
          "emitted fmadds's extra float rounding is observable (differs from the double fused result)");
}}

/* --------------------------------------------------------------------- blrl */
static void test_blrl() {{
    printf("-- emitted blrl --\n");
    g_ic_count = 0; g_ic_saw_ctr = 0; g_tramp_ran = 0; g_trampoline_fn = nullptr;

    ppu_context cb; memset(&cb, 0, sizeof(cb));
    const uint64_t TARGET  = 0x00501234ULL;   /* what `mtlr rX` staged into LR */
    const uint64_t RETADDR = 0x00402008ULL;   /* CIA(blrl=0x402004) + 4 */
    const uint64_t OWN_CTR = 0x0BADF00DDEADBEEFULL;  /* caller's own live CTR */
    cb.lr  = TARGET;
    cb.ctr = OWN_CTR;

    emit_blrl(&cb);

    CHECK(g_ic_count == 1, "emitted blrl dispatches through ps3_indirect_call once (a real call, not return)");
    CHECK(g_ic_saw_ctr == TARGET, "emitted blrl keys the dispatch on the LR target (read-before-write order)");
    CHECK(cb.lr == RETADDR, "emitted blrl links LR <- CIA+4 (callee's blr returns here)");
    CHECK(cb.ctr == OWN_CTR, "emitted blrl leaves CTR UNCHANGED (Book I 4.2.2: blrl writes only LR)");
    CHECK(g_tramp_ran == 1, "emitted blrl drains a trampoline the dispatched call left pending");
    CHECK(g_trampoline_fn == nullptr, "emitted blrl clears the pending trampoline after draining");
}}

int main() {{
    test_lmw_stmw();
    test_dform_base();
    test_fma_family();
    test_blrl();
    printf("\nResults: %s (%d failure(s))\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}}
"""


def build_and_run(emitted: dict, cxx: str) -> int:
    src = HARNESS_TEMPLATE.format(
        ppu_context_h=os.path.join(RUNTIME_PPU, "ppu_context.h"),
        ppu_memory_h=os.path.join(RUNTIME_PPU, "ppu_memory.h"),
        ppu_ops_h=os.path.join(RUNTIME_PPU, "ppu_ops.h"),
        fp_helpers=extract_fp_helpers(),
        LMW=emitted["lmw"], STMW=emitted["stmw"],
        LMW_RA0=emitted["lmw_ra0"], STMW_RA0=emitted["stmw_ra0"],
        LWZ=emitted["lwz"], LWZ_RA0=emitted["lwz_ra0"],
        STW=emitted["stw"], STW_RA0=emitted["stw_ra0"],
        LWA=emitted["lwa"], LWA_RA0=emitted["lwa_ra0"],
        LFS=emitted["lfs"], LFS_RA0=emitted["lfs_ra0"],
        LFD=emitted["lfd"], LFD_RA0=emitted["lfd_ra0"],
        STFS=emitted["stfs"], STFS_RA0=emitted["stfs_ra0"],
        STFD=emitted["stfd"], STFD_RA0=emitted["stfd_ra0"],
        FMADD=emitted["fmadd"], FMADDS=emitted["fmadds"],
        FMSUB=emitted["fmsub"], FMSUBS=emitted["fmsubs"],
        FNMADD=emitted["fnmadd"], FNMADDS=emitted["fnmadds"],
        FNMSUB=emitted["fnmsub"], FNMSUBS=emitted["fnmsubs"],
        BLRL=emitted["blrl"],
    )
    tmp = tempfile.mkdtemp(prefix="ppu_emit_")
    try:
        cpp = os.path.join(tmp, "harness.cpp")
        exe = os.path.join(tmp, "harness")
        with open(cpp, "w") as f:
            f.write(src)
        cmd = [cxx, "-std=c++17", "-O0", "-ffp-contract=off", cpp, "-o", exe, "-lm"]
        cp = subprocess.run(cmd, capture_output=True, text=True)
        if cp.returncode != 0:
            print("[FAIL] harness did not compile:")
            print(cp.stderr)
            return 1
        run = subprocess.run([exe], capture_output=True, text=True)
        sys.stdout.write(run.stdout)
        if run.stderr:
            sys.stderr.write(run.stderr)
        return run.returncode
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def pick_cxx(argv) -> str:
    for i, a in enumerate(argv):
        if a == "--cxx" and i + 1 < len(argv):
            return argv[i + 1]
    if os.environ.get("CXX"):
        return os.environ["CXX"]
    for cand in ("c++", "clang++", "g++"):
        if shutil.which(cand):
            return cand
    return "c++"


# ---------------------------------------------------------------------------
# 3) Preamble emission goldens (Fase 1, criterio 1 do ROADMAP) + prova por
#    mutacao (D-07): cada uma das quatro emissoes que o build consome tem um
#    check de presenca E uma mutacao automatizada que prova que remove-la
#    derruba o check correspondente. Nunca no-op: reforca-se aqui, nao so na
#    intencao, porque um "PASS" que mutation-testing nao consegue provar que
#    falha e' o modo de falha que esta fase existe para fechar.
# ---------------------------------------------------------------------------

def assert_mutation_breaks(label: str, mutate, check) -> None:
    """Prova por mutacao (D-07): `mutate()` devolve um estado mutado (nao
    escreve em disco); `check(mutated)` tem de FALHAR (levantar AssertionError
    ou devolver um valor falsy) contra esse estado. Se a checagem nao falhar
    sob mutacao, a mutacao e' um no-op relativamente ao que se esta a provar --
    e isso e' um erro do teste, nao um PASS.
    Imprime uma linha PASS quando a mutacao e' detectada e levanta
    (SystemExit) quando passa despercebida."""
    mutated = mutate()
    broke = False
    try:
        result = check(mutated)
        broke = not result  # check() devolveu False/None: falhou "suavemente"
    except AssertionError:
        broke = True
    if not broke:
        raise SystemExit(
            f"\n[FAIL] mutacao '{label}' passou despercebida -- o check "
            f"correspondente nao morde. Prova por mutacao (D-07) invalidada.")
    print(f"[PASS] mutacao '{label}': removida a emissao, o check correspondente falhou como esperado")


def check_ppu_stubs_emission() -> None:
    """Emissao 3 do criterio 1 (D-10 enumera 4 no total): geracao de
    ppu_stubs.cpp. Restaurada nesta tarefa a partir do commit e52da4a
    (fork), apagado pelo 11a1c3c ao substituir o arquivo pelo do upstream."""
    # Teste 1: um alvo sintetico so' em call_targets, nenhuma funcao liftada
    # -- emit_fallback_stubs() tem de devolver o thunk completo.
    l1 = PPULifter()
    target = 0x00ABCDEF
    l1.call_targets.add(target)
    stub_src = l1.emit_fallback_stubs()
    assert stub_src is not None, "emit_fallback_stubs() devolveu None com um alvo indefinido pendente"
    assert f"func_{target:08X}" in stub_src, "thunk do alvo sintetico ausente do ppu_stubs.cpp emitido"
    assert "void ppu_unlifted_stub(uint64_t addr, ppu_context* ctx);" in stub_src, \
        "declaracao de ppu_unlifted_stub ausente do ppu_stubs.cpp emitido"
    assert f"ppu_unlifted_stub(0x{target:08X}ULL, ctx);" in stub_src, \
        "chamada a ppu_unlifted_stub com o endereco do alvo ausente"
    print("[PASS] emissao ppu_stubs (Teste 1): alvo indefinido produz thunk + declaracao + chamada")

    # Teste 2: o mesmo alvo, agora TAMBEM liftado -- lift completo, sem stub.
    l2 = PPULifter()
    l2.call_targets.add(target)
    l2.functions.append(LiftedFunction(name=f"func_{target:08X}", start_addr=target,
                                        end_addr=target + 4))
    assert l2.emit_fallback_stubs() is None, \
        "emit_fallback_stubs() deveria devolver None quando todo alvo referenciado esta liftado"
    print("[PASS] emissao ppu_stubs (Teste 2): lift completo nao gera stub (emit_fallback_stubs() is None)")

    # Teste 4: undefined_targets() ordenado, deduplicado, uniao dos dois
    # conjuntos (mesmo alvo em call_targets E branch_targets conta uma vez).
    l4 = PPULifter()
    l4.call_targets.update({0x2000, 0x1000})
    l4.branch_targets.update({0x1000, 0x3000})
    assert l4.undefined_targets() == [0x1000, 0x2000, 0x3000], \
        f"undefined_targets() devolveu {l4.undefined_targets()!r}, esperado [0x1000, 0x2000, 0x3000]"
    print("[PASS] emissao ppu_stubs (Teste 4): undefined_targets() ordenado e deduplicado sobre a uniao")


def check_ppu_stubs_mutation() -> None:
    """Teste 3 (mutacao, D-07): com PPULifter.emit_fallback_stubs
    temporariamente substituido por um stub que devolve sempre None, o
    Teste 1 (alvo indefinido -> thunk emitido) tem de FALHAR. O atributo
    original e' restaurado num finally, nunca escrevendo em disco."""
    target = 0x00ABCDEF
    original = PPULifter.emit_fallback_stubs
    try:
        def check(_mutated) -> bool:
            PPULifter.emit_fallback_stubs = lambda self, header_name="ppu_recomp.h": None
            try:
                l = PPULifter()
                l.call_targets.add(target)
                stub_src = l.emit_fallback_stubs()
                return stub_src is not None and f"func_{target:08X}" in stub_src
            finally:
                PPULifter.emit_fallback_stubs = original
        assert_mutation_breaks(
            "emit_fallback_stubs suprimido (sempre devolve None)",
            mutate=lambda: None,
            check=check,
        )
    finally:
        PPULifter.emit_fallback_stubs = original


# Emissoes 1/2/4 (stdlib.h, ramo __APPLE__ do TLS, contrato ppu_timebase_now):
# checks de presenca no SOURCE_PREAMBLE ja importado + mutacoes em copias em
# memoria. Nunca tocam em tools/ppu_lifter.py no disco.
NEEDLE_STDLIB = "#include <stdlib.h>"
NEEDLE_APPLE_TLS = (
    "#elif defined(__APPLE__)\n"
    "extern \"C\" thread_local void (*g_trampoline_fn)(void*);"
)
NEEDLE_TIMEBASE_DECL = "extern \"C\" uint64_t ppu_timebase_now(void);"


def check_preamble_needle(preamble: str, needle: str) -> bool:
    return needle in preamble


def check_preamble_emissions() -> None:
    """Emissoes 1, 2 e 4 (metade da declaracao) do criterio 1: presenca
    literal no SOURCE_PREAMBLE ja importado no topo do arquivo."""
    assert check_preamble_needle(SOURCE_PREAMBLE, NEEDLE_STDLIB), \
        "emissao 1 (#include <stdlib.h>) ausente do SOURCE_PREAMBLE"
    print("[PASS] emissao 1: #include <stdlib.h> presente no SOURCE_PREAMBLE")

    assert check_preamble_needle(SOURCE_PREAMBLE, NEEDLE_APPLE_TLS), \
        "emissao 2 (ramo __APPLE__ do TLS de g_trampoline_fn) ausente do SOURCE_PREAMBLE"
    print("[PASS] emissao 2: ramo #elif defined(__APPLE__) do TLS de g_trampoline_fn presente")

    assert check_preamble_needle(SOURCE_PREAMBLE, NEEDLE_TIMEBASE_DECL), \
        "emissao 4 (declaracao extern de ppu_timebase_now) ausente do SOURCE_PREAMBLE"
    print("[PASS] emissao 4 (declaracao): extern \"C\" uint64_t ppu_timebase_now(void); presente no preambulo")


def check_timebase_definer(text: str) -> bool:
    """Metade 'o definidor existe' de D-10. Descarta linhas de comentario
    ANTES de procurar a assinatura -- o proprio bloco de comentario de
    sys_timer.c cita 'ppu_timebase_now' varias vezes em prosa, entao uma
    checagem de substring ingenua passaria mesmo com a definicao apagada."""
    code_lines = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("*") or stripped.startswith("/*") or stripped.startswith("//"):
            continue
        code_lines.append(line)
    code_only = "\n".join(code_lines)
    return "uint64_t ppu_timebase_now(void)" in code_only


def check_timebase_definer_present() -> None:
    """Metade 'o definidor existe' de D-10, contra o arquivo real no disco."""
    sys_timer_path = os.path.join(REPO, "runtime", "syscalls", "sys_timer.c")
    with open(sys_timer_path, "r") as f:
        text = f.read()
    assert check_timebase_definer(text), (
        f"definidor 'uint64_t ppu_timebase_now(void)' ausente (fora de comentario) "
        f"de {sys_timer_path}")
    print("[PASS] emissao 4 (definidor): uint64_t ppu_timebase_now(void) definido em "
          "runtime/syscalls/sys_timer.c, fora de bloco de comentario")

    # Assercao do filtro anti-auto-invalidacao (task 2, acceptance criteria):
    # um texto que contenha SOMENTE o bloco de comentario (sem a definicao)
    # tem de FALHAR o check -- prova de que o filtro de comentario funciona.
    comment_only = (
        "/* ---------------------------------------------------------------\n"
        " * ppu_timebase_now -- o nome por que o LIFTER chama isto.\n"
        " * ppu_lifter.py declara extern \"C\" uint64_t ppu_timebase_now(void);\n"
        " * no preambulo e emite mftb/mftbu como ppu_timebase_now().\n"
        " * -------------------------------------------------------------*/\n"
    )
    assert not check_timebase_definer(comment_only), (
        "check_timebase_definer() passou contra um texto que so' tem o bloco de "
        "comentario -- o filtro anti-auto-invalidacao nao esta a funcionar")
    print("[PASS] filtro anti-auto-invalidacao: texto so'-comentario nao satisfaz o check do definidor")


def check_preamble_mutations() -> None:
    """As tres mutacoes do preambulo (D-07): remover cada agulha de uma COPIA
    em memoria de SOURCE_PREAMBLE (ou do texto de sys_timer.c) e exigir que o
    check correspondente falhe contra a copia. Nada e' escrito em disco."""
    assert_mutation_breaks(
        "stdlib.h removido do preambulo (copia em memoria)",
        mutate=lambda: SOURCE_PREAMBLE.replace(NEEDLE_STDLIB, ""),
        check=lambda mutated: check_preamble_needle(mutated, NEEDLE_STDLIB),
    )
    assert_mutation_breaks(
        "ramo __APPLE__ do TLS removido do preambulo (copia em memoria)",
        mutate=lambda: SOURCE_PREAMBLE.replace(NEEDLE_APPLE_TLS, ""),
        check=lambda mutated: check_preamble_needle(mutated, NEEDLE_APPLE_TLS),
    )

    # O contrato ppu_timebase_now tem DOIS lados e cada um parte o build de
    # maneira diferente: sem a DECLARACAO no preambulo os chunks nem compilam;
    # sem o DEFINIDOR compilam e so' o link e' que rebenta -- foi assim que
    # apareceu (2026-07-26): um lift regenerado deu
    #   Undefined symbols: "_ppu_timebase_now", referenced from func_00195D48...
    # porque o lifter emitia o nome ppu_* e o runtime so' definia ps3_*.
    # Mutar so' um dos lados deixaria o outro por provar, e o criterio 4 da
    # fase pede as emissoes removidas UMA A UMA.
    assert_mutation_breaks(
        "declaracao de ppu_timebase_now removida do preambulo (copia em memoria)",
        mutate=lambda: SOURCE_PREAMBLE.replace(NEEDLE_TIMEBASE_DECL, ""),
        check=lambda mutated: check_preamble_needle(mutated, NEEDLE_TIMEBASE_DECL),
    )

    sys_timer_path = os.path.join(REPO, "runtime", "syscalls", "sys_timer.c")
    with open(sys_timer_path, "r") as f:
        real_sys_timer_text = f.read()
    assert_mutation_breaks(
        "assinatura do definidor de ppu_timebase_now removida (copia em memoria de sys_timer.c)",
        mutate=lambda: real_sys_timer_text.replace("uint64_t ppu_timebase_now(void)", ""),
        check=check_timebase_definer,
    )


# ---------------------------------------------------------------------------
# 4) Instrumentacao OOB dos quatro ppu_res_* atomicos (Fase 1, criterio 3,
#    D-01..D-04): lwarx/stwcx/ldarx/stdcx emitidos pelo lifter tem cada um de
#    chamar ps3_mem_oob_report em acesso fora dos limites, com o par (len,
#    op) estatico de D-03 -- report-only (D-04), nunca abortar, e aditivo:
#    o caminho dentro-dos-limites nao pode mudar (a Task 2 prova isso em
#    execucao com um harness compilado).
# ---------------------------------------------------------------------------
NEEDLE_PPU_VM_SIZE_DECL = 'extern "C" uint32_t ppu_vm_size;'
NEEDLE_OOB_REPORT_DECL = (
    'extern "C" void ps3_mem_oob_report(uint32_t ea, uint32_t len, '
    'const char* op, void* caller_ra);'
)

PPU_RES_ORDER = ["ppu_res_lwarx", "ppu_res_stwcx", "ppu_res_ldarx", "ppu_res_stdcx"]
PPU_RES_EXPECTED = {
    "ppu_res_lwarx": ("4", "read"),
    "ppu_res_stwcx": ("4", "write"),
    "ppu_res_ldarx": ("8", "read"),
    "ppu_res_stdcx": ("8", "write"),
}
PPU_RES_DISPATCH_NEEDLE = "/* Indirect call dispatch"


def extract_ppu_res_bodies(preamble: str) -> dict:
    """Fatia cada um dos quatro ppu_res_* do texto dado por indice de duas
    agulhas (mesma tecnica de extract_fp_helpers()): do nome da funcao ATUAL
    ate' o nome da PROXIMA funcao da lista, ou -- para a ultima -- ate' o
    comentario que abre o dispatch de chamada indireta (SOURCE_PREAMBLE
    completo) ou o fim do texto (a fatia extraida para compilacao termina
    antes desse comentario, entao nao o contem)."""
    bodies = {}
    for i, name in enumerate(PPU_RES_ORDER):
        start = preamble.index(name + "(ppu_context*")
        if i + 1 < len(PPU_RES_ORDER):
            stop = preamble.index(PPU_RES_ORDER[i + 1] + "(ppu_context*")
        else:
            try:
                stop = preamble.index(PPU_RES_DISPATCH_NEEDLE)
            except ValueError:
                stop = len(preamble)
        bodies[name] = preamble[start:stop]
    return bodies


def check_ppu_res_presence(preamble: str) -> bool:
    """Teste 1: o helper de bounds check e as duas declaracoes novas estao
    presentes no texto dado."""
    return (
        "ppu_res_oob" in preamble
        and NEEDLE_PPU_VM_SIZE_DECL in preamble
        and NEEDLE_OOB_REPORT_DECL in preamble
    )


def check_ppu_res_matrix(preamble: str) -> bool:
    """Teste 2 (matriz D-03): cada um dos quatro corpos tem EXATAMENTE uma
    chamada a ps3_mem_oob_report, com o par (len, op) correto."""
    try:
        bodies = extract_ppu_res_bodies(preamble)
    except ValueError:
        return False
    for name in PPU_RES_ORDER:
        calls = re.findall(r"ps3_mem_oob_report\(([^;]*)\)\s*;", bodies[name])
        if len(calls) != 1:
            return False
        length, op = PPU_RES_EXPECTED[name]
        if (", " + length + ",") not in calls[0]:
            return False
        if (chr(34) + op + chr(34)) not in calls[0]:
            return False
    return True


def check_ppu_res_instrumentation_present() -> None:
    """Testes 1 e 2 contra o SOURCE_PREAMBLE real ja importado."""
    assert check_ppu_res_presence(SOURCE_PREAMBLE), (
        "instrumentacao OOB dos ppu_res_* ausente: faltou ppu_res_oob, a "
        "declaracao de ppu_vm_size ou a de ps3_mem_oob_report no SOURCE_PREAMBLE")
    print("[PASS] instrumentacao ppu_res_*: helper ppu_res_oob + declaracoes "
          "extern de ppu_vm_size/ps3_mem_oob_report presentes no SOURCE_PREAMBLE")

    assert check_ppu_res_matrix(SOURCE_PREAMBLE), (
        "matriz len/op (D-03) incorreta em um dos quatro ppu_res_*: cada corpo "
        "tem de ter exatamente uma chamada a ps3_mem_oob_report com o (len, op) certo")
    print("[PASS] matriz len/op D-03: lwarx=(4,read) stwcx=(4,write) "
          "ldarx=(8,read) stdcx=(8,write), uma chamada de reporte por corpo")


def check_ppu_res_mutation() -> None:
    """Teste 3 (mutacao, D-07/D-08): remover a chamada de reporte de QUALQUER
    um dos quatro corpos (copia em memoria do SOURCE_PREAMBLE) tem de derrubar
    a matriz D-03. Nada e' escrito em disco."""
    for name in PPU_RES_ORDER:
        def mutate(target=name):
            bodies = extract_ppu_res_bodies(SOURCE_PREAMBLE)
            body = bodies[target]
            calls = re.findall(r"ps3_mem_oob_report\([^;]*\)\s*;", body)
            assert len(calls) == 1, (
                f"{target}: esperava 1 chamada de reporte para mutar, achou {len(calls)}")
            mutated_body = body.replace(calls[0], "", 1)
            return SOURCE_PREAMBLE.replace(body, mutated_body, 1)
        assert_mutation_breaks(
            f"chamada de reporte removida de {name} (copia em memoria)",
            mutate=mutate,
            check=check_ppu_res_matrix,
        )


# ---------------------------------------------------------------------------
# 5) Compilar e rodar a fatia dos atomicos (Fase 1, criterio 3, Task 2):
#    prova em EXECUCAO -- nao so' por texto -- que o caminho dentro-dos-
#    limites permanece bit-a-bit igual (aditivo) e que os quatro caminhos
#    OOB reportam sem abortar (D-04), com o par (len, op) certo (D-03)
#    observado por um mock do reporter em tempo de execucao.
# ---------------------------------------------------------------------------

def extract_ppu_res_slice() -> str:
    """Fatia o bloco novo de instrumentacao + os quatro atomicos VERBATIM do
    SOURCE_PREAMBLE (mesma tecnica de extract_fp_helpers()): da declaracao de
    ppu_vm_size (primeira agulha do bloco novo, D-01) ate' o comentario que
    abre o dispatch de chamada indireta. Contem, e so' contem: as duas
    forward-declarations, a macro PPU_RES_RA(), ppu_res_oob(), os dois
    helpers de bswap e os quatro atomicos -- nada re-digitado."""
    start = SOURCE_PREAMBLE.index(NEEDLE_PPU_VM_SIZE_DECL)
    end = SOURCE_PREAMBLE.index(PPU_RES_DISPATCH_NEEDLE)
    return SOURCE_PREAMBLE[start:end].strip()


ATOMICS_BUF_SIZE = 64

ATOMICS_HARNESS_TEMPLATE = r"""
/* AUTO-GENERATED by test_ppu_lifter_emit.py -- do not edit. The body below
 * is the LITERAL slice tools/ppu_lifter.py's SOURCE_PREAMBLE produces for
 * the OOB instrumentation of the four ppu_res_* atomics (Fase 1, criterio
 * 3, D-01..D-04), so what is compiled and run here is exactly what the
 * lifter ships into every generated .cpp -- no re-typed parallel copy. */
#include "{ppu_context_h}"

#include <cstdint>
#include <cstdio>
#include <cstring>

/* Backing store for the guest address space, sized to ppu_vm_size below. */
static uint8_t g_atomics_mem[{buf_size}];
extern "C" uint8_t* vm_base = g_atomics_mem;
extern "C" uint32_t ppu_vm_size = sizeof(g_atomics_mem);

/* Mock of the reporter the instrumentation calls on an OOB access: records
 * the call count and the last (ea, len, op) seen, so the matrix (D-03) and
 * report-only behaviour (D-04) can be asserted at RUNTIME, not just by
 * text. */
static int      g_oob_count = 0;
static uint32_t g_oob_last_ea = 0;
static uint32_t g_oob_last_len = 0;
static char     g_oob_last_op[8] = {{0}};
extern "C" void ps3_mem_oob_report(uint32_t ea, uint32_t len, const char* op, void* /*caller_ra*/) {{
    g_oob_count++;
    g_oob_last_ea = ea;
    g_oob_last_len = len;
    std::strncpy(g_oob_last_op, op, sizeof(g_oob_last_op) - 1);
}}

/* ---- LITERAL slice of tools/ppu_lifter.py's SOURCE_PREAMBLE ---- */
{ppu_res_slice}

static int fails = 0;
#define CHECK(c, msg) do {{ if (c) printf("[PASS] %s\n", msg); \
    else {{ printf("[FAIL] %s\n", msg); fails++; }} }} while (0)

static void reset_oob() {{
    g_oob_count = 0; g_oob_last_ea = 0; g_oob_last_len = 0; g_oob_last_op[0] = 0;
}}

/* ------------------------------------------------- controle dentro dos limites */
static void test_in_bounds_control() {{
    printf("-- ppu_res_* dentro dos limites (controle, sem reporte) --\n");
    memset(g_atomics_mem, 0, sizeof(g_atomics_mem));
    reset_oob();

    ppu_context ctx; memset(&ctx, 0, sizeof(ctx));
    const uint32_t EA = 0x10;
    uint32_t loaded = ppu_res_lwarx(&ctx, EA);
    CHECK(g_oob_count == 0, "lwarx dentro dos limites: nenhum reporte");
    CHECK(ctx.reserve_valid == 1, "lwarx dentro dos limites: reserva marcada valida");
    CHECK(loaded == 0, "lwarx dentro dos limites: le' o valor real (zero, memoria zerada)");

    ppu_res_stwcx(&ctx, EA, 0xCAFEBABEu);
    CHECK(g_oob_count == 0, "stwcx dentro dos limites: nenhum reporte");
    CHECK(((ctx.cr >> 28) & 0xF) == 2u, "stwcx dentro dos limites: CAS com sucesso, CR0[EQ] ligado");
    uint32_t raw;
    memcpy(&raw, g_atomics_mem + EA, 4);
    CHECK(ppu_res_bswap32(raw) == 0xCAFEBABEu,
          "stwcx dentro dos limites: memoria guest carrega o valor novo em big-endian");
}}

/* ------------------------------------------------------- carga fora dos limites */
static void test_load_oob() {{
    printf("-- lwarx/ldarx fora dos limites (report-only) --\n");
    ppu_context ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.reserve_valid = 1; ctx.reserve_addr = 0x10;

    reset_oob();
    uint32_t v32 = ppu_res_lwarx(&ctx, ppu_vm_size);
    CHECK(v32 == 0, "lwarx OOB: devolve 0");
    CHECK(ctx.reserve_valid == 0, "lwarx OOB: limpa a reserva");
    CHECK(g_oob_count == 1, "lwarx OOB: reporte incrementou o contador (nao abortou)");
    CHECK(g_oob_last_len == 4, "lwarx OOB: len reportado == 4");
    CHECK(strcmp(g_oob_last_op, "read") == 0, "lwarx OOB: op reportada == read");

    ctx.reserve_valid = 1; ctx.reserve_addr = 0x10;
    reset_oob();
    uint64_t v64 = ppu_res_ldarx(&ctx, ppu_vm_size);
    CHECK(v64 == 0, "ldarx OOB: devolve 0");
    CHECK(ctx.reserve_valid == 0, "ldarx OOB: limpa a reserva");
    CHECK(g_oob_count == 1, "ldarx OOB: reporte incrementou o contador (nao abortou)");
    CHECK(g_oob_last_len == 8, "ldarx OOB: len reportado == 8");
    CHECK(strcmp(g_oob_last_op, "read") == 0, "ldarx OOB: op reportada == read");
}}

/* --------------------------------------------- store-conditional fora dos limites */
static void test_store_oob() {{
    printf("-- stwcx/stdcx fora dos limites (report-only, CAS NAO executa) --\n");
    ppu_context ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.reserve_valid = 1;
    ctx.reserve_addr  = ppu_vm_size;   /* reserva casando o endereco OOB */
    ctx.reserve_value = 0;
    ctx.cr = 0xFFFFFFFFu;               /* poison: prova que CR0 e' de facto zerado */

    reset_oob();
    ppu_res_stwcx(&ctx, ppu_vm_size, 0x11111111u);
    CHECK(g_oob_count == 1, "stwcx OOB: reporte incrementou o contador (nao abortou)");
    CHECK(g_oob_last_len == 4, "stwcx OOB: len reportado == 4");
    CHECK(strcmp(g_oob_last_op, "write") == 0, "stwcx OOB: op reportada == write");
    CHECK(((ctx.cr >> 28) & 0xF) == 0u, "stwcx OOB: CR0[EQ] fica em zero (CAS nao rodou)");
    CHECK(ctx.reserve_valid == 0, "stwcx OOB: reserva limpa apos a tentativa");

    ppu_context ctx2; memset(&ctx2, 0, sizeof(ctx2));
    ctx2.reserve_valid = 1;
    ctx2.reserve_addr  = ppu_vm_size;
    ctx2.reserve_value = 0;
    ctx2.cr = 0xFFFFFFFFu;

    reset_oob();
    ppu_res_stdcx(&ctx2, ppu_vm_size, 0x2222222222222222ull);
    CHECK(g_oob_count == 1, "stdcx OOB: reporte incrementou o contador (nao abortou)");
    CHECK(g_oob_last_len == 8, "stdcx OOB: len reportado == 8");
    CHECK(strcmp(g_oob_last_op, "write") == 0, "stdcx OOB: op reportada == write");
    CHECK(((ctx2.cr >> 28) & 0xF) == 0u, "stdcx OOB: CR0[EQ] fica em zero (CAS nao rodou)");
    CHECK(ctx2.reserve_valid == 0, "stdcx OOB: reserva limpa apos a tentativa");
}}

/* --------------------------------------------------------------- fronteira exata */
static void test_exact_boundary() {{
    printf("-- fronteira exata: > (nao >=) e soma promovida a 64 bits --\n");
    ppu_context ctx; memset(&ctx, 0, sizeof(ctx));

    reset_oob();
    uint32_t ea_end = ppu_vm_size - 4;   /* termina exatamente no ultimo byte */
    ppu_res_lwarx(&ctx, ea_end);
    CHECK(g_oob_count == 0, "lwarx terminando exatamente no ultimo byte: NAO reporta");

    reset_oob();
    uint32_t ea_over = ppu_vm_size - 3;  /* comeca um byte antes do fim: 1 byte alem */
    ppu_res_lwarx(&ctx, ea_over);
    CHECK(g_oob_count == 1, "lwarx comecando um byte antes do fim: REPORTA (1 byte alem)");
}}

int main() {{
    test_in_bounds_control();
    test_load_oob();
    test_store_oob();
    test_exact_boundary();
    printf("\nResults: %s (%d failure(s))\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}}
"""


def build_and_run_atomics(cxx: str) -> int:
    src = ATOMICS_HARNESS_TEMPLATE.format(
        ppu_context_h=os.path.join(RUNTIME_PPU, "ppu_context.h"),
        buf_size=ATOMICS_BUF_SIZE,
        ppu_res_slice=extract_ppu_res_slice(),
    )
    tmp = tempfile.mkdtemp(prefix="ppu_res_atomics_")
    try:
        cpp = os.path.join(tmp, "harness_atomics.cpp")
        exe = os.path.join(tmp, "harness_atomics")
        with open(cpp, "w") as f:
            f.write(src)
        cmd = [cxx, "-std=c++17", "-O0", "-ffp-contract=off", cpp, "-o", exe, "-lm"]
        cp = subprocess.run(cmd, capture_output=True, text=True)
        if cp.returncode != 0:
            print("[FAIL] harness dos atomicos ppu_res_* nao compilou:")
            print(cp.stderr)
            return 1
        run = subprocess.run([exe], capture_output=True, text=True)
        sys.stdout.write(run.stdout)
        if run.stderr:
            sys.stderr.write(run.stderr)
        return run.returncode
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def check_ppu_res_slice_mutation() -> None:
    """Mutacao (Task 2, passo 5): remover a chamada de reporte de um dos
    quatro corpos numa copia em memoria da FATIA extraida para compilacao
    (nao do SOURCE_PREAMBLE inteiro) tem de derrubar a matriz D-03. A fatia
    contem os mesmos quatro corpos, entao check_ppu_res_matrix() se aplica
    diretamente. Nada e' escrito em disco."""
    slice_text = extract_ppu_res_slice()
    name = "ppu_res_stwcx"  # qualquer um dos quatro serve; prova a fatia, nao repete os 4

    def mutate():
        bodies = extract_ppu_res_bodies(slice_text)
        body = bodies[name]
        calls = re.findall(r"ps3_mem_oob_report\([^;]*\)\s*;", body)
        assert len(calls) == 1, (
            f"{name}: esperava 1 chamada de reporte para mutar na fatia, achou {len(calls)}")
        mutated_body = body.replace(calls[0], "", 1)
        return slice_text.replace(body, mutated_body, 1)

    assert_mutation_breaks(
        f"chamada de reporte removida de {name} na FATIA extraida para compilacao (copia em memoria)",
        mutate=mutate,
        check=check_ppu_res_matrix,
    )


# ---------------------------------------------------------------------------
# 6) Gate G4 (Fase 1, criterio 4 do ROADMAP): nenhum token proibido pelo G4
#    pode nascer FORA de uma guarda MSVC/Windows no que tools/ppu_lifter.py
#    pode emitir para o alvo POSIX. Varre o ARQUIVO INTEIRO (nao so' o
#    SOURCE_PREAMBLE): D-09 fala de "tudo que o lifter pode emitir", e as
#    diretivas tambem aparecem como payload de string Python
#    (lines.append("#if ...")), nao so' como linhas literais dentro das
#    constantes triplo-aspeadas -- o rastreador tem de ver as duas formas.
#
#    Guarda aceita = _MSC_VER OU _WIN32 (nao so' _WIN32): as tres ocorrencias
#    reais hoje (__declspec(align(16)) em ~:67, #include <intrin.h> em
#    ~:237, __declspec(thread) em ~:413) estao sob _MSC_VER, nao _WIN32. Um
#    gate que so' aceitasse _WIN32 falharia no dia um contra codigo ja
#    correto (Pitfall 2 da 01-RESEARCH.md).
#
#    Rastreio de estado de guarda POR RAMO, nao por bloco: if/ifdef/ifndef
#    empurram a condicao para uma pilha; elif/else SUBSTITUEM o topo (e' o
#    que torna o rastreio por-ramo -- o ramo #elif defined(__APPLE__) da TLS
#    de g_trampoline_fn esta dentro do MESMO #if que o ramo _MSC_VER, mas
#    NAO esta guardado por _MSC_VER; um rastreador por-bloco daria falso
#    negativo se um token proibido aparecesse ali); endif desempilha.
#
#    LIMITACAO DECLARADA (D-09): esta varredura NAO pega token injetado por
#    um patch_*.py sobre a saida de um lift real -- essa cobertura exige
#    EBOOT e lift e fica para uma fase posterior (Fase 3/4). A lacuna e'
#    consciente, nao silenciosa.
# ---------------------------------------------------------------------------
G4_FORBIDDEN_TOKENS = ("<intrin.h>", "Sleep(", "__declspec", "GetModuleHandleA")
G4_GUARD_MACROS = ("_MSC_VER", "_WIN32")

_G4_DIRECTIVE_RE = re.compile(r'#(if|ifdef|ifndef|elif|else|endif)\b')


def _g4_scan(text: str, source_label: str) -> tuple:
    """Varre `text` linha a linha com rastreio de estado de guarda POR RAMO.
    Devolve (violations, diagnostics, residual_depth). `violations` e' uma
    lista de (numero_de_linha, token, trecho_da_linha). `diagnostics` regista
    diretiva desbalanceada (nunca levanta -- e' tratado como reset, nunca
    como excecao que derruba o teste por uma razao errada)."""
    stack = []
    violations = []
    diagnostics = []

    for lineno, raw_line in enumerate(text.splitlines(), start=1):
        stripped = raw_line.strip()
        if not stripped:
            continue

        m = _G4_DIRECTIVE_RE.search(stripped)
        is_directive = False
        if m:
            start = m.start()
            preceding = stripped[start - 1] if start > 0 else None
            # "#" no inicio da linha (ja' strip()ada) OU logo apos aspa --
            # descarta comentario Python tipo "# if it falls off the end"
            # (":482" no plano; a linha real hoje tem "# if" COM espaco, que
            # o "#if" sem espaco do regex ja' nao casa de forma alguma).
            if start == 0 or preceding in ('"', "'"):
                is_directive = True

        if is_directive:
            kind = m.group(1)
            rest = stripped[m.end():]
            quote_pos = None
            for qc in ('"', "'"):
                p = rest.find(qc)
                if p != -1 and (quote_pos is None or p < quote_pos):
                    quote_pos = p
            condition = rest[:quote_pos] if quote_pos is not None else rest

            if kind in ("if", "ifdef", "ifndef"):
                stack.append(condition)
            elif kind in ("elif", "else"):
                if stack:
                    stack[-1] = condition
                else:
                    diagnostics.append(
                        f"{source_label}:{lineno}: '#{kind}' sem '#if' na pilha -- tratado como reset")
                    stack.append(condition)
            elif kind == "endif":
                if stack:
                    stack.pop()
                else:
                    diagnostics.append(
                        f"{source_label}:{lineno}: '#endif' com pilha vazia -- tratado como reset")
            continue  # uma linha de diretiva nunca e' avaliada contra tokens

        guarded = any(macro in cond for cond in stack for macro in G4_GUARD_MACROS)
        if guarded:
            continue

        for token in G4_FORBIDDEN_TOKENS:
            if token in raw_line:
                violations.append((lineno, token, raw_line.strip()))

    return violations, diagnostics, len(stack)


def check_g4_gate() -> None:
    """G4 contra o tools/ppu_lifter.py REAL no disco."""
    lifter_path = os.path.join(TOOLS, "ppu_lifter.py")
    with open(lifter_path, "r") as f:
        text = f.read()
    violations, diagnostics, residual = _g4_scan(text, "tools/ppu_lifter.py")

    for diag in diagnostics:
        print(f"[DIAG] G4: {diag}")

    assert not violations, (
        "G4: token(s) proibido(s) fora de guarda _MSC_VER/_WIN32 em tools/ppu_lifter.py -- "
        + "; ".join(f"linha {ln}: '{tok}' ({snippet})" for ln, tok, snippet in violations))
    assert residual == 0, (
        f"G4: profundidade residual de pilha = {residual} ao fim de tools/ppu_lifter.py "
        f"(esperado 0) -- o reconhecedor de diretiva do proprio teste esta a casar algo "
        f"que nao e' diretiva; corrigir o TESTE, nunca o ppu_lifter.py")
    print(f"[PASS] G4: nenhum token proibido fora de guarda _MSC_VER/_WIN32 em "
          f"tools/ppu_lifter.py ({len(diagnostics)} diagnostico(s) de subfluxo, "
          f"profundidade residual = {residual})")


def check_g4_negative_control() -> None:
    """Controle negativo (obrigatorio em todo run, disciplina D-07): injeta,
    FORA de qualquer condicional, uma linha com um token proibido numa copia
    EM MEMORIA do texto do lifter -- nada e' escrito em disco -- e exige que
    o varredor reporte EXATAMENTE uma violacao nessa linha. Sem isto o gate
    G4 poderia estar verde por nunca disparar."""
    lifter_path = os.path.join(TOOLS, "ppu_lifter.py")
    with open(lifter_path, "r") as f:
        real_text = f.read()
    mutated = real_text + "\nSleep(1000);  /* controle negativo G4 -- nunca escrito em disco */\n"
    violations, _diagnostics, residual = _g4_scan(mutated, "tools/ppu_lifter.py(mutado em memoria)")
    assert residual == 0, (
        f"controle negativo G4: profundidade residual = {residual} contra o texto mutado "
        f"(deveria ser 0, igual ao texto real -- a mutacao nao deveria desbalancear diretivas)")
    assert len(violations) == 1, (
        f"controle negativo G4 esperava exatamente 1 violacao, achou {len(violations)}: {violations}")
    assert violations[0][1] == "Sleep(", (
        f"controle negativo G4 detectou o token errado: {violations[0]!r}")
    print("[PASS] controle negativo G4: token 'Sleep(' injetado fora de guarda foi "
          "detectado (1 violacao) -- o gate tem dentes provados")


# ---------------------------------------------------------------------------
# 7) Gate de sincronizacao do build_macos.sh (Fase 1, criterio 1, D-05/D-06).
#
#    METADE ANCORA (sempre roda, PODE FALHAR): games/gow2/build_macos.sh (o
#    checkout presente em CI e em clone limpo) tem de existir e referenciar
#    ppu_stubs.cpp em pelo menos PPU_STUBS_MIN_SITES sitios distintos. Sem
#    isso a restauracao de ppu_stubs.cpp no plano 01-01 estaria a pinar um
#    contrato que nenhum consumidor exercita.
#
#    METADE SINCRONIZACAO (WARN-only, NUNCA FALHA -- D-06): compara com o
#    checkout irmao ../gow2-recomp/build_macos.sh (fora da arvore
#    versionada, ausente na CI por construcao -- .github/workflows/ci.yml
#    so' faz checkout de ps3recomp). Pula quando o irmao nao existe, PASSA
#    quando identico, e AVISA (nunca falha) quando divergente -- a
#    divergencia conhecida hoje (bloco host_gow2_factory, commit 1f651aa) e'
#    debito do host_gow2_factory (Fase 2), nao desta fase.
#
#    D-2.5 (02-CONTEXT.md, Fase 2) mediu esta divergencia e decidiu MANTER
#    o WARN: promover a FAIL exige resolver a divergencia de verdade primeiro
#    (reconciliar games/gow2/ com o irmao), o que exige um `git subtree pull`
#    de games/gow2/ seguido de push ao espelho -- accao externa que precisa
#    de autorizacao explicita do utilizador (o CLAUDE.md da raiz restringe
#    push ao fork pessoal). A divida fica registada aqui e em D-2.5; a
#    promocao e' um follow-up de uma linha apos autorizacao.
#
#    DESVIO DECLARADO DA LETRA DE D-05 (ver <objective> do 01-03-PLAN.md e
#    D-05 em 01-CONTEXT.md): a forma literal do marcador de pulo ("SKIP: ...")
#    NAO pode ser usada aqui. CMakeLists.txt:298 define PS3RECOMP_SKIP_REGEX
#    = "SKIP:" DESANCORADA (bate em qualquer posicao da saida capturada, nao
#    so' no inicio) e :325-330 aplica essa propriedade a py.ppu_lifter_emit
#    (este arquivo). Como a CI nunca tem o irmao, o ramo de ausencia e' o
#    caminho SEMPRE tomado la -- se este sub-gate imprimisse "SKIP:", o
#    CTest classificaria o TESTE INTEIRO (os 4 pinos de emissao, a
#    instrumentacao OOB e o G4) como *Skipped* em TODA execucao da CI. Pior:
#    scripts/test_python.sh usa um grep ANCORADO em '^' -- os dois
#    classificadores discordariam entre si. Por isso o marcador de pulo
#    abaixo comunica "pulado" sem carregar essa substring. A INTENCAO de
#    D-05/D-06 (pular, avisar, nunca falhar) e' preservada; so' a forma
#    literal do token e' que muda.
# ---------------------------------------------------------------------------
BUILD_MACOS_REL = os.path.join("games", "gow2", "build_macos.sh")
PPU_STUBS_MIN_SITES = 3


def check_build_macos_anchor(text: str) -> bool:
    """Metade ancora de D-05, isolada para reuso pela assercao de dentes:
    o texto dado referencia ppu_stubs.cpp em pelo menos PPU_STUBS_MIN_SITES
    sitios distintos."""
    return text.count("ppu_stubs.cpp") >= PPU_STUBS_MIN_SITES


def check_build_macos_anchor_present() -> None:
    """Metade ancora de D-05 contra o arquivo real -- SEMPRE roda, PODE
    FALHAR. Ancora deste teste em games/gow2/build_macos.sh (presente em CI
    e em clone limpo), nao no checkout irmao."""
    path = os.path.join(REPO, BUILD_MACOS_REL)
    assert os.path.isfile(path), f"ancora D-05 ausente: {path} nao existe"
    with open(path, "r") as f:
        text = f.read()
    count = text.count("ppu_stubs.cpp")
    assert check_build_macos_anchor(text), (
        f"ancora D-05: {BUILD_MACOS_REL} referencia ppu_stubs.cpp em {count} sitio(s), "
        f"esperado >= {PPU_STUBS_MIN_SITES} -- a restauracao de ppu_stubs.cpp no plano "
        f"01-01 estaria a pinar um contrato que nenhum consumidor exercita")
    print(f"[PASS] ancora D-05: {BUILD_MACOS_REL} referencia ppu_stubs.cpp em "
          f"{count} sitio(s) (>= {PPU_STUBS_MIN_SITES})")

    # Assercao de dentes (acceptance criteria da Task 2): um texto sem
    # nenhuma mencao de ppu_stubs.cpp tem de FALHAR a metade ancora --
    # prova de que esta assercao nao esta' verde por vacuidade.
    assert not check_build_macos_anchor("echo sem ppu_stubs aqui\n"), (
        "ancora D-05 sem dentes: um texto sem 'ppu_stubs.cpp' passou o check")
    print("[PASS] ancora D-05 (dentes): texto sem ppu_stubs.cpp falha o check, como esperado")


def _default_sibling_build_macos_path() -> str:
    return os.path.join(os.path.dirname(REPO), "gow2-recomp", "build_macos.sh")


def check_build_macos_sync() -> None:
    """Metade sincronizacao de D-05, WARN-only (D-06) -- NUNCA levanta, NUNCA
    contribui para o codigo de saida deste teste. PS3_SYNC_GATE_SIBLING (OFF
    por default) so' existe para exercitar o ramo de ausencia numa maquina
    que tem o irmao (probe gated, CLAUDE.md regra 6)."""
    sibling_path = os.environ.get("PS3_SYNC_GATE_SIBLING") or _default_sibling_build_macos_path()

    if not os.path.isfile(sibling_path):
        # Marcador de pulo que NAO contem a substring "SKIP:" -- ver o
        # comentario de bloco acima desta secao para o porque.
        print(f"[PULADO] sub-gate de sincronizacao build_macos.sh: irmao ausente em {sibling_path}")
        return

    path = os.path.join(REPO, BUILD_MACOS_REL)
    with open(path, "r") as f:
        ours = f.read()
    with open(sibling_path, "r") as f:
        theirs = f.read()

    if ours == theirs:
        print(f"[PASS] sub-gate de sincronizacao build_macos.sh: identico ao irmao ({sibling_path})")
        return

    diff_lines = list(difflib.unified_diff(
        ours.splitlines(keepends=True), theirs.splitlines(keepends=True),
        fromfile=BUILD_MACOS_REL, tofile=sibling_path))
    hunk_count = sum(1 for line in diff_lines if line.startswith("@@"))
    print(f"[AVISO] sub-gate de sincronizacao build_macos.sh: DIVERGENTE do irmao "
          f"({hunk_count} hunk(s)). A divergencia conhecida hoje e' o bloco "
          f"host_gow2_factory (commit 1f651aa); a promocao deste sub-gate a FAIL "
          f"pertence a Fase 2 (D-06), nao a esta fase. Primeiras linhas do diff:")
    for line in diff_lines[:12]:
        print("    " + line.rstrip("\n"))
    # Nunca levanta, nunca muda o codigo de saida -- WARN-only por D-06.


def main() -> int:
    print("=== ppu_lifter preamble emission goldens (Fase 1, criterio 1) ===")
    check_preamble_emissions()
    check_timebase_definer_present()
    check_ppu_stubs_emission()
    print("\n=== instrumentacao OOB dos quatro ppu_res_* (Fase 1, criterio 3) ===")
    check_ppu_res_instrumentation_present()
    print("\n=== prova por mutacao (D-07) ===")
    check_ppu_stubs_mutation()
    check_preamble_mutations()
    check_ppu_res_mutation()
    check_ppu_res_slice_mutation()
    print("\n=== ppu_lifter emission goldens ===")
    emitted = check_goldens()
    print("\n=== compiled emission harness ===")
    rc = build_and_run(emitted, pick_cxx(sys.argv[1:]))
    print("\n=== compiled ppu_res_* atomics harness (Fase 1, criterio 3) ===")
    rc_atomics = build_and_run_atomics(pick_cxx(sys.argv[1:]))
    rc = rc or rc_atomics
    print("\n=== gate G4: tokens proibidos fora de guarda MSVC/Windows (Fase 1, criterio 4) ===")
    check_g4_gate()
    check_g4_negative_control()
    print("\n=== gate de sincronizacao build_macos.sh (Fase 1, criterio 1, D-05/D-06) ===")
    check_build_macos_anchor_present()
    check_build_macos_sync()
    print(f"\n[{'OK' if rc == 0 else 'FAIL'}] test_ppu_lifter_emit ({rc})")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
