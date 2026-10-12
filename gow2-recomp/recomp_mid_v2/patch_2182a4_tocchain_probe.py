#!/usr/bin/env python3
"""Sonda da cadeia do TOC que escolhe o `obj` em `func_002182A4`.

Porque existe (E19, 2026-08-05)
-------------------------------
O descritor do gestor de pools NAO vem do `this`: vem de `r29 = r3 + 0x118`, e
`r3` e' carregado de uma cadeia ancorada no TOC. Medido:

    sas  obj=0x406388F8 -> r29=0x40638A10 -> base=0x40773710  divisor=128  (12x)
    ma   obj=0x400C6C68 -> r29=0x400C6D80 -> base=0x4007FCE8  divisor=130  ( 1x)

A cadeia, no lift (ppu_recomp_000.cpp):

    492839  A = *(r2 - 0x2B18)     <- ancora no TOC
    492864  B = *(A + 0xC)         -> r3 = B  (e' o `obj`)
    492870  C = *(B + 0x0)
    492874  D = *(C + 0x50)
    492882  r29 = B + 0x118

A pergunta unica que resta nesta frente: **em qual destes quatro passos as duas
familias divergem?** Se A ja' difere, o problema esta' a montante do TOC; se so'
B difere, a celula +0xC e' que muda; e assim por diante.

O que mede
----------
Uma linha por chamada, no ponto onde r29 acaba de ser calculado. A e' relido
(o r10 original foi sobrescrito em 492870, mas o TOC em r2 continua valido).

Gate: `PS3_TRACE_TOCCHAIN` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_TOCCHAIN_CAP` (default 200).

Uso:  patch_2182a4_tocchain_probe.py [DIR_DE_LIFT]
rc: 0 aplicado ou ja' aplicado; 2 se a agulha nao casou (SEM-EFEITO).
"""
import os
import sys
import glob

MARKER = "TOCCHAIN-2182A4-PROBE"

# Verificada unica no lift a 2026-08-05 (1 ocorrencia em ppu_recomp_000.cpp).
NEEDLE = (
    "        ctx->gpr[0] = ctx->gpr[3] + (int64_t)(0x118);\n"
    "        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);\n"
    "        ctx->gpr[29] = ppc_rldicl(ctx->gpr[0], 0, 32);\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": os quatro degraus da cadeia do TOC ate' ao obj.\n"
    "         * Ver docs/re_sessions/2026-08-05-E19-o-descritor-vem-do-TOC.md */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_TOCCHAIN\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_TOCCHAIN_CAP\"); _cap=(_c&&*_c)?atoi(_c):200; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _A=vm_read32((uint32_t)ctx->gpr[2] - 0x2B18u);   /* relido */\n"
    "            uint32_t _B=(uint32_t)ctx->gpr[3];                        /* o obj */\n"
    "            uint32_t _C=(uint32_t)ctx->gpr[10];\n"
    "            uint32_t _D=(uint32_t)ctx->gpr[11];\n"
    "            fprintf(stderr,\"[TOCCHAIN] #%d A=0x%08X B=0x%08X%s C=0x%08X D=0x%08X r29=0x%08X\\n\",\n"
    "              _n, _A, _B, (_B==0x400C6C68u)?\" <B-MAU>\":\"\", _C, _D,\n"
    "              (uint32_t)ctx->gpr[29]);\n"
    "            fflush(stderr); } } }\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ja"
    if t.count(NEEDLE) != 1:
        return t, "nao-casou"
    return t.replace(NEEDLE, REPL, 1), "ok"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(here), "recomp_macos_v2")

    alvos = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not alvos:
        print("SEM-EFEITO: nenhum ppu_recomp_*.cpp em %s" % lift)
        return 2

    aplicados = ja = 0
    for f in alvos:
        with open(f, encoding="utf-8", errors="replace") as fh:
            src = fh.read()
        novo, estado = patch_text(src)
        if estado == "ja":
            ja += 1
            print("JA-APLICADO  %s" % os.path.basename(f))
        elif estado == "ok":
            with open(f, "w", newline="\n", encoding="utf-8") as fh:
                fh.write(novo)
            aplicados += 1
            print("CONVERTIDO   %s" % os.path.basename(f))

    if aplicados == 0 and ja == 0:
        print("SEM-EFEITO: agulha da cadeia do TOC nao casou em %s" % lift)
        return 2

    print("patch_2182a4_tocchain_probe: aplicados=%d ja=%d" % (aplicados, ja))
    return 0


if __name__ == "__main__":
    sys.exit(main())
