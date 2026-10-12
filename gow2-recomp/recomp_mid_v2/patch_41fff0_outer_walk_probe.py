#!/usr/bin/env python3
"""Sonda do walk EXTERIOR (`func_0041FF70`) -- a lista que nao termina.

Cadeia medida (2026-08-01/02)
----------------------------
Boot preso em 27 539 083 iteracoes. Camada a camada, tudo o que se abriu estava
CORRECTO menos um bug nosso:

  cellPadSetActDirect ......... BUG NOSSO, corrigido (EA guest desreferenciado)
  laco de irmaos .............. fiel (4a suspeita de lifter, refutada)
  listas de irmaos ............ bem formadas, head==sentinela, 12 nos, TERMINAM
  descida da arvore ........... 12 descidas distintas, sem ciclo entre niveis
  walk exterior ............... <- ESTE

A sonda da descida deu a aresta:

    [DESCENT] CICLO: pai=0x40007DE4 -> filho=0x42F86ADC (ja' visto na #1 de 12)

Doze descidas distintas e a 13a repete a primeira: o walk interior reinicia do
topo. Quem o reinicia e' o exterior.

O laco exterior, lido do lift:

    r29 = *(this + 0x24)              // cabeca
  loc_0041FFE8:
    if (r29 == 0) goto fim;           // termina em NULL (nao em sentinela)
  loc_0041FFF0:
    r31 = r29 - 8                     // container_of
    r29 = *(r29 + 0)                  // AVANCA -- correcto
    flags = *(uint16*)(r31 + 4)
    if (flags & 0x10) continue;       // salta esta entrada
    ...
    ... chama func_0041F700 (walk + push) ...

Tambem avanca correctamente. Logo, se nao termina, a lista e' CICLICA: a cadeia
de `*(no)` fecha-se sem passar por 0.

O que esta sonda mede
---------------------
Os primeiros nos do walk exterior: o no' corrente, o proximo, e as flags que
decidem se a entrada e' saltada. Se os enderecos repetirem, esta' provado o ciclo
e fica identificada a aresta que o fecha.

Gate: `PS3_TRACE_OUTER` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_OUTER_CAP` (default 32, `-1` = ilimitado -- nunca sem cap aqui).
Read-only.

Uso:  patch_41fff0_outer_walk_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "OUTER-WALK-PROBE"

NEEDLE = (
    "loc_0041FFF0:\n"
    "        ctx->gpr[31] = ctx->gpr[29] + (int64_t)(-8);\n"
)

REPL = (
    "loc_0041FFF0:\n"
    "        /* " + MARKER + ": os primeiros nos do walk exterior */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_OUTER\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_OUTER_CAP\"); _cap=(_c&&*_c)?atoi(_c):32; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _cur=(uint32_t)ctx->gpr[29];\n"
    "            uint32_t _ok=(_cur>=0x10000u&&_cur<0x4F000000u);\n"
    "            fprintf(stderr,\"[OUTER] #%d no=0x%08X proximo=0x%08X flags=0x%04X\\n\",\n"
    "              _n,_cur, _ok?vm_read32(_cur):0u,\n"
    "              _ok?(unsigned)vm_read16(_cur-8u+4u):0u);\n"
    "            fflush(stderr); } } }\n"
    "        ctx->gpr[31] = ctx->gpr[29] + (int64_t)(-8);\n"
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
        print("MISSING  agulha do walk exterior nao encontrada", file=sys.stderr)
        return 2
    print("patch_41fff0_outer_walk_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
