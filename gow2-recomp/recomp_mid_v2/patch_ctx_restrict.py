#!/usr/bin/env python3
"""patch_ctx_restrict.py -- marca o ppu_context das funcoes liftadas como __restrict.

Porque: cada instrucao liftada le e escreve `ctx->gpr[n]`, `ctx->cr`, `ctx->xer`.
Como `ctx` e' um ponteiro e o corpo chama funcoes externas (`vm_write32`,
`ps3_hle_call`, ...), o Clang nao consegue provar que aquelas chamadas nao
escrevem no proprio contexto, e por isso RECARREGA os campos do contexto da
memoria depois de cada uma. O resultado e' uma sequencia de load/modify/store
por instrucao guest em vez de registradores fisicos.

`__restrict` no parametro e' exactamente o que o XenonRecomp usa
(`void f(PPCContext& __restrict ctx, uint8_t* base)`) e e' verdade aqui: o
contexto e' uma struct do HOST (stack da thread guest ou heap do runtime), e os
acessos a memoria guest vao todos para `vm_base`, outro objecto. Nenhum escritor
de memoria guest pode alcancar o contexto.

O ponteiro continua a ESCAPAR nas chamadas que recebem `ctx` (func_* aninhadas,
ps3_indirect_call, lv2_syscall), e `__restrict` nao autoriza o compilador a
assumir que o callee nao o modifica -- a recarga depois dessas chamadas continua
a acontecer, que e' o comportamento correcto.

So' toca nas funcoes liftadas (`func_XXXXXXXX` / `__imp_func_XXXXXXXX`), nunca
nas declaracoes de funcoes do runtime, cujas definicoes vivem noutro ficheiro
sem o qualificador.

MEDIDO 2026-09-17, e o resultado e' NEUTRO: 4 corridas de 140 s no palacio,
alternadas, com claude_runs/fps_aligned.py --

  sem restrict (-O1)   fps mediana 22,0 (746 draws) e 23,0 (724 draws)
  com restrict (-O1)   fps mediana 23,0 (758 draws) e 22,0 (773 draws)

A diferenca de media (21,75 -> 22,31) cabe dentro da dispersao das proprias
repeticoes de cada lado. Ou seja: o Clang ja' estava a tratar bem o aliasing do
contexto, ou o gargalo esta' noutro sitio (o giant lock fica segurado ~93% do
relogio). O patch fica porque e' correcto e nao custa nada, NAO porque acelera.

Idempotente: se o marcador ja' esta' no header, reporta ALREADY.
Usage: patch_ctx_restrict.py <lift_dir>    rc 0 ok / 2 sem lift / 3 needle ausente
"""
from __future__ import annotations

import glob
import os
import re
import sys

MARK = "PPU_RESTRICT"
PREAMBLE = """\
/* Qualificador de aliasing do contexto liftado (patch_ctx_restrict.py).
 * O ppu_context e' uma struct do host; a memoria guest vive em vm_base. Nenhum
 * escritor de memoria guest alcanca o contexto, logo o Clang pode manter
 * ctx->gpr/cr/xer em registradores entre acessos a memoria guest. */
#ifndef PPU_RESTRICT
#  if defined(__clang__) || defined(__GNUC__) || defined(_MSC_VER)
#    define PPU_RESTRICT __restrict
#  else
#    define PPU_RESTRICT
#  endif
#endif
"""
HDR_NEEDLE = "#include <stdint.h>\n"

# `void func_00010200(ppu_context* ctx)` e `PPC_FUNC_IMPL(...)` nao sao tocadas
# pela mesma regra: a primeira e' textual, a segunda expande dentro do header.
DEF_RE = re.compile(r"\b((?:__imp_)?[A-Za-z0-9_]*func_[0-9A-Fa-f]{8})\(ppu_context\* ctx\)")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    d = sys.argv[1]
    hdr = os.path.join(d, "ppu_recomp.h")
    chunks = sorted(glob.glob(os.path.join(d, "ppu_recomp_*.cpp")))
    if not os.path.isfile(hdr) or not chunks:
        print(f"restrict: sem lift em {d}", file=sys.stderr)
        return 2

    h = open(hdr).read()
    if MARK in h:
        print("restrict: header ALREADY")
    else:
        if HDR_NEEDLE not in h:
            print(f"restrict: needle ausente em {hdr}", file=sys.stderr)
            return 3
        h = h.replace(HDR_NEEDLE, HDR_NEEDLE + PREAMBLE, 1)

    total = 0
    h2, n = DEF_RE.subn(r"\1(ppu_context* PPU_RESTRICT ctx)", h)
    total += n
    open(hdr, "w").write(h2)
    print(f"restrict: header decls={n}")

    for c in chunks:
        s = open(c).read()
        s2, n = DEF_RE.subn(r"\1(ppu_context* PPU_RESTRICT ctx)", s)
        if n:
            open(c, "w").write(s2)
        total += n
    print(f"restrict: total={total} sitios em {len(chunks)} chunks + header")
    return 0


if __name__ == "__main__":
    sys.exit(main())
