#!/usr/bin/env python3
"""Re-seed the FIOS op freelist after MovieStop so the next WAD open can alloc.

WHY
---
Medido no binario de producao com PS3_TRACE_FIOSOPEN=1 (regressao do marco
v1.1, 2026-07-31):

    [FIOSOPEN] 002B4224 poll #122880 container=0x00869E04 io=0x0
    [FIOSOPEN] 0030D5CC op_alloc #5 r3=0x00000000

A free-list de operacoes FIOS esgota depois do MovieStop e `op_alloc` devolve
NULL -- sem op para pollar, o guest nunca abre `/_movies/smlogo_v2.m2v`, sem
2o StartSeq, sem WADs, `R_PermA=0`, `thr_auto_load` nunca acaba.

Este fix (FIOS-FREELIST-REBUILD) existia no lift `recomp_macos_v2.pre_v4`
(o que gera o binario bom) mas era uma EDICAO MANUAL no .cpp gerado --
nunca virou script idempotente. O re-lift do marco v1.0 apagou-a, violando
a regra do CLAUDE.md ("todo fix nos ppu_recomp_XXX.cpp vira script
idempotente commitado para sobreviver a re-lift"). `patch_fios_f2a_f2b_wad.py`
so imprimia instrucoes em prosa -- nunca aplicou nada (ver seu proprio
docstring, agora marcado como documental).

O bloco abaixo foi extraido *verbatim* de
`recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:37473-37510` (e do mesmo sitio
espelhado 3x em ppu_recomp_002.cpp, ~224773/224866/224935) -- nao foi
reescrito de memoria.

FIX
---
No mesmo ponto do epilogo do MovieStop (SMPD st620=0), depois do write +
DRAIN_TRAMPOLINE + nop: le o media pool do container corrente por TOC;
se media+0x16C==0 ou a head da freelist (media+0x200) ==0, forca um
re-seed: encadeia 8 ops (offsets fixos medidos, stride 0xE0) e escreve a
nova head. Gated por PS3_FIOS_FREELIST_REBUILD (default ON, 0 desliga).

ORDEM COM patch_fios_stop_yield.py
-----------------------------------
Ambos os patches ancoram no MESMO trio de linhas fixas (write 0x620 + chamada
a func_0043FF30, com ou sem o prefixo ctx->lr do lifter, + nop) e inserem o
seu bloco IMEDIATAMENTE a seguir a essa ancora -- por isso sao compostos
corretamente em QUALQUER ordem de execucao (cada re.sub empurra o que ja
estava la para depois do bloco novo). A ordem alfabetica do
apply_all_patches.sh ("freelist_rebuild" < "stop_yield") produz, numa
corrida a partir de um lift limpo:

    ancora -> FIOS-STOP-YIELD -> FIOS-FREELIST-REBUILD -> ctx->gpr[0]=...

que e' exatamente a ordem medida no pre_v4 (yield primeiro, depois o
rebuild fora do giant-lock-release). Idempotencia e' a nivel de ficheiro
inteiro (MARKER in t) -- reruns nao duplicam nem reordenam, mesmo que a
outra patch ja tenha corrido.

Marker: FIOS-FREELIST-REBUILD. Idempotente.
"""
from pathlib import Path
import re
import sys

MARKER = "FIOS-FREELIST-REBUILD"
ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# Mesma ancora de patch_fios_stop_yield.py -- ver nota de ordem no docstring
# acima. Casa exatamente os 4 sitios reais (1 em ppu_recomp_001.cpp, 3 em
# ppu_recomp_002.cpp); confirmado por varredura contra os 7 chunks.
NEEDLE_RE = re.compile(
    r"( *vm_write32\(ctx->gpr\[31\] \+ 0x620, ctx->gpr\[0\]\);\n"
    r" *(?:ctx->lr = 0x[0-9A-Fa-f]+; )?func_0043FF30\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r" */\* nop \*/;\n)"
)

# Bloco extraido verbatim de recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:37473-37510.
BLOCK = (
    "\n        /* " + MARKER + ": re-seed freelist after MovieStop so WAD open\n"
    "         * can allocate (F2a). Gated PS3_FIOS_FREELIST_REBUILD (default ON). */\n"
    "        { static int _on=-1; if(_on<0){const char* e=getenv(\"PS3_FIOS_FREELIST_REBUILD\");\n"
    "            _on=(!e||*e!='0')?1:0;}\n"
    "          if(_on && vm_base){\n"
    "            uint32_t toc=(uint32_t)ctx->gpr[2];\n"
    "            uint32_t slot=(toc>=0x1460u)?vm_read32(toc-0x1460u):0u;\n"
    "            uint32_t media=(slot&&slot<0x4F000000u)?vm_read32(slot+0x118u):0u;\n"
    "            if(media>=0x10000u && media<0x4F000000u){\n"
    "              uint32_t head=vm_read32(media+0x200u);\n"
    "              uint32_t c16c=vm_read32(media+0x16Cu);\n"
    "              uint32_t c218=vm_read32(media+0x218u);\n"
    "              if(c16c==0u || head==0u){\n"
    "                if(c16c==0u) vm_write32(media+0x16Cu, 1u);\n"
    "                if(head==0u){\n"
    "                  /* Measured offsets of ops relative to media (0xE0 stride pool) */\n"
    "                  const uint32_t offs[8]={0x250u,0x330u,0x410u,0x4F0u,0x5D0u,0x6B0u,0x790u,0x870u};\n"
    "                  uint32_t chain=0;\n"
    "                  for(int i=7;i>=0;i--){\n"
    "                    uint32_t op=media+offs[i];\n"
    "                    if(op>=0x4F000000u) continue;\n"
    "                    vm_write32(op+0x0u, chain);\n"
    "                    vm_write32(op+0x90u, 0u);\n"
    "                    vm_write32(op+0x40u, 0u);\n"
    "                    chain=op;\n"
    "                  }\n"
    "                  vm_write32(media+0x200u, chain);\n"
    "                  vm_write32(media+0x218u, 0u);\n"
    "                  fprintf(stderr,\"[FIOSOPEN] FREELIST-REBUILD media=0x%08X head=0x%08X was16C=%u was218=%u\\n\",\n"
    "                    media, chain, c16c, c218);\n"
    "                  fflush(stderr);\n"
    "                } else if(c16c==0u){\n"
    "                  fprintf(stderr,\"[FIOSOPEN] 16C-only rebuild media=0x%08X head=0x%08X\\n\", media, head);\n"
    "                  fflush(stderr);\n"
    "                }\n"
    "              }\n"
    "            }\n"
    "          } }\n"
    "\n"
)


def _insert(m: "re.Match[str]") -> str:
    return m.group(1) + BLOCK


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t:
        return "ALREADY"
    matches = list(NEEDLE_RE.finditer(t))
    if not matches:
        return "SKIP"
    n = 0

    def repl(m: "re.Match[str]") -> str:
        nonlocal n
        n += 1
        return _insert(m)

    t = NEEDLE_RE.sub(repl, t)
    p.write_text(t, encoding="utf-8")
    return "APPLIED x%d" % n


def main() -> int:
    files = sorted(ROOT.glob("ppu_recomp_*.cpp"))
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % ROOT)
        return 1
    for f in files:
        print("%s: %s" % (f.name, patch_file(f)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
