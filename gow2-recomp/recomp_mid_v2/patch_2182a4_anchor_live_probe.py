#!/usr/bin/env python3
"""Sonda da ancora VIVA em `func_002182A4` -- captura, nao recalculo.

Porque existe (E22, 2026-08-05)
-------------------------------
A sonda `TOCCHAIN` imprimia a ancora `A` assim:

    uint32_t _A = vm_read32((uint32_t)ctx->gpr[2] - 0x2B18u);   /* relido */

Isso e' um **recalculo**, nao uma captura: no ponto onde a sonda corre (492884)
o `gpr[10]` que o programa usou em 492864 ja' foi sobrescrito (492870). Estava
a medir aritmetica minha, deterministica -- e por isso `A` saia "constante" nas
13 chamadas.

A vigia de traducao provou-o: ZERO leituras de `0x00868D54` numa corrida com 13
chamadas, quando cada uma teria de traduzir esse endereco se `B = *(A+0xC)`.
Ver docs/re_sessions/2026-08-05-E22-a-sonda-que-recalculava.md.

O que esta sonda faz de diferente
---------------------------------
Corre **imediatamente a seguir** a `gpr[11] = vm_read32(gpr[10] + 0xC)`, onde
os tres valores ainda estao vivos:

    A    = gpr[10]   a ancora REAL usada na leitura (sobrescrita em 492870)
    B    = gpr[11]   o resultado da leitura
    this = gpr[3]    o argumento de entrada (so' e' sobrescrito em 492868)

Com A vivo sabe-se qual e' a celula verdadeira, e so' entao faz sentido
apontar-lhe o `PS3_WATCH_EA`.

Gate: `PS3_TRACE_ANCHOR` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_ANCHOR_CAP` (default 200).

Uso:  patch_2182a4_anchor_live_probe.py [DIR_DE_LIFT]
rc: 0 aplicado ou ja' aplicado; 2 se a agulha nao casou (SEM-EFEITO).
"""
import os
import sys
import glob

MARKER = "ANCHOR-LIVE-2182A4-PROBE"

# Verificada unica no lift a 2026-08-05 (1 ocorrencia em ppu_recomp_000.cpp).
NEEDLE = (
    "        ctx->gpr[11] = vm_read32(ctx->gpr[10] + 0xC);\n"
    "        ctx->gpr[31] = ctx->gpr[3] | ctx->gpr[3];\n"
)

REPL = (
    "        ctx->gpr[11] = vm_read32(ctx->gpr[10] + 0xC);\n"
    "        /* " + MARKER + ": A, B e this VIVOS, antes de gpr[10] e gpr[3]\n"
    "         * serem sobrescritos (492870 e 492868). Ver E22. */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_ANCHOR\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_ANCHOR_CAP\"); _cap=(_c&&*_c)?atoi(_c):200; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _A=(uint32_t)ctx->gpr[10];\n"
    "            uint32_t _B=(uint32_t)ctx->gpr[11];\n"
    "            uint32_t _T=(uint32_t)ctx->gpr[3];\n"
    "            fprintf(stderr,\"[ANCHOR] #%d A=0x%08X celula=0x%08X B=0x%08X this=0x%08X%s\\n\",\n"
    "              _n, _A, _A+0xCu, _B, _T,\n"
    "              (_B==0x400C6B50u)?\" <B-MAU>\":\"\");\n"
    "            fflush(stderr); } } }\n"
    "        ctx->gpr[31] = ctx->gpr[3] | ctx->gpr[3];\n"
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
        print("SEM-EFEITO: agulha da ancora viva nao casou em %s" % lift)
        return 2

    print("patch_2182a4_anchor_live_probe: aplicados=%d ja=%d" % (aplicados, ja))
    return 0


if __name__ == "__main__":
    sys.exit(main())
