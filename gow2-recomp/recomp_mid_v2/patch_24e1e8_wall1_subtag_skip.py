#!/usr/bin/env python3
"""Fase 11 (parede 1) -- FIX: poda por subtag no dispatch de `func_0024E1E8`
(e nos fragmentos irmaos gerados pelo lifter para o mesmo padrao: chunk
`.opd.FUN_0024e198` no Ghidra).

Contexto medido (ver docs/re_sessions/2026-08-05-E6-parede1-mesma-doenca.md,
previsoes commitadas ANTES desta sonda/fix):

  - `func_0024E1E8` resolve a fabrica de um objecto so' pelo `low16` do seu
    cabecalho (`idx = (vm_read16(obj+2) << 2) & 0x3FFFC; fab = tab[idx]`) e
    invoca incondicionalmente `fab->vt[0x18](fab, obj)` -- SEM olhar para o
    `subtag` (os 12 bits altos do mesmo w0). E' a MESMA doenca da parede 4
    (docs/re_sessions/2026-08-04-E2-quem-forca-low16.md): o desserializador
    `0x00254C40` preserva o subtag mas forca low16:=1, e um registo WAD
    (subtag=3) fica indistinguivel de um container de lista (subtag=1) na
    hora do despacho.
  - Cascata medida (3/3 corridas, `PS3_TRACE_B71`): `func_0024E1E8` ->
    `func_0039D428` -> `func_0039D764` -> `func_002545B0` (a parede 1). O
    metodo final le `arg+0x7C` como sentinela de lista intrusiva -- mas no
    objecto WAD medido esse offset e' `matriz[1][0]` (0.0f, escrito por
    `func_0024C1F8`), `head=0`, e o walk nunca fecha:
    `[ppu] FATAL: stuck calling 0x00514E80 (2000 times)`, 6/6 corridas
    (E5, `docs/re_sessions/2026-08-04-E5-aceite-parede-4.md`).
  - Censo E6 (`PS3_TRACE_TYPETAG_W0`, 3/3 corridas deterministicas): o
    objecto que chega a este dispatch com `w0=0x40030001` (`subtag=3,
    low16=1`) e' o UNICO que passa por este sitio numa corrida inteira
    (`seen=1 would_prune=1` nas 3) -- filtro cirurgico, sem risco medido de
    podar um caso legitimo.

Onde vai o fix: logo a seguir ao calculo do indice (`ctx->gpr[11] =
vm_read32((ctx->gpr[23] + ctx->gpr[0])` -- a mesma agulha de
`patch_24e2d4_typetag_probe.py`/`patch_24e2d4_typetag_w0_probe.py`, NAO
reinventada), le o `w0` COMPLETO do proprio objecto (`ctx->gpr[21]`,
inalterado entre a agulha e a chamada -- confirmado no disassembly do
EBOOT.ELF: nenhuma instrucao escreve r21 nesse intervalo) e guarda a decisao
de poda numa flag local. Mais abaixo, envolve APENAS a chamada
`ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);` -- e so' essa -- num
`if (!skip)`. Tudo o que fica ENTRE (as sondas TYPETAG/TYPETAG-W0/B71, se
estiverem instaladas) e' preservado byte a byte; este patch nao depende de
elas estarem presentes.

Predicado identico, byte a byte, ao `tools/test_wall4_subtag_fix.py::
prune(w0)`: `low16(w0)==1 and subtag(w0)!=1`. Incondicional (sem gate
`PS3_*`) -- e' um fix, nao um interruptor de diagnostico (CLAUDE.md regra
5, D-11.1 candidato B: "filtrar tambem por subtag, nao so' por flags/low16",
aqui aplicado ao dispatcher generico em vez do walk exterior de shaders).

Candidato C (validar a lista antes de a percorrer, em `func_002545B0`) e'
PROIBIDO como fix -- este patch nao o e': nao toca em `func_002545B0` nem no
que ele desreferencia; filtra o DESPACHO, antes de a chamada acontecer, pela
classificacao do proprio objecto -- a definicao de B.

Esta funcao dispatcher aparece varias vezes no lift, como fragmentos
distintos (`func_0024E1E8`, `func_0024E208`, `func_0024E26C`, ...) -- o
MESMO padrao de codigo copiado pelo lifter para cada ponto de entrada
alcancavel deste bloco. O patch aplica-se a TODOS os fragmentos com esta
agulha, em TODOS os chunks -- e' o mesmo padrao de "a agulha repete-se entre
chunks" ja documentado por `patch_254628_list_probe.py`.

Idempotente: procura o marcador logo a seguir a cada ocorrencia da agulha;
se ja' la' estiver, conta como ALREADY para essa ocorrencia e nao mexe.

Tri-estado (D-4.1 do 04-05-PLAN.md, CLAUDE.md regra do "patch_*.py
idempotente com tri-estado"):
  CONVERTED        -> rc=0, escreveu pelo menos um sitio novo
  ALREADY-APPLIED  -> rc=0, nao escreveu nada, MAS todos os sitios ja' tinham
                       o marcador (pos-condicao verdadeira)
  NO-EFFECT        -> rc=2, nao escreveu nada e a pos-condicao e' falsa (a
                       agulha nao foi encontrada em nenhum chunk -- o lift
                       mudou de forma, ou o dir esta errado)

Uso:  patch_24e1e8_wall1_subtag_skip.py [DIR_DE_LIFT]
"""
from __future__ import annotations

