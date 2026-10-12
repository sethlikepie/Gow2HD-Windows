#!/usr/bin/env python3
"""
Fix group factory OPDs (0039E6B4) + probe 2B0FB4 post-create gate that skips VT48.

Root cause candidates:
1. func_0039E6B4 uses ps3_indirect_call on nested OPDs (broken same class as prior OPD bugs)
2. After create, VT48 only runs if *(obj) low16==3 and field==1; need evidence of actual header

REPARACAO 2026-07-25 (re-lift com lifter novo)
----------------------------------------------
Quatro mudancas de forma partiam as agulhas literais (nenhuma semantica):

  1. shape-TOCFIX: apos a chamada indirecta o restauro do TOC passou de
     `ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);` para
     `ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/`. Era isto que dava
     "factory OPD-A pattern missing".
  2. chunk-fixo: o script abria `ppu_recomp_001.cpp` e `ppu_recomp_003.cpp` por
     nome. O lifter passou de 31 para 7 chunks; agora procura-se cada funcao em
     todos os chunks resolvidos por `resolve_lift_paths`.
  3. ordem no apply_all_patches: `patch_39e5d8_opd.py` corre antes (3 < f) e ja
     converte os 2 OPD de func_0039E6B4 para `ps3_call_opd`. A agulha passa a
     aceitar as duas formas, portanto este script continua a poder acrescentar as
     probes [WADLD-FACT] sem depender de quem correu primeiro.
  4. cast do rlwinm: na agulha do VT48CHK o lifter passou a emitir
     `(uint64_t)ppc_rlwinm(...)` onde antes escrevia `(uint32_t)ppc_rlwinm(...)`;
     o regex aceita os dois.

A linha do TOC e a linha da chamada sao reemitidas a partir do que foi
encontrado (grupos capturados) — o script nunca inventa o valor do TOC.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

DECL = 'extern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);'
INDIRECT_DECL = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'

# Restauro do TOC: forma antiga (reload do frame) OU forma nova (TOCFIX estatico).
_TOC = (
    r"(        ctx->gpr\[2\] = "
    r"(?:vm_read64\(ctx->gpr\[1\] \+ 0x28\);"
    r"|0x[0-9A-Fa-f]+ULL; /\* ?TOCFIX[^\n]*)\n)"
)


def opd_re(reg: int, off: str) -> "re.Pattern[str]":
    """Bloco 'chamada por OPD via gpr[reg]' carregado de vtable+off.

    Tolera as duas formas do lift: a antiga (carrega code/TOC e faz
    ps3_indirect_call) e a ja convertida (ps3_call_opd), alem das duas formas de
    restauro do TOC.
    """
    g = r"ctx->gpr\[" + str(reg) + r"\]"
    return re.compile(
        r"(        " + g + r" = vm_read32\(ctx->gpr\[9\] \+ 0x" + off + r"\);\n)"
        r"(?:        ctx->gpr\[0\] = vm_read32\(" + g + r" \+ 0x0\);\n)?"
        r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
        r"(?:        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n)?"
        r"(?:        ctx->gpr\[2\] = vm_read32\(" + g + r" \+ 0x4\);\n)?"
        r"        (?:ps3_indirect_call\(ctx\)"
        r"|ps3_call_opd\(ctx, \(uint32_t\)" + g + r"\)); DRAIN_TRAMPOLINE\(ctx\);\n"
        + _TOC
    )


def call_lines(reg: int) -> str:
    return (
        "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
        "        ps3_call_opd(ctx, (uint32_t)ctx->gpr[%d]); DRAIN_TRAMPOLINE(ctx);\n" % reg
    )


def region_of(s: str, name: str):
    i = s.find("void %s(ppu_context" % name)
    if i < 0:
        return None
    j = s.find("\nvoid func_", i + 10)
    j = j + 1 if j > i else len(s)
    return i, j


# ---------------------------------------------------------------------------
# 1) Fix factory OPD calls em func_0039E6B4
# ---------------------------------------------------------------------------
PROBE_A = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<16)
            fprintf(stderr,"[WADLD-FACT] #A n=%d self=0x%08X opd=0x%08X code=0x%08X\\n",
              n,(uint32_t)ctx->gpr[29],(uint32_t)ctx->gpr[11],
              ctx->gpr[11]?vm_read32(ctx->gpr[11]+0x0):0); fflush(stderr);} }
"""

PROBE_B = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<16)
            fprintf(stderr,"[WADLD-FACT] #B n=%d obj=0x%08X opd=0x%08X code=0x%08X\\n",
              n,(uint32_t)ctx->gpr[11],(uint32_t)ctx->gpr[10],
              ctx->gpr[10]?vm_read32(ctx->gpr[10]+0x0):0); fflush(stderr);} }
"""

PROBE_BR = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<16)
            fprintf(stderr,"[WADLD-FACT] #BR n=%d r3=0x%08X hdr=0x%08X\\n",
              n,(uint32_t)ctx->gpr[3],
              ctx->gpr[3]?vm_read32(ctx->gpr[3]+0x0):0); fflush(stderr);} }
"""

RE_A = opd_re(11, "4C")
RE_B = opd_re(10, "28")


def _repl_a(m: "re.Match[str]") -> str:
    return m.group(1) + PROBE_A + call_lines(11) + m.group(2)


def _repl_b(m: "re.Match[str]") -> str:
    return m.group(1) + PROBE_B + call_lines(10) + m.group(2) + PROBE_BR


