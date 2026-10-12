#!/usr/bin/env python3
"""GATE DE DIAGNOSTICO (nao e' um fix): tratar `head` implausivel como lista
vazia em `func_002547AC` -- o laco TERMINAL onde o B71 fica preso.

Leia isto antes de usar
-----------------------
Isto **NAO corrige nada**. E' um salto por cima de uma parede, ligado por env
var e OFF por default, cujo unico proposito e' responder a: **esta e' a ultima
parede dentro do B71, ou ha' mais?** Qualquer resultado obtido com ele fica
marcado como obtido com gate (CLAUDE.md regras 4 e 5).

Porque este sitio, medido a 2026-08-01
--------------------------------------
Rasto de chamadas (`PS3_TRACE_B71`, 291 pontos) com os dois gates anteriores
ligados. O ponto terminal, identico em todas as corridas:

    [B71] func_0039E40C #001 -> ps3_indirect_call this=0x400C61C8 ctr=0x00254788

`func_00254788` **nao tem uma unica chamada** -- por isso o rasto de call-sites
nao a instrumentava e o silencio parecia comecar antes dela. E' um laco puro.
Trampolina para `func_002547AC`, que faz:

    r30 = arg - 4 + 0x80 = arg + 0x7C     // sentinela
    r9  = *(r30)                          // head
    if (r30 == r9) goto vazio;            // teste de vazio: head == SENTINELA
  loc_002547F8:
    r9  = *(r11 + 8);  r31 = *(r11 + 0)   // caminha a lista
    ...

**E' exactamente o padrao da parede de Julho** (`func_002545B0`, sentinela em
`arg+0x7C`, teste `head == sentinela`): um `head` que nao e' a sentinela nem um
no' valido faz o walk nunca terminar. Metodo diferente, mesma familia de
objectos, mesmo defeito de tipo.

O que este gate faz
-------------------
Alarga o teste de vazio: se o `head` nao for um ponteiro guest plausivel, trata
a lista como vazia em vez de a percorrer. Nao inventa nos nem estampa valores.

Barulhento de proposito: uma linha por salto.

Gate: `PS3_LIST547_EMPTY_IF_BAD=1`.

Uso:  patch_2547f8_empty_list_gate.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "LIST547-EMPTY-GATE"

NEEDLE = (
    "        ctx->gpr[11] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
    "        if (((ctx->cr >> 0) & 2)) goto loc_00254878;\n"
)

REPL = (
    "        ctx->gpr[11] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
    "        /* " + MARKER + " (DIAGNOSTICO, OFF por default, NUNCA um fix):\n"
    "         * o teste de vazio do jogo e' `head == sentinela`; um head que nao\n"
    "         * e' ponteiro nunca o satisfaz e o walk nao termina. Com o gate\n"
    "         * tratamos isso como lista vazia, so' para ver o que ha' a jusante. */\n"
    "        { static int _g=-1; if(_g<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_LIST547_EMPTY_IF_BAD\");\n"
    "            _g=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          uint32_t _h=(uint32_t)ctx->gpr[9];\n"
    "          if(_g && (_h<0x10000u || _h>=0x4F000000u)){\n"
    "            static int _n=0; if(_n++<32){\n"
    "              fprintf(stderr,\"[LIST547-GATE] head=0x%08X em sent=0x%08X -> tratado \"\n"
    "                \"como vazio (GATE, nao e' comportamento natural)\\n\",\n"
    "                _h,(uint32_t)ctx->gpr[30]); fflush(stderr); }\n"
    "            goto loc_00254878; } }\n"
    "        if (((ctx->cr >> 0) & 2)) goto loc_00254878;\n"
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
        print("MISSING  agulha do teste de vazio de func_002547AC nao encontrada",
              file=sys.stderr)
        return 2
    print("patch_2547f8_empty_list_gate: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
