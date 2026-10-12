#!/usr/bin/env python3
"""Sonda do `r21` em cada fragmento do walker do WAD -- onde nasce o `arg` que
faz o consumidor entrar em laco infinito.

Porque medir e nao inferir
--------------------------
O `arg` que `func_002545B0` recebe (e cujo `+0x7C` ele le' como sentinela de
lista, quando esse offset e' provadamente `matriz[0][3]`) esta em `r21` no sitio
`0x0024E2D4`. `r21` e' callee-saved: o prologo de `func_0024E198` guarda-o mas
nao o define, e em `0x0024E268` so' o trunca (`clrldi r21,r21,32`).

**Ja' inferi duas vezes a origem e falhei as duas.** A ultima: vi no rasto que
`func_0024E3D0` chama `func_0039E5A8` ("da'-me o produto corrente"), assumi que
era dali, e a sonda `PS3_TRACE_PRODUCT` refutou-o -- 253 consultas, todas sas,
20 fabricas, e **nenhum** produto devolvido e' o objecto do problema
(`0x4077ACxx`); os produtos tem todos vtables reais (`hdr=0x0051xxxx`).

E' a mesma classe de erro que ja' me apanhou hoje com o `lr` do guest, com o
`ra1` do host e com o xref de `bl` que nao ve' despachos indirectos: **usar a
estrutura para adivinhar o dado em vez de medir o dado.** Esta sonda mede.

O que faz
---------
Imprime `r21` (e `r3`) a' entrada de cada fragmento de `func_0024E198`. Como os
fragmentos correm por ordem de fluxo, **o primeiro que imprimir um `r21` na
gama `0x4077xxxx` e' aquele em que ele nasce** -- ou o imediatamente anterior,
se nascer a meio.

Fragmentos cobertos (todos os `func_0024E1xx`..`func_0024E4xx` que existam no
lift, para nao falhar nenhum caminho trampolinado).

Gate: `PS3_TRACE_R21` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_R21_CAP` (default 200, `-1` = ilimitado). Read-only.

Uso:  patch_24e198_r21_origin_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se nenhum fragmento casou.
"""
import os
import re
import sys
import glob

MARKER = "R21-ORIGIN-PROBE"

HEAD_RE = re.compile(
    r'^void (func_0024E[1-4][0-9A-F]{2})\(ppu_context\* ctx\) \{$', re.M)


def probe(fn):
    return (
        "        /* " + MARKER + " */\n"
        "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
        "            const char* _e=getenv(\"PS3_TRACE_R21\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
        "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
        "            const char* _c=getenv(\"PS3_TRACE_R21_CAP\"); _cap=(_c&&*_c)?atoi(_c):200; }\n"
        "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
        "            fprintf(stderr,\"[R21] " + fn + " r21=0x%08X r3=0x%08X\\n\",\n"
        "              (uint32_t)ctx->gpr[21], (uint32_t)ctx->gpr[3]);\n"
        "            fflush(stderr); } } }\n"
    )


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    n = 0
    for m in list(HEAD_RE.finditer(t)):
        fn = m.group(1)
        head = "void %s(ppu_context* ctx) {\n" % fn
        if t.count(head) != 1:
            continue
        t = t.replace(head, head + probe(fn), 1)
        n += 1
    if not n:
        return t, "MISSING", 0
    return t, "APPLIED", n


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
            print("APPLIED  %-20s fragmentos=%d" % (os.path.basename(path), n))
        elif state == "ALREADY":
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  nenhum fragmento func_0024E1xx..4xx encontrado", file=sys.stderr)
        return 2
    print("patch_24e198_r21_origin_probe: applied=%d already=%d fragmentos=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