import glob
import os
import sys

MARKER = "PAREDE1-E6-SUBTAG-SKIP-FIX"

PREFIX = (
    "        ctx->gpr[0] = vm_read16(ctx->gpr[21] + 0x2);\n"
    "        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[0], 2, 14, 29);\n"
    "        ctx->gpr[4] = ctx->gpr[21] | ctx->gpr[21];\n"
    "        ctx->gpr[11] = vm_read32((ctx->gpr[23] + ctx->gpr[0]));\n"
)

CALL = "ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"

SKIP_BLOCK = (
    "        /* " + MARKER + " -- ver docs/re_sessions/2026-08-05-E6-parede1-mesma-doenca.md.\n"
    "         * mesma doenca da parede 4, sitio diferente: fab=tab[low16(obj)]\n"
    "         * ignora o subtag nos 12 bits altos. Um registo WAD (subtag=3)\n"
    "         * colide com low16=1 (familia de listas) e seria despachado\n"
    "         * para um metodo que le +0x7C do objecto como lista intrusiva --\n"
    "         * mas nesse objecto e' matriz[1][0] (0.0f), head=0, walk nunca\n"
    "         * fecha (FATAL stuck calling 0x00514E80, medido 6/6). Censo E6\n"
    "         * (3/3 corridas): SO' este objecto passa por este sitio numa\n"
    "         * corrida inteira (seen=1 would_prune=1). Predicado identico ao\n"
    "         * de tools/test_wall4_subtag_fix.py::prune(w0). Incondicional\n"
    "         * (sem gate PS3_) -- fix, nao diagnostico.\n"
    "         *\n"
    "         * CORRECCAO (medida por build real, nao so' pela agulha de texto):\n"
    "         * a v1 desta sonda declarava `ps3_e6w1_skip` ao nivel do corpo da\n"
    "         * funcao (sem `{}` proprio) -- compilou limpo enquanto so' havia UM\n"
    "         * sitio por funcao, mas 3 dos 8 fragmentos irmaos (000/002/004)\n"
    "         * tem o MESMO padrao repetido 2+ vezes DENTRO da mesma funcao\n"
    "         * (mesmo dispatcher, chamado de novo mais abaixo no corpo lifted),\n"
    "         * e o clang++ recusou com \"redefinition of 'ps3_e6w1_skip'\" --\n"
    "         * 8 erros reais em 3 chunks, medido por `clang++ -c` a serio, nao\n"
    "         * inferido da agulha de texto. Fix: TODO o bloco (comentario +\n"
    "         * flag + verificacao + chamada guardada) fica dentro de um UNICO\n"
    "         * par de chavetas por ocorrencia -- cada ocorrencia tem o seu\n"
    "         * proprio escopo, nao ha colisao de nome entre ocorrencias\n"
    "         * irmas na mesma funcao. */\n"
    "        {\n"
    "        bool ps3_e6w1_skip = false;\n"
    "        {\n"
    "            uint32_t _e6obj = (uint32_t)ctx->gpr[21];\n"
    "            if (_e6obj >= 0x10000u && _e6obj < 0x4F000000u) {\n"
    "                uint32_t _e6w0 = vm_read32(_e6obj);\n"
    "                uint32_t _e6low16 = _e6w0 & 0xFFFFu;\n"
    "                uint32_t _e6subtag = (_e6w0 >> 16) & 0xFFFu;\n"
    "                if (_e6low16 == 1u && _e6subtag != 1u) {\n"
    "                    ps3_e6w1_skip = true;\n"
    "                    fprintf(stderr, \"[PAREDE1-SKIP] obj=0x%08X w0=0x%08X"
    " subtag=0x%03X -- dispatch podado (fix E6, nao gate)\\n\",\n"
    "                        _e6obj, _e6w0, _e6subtag);\n"
    "                    fflush(stderr);\n"
    "                }\n"
    "            }\n"
    "        }\n"
)

