#!/usr/bin/env python3
"""Probe gated no ENTRY de func_002B3D1C -- despeja o layout do op de async-read.

Porque: `func_002B3D1C` e' o unico choke point do read assincrono em bloco do
FIOS (2 call sites directos no chunk 001, mais espelhos noutros chunks). O plano
`ps3recomp/docs/superpowers/plans/2026-07-20-fios-async-read-movie-io-hle.md`
(Task 2) exige MEDIR o layout do op antes de fixar qualquer offset no HLE.

O que o lift ESTATICO ja diz (nao e' o mesmo que provado em runtime):

    ABI      r3 = op, r4 = dst EA, r5 = n bytes pedidos
    op+0x04  handle passado como r5 a func_0030FC00 (submit de I/O)
    op+0x08  status/resultado do submit (escrito por func_002B3E0C)
    op+0x0C  flags; o bit 0x200 (rlwinm 0,22,22) escolhe o ramo deferido, que
             escreve de volta flags|0x4000 (marca "in-flight")
    op+0x10  limite/fim
    op+0x14  cursor -- avanca por `n` (ou pelo restante, no ramo func_002B3DD8)

NADA disto fica travado no HLE (Tasks 3-5) so' com a leitura estatica. A
confirmacao viva faz-se com esta probe: entre chamadas consecutivas do MESMO op,
+14 tem de avancar exactamente pelo `r5` da chamada anterior (ou pelo restante,
se houve clamp) e +10 tem de ficar constante. Sem esse par de observacoes, o
campo continua nao-provado e nao pode ser assumido pelo HLE.

So' LE (vm_read32 tem guarda de out-of-bounds e devolve 0), nunca escreve no
guest: com PS3_TRACE_AREAD desligada e' um no-op exacto sobre o baseline
(regra 6 do CLAUDE.md -- probes gated por env, OFF por default).

Saida: [AREAD] #N op=0x… r4=… r5=… +00=… +04=… +08=… +0C=… +10=… +14=… +18=… +1C=…

Uso:  patch_2b3d1c_probe.py [DIR_DE_LIFT]     (default: o dir deste ficheiro)
Reaplicado por ../apply_all_patches.sh apos cada re-lift. Idempotente: a 2a
corrida nao reescreve o ficheiro (fica ALREADY-APPLIED no catalogo).

Nota de implementacao: a substituicao e' `str.replace()` textual exacta, NAO
`re.sub` com string de substituicao -- essa interpreta escapes e ja' meteu uma
quebra de linha REAL dentro de um literal C, gerando fonte que nao compila.
Ficheiro alvo tipico: ppu_recomp_001.cpp (localizado por nome, nao fixo).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

MARKER = "AREAD-PROBE"
FUNC = "void func_002B3D1C"

SIG = "void func_002B3D1C(ppu_context* ctx) {\n"

# 1o statement do corpo: o `cmpwi r5,0` da entrada. Serve de ancora -- confirma
# que r5 continua a ser a contagem de bytes -- e e' re-emitido intacto a seguir
# a probe (a probe INSERE, nao substitui codigo do jogo).
FIRST = (
    "        { int64_t a = (int32_t)ctx->gpr[5]; int64_t b = (int64_t)0;"
    " uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2;"
    " ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }"
)

# Raw string: o `\n` fica literal (backslash + n) dentro do literal C.
PROBE = r'''        /* AREAD-PROBE: dump gated do op de async-read FIOS (PS3_TRACE_AREAD=1).
         * ABI do lift: r3=op, r4=dst EA, r5=n. So leitura; OFF por default. */
        { static int on=-1; if(on<0){extern char* getenv(const char*);
            const char* e=getenv("PS3_TRACE_AREAD"); on=(e&&*e&&*e!='0')?1:0;}
          if(on){ static int n=0; if(n++<256){
            uint32_t _op=(uint32_t)ctx->gpr[3];
            fprintf(stderr,
              "[AREAD] #%d op=0x%08X r4=0x%08X r5=%u"
              " +00=%08X +04=%08X +08=%08X +0C=%08X"
              " +10=%08X +14=%08X +18=%08X +1C=%08X\n",
              n, _op, (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5],
              _op?vm_read32(_op+0x00):0u, _op?vm_read32(_op+0x04):0u,
              _op?vm_read32(_op+0x08):0u, _op?vm_read32(_op+0x0C):0u,
              _op?vm_read32(_op+0x10):0u, _op?vm_read32(_op+0x14):0u,
              _op?vm_read32(_op+0x18):0u, _op?vm_read32(_op+0x1C):0u);
            fflush(stderr); } } }
'''

# CORRECCAO 2026-07-25 (shape-callee-save): o lifter passou a emitir, logo a
# seguir a assinatura, uma sombra dos registos nao-volateis --
#     uint64_t _cs_30 = ctx->gpr[30];
#     uint64_t _cs_31 = ctx->gpr[31];
# -- antes do 1o statement do jogo. A needle literal `SIG + FIRST` deixou de
# casar por causa dessas linhas (medido: "prologo nao bate com a needle").
# Passa a casar-se SIG + (bloco opcional de _cs_NN) + FIRST por regex, e a
# probe e' inserida ENTRE o bloco de callee-save e o FIRST -- assim continua a
# casar com o lift antigo (bloco vazio) e com o novo, e nao se mete codigo
# entre a assinatura e a captura dos nao-volateis.
CS_BLOCK_RE = r"(?:[ \t]*uint64_t _cs_\d+ = ctx->gpr\[\d+\];\n)*"
NEEDLE_RE = re.compile(re.escape(SIG) + "(" + CS_BLOCK_RE + ")" + re.escape(FIRST))


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent

    target = None
    for path in sorted(root.glob("ppu_recomp_*.cpp")):
        if FUNC in path.read_text(encoding="utf-8", errors="replace"):
            target = path
            break
    if target is None:
        raise SystemExit(f"func_002B3D1C ausente no lift em {root}")

    src = target.read_text(encoding="utf-8", errors="replace")

    # Regiao = corpo da funcao, ate' a proxima definicao. So' se procura o
    # marcador AQUI: outra funcao pode ganhar uma probe de nome parecido.
    i = src.find(FUNC)
    j = src.find("void func_", i + 10)
    region = src[i:j] if j > i else src[i:]

    if MARKER in region:
        print(f"{target.name}: 002B3D1C probe ja aplicada")
        return 0

    m = NEEDLE_RE.search(region)
    if m is None:
        raise SystemExit(
            f"{target.name}: prologo de func_002B3D1C nao bate com a needle "
            "-- o shape do lift mudou; reveja a probe antes de forcar"
        )

    # Eco do que o lift estatico mostra, para a medicao nao ter de reler o
    # chunk a mao. Nao gateia nada: e' informativo.
    seen = [
        name
        for name, frag in (
            ("+14 cursor", "vm_write32(ctx->gpr[31] + 0x14"),
            ("+10 limite", "vm_read32(ctx->gpr[31] + 0x10)"),
            ("+0C flags", "vm_read32(ctx->gpr[31] + 0xC)"),
            ("in-flight |0x4000", "| 0x4000"),
        )
        if frag in region
    ]
    print(f"{target.name}: layout estatico visivel -> {', '.join(seen) or '(nenhum)'}")

    # Insercao textual no ponto medido pela regex (fim do bloco de callee-save,
    # imediatamente antes do FIRST) -- nunca re.sub, que interpretaria os
    # escapes do literal C da probe (ver nota de implementacao no docstring).
    cut = m.end(1)
    region = region[:cut] + PROBE + region[cut:]
    src = src[:i] + region + src[j:]
    target.write_text(src, encoding="utf-8", newline="\n")
    print(f"{target.name}: probe AREAD-PROBE inserida em func_002B3D1C")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
