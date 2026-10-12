#!/usr/bin/env python3
"""Sonda da cadeia de IRMAOS do walk de `func_0041F700` -- ciclo ou arvore enorme?

O que ja' esta medido (2026-08-01/02)
------------------------------------
Com os tres gates de diagnostico e o fix do `cellPad` (crash real do utilizador,
`ps3recomp edd3e7a`), o boot entra no loop principal, despacha o primeiro estado
e fica preso em `func_0041F700` -- **27 539 083** iteracoes do despacho interno.

E o laco NAO e' um bug do lifter: verificado contra o corpo, `r31 = r9` avanca
correctamente (4a suspeita de bug do lifter da sessao, 4a refutada).

A sonda de entrada mostrou que as listas estao **bem formadas**:

    arg=0x4063858C sent=0x40638608 head=0x40638608   <- head==sentinela (vazia)
    arg=0x4063858C sent=0x40638608 head=0x40007F34   <- com filhos

40 entradas em `func_0041F700` contra 27,5 M de despachos internos. Logo o ciclo
**nao esta na profundidade** (senao havia milhoes de entradas): esta na cadeia de
IRMAOS percorrida dentro de uma unica chamada -- o `r31 = *(r31)` que devia
voltar a' sentinela.

O que esta sonda mede
---------------------
Os primeiros N nos visitados no laco (`r31`, o no' corrente, e `*(r31)`, o
proximo). Duas leituras possiveis, e so' a medicao as separa:

  - **os enderecos repetem-se** -> ciclo: a cadeia fecha-se sobre si sem passar
    pela sentinela. O defeito e' de quem a construiu.
  - **os enderecos crescem sempre** -> a lista e' genuinamente enorme (ou o
    `*(no)` aponta para memoria sequencial que nunca bate na sentinela, o que
    tambem e' um defeito, mas de outro tipo).

Imprime tambem a sentinela, para se ver quao longe esta do que se percorre.

Gate: `PS3_TRACE_SIBLING` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_SIBLING_CAP` (default 40, `-1` = ilimitado -- **cuidado**: sao 27 M
de iteracoes, nunca correr sem cap). Read-only.

Uso:  patch_41f774_sibling_cycle_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "SIBLING-CYCLE-PROBE"

NEEDLE = (
    "loc_0041F774:\n"
    "        ctx->gpr[11] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
    "        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x8);\n"
)

REPL = (
    "loc_0041F774:\n"
    "        /* " + MARKER + ": os primeiros nos da cadeia de irmaos */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_SIBLING\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_SIBLING_CAP\"); _cap=(_c&&*_c)?atoi(_c):40; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _cur=(uint32_t)ctx->gpr[31];\n"
    "            uint32_t _nxt=(_cur>=0x10000u&&_cur<0x4F000000u)?vm_read32(_cur):0u;\n"
    "            fprintf(stderr,\"[SIBLING] #%d no=0x%08X proximo=0x%08X sent=0x%08X\\n\",\n"
    "              _n,_cur,_nxt,(uint32_t)ctx->gpr[30]);\n"
    "            fflush(stderr); } } }\n"
    "        ctx->gpr[11] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
    "        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x8);\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    n = t.count(NEEDLE)
    if not n:
        return t, "MISSING", 0
    return t.replace(NEEDLE, REPL), "APPLIED", n


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not chunks:
        print("ERRO: nenhum ppu_recomp_*.cpp em %s" % lift, file=sys.stderr)
        return 2
    applied = already = sites = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        out, state, n = patch_text(src)
        if state == "APPLIED":
            with open(path, "w") as fh:
                fh.write(out)
            applied += 1
            sites += n
            print("APPLIED  %-20s sites=%d" % (os.path.basename(path), n))
        elif state == "ALREADY":
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  agulha do laco de irmaos nao encontrada", file=sys.stderr)
        return 2
    print("patch_41f774_sibling_cycle_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
