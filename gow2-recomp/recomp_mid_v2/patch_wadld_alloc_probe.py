#!/usr/bin/env python3
"""[WADLD-ALLOC] -- probe de diagnostico do alocador de tymap em func_002BACE8.

O QUE INSTALA
=============
Um unico bloco de diagnostico logo no topo de `func_002BACE8`, a funcao de
alocacao usada pelo WAD-load para reservar espaco no pool do tymap. Imprime,
por chamada amostrada (ate' 40), o estado que decide se a alocacao cabe:

    arena     = o EA da arena de alocacao       (ctx->gpr[3])
    need      = o tamanho pedido                (ctx->gpr[29])
    head      = arena+4                         (cabeca da lista livre)
    desc      = *(TOC-0x20D8)                    (descritor do pool corrente)
    pool_idx  = desc+4  count = desc+8           (indice/contagem do pool)
    stream_av = o+0x1A8 -> +0x10                 (disponibilidade do stream)
    cur       = o+0x1A8 -> +0x8                  (cursor corrente)
    peek      = 4 palavras a partir de sbase+scu (amostra do buffer)

Gating: `PS3_TRACE_TYMAP`, OFF por default (regra 6 do CLAUDE.md) -- o MESMO
env var usado pela probe original (nao um novo). Com a env var ausente o
bloco e' um `if(on)`: nao le memoria guest, nao imprime. No-op no baseline.

PORQUE ESTE FICHEIRO NASCEU
===========================
A probe existia no lift de 20 jul (`recomp_macos_v2.pre_v4/ppu_recomp_002.cpp`,
func_002BACE8) mas era uma EDICAO A MAO: nenhum patch_*.py a escrevia. O
re-lift apagou-a e nada se queixou -- exactamente o mesmo padrao ja fechado
para [BA808] (patch_ba808_wadld_eof.py) e para [B71]. `[WADLD-ALLOC]` ficou
ORFAO REAL (D-3.3, 03-CONTEXT.md/03-03-PLAN.md Fase 3): nenhum patch_*.py o
escreve nos dois repositorios (confirmado por busca exaustiva antes desta
task). Este script fecha esse buraco -- passa a ter escritor, idempotente, e
falha (rc!=0) se a ancora deixar de existir.

O QUE MUDOU ENTRE O LIFT DE 20 JUL E O LIFT ACTUAL (tolerado, nao reposto)
==========================================================================
O corpo de `func_002BACE8` ganhou dois detalhes que o lift de 20 jul nao
tinha, e que esta probe NAO reescreve (apenas tolera na ancora, via
lookahead nao-consumido):
  - um prefixo `ctx->lr = 0x002BACEC; ` antes da chamada a func_00262808
    (o lifter actual grava o LR do "retorno" antes do trampolim -- ausente
    no `.pre_v4`);
  - a linha seguinte ao ponto de insercao mudou de
    `ctx->gpr[4] = (uint32_t)ppc_rlwinm(...)` para
    `ctx->gpr[4] = (uint64_t)ppc_rlwinm(...)` (cast largo, mesma semantica).
Nenhuma das duas altera o comportamento que a probe descreve -- sao apenas
formas de emissao do lifter, por isso a ancora tolera ambas sem as tocar.

USO
===
    ./patch_wadld_alloc_probe.py [DIR_DE_LIFT|CHUNK.cpp ...]

rc=0 APPLIED/ALREADY, rc=1 se a funcao ou a ancora nao existirem.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

DEFAULT_LIFT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

FN = "func_002BACE8"
MARK = "[WADLD-ALLOC]"

FN_RE = re.compile(r"^void " + FN + r"\(ppu_context\* ctx\) \{", re.M)
NEXT_FN_RE = re.compile(r"^void func_[0-9A-Fa-f]+\(ppu_context\* ctx\) \{", re.M)

# Ancora: o "nop" logo depois da chamada a func_00262808 (a chamada que
# resolve o alocador), com um lookahead (NAO consumido) para a linha do
# ppc_rlwinm que vem a seguir -- e' exactamente onde a probe original vivia
# no lift de 20 jul. O prefixo "ctx->lr = 0x...; " e' opcional (so' existe no
# lift actual); o cast em uint32/uint64 no lookahead nao e' capturado nem
# reescrito, so' serve para confirmar o ponto de insercao. Sem re.S, sem
# inferir por numero de linha.
ANCHOR = re.compile(
    r"(?:^ {8}ctx->lr = 0x[0-9A-Fa-f]+; )?"
    r"func_00262808\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r" {8}/\* nop \*/;\n"
    r"(?= {8}ctx->gpr\[4\] = \(uint(?:32|64)_t\)ppc_rlwinm\(\(uint32_t\)ctx->gpr\[29\], 0, 0, 27\);\n)",
    re.M,
)

# Byte a byte como estava no lift de 20 jul (pre_v4, func_002BACE8).
PROBE = (
    '        { static int on=-1; if(on<0){extern char* getenv(const char*);'
    ' on=getenv("PS3_TRACE_TYMAP")?1:0;}\n'
    '          if(on){ static int n=0; if(n++<40){\n'
    '            uint32_t arena=(uint32_t)ctx->gpr[3], need=(uint32_t)ctx->gpr[29];\n'
    '            uint32_t head=arena?vm_read32(arena+4):0;\n'
    '            uint32_t desc=vm_read32(ctx->gpr[2]-0x20D8);\n'
    '            uint32_t idx=desc?vm_read32(desc+4):0, cnt=desc?vm_read32(desc+8):0;\n'
    '            uint32_t st=vm_read32((uint32_t)ctx->gpr[31]+0x1A8u);\n'
    '            uint32_t sav=st?vm_read32(st+0x10u):0, scu=st?vm_read32(st+0x8u):0;\n'
    '            uint32_t sbase=st?vm_read32(st+0x0u):0;\n'
    '            uint32_t b0=0,b1=0,b2=0,b3=0;\n'
    '            if(sbase && sav>=16){ b0=vm_read32(sbase+scu); b1=vm_read32(sbase+scu+4);\n'
    '              b2=vm_read32(sbase+scu+8); b3=vm_read32(sbase+scu+12); }\n'
    '            fprintf(stderr,"[WADLD-ALLOC] BACE8 #%d arena=0x%08X head=0x%08X'
    ' need=0x%X pool_idx=%u count=%u stream_av=%u cur=%u peek=%08X %08X %08X %08X\\n",\n'
    '              n, arena, head, need, idx, cnt, sav, scu, b0,b1,b2,b3); fflush(stderr);} } }\n'
)


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], DEFAULT_LIFT) if p.is_file()]
    if not paths:
        print("FAILED: nenhum chunk de lift encontrado")
        return 1

    target = None
    for p in paths:
        text = p.read_text(encoding="utf-8", errors="replace")
        if FN_RE.search(text):
            target = (p, text)
            break
    if target is None:
        print(f"FAILED: {FN} nao definido em nenhum dos {len(paths)} chunk(s)")
        return 1
    path, src = target

    m = FN_RE.search(src)
    nxt = NEXT_FN_RE.search(src, m.end())
    lo, hi = m.start(), (nxt.start() if nxt else len(src))
    region = src[lo:hi]

    if MARK in region:
        print(f"{path.name}: ALREADY {FN} ja' tem a probe {MARK} (nada a fazer)")
        return 0

    hits = list(ANCHOR.finditer(region))
    if len(hits) != 1:
        print(
            f"FAILED: ancora 'func_00262808(...); DRAIN_TRAMPOLINE(...); /* nop */;' "
            f"aparece {len(hits)}x em {FN} ({path.name}); esperado 1. "
            f"O shape do lift mudou -- investigar, nunca forcar."
        )
        return 1

    cut = lo + hits[0].end()
    # Path.write_text(newline=...) so' existe em 3.10+; o /usr/bin/python3 do
    # Mac de build e' 3.9.6. Abrir explicitamente mantem LF em qualquer host.
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write(src[:cut] + PROBE + src[cut:])
    print(f"{path.name}: APPLIED probe {MARK} em {FN} (1 site, gated PS3_TRACE_TYMAP)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
