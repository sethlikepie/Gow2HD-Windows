#!/usr/bin/env python3
"""Fix nested OPDs in GroupEnd finalize chain (0039E5D8 and sibling 0039E* helpers).

REPARACAO 2026-07-26 (re-lift com lifter novo)
----------------------------------------------
Tres mudancas de FORMA do lift partiam as agulhas literais (nenhuma semantica):

  1. shape-TOCFIX: depois da chamada indirecta o restauro do TOC passou de
     `ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);` para
     `ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/`.
     TODAS as agulhas deste script terminavam na forma antiga, logo deixaram de
     casar: 8 sitios OPD ficaram por converter e a probe [WADLD-FIN] por instalar,
     em silencio ("fixed 0" + "OK" + rc=0).
  2. callee-save: o epilogo passou de `ctx->gpr[28] = vm_read64(ctx->gpr[1]+0x70);`
     para `ctx->gpr[28] = _cs_28;`. Era a ultima linha da agulha da probe.
  3. chunk-fixo: o script abria `ppu_recomp_001.cpp` por nome; o numero de chunks
     do lifter deixou de ser estavel. Agora resolve-se por `resolve_lift_paths`.

As agulhas passaram a aceitar AS DUAS formas (antiga e nova) — o lift de 20 jul
(`recomp_macos_v2.pre_v4`) continua a ser processado como estava. A linha do TOC
e reemitida a partir do grupo capturado: o script nunca inventa o valor do TOC.

Alem disso a regiao de cada funcao passou a ser delimitada EXPLICITAMENTE pela
chaveta de fecho (`\\n}\\n`) em vez de "ate a proxima `void func_`": no lift novo
ha codigo auxiliar injectado entre funcoes e a regiao de func_0039E6B4 esticava
de 71 para 626 linhas.

Codigo de saida (era sempre 0, mesmo sem converir nada):
  0  APPLIED         -> converteu >= 1 sitio
  0  ALREADY-APPLIED -> nada a converter, os sitios ja estao em ps3_call_opd
  1  FAILED          -> sobrou `ps3_indirect_call` numa regiao alvo, ou nenhum
                        sitio OPD foi reconhecido, ou a probe [WADLD-FIN] nao
                        pode ser instalada. Um patch que corre e nao produz
                        efeito TEM de ser ruidoso.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

DECL = 'extern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);'
INDIRECT_DECL = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'

TARGETS = [
    "func_0039E5D8",  # GroupEnd nested finalize (hot)
    "func_0039E794",  # sibling factory-like
    "func_0039E18C",
    "func_0039EE64",
    "func_0039E6B4",  # already patched but re-check
]

# Restauro do TOC: forma antiga (reload do frame) OU forma nova (TOCFIX estatico).
_TOC_BODY = (
    r"        ctx->gpr\[2\] = "
    r"(?:vm_read64\(ctx->gpr\[1\] \+ 0x28\);"
    r"|0x[0-9A-Fa-f]+ULL; /\* ?TOCFIX[^\n]*)\n"
)
_TOC = "(" + _TOC_BODY + ")"


def opd_re(reg: int, off: str) -> "re.Pattern[str]":
    """Bloco 'chamada por OPD via gpr[reg]' carregado de vtable+off.

    Tolera as duas formas do lift: a antiga (carrega code/TOC e faz
    ps3_indirect_call) e a ja convertida (ps3_call_opd), alem das duas formas de
    restauro do TOC. Grupos: 1=load da vtable, 2=forma da chamada, 3=linha do TOC.
    """
    g = r"ctx->gpr\[" + str(reg) + r"\]"
    return re.compile(
        r"(        " + g + r" = vm_read32\(ctx->gpr\[9\] \+ 0x" + off + r"\);\n)"
        r"(?:        ctx->gpr\[0\] = vm_read32\(" + g + r" \+ 0x0\);\n)?"
        r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
        r"(?:        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n)?"
        r"(?:        ctx->gpr\[2\] = vm_read32\(" + g + r" \+ 0x4\);\n)?"
        r"        (ps3_indirect_call\(ctx\)"
        r"|ps3_call_opd\(ctx, \(uint32_t\)" + g + r"\)); DRAIN_TRAMPOLINE\(ctx\);\n"
        + _TOC
    )


def call_lines(reg: int) -> str:
    return (
        "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
        "        ps3_call_opd(ctx, (uint32_t)ctx->gpr[%d]); DRAIN_TRAMPOLINE(ctx);\n" % reg
    )


# vt+0x4C via gpr[11] (offset fixo, como na versao literal original)
RE_4C = opd_re(11, "4C")
# generico: gpr[10] = vt+imm
RE_10 = opd_re(10, "[0-9A-Fa-f]+")

# Probe [WADLD-FIN]: ancora explicita entre o TOC do 2o call_opd e o restauro do
# LR (gpr[0] <- 0xA0). NAO usa o restauro de gpr[28], que mudou de forma.
RE_FIN = re.compile(
    r"(        ps3_call_opd\(ctx, \(uint32_t\)ctx->gpr\[10\]\); DRAIN_TRAMPOLINE\(ctx\);\n"
    + _TOC_BODY + r")"
    r"(        ctx->gpr\[0\] = vm_read64\(ctx->gpr\[1\] \+ 0xA0\);\n)"
)

PROBE_FIN = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<32)
            fprintf(stderr,"[WADLD-FIN] #%d r3=0x%08X opd=0x%08X\\n",
              n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[10]); fflush(stderr);} }
"""


