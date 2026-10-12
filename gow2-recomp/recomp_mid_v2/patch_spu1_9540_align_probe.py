#!/usr/bin/env python3
"""Sonda do erro de alinhamento da LFQueue, do lado SPU (`spu1`).

Porque existe (E69/E70)
-----------------------
Um ciclo de DMAs de SPU fora de alcance (`ea=0x00003300`) dispara em ~36% das
corridas (4 em 11) e e' sempre precedido por:

    [ATTERR]   a=0x80410910
    [EDGEATT1] a=0x0000BA10 b=0x80410910

`0x80410910` e' `CELL_SPURS_LFQUEUE_ERROR_ALIGN`. O E69 atribuiu-o ao nosso
`LFQ_ERR_ALIGN` em `libs/spurs/cellSpursLFQueue.c`; instrumentados os tres
sitios, deram ZERO em quatro corridas, incluindo as duas que mostraram o ciclo
(E70). Vem do codigo SPU do proprio jogo:

    void spu1_spu_func_00009524(spu_context* ctx) {
        r18 = ls_read128(r80);
        r17 = rotqby(r18, r80);
        r16 = andi(r17, 15);            // buffer & 15
        if (r16 != 0) goto 0x9540;      // -> ERRO
        r20 = shlqbyi(r8, 4);
        r19 = andi(r20, 127);           // queue & 127
        if (r19 == 0) goto 0x954C;      // OK
        goto 0x9540;                    // -> ERRO
    }
    void spu1_spu_func_00009540(spu_context* ctx) {
        r7 = ilhu(0x8041); r7 = iohl(r7, 0x910);   // 0x80410910
        goto 0x9658;                                // epilogo: r3 = r7
    }

E' o MESMO par de invariantes que a nossa LFQueue verifica -- fila alinhada a
128, buffer a 16 -- so' que aqui quem verifica e' o guest, no SPU.

O que esta sonda responde
-------------------------
QUAL dos dois falha, e com que valor. Sem isso nao da' para saber se o defeito
esta' no endereco da fila ou no do buffer, e sao origens diferentes.

Imprime no sitio do erro (`0x9540`), com os dois valores brutos ANTES das
mascaras: `r17` (buffer) e `r20` (fila). Quem le' a linha faz `&15` e `&127` e
ve' logo qual disparou.

Nota de convencao: e' o primeiro patch para o `spu_lifted/`. O `build_macos.sh`
NAO regenera essas arvores (compila os ficheiros que la' estao), logo uma edicao
a mao sobrevive ao build -- mas nao a um re-lift de SPU. Este script existe para
esse caso, e segue o mesmo contrato dos `patch_*.py` do PPU: idempotente, rc=2
se a agulha nao casar.

Gate: `PS3_SPU_DBG` (o mesmo que as sondas `[ATTERR]`/`[EDGEATT1]` ja' usam via
`spu_dbg_log`).

A v1 copiou das vizinhas o `if (n < 12)` -- cap no PRINT, contagem invisivel --
depois de eu ter passado um dia a corrigir exactamente esse padrao no PPU
(E63/E64). Deu 12 linhas em 12, ou seja o cap batido, e eu quase generalizei a
partir das duas primeiras (E71/E72). A v2 usa as primeiras 16 mais cada potencia
de 2, como o resto da arvore.

Uso:  patch_spu1_9540_align_probe.py [DIR_DO_SPU_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys

MARKER = "LFQALIGN-SPU-V2"

NEEDLE = (
    "void spu1_spu_func_00009540(spu_context* ctx) {\n"
    "        ctx->gpr[7] = spu_ilhu(0x8041);\n"
)

REPL = (
    "void spu1_spu_func_00009540(spu_context* ctx) {\n"
    "        /* " + MARKER + ": qual invariante falhou -- r17 = buffer, r20 = fila.\n"
    "         * Ver recomp_mid_v2/patch_spu1_9540_align_probe.py (ledger E70). */\n"
    "        { static unsigned long n = 0; ++n;\n"
    "          if (n <= 16 || (n & (n - 1)) == 0) {\n"
    "            extern int spu_dbg_log(const char* fmt, unsigned a, unsigned b);\n"
    "            spu_dbg_log(\"[LFQALIGN-SPU] buffer=0x%08X fila=0x%08X\\n\",\n"
    "                        ctx->gpr[17]._u32[0], ctx->gpr[20]._u32[0]); } }\n"
    "        ctx->gpr[7] = spu_ilhu(0x8041);\n"
)


BLOCK_V1 = (
    '        /* LFQALIGN-SPU: qual invariante falhou -- r17 = buffer, r20 = fila.\n'
    '         * Ver recomp_mid_v2/patch_spu1_9540_align_probe.py (ledger E70). */\n'
    '        { static int n = 0; if (n < 12) { n++;\n'
    '            extern int spu_dbg_log(const char* fmt, unsigned a, unsigned b);\n'
    '            spu_dbg_log("[LFQALIGN-SPU] buffer=0x%08X fila=0x%08X\\n",\n'
    '                        ctx->gpr[17]._u32[0], ctx->gpr[20]._u32[0]); } }\n'
)

def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    t = t.replace(BLOCK_V1, "")   # remove a v1 (cap de 12) antes de por a v2
    n = t.count(NEEDLE)
    if not n:
        return t, "MISSING", 0
    return t.replace(NEEDLE, REPL), "APPLIED", n


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "spu_lifted", "spu1_v2")
    path = os.path.join(os.path.abspath(root), "spu_recomp.c")
    if not os.path.exists(path):
        print("ERRO: %s nao existe" % path, file=sys.stderr)
        return 2
    with open(path, "r", errors="replace") as fh:
        src = fh.read()
    out, state, n = patch_text(src)
    if state == "APPLIED":
        with open(path, "w") as fh:
            fh.write(out)
        print("APPLIED  spu1_v2/spu_recomp.c  sites=%d" % n)
        return 0
    if state == "ALREADY":
        print("ALREADY  spu1_v2/spu_recomp.c")
        return 0
    print("MISSING  agulha do prologo de spu1_spu_func_00009540 nao encontrada",
          file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
