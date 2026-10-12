#!/usr/bin/env python3
"""Sonda de TODOS os stores de `func_00263178` -- qual deles escreve o 5.

Estado da cadeia (medido a 2026-08-01)
--------------------------------------
O boot morre/pendura dentro do B71 (`func_000B71B8`), a 9a de 11 chamadas de
`func_002B2E74`, que por sua vez e' a 7a de 9 do `main()`. Como nao retorna, o
jogo **nunca entra no loop principal** (`func_00242C94`, a 8a chamada).

A ponta da cadeia: `func_002182A4` le' `tabela[0]` como ponteiro de pool e passa
ao pop da free-list (`func_00263554`), que devolve lixo; `func_00220284` escreve
16 words atraves dele. Medido:

    [POOLIDX] MAU pool=0x00000005 slot=0x4007FCE8 base=0x4007FCE8 idx=0
              divisor=130 obj=0x400C6C68
    [POOLIDX] ok  pool=0x40773748 slot=0x40773630 base=0x40773630 idx=0
              divisor=128 obj=0x406388F8

O indice esta CERTO nos dois casos (idx=0). O que difere e' o conteudo.

E o `PS3_WATCH_STORE` nas duas tabelas mostra dois sitios DIFERENTES da mesma
funcao:

    [0x40773630]=0x407505AC  ra0=func_00263178+0x9B0   <- ponteiro (tabela sa)
    [0x40773630]=0x27182818  ra0=func_00263680+0x444   <- magic 'e', depois
    [0x4007FCE8]=0x00000005  ra0=func_00263178+0x9E8   <- 5 (tabela ma)

Decodificar a mao qual store e' `+0x9E8` nao converge: o laco tem tres saidas e
uma dezena de stores, e os valores que escreve sao ETIQUETADOS
(`r5 | 0x80000000`), nem ponteiros nem inteiros pequenos.

O que esta sonda faz
--------------------
Marca cada `vm_write32` do corpo de `func_00263178` com um indice e imprime
`(indice, endereco, valor)` -- por default so' quando o valor e' implausivel
como ponteiro (`< 0x10000`, que e' o caso do 5), ou tudo com
`PS3_TRACE_ALLOCST=all`.

Assim o proximo passo comeca com a resposta ("o store #N escreve o 5") em vez
da pergunta, e sem mais uma reconstrucao de ~5 min por tentativa.

Gate: `PS3_TRACE_ALLOCST` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_ALLOCST_CAP` (default 200, `-1` = ilimitado). Read-only.

Uso:  patch_263178_store_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a funcao nao foi encontrada.
"""
import os
import re
import sys
import glob

MARKER = "ALLOCST-PROBE"

HEAD = "void func_00263178(ppu_context* ctx) {\n"
STORE_RE = re.compile(
    r'^        vm_write32\((ctx->gpr\[\d+\](?: \+ 0x[0-9A-F]+)?), (ctx->gpr\[\d+\])\);$',
    re.M)


def probe(idx, addr_expr, val_expr):
    return (
        "        /* " + MARKER + " */\n"
        "        { static int _on=-1; static int _all=0;\n"
        "          if(_on<0){ extern char* getenv(const char*);\n"
        "            const char* _e=getenv(\"PS3_TRACE_ALLOCST\");\n"
        "            _on=(_e&&*_e&&*_e!='0')?1:0; _all=(_e&&*_e=='a')?1:0; }\n"
        "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
        "            const char* _c=getenv(\"PS3_TRACE_ALLOCST_CAP\");\n"
        "            _cap=(_c&&*_c)?atoi(_c):200; }\n"
        "          if(_on){ uint32_t _a=(uint32_t)(" + addr_expr + ");\n"
        "            uint32_t _v=(uint32_t)(" + val_expr + ");\n"
        "            if(_all || _v<0x10000u){ static int _n=0; if(_cap<0||_n++<_cap){\n"
        "              fprintf(stderr,\"[ALLOCST] store#%02d [0x%08X]=0x%08X\\n\",\n"
        "                " + str(idx) + ", _a, _v);\n"
        "              fflush(stderr); } } } }\n"
    )


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    if HEAD not in t:
        return t, "MISSING", 0
    ini = t.index(HEAD) + len(HEAD)
    fim = t.index("\n}\n", ini) + 1
    body = t[ini:fim]
    pieces = []
    last = 0
    idx = 0
    for m in STORE_RE.finditer(body):
        idx += 1
        pieces.append(body[last:m.start()])
        pieces.append(probe(idx, m.group(1), m.group(2)))
        last = m.start()
    pieces.append(body[last:])
    if not idx:
        return t, "MISSING", 0
    return t[:ini] + "".join(pieces) + t[fim:], "APPLIED", idx


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
            print("APPLIED  %-20s stores=%d" % (os.path.basename(path), n))
        elif state == "ALREADY":
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  func_00263178 nao encontrada", file=sys.stderr)
        return 2
    print("patch_263178_store_probe: applied=%d already=%d stores=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