def region_of(s: str, name: str):
    """Regiao da funcao, delimitada EXPLICITAMENTE pela chaveta de fecho."""
    i = s.find("void %s(ppu_context" % name)
    if i < 0:
        return None
    j = s.find("\n}\n", i)
    j = j + 3 if j > i else len(s)
    return i, j


def patch_region(region: str, name: str, stats: dict) -> str:
    conv = 0
    already = 0

    def repl(reg: int):
        def _f(m: "re.Match[str]") -> str:
            nonlocal conv, already
            if m.group(2).startswith("ps3_indirect_call"):
                conv += 1
            else:
                already += 1
            # a linha do TOC (group 3) e reemitida tal e qual foi encontrada
            return m.group(1) + call_lines(reg) + m.group(3)
        return _f

    region = RE_4C.sub(repl(11), region)
    region = RE_10.sub(repl(10), region)

    # probe no resultado da 2a chamada de 39E5D8
    if name == "func_0039E5D8":
        if "WADLD-FIN" in region:
            stats["probe"] = "already"
        else:
            region, k = RE_FIN.subn(lambda m: m.group(1) + PROBE_FIN + m.group(2),
                                    region, count=1)
            stats["probe"] = "installed" if k else "MISSING"
            if k:
                print("  %s: FIN probe" % name)

    left = region.count("ps3_indirect_call")
    opd = region.count("ps3_call_opd")
    stats["conv"] += conv
    stats["already"] += already
    stats["left"] += left
    stats["opd"] += opd
    print("  %s: fixed %d OPD sites (already=%d, call_opd=%d, indirect_left=%d)"
          % (name, conv, already, opd, left))
    return region


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "ppu_recomp_001.cpp")
    stats = {"conv": 0, "already": 0, "left": 0, "opd": 0, "probe": None}
    seen = set()

    for p in paths:
        if not p.exists():
            print("skip %s (nao existe)" % p)
            continue
        s = p.read_text(encoding="utf-8", errors="replace")
        orig = s
        touched = False

        for name in TARGETS:
            r = region_of(s, name)
            if r is None:
                continue
            seen.add(name)
            i, j = r
            new_region = patch_region(s[i:j], name, stats)
            if new_region != s[i:j]:
                s = s[:i] + new_region + s[j:]
                touched = True

        if touched and DECL not in s:
            if INDIRECT_DECL not in s:
                print("ERRO: %s: sem declaracao de ps3_indirect_call" % p.name)
                return 1
            s = s.replace(INDIRECT_DECL, INDIRECT_DECL + "\n" + DECL, 1)
            print("  %s: declared ps3_call_opd" % p.name)

        if s != orig:
            p.write_text(s, encoding="utf-8", newline="\n")

    missing = [t for t in TARGETS if t not in seen]
    for t in missing:
        print("skip missing %s" % t)

    # ---- veredicto -----------------------------------------------------------
    if not seen:
        print("FALHOU: nenhuma das %d funcoes alvo foi encontrada no lift" % len(TARGETS))
        return 1
    if stats["left"]:
        print("FALHOU: sobraram %d ps3_indirect_call nas regioes alvo "
              "(shape do lift mudou; agulha nao reconhece o sitio)" % stats["left"])
        return 1
    if stats["probe"] == "MISSING":
        print("FALHOU: probe [WADLD-FIN] nao instalada em func_0039E5D8 "
              "(ancora do epilogo nao casou)")
        return 1
    if stats["conv"] == 0 and stats["opd"] == 0:
        print("FALHOU: 0 conversoes e 0 ps3_call_opd nas regioes alvo — "
              "o patch correu sem produzir efeito")
        return 1
    if stats["conv"] == 0:
        print("ALREADY-APPLIED: 0 conversoes, %d sitios ja em ps3_call_opd, probe=%s"
              % (stats["opd"], stats["probe"]))
        return 0
    print("OK: %d sitios OPD convertidos (%d ja aplicados, probe=%s)"
          % (stats["conv"], stats["already"], stats["probe"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
