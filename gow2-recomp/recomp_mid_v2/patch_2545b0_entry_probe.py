#!/usr/bin/env python3
"""Sonda da entrada real de `func_002545B0` — os dois lados do conflito, medidos.

Porque a entrada e' `func_002545B0` e nao `func_002545D4`
---------------------------------------------------------
O `ICALL-BAD` simbolizado dava `func_002545D4+0x5AC`, mas esse e' um FRAGMENTO:
o prologo verdadeiro esta em `0x002545B0` (confirmado desmontando o EBOOT.ELF --
`stdu r1,-176(r1)`, os quatro `std r28..r31`, e o `mr r28, r4` que define o
objecto). O lift gera `func_002545B0` a fazer `ctx->gpr[28] = ctx->gpr[4]` e a
trampolinar para `func_002545D4`. Sondar a entrada e' o unico sitio onde `r3`,
`r4` e o `lr` do chamador estao todos validos.

Tentativas que NAO deram, e ficam registadas para nao se repetirem
-----------------------------------------------------------------
- O `lr` do guest num `bctrl` fica preso no ultimo `bl` -- mandou-me para o
  walker `func_0024D5BC`, que a sonda `PS3_TRACE_D5BC` provou saudavel (758 nos).
- O rbp frame walk simbolizado dava `#2 func_0039D764+0x220`, mas a sonda
  `PS3_TRACE_VT64` mediu os 224 despachos `*(vt+0x64)` dessa funcao e **nenhum**
  tinha este objecto nem chamava `0x002545B0`. Com trampolins e tail-calls os
  frames host nao mapeiam 1:1 nas chamadas guest.

O que esta sonda mediu (2 chamadas na corrida inteira)
------------------------------------------------------
    [E545B0] #1 this=0x400C61C8 arg=0x4063858C lr=0x0004200C sent=0x40638608 head=0x40007F34
    [E545B0] #2 this=0x400C61C8 arg=0x407790D0 lr=0x0024E2D4 sent=0x4077914C head=0x00000000

**Mesmo `this`, dois `arg` diferentes.** O #1 tem uma lista circular valida
(`head` aponta para um no' real). O #2 tem `head = 0`, e e' o que entra em laco
infinito.

E o `0x4077914C` do #2 e' o mesmo endereco que o `PS3_WATCH_STORE` apanhou a ser
escrito a 0.0 por `func_0024C1F8` -- um inicializador de matriz identidade, cujo
objecto e' `arg-4 = 0x407790CC` e cujo `+0x80` e' a primeira coluna da linha 1.

Ou seja, sobre a MESMA base `0x407790CC`, o offset `+0x80` e':
    - `matriz[1][0]` para `func_0024C1F8`
    - cabeca da lista circular para `func_002545B0`

O #1 prova que o layout de `func_002545B0` esta certo **para o tipo certo**.
Logo o defeito e' o objecto `0x407790CC` chegar as maos desta funcao -- vindo do
walker de registos do WAD (`lr=0x0024E2D4` cai em `func_0024E1E8`/`func_0024E270`).

Gate: `PS3_TRACE_E545B0` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_E545B0_CAP` (default 120).

Uso:  patch_2545b0_entry_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "ENTRY-2545B0-PROBE"

NEEDLE = (
    "        ctx->gpr[28] = ctx->gpr[4] | ctx->gpr[4];\n"
    "        vm_write32(ctx->gpr[1] + 0xB8, ctx->gpr[12]);\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": entrada real de func_002545B0 -- this, arg e o lr do chamador */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_E545B0\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_E545B0_CAP\"); _cap=(_c&&*_c)?atoi(_c):120; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _a=(uint32_t)ctx->gpr[4];\n"
    "            uint32_t _sent=_a?(uint32_t)(_a+0x7Cu):0u;\n"
    "            fprintf(stderr,\"[E545B0] #%d this=0x%08X arg=0x%08X lr=0x%08X sent=0x%08X head=0x%08X\\n\",\n"
    "              _n, (uint32_t)ctx->gpr[3], _a, (uint32_t)ctx->lr, _sent,\n"
    "              (_sent>=0x10000u && _sent<0x4F000000u)?vm_read32(_sent):0xFFFFFFFFu);\n"
    "            fflush(stderr); } } }\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY"
    if t.count(NEEDLE) != 1:
        return t, "MISSING"
    return t.replace(NEEDLE, REPL, 1), "APPLIED"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not chunks:
        print("ERRO: nenhum ppu_recomp_*.cpp em %s" % lift, file=sys.stderr)
        return 2
    applied = already = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        out, state = patch_text(src)
        if state == "APPLIED":
            with open(path, "w") as fh:
                fh.write(out)
            applied += 1
            print("APPLIED  %s" % os.path.basename(path))
        elif state == "ALREADY":
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  agulha da entrada func_002545B0 nao encontrada", file=sys.stderr)
        return 2
    print("patch_2545b0_entry_probe: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