def patch_factory(p: Path) -> bool:
    s = p.read_text(encoding="utf-8", errors="replace")
    orig = s

    if DECL not in s:
        if INDIRECT_DECL not in s:
            raise SystemExit("%s: no ps3_indirect_call decl" % p.name)
        s = s.replace(INDIRECT_DECL, INDIRECT_DECL + "\n" + DECL, 1)
        print("%s: declared ps3_call_opd" % p.name)

    i, j = region_of(s, "func_0039E6B4")
    region = s[i:j]

    if "WADLD-FACT" in region:
        print("%s: factory already patched" % p.name)
    else:
        if not RE_A.search(region):
            raise SystemExit("factory OPD-A pattern missing")
        if not RE_B.search(region):
            raise SystemExit("factory OPD-B pattern missing")
        region = RE_A.sub(_repl_a, region, count=1)
        region = RE_B.sub(_repl_b, region, count=1)
        s = s[:i] + region + s[j:]
        print("%s: factory OPDs -> ps3_call_opd + FACT probes" % p.name)

    if s != orig:
        p.write_text(s, encoding="utf-8", newline="\n")
        print("%s: written" % p.name)
        return True
    return False


# ---------------------------------------------------------------------------
# 2) Gate probe em func_002B0FB4
# ---------------------------------------------------------------------------
GATE_NEEDLE = """        ctx->gpr[24] = ctx->gpr[3] | ctx->gpr[3];
        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_002B0EF4; return; }
        ctx->gpr[0] = vm_read8(ctx->gpr[27] + 0x0);"""

GATE_PROBE = """        ctx->gpr[24] = ctx->gpr[3] | ctx->gpr[3];
        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_002B0EF4; return; }
        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<80){
            uint32_t obj=(uint32_t)ctx->gpr[3], flg_ea=(uint32_t)ctx->gpr[27], r29=(uint32_t)ctx->gpr[29];
            uint8_t fb = flg_ea ? vm_read8(flg_ea+0x0) : 0;
            uint32_t oh = obj ? vm_read32(obj+0x0) : 0;
            uint16_t lo = (uint16_t)(oh & 0xFFFF);
            uint32_t mid = (uint32_t)((oh >> 16) & 0xFFF); /* approx of rldicl(,48,52) field interest */
            fprintf(stderr,"[WADLD-GATE] #%d obj=0x%08X hdr=0x%08X lo16=%u mid=%u flagb=0x%02X r29=0x%08X\\n",
              n, obj, oh, (unsigned)lo, mid, fb, r29); fflush(stderr);} } }
        ctx->gpr[0] = vm_read8(ctx->gpr[27] + 0x0);"""

# O cast do rlwinm mudou de (uint32_t) para (uint64_t) no lifter novo: aceitar os
# dois. O resto da agulha fica literal (via re.escape) por ser o que a torna unica.
VT48_HEAD = "        ctx->gpr[9] = vm_read32(ctx->gpr[31] + 0x0);\n"
VT48_RE = re.compile(
    re.escape(VT48_HEAD)
    + r"(        ctx->gpr\[0\] = \((?:uint32_t|uint64_t)\)ppc_rlwinm\(\(uint32_t\)ctx->gpr\[9\], 0, 16, 31\);\n)"
    + "(" + re.escape(
        "        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int64_t)3; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
        "        if ((!((ctx->cr >> 0) & 2))) goto loc_002B10B8;\n"
        "        ctx->gpr[0] = ppc_rldicl(ctx->gpr[9], 48, 52);\n"
        "        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int64_t)1; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
        "        if ((!((ctx->cr >> 0) & 2))) goto loc_002B10B8;"
    ) + ")"
)

VT48_PROBE = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<40){
            uint32_t w=(uint32_t)ctx->gpr[9], lo=(uint32_t)ctx->gpr[0];
            uint32_t fld=(uint32_t)ppc_rldicl(w, 48, 52);
            fprintf(stderr,"[WADLD-VT48CHK] #%d w=0x%08X lo16=%u fld=%u pass=%d\\n",
              n, w, lo, fld, (lo==3 && fld==1)); fflush(stderr);} } }
"""


def _repl_vt48(m: "re.Match[str]") -> str:
    return VT48_HEAD + m.group(1) + VT48_PROBE + m.group(2)


def patch_gate(p: Path) -> bool:
    s = p.read_text(encoding="utf-8", errors="replace")
    orig = s
    i, j = region_of(s, "func_002B0FB4")
    region = s[i:j]

    if "WADLD-GATE" in region:
        print("%s: GATE probe already present" % p.name)
        return False
    if GATE_NEEDLE not in region:
        raise SystemExit("2B0FB4 gate needle missing")
    region = region.replace(GATE_NEEDLE, GATE_PROBE, 1)

    if "WADLD-VT48CHK" not in region:
        if not VT48_RE.search(region):
            print("WARNING: VT48CHK needle missing (partial patch)")
        else:
            region = VT48_RE.sub(_repl_vt48, region, count=1)
            print("%s: VT48CHK probe added" % p.name)

    s = s[:i] + region + s[j:]
    if s != orig:
        p.write_text(s, encoding="utf-8", newline="\n")
        print("%s: GATE probe written" % p.name)
        return True
    return False


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], str(Path(__file__).resolve().parent))
             if p.exists()]
    if not paths:
        print("nenhum ppu_recomp_*.cpp no alvo", file=sys.stderr)
        return 1

    seen_fact = seen_gate = False
    for p in paths:
        s = p.read_text(encoding="utf-8", errors="replace")
        if "void func_0039E6B4(ppu_context" in s:
            seen_fact = True
            patch_factory(p)
        if "void func_002B0FB4(ppu_context" in s:
            seen_gate = True
            patch_gate(p)

    if not seen_fact:
        raise SystemExit("func_0039E6B4 not found")
    if not seen_gate:
        raise SystemExit("2B0FB4 not found")
    print("OK patch_factory_opd_gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
