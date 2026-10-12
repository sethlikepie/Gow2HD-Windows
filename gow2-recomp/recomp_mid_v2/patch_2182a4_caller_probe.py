#!/usr/bin/env python3
"""Sonda do CHAMADOR de `func_002182A4` -- quem a invoca com o objecto errado?

Porque esta sonda existe (medido a 2026-08-05, E13)
---------------------------------------------------
`func_002182A4` escolhe o pool que vai ao pop da free-list. Com
`PS3_TRACE_POOLIDX=all`, 2 corridas de 90 s na receita menu-fast:

    sas  obj=0x406388F8 divisor=128 base=0x40773710/0x407736F0   (12 chamadas)
    ma   obj=0x400C6C68 divisor=130 base=0x4007FCE8              ( 1 chamada)

O descritor `r29` da chamada ma nao e' um gestor de pools: `*(r29+0)` da' 130
(nao e' potencia de dois) e `*(r29+0x14)` aponta para uma tabela cuja celula 0
contem **5** -- uma contagem, nao um ponteiro. Dai' sai a cadeia inteira ate'
as 16 escritas UNCOMMITTED em `func_00220284` (ver docs/re_sessions/
2026-08-05-E13-o-objecto-errado.md).

Detalhe que motiva medir o CHAMADOR e nao o objecto: o `base` das chamadas sas
VARIA entre corridas (0x40773710 -> 0x407736F0, heap a mexer-se), enquanto o da
chamada ma e' byte a byte o mesmo nas duas. Isso aponta para um objecto
estatico ou alocado muito cedo -- e `0x400C6C68` cai na vizinhanca 0x400Cxxxx
da fabrica (`this=0x400C5048`) e do "objecto de Julho" (`this=0x400C61C8`)
mapeados a 2026-08-01.

Porque nao chega a analise estatica
-----------------------------------
`func_002182A4` **nao tem um unico call site estatico** no lift: a unica
ocorrencia do simbolo e' a entrada na tabela de funcoes
(`ppu_recomp_006.cpp:393855`). E' alcancada so' por despacho indirecto, o que
por si ja' e' consistente com o diagnostico de "metodo sobre o objecto errado".

Por isso o chamador tem de ser medido em runtime. Usa-se `ctx->lr` (o LR do
GUEST, capturado antes de qualquer `bl` interno o sobrescrever), e NAO o stack
do host: o `ra` do host e' retorno de trampolim e varia entre corridas.

O que mede
----------
Uma linha por entrada em `func_002182A4`:

    this  = r3      -- o objecto (esperado 0x406388F8; o mau e' 0x400C6C68)
    lr    = ctx->lr -- o chamador GUEST, que e' a incognita
    r4/r5           -- os primeiros argumentos, para desempatar sobrecargas

Sem filtro: sao ~13 chamadas por corrida e o que interessa e' ver as boas ao
lado da ma, como no E13. O cap existe so' como rede.

Gate: `PS3_TRACE_CALLER2182A4` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_CALLER2182A4_CAP` (default 200).

Uso:  patch_2182a4_caller_probe.py [DIR_DE_LIFT]
rc: 0 aplicado ou ja' aplicado; 2 se a agulha nao casou (SEM-EFEITO).
"""
import os
import sys
import glob

MARKER = "CALLER-2182A4-PROBE"

# Prologo de func_002182A4. Verificado unico no lift (1 ocorrencia em
# ppu_recomp_000.cpp) a 2026-08-05 -- o acesso ao TOC em -0x2B18 e' o que o
# torna distinto. Se um re-lift mudar o prologo, esta sonda passa a SEM-EFEITO
# (rc=2) em vez de casar noutro sitio: e' o comportamento desejado.
NEEDLE = (
    "        ctx->gpr[10] = vm_read32(ctx->gpr[2] + -0x2B18);\n"
    "        ctx->gpr[0] = ctx->lr;\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": quem chama func_002182A4, e com que objecto.\n"
    "         * ctx->lr aqui ainda e' o LR do CHAMADOR guest -- o prologo ja' o\n"
    "         * copiou para gpr[0] mas nenhum bl interno correu ainda. Ver\n"
    "         * docs/re_sessions/2026-08-05-E13-o-objecto-errado.md */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_CALLER2182A4\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_CALLER2182A4_CAP\"); _cap=(_c&&*_c)?atoi(_c):200; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _this=(uint32_t)ctx->gpr[3];\n"
    "            fprintf(stderr,\"[CALL2182A4] #%d this=0x%08X%s lr=0x%08X r4=0x%08X r5=0x%08X\\n\",\n"
    "              _n, _this, (_this==0x400C6C68u)?\" <O-MAU>\":\"\", (uint32_t)ctx->lr,\n"
    "              (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5]);\n"
    "            fflush(stderr); } } }\n"
)


def patch_text(t):
    """Devolve (texto, estado). Estado: 'ja' | 'ok' | 'nao-casou'."""
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
        # A agulha nao casou em lado nenhum: rc != 0 para o catalogo NAO
        # classificar isto como JA-APLICADO. Um patch que nao fez nada tem de
        # dizer que nao fez nada (licao de 2026-08-04).
        print("SEM-EFEITO: agulha do prologo de func_002182A4 nao casou em %s" % lift)
        return 2

    print("patch_2182a4_caller_probe: aplicados=%d ja=%d" % (aplicados, ja))
    return 0


if __name__ == "__main__":
    sys.exit(main())