# Fecha o par de chavetas aberto em SKIP_BLOCK -- escrito DEPOIS da chamada
# guardada, para que `ps3_e6w1_skip` fique confinado a esta ocorrencia so'.
SKIP_BLOCK_CLOSE = "        }\n"


def process(text: str) -> tuple[str, int, int]:
    """Devolve (novo_texto, sitios_convertidos, sitios_ja_aplicados)."""
    out = []
    pos = 0
    converted = 0
    already = 0
    while True:
        i = text.find(PREFIX, pos)
        if i < 0:
            out.append(text[pos:])
            break
        prefix_end = i + len(PREFIX)
        lookahead = text[prefix_end:prefix_end + 200]
        out.append(text[pos:prefix_end])
        if MARKER in lookahead:
            already += 1
            pos = prefix_end
            continue
        j = text.find(CALL, prefix_end)
        if j < 0:
            raise RuntimeError(
                "PREFIX encontrado em offset %d mas nenhum ps3_indirect_call "
                "a seguir -- forma do lift mudou, abortando (nao escrevo "
                "parcial)" % i
            )
        call_end = j + len(CALL)
        out.append(SKIP_BLOCK)
        out.append(text[prefix_end:j])
        out.append("        if (!ps3_e6w1_skip) {\n    ")
        out.append(text[j:call_end])
        out.append("        }\n")
        out.append(SKIP_BLOCK_CLOSE)
        pos = call_end
        converted += 1
    return "".join(out), converted, already


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not chunks:
        print("ERRO: nenhum ppu_recomp_*.cpp em %s" % lift, file=sys.stderr)
        return 2

    total_converted = 0
    total_already = 0
    files_written = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        try:
            out, converted, already = process(src)
        except RuntimeError as exc:
            print("FALHA em %s: %s" % (os.path.basename(path), exc), file=sys.stderr)
            return 2
        total_converted += converted
        total_already += already
        if converted:
            with open(path, "w") as fh:
                fh.write(out)
            files_written += 1
            print("APPLIED  %-20s sitios_novos=%d ja_tinha=%d"
                  % (os.path.basename(path), converted, already))
        elif already:
            print("ALREADY  %-20s sitios_ja_tinha=%d" % (os.path.basename(path), already))

    if total_converted:
        print("patch_24e1e8_wall1_subtag_skip: CONVERTED sitios_novos=%d ja_tinha=%d"
              % (total_converted, total_already))
        return 0
    if total_already:
        print("patch_24e1e8_wall1_subtag_skip: ALREADY-APPLIED sitios=%d"
              % total_already)
        return 0
    print(
        "patch_24e1e8_wall1_subtag_skip: NO-EFFECT -- agulha (PREFIX) nao "
        "encontrada em nenhum chunk de %s; forma do lift mudou ou dir errado"
        % lift,
        file=sys.stderr,
    )
    return 2


if __name__ == "__main__":
    sys.exit(main())
