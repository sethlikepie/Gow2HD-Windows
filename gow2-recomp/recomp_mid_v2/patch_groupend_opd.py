#!/usr/bin/env python3
"""
GroupEnd expand path OPD fix (func_002B0AA8).

Evidence:
- Type-1 SHGX factory creates objects (VT28R nonzero) but never reaches ICGLdr.
- GroupEnd handler 0x2B0AA8 runs once per group and calls member vtable+0x2C
  via ps3_indirect_call (treats OPD as raw code) — same broken class as prior
  OPD bugs that blocked type loaders.
- Also fix 0x2B0DB0 (two OPD sites) used by another stream type.

REPARACAO 2026-07-25 (re-lift com lifter novo)
----------------------------------------------
Tres mudancas de forma partiam as agulhas literais (nenhuma semantica):

  1. shape-TOCFIX: o restauro do TOC depois da chamada indirecta passou de
     `ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);` para
     `ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/`. As agulhas antigas
     exigiam a primeira forma e falhavam com "2B0AA8 OPD pattern missing".
  2. chunk-fixo: o script abria `ppu_recomp_001.cpp` por nome; o lifter passou de
     31 para 7 chunks e as funcoes podem migrar. Agora resolve-se o alvo com
     `resolve_lift_paths` e procura-se a funcao em qualquer chunk.
  3. ordem no apply_all_patches: `patch_39e5d8_opd.py` corre antes (3 < g) e pode
     JA ter convertido o sitio para `ps3_call_opd`. A agulha passa a aceitar as
     duas formas (`ps3_indirect_call` ou `ps3_call_opd`), pelo que o resultado e'
     o mesmo texto final e a segunda passagem e' um no-op exacto.

A linha do TOC e' reemitida TAL E QUAL foi encontrada (grupo capturado): o script
nunca escolhe entre `vm_read64` e o valor estatico do TOCFIX — isso e' decisao do
lifter, nao deste patch.
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

# Bloco "chamada por OPD via gpr[10]": carga do slot da vtable + (forma antiga:
# carga do code/TOC + ctr) + chamada + restauro do TOC.
OPD_RE = re.compile(
    r"(        ctx->gpr\[10\] = vm_read32\(ctx->gpr\[9\] \+ 0x[0-9A-Fa-f]+\);\n)"
    r"(?:        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[10\] \+ 0x0\);\n)?"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"(?:        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n)?"
    r"(?:        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[10\] \+ 0x4\);\n)?"
    r"        (?:ps3_indirect_call\(ctx\)"
    r"|ps3_call_opd\(ctx, \(uint32_t\)ctx->gpr\[10\]\)); DRAIN_TRAMPOLINE\(ctx\);\n"
    + _TOC
)

CALL_LINES = (
    "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
    "        ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]); DRAIN_TRAMPOLINE(ctx);\n"
)

GEND_PROBE = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<48)
            fprintf(stderr,"[WADLD-GEND] #%d self=0x%08X member=0x%08X opd=0x%08X code=0x%08X\\n",
              n,(uint32_t)ctx->gpr[11],(uint32_t)ctx->gpr[5],(uint32_t)ctx->gpr[10],
              ctx->gpr[10]?vm_read32(ctx->gpr[10]+0x0):0); fflush(stderr);} }
"""

GENDR_PROBE = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<48)
            fprintf(stderr,"[WADLD-GENDR] #%d r3=0x%08X\\n", n,(uint32_t)ctx->gpr[3]); fflush(stderr);} }
"""


def _plain(m: "re.Match[str]") -> str:
    return m.group(1) + CALL_LINES + m.group(2)


def _with_probes(m: "re.Match[str]") -> str:
    return m.group(1) + GEND_PROBE + CALL_LINES + m.group(2) + GENDR_PROBE


def region_of(s: str, name: str):
    i = s.find("void %s(ppu_context" % name)
    if i < 0:
        return None
    j = s.find("\nvoid func_", i + 10)
    j = j + 1 if j > i else len(s)
    return i, j


def patch_file(p: Path) -> bool:
    """Devolve True se escreveu; imprime o que fez."""
    s = p.read_text(encoding="utf-8", errors="replace")
    orig = s

    if region_of(s, "func_002B0AA8") is None and region_of(s, "func_002B0DB0") is None:
        return False

    # declaracao do helper de OPD (idempotente; procura no ficheiro inteiro para
    # nao duplicar a linha quando o preambulo do lift cresce)
    if DECL not in s:
        if INDIRECT_DECL not in s:
            raise SystemExit("%s: no ps3_indirect_call decl" % p.name)
        s = s.replace(INDIRECT_DECL, INDIRECT_DECL + "\n" + DECL, 1)
        print("%s: declared ps3_call_opd" % p.name)

    # --- GroupEnd 2B0AA8 (1 sitio + probes) ---
    r = region_of(s, "func_002B0AA8")
    if r:
        i, j = r
        region = s[i:j]
        if "WADLD-GEND" in region:
            print("2B0AA8 already patched")
        else:
            hits = list(OPD_RE.finditer(region))
            if len(hits) != 1:
                raise SystemExit(
                    "2B0AA8 OPD pattern: esperava 1 sitio, encontrei %d" % len(hits)
                )
            region = OPD_RE.sub(_with_probes, region, count=1)
            s = s[:i] + region + s[j:]
            print("patched 2B0AA8 GroupEnd OPD")

    # --- 2B0DB0 (dois sitios, sem probes) ---
    r = region_of(s, "func_002B0DB0")
    if r:
        i, j = r
        region = s[i:j]
        before = region
        region, n = OPD_RE.subn(_plain, region)
        if n == 0:
            print("WARNING: 2B0DB0 OPD pattern not found")
        elif region == before:
            print("2B0DB0 already uses call_opd (x%d)" % n)
        else:
            s = s[:i] + region + s[j:]
            print("patched 2B0DB0 OPD sites x%d" % n)

    if s != orig:
        p.write_text(s, encoding="utf-8", newline="\n")
        return True
    return False


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], str(Path(__file__).resolve().parent))
             if p.exists()]
    if not paths:
        print("nenhum ppu_recomp_*.cpp no alvo", file=sys.stderr)
        return 1
    seen = False
    for p in paths:
        s = p.read_text(encoding="utf-8", errors="replace")
        if "void func_002B0AA8(ppu_context" not in s and \
           "void func_002B0DB0(ppu_context" not in s:
            continue
        seen = True
        patch_file(p)
    if not seen:
        print("2B0AA8/2B0DB0 ausentes no lift", file=sys.stderr)
        return 1
    print("OK patch_groupend_opd")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
