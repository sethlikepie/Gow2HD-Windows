#!/usr/bin/env python3
"""Sonda da lista de `func_002547AC` -- ciclica, ou termina?

Porque este e' o ultimo suspeito (medido 2026-08-01/02)
-------------------------------------------------------
Boot preso em 27 539 083 iteracoes. Seis camadas abertas, TODAS correctas menos
uma que era nossa:

  cellPadSetActDirect ......... BUG NOSSO, corrigido (EA guest desreferenciado)
  laco de irmaos de 0041F700 .. fiel (4a suspeita de lifter, refutada)
  listas de irmaos ............ bem formadas, head==sentinela, 12 nos, terminam
  descida da arvore ........... 12 descidas distintas, sem ciclo entre niveis
  walk exterior 0041FF70 ...... TERMINA, 2 entradas, acaba em NULL

Com 2 entradas no exterior e 12 irmaos no interior sao 24 despachos por chamada.
27 539 083 / 24 = **~1,15 milhoes de chamadas** a `func_0041FF70`, vindas de
`func_002546FC` -- que faz parte do walk `func_00254788`/`func_002547AC`.

E esse walk esta atras de um gate MEU (`PS3_LIST547_EMPTY_IF_BAD`), que trata
`head` implausivel como lista vazia -- **mas nao trata `head` plausivel e
ciclico**. Se a lista for ciclica em vez de nula, o gate deixa-a passar e o walk
repete, o que produz exactamente esta ordem de grandeza.

O que esta sonda mede
---------------------
Os primeiros nos do walk: o no' corrente, o proximo (`*(no)`) e a sentinela. Se
os enderecos repetirem, o ciclo esta provado e a aresta identificada. Se
terminarem na sentinela, o suspeito cai e a pergunta sobe outra camada.

Nota de metodo: e' a mesma sonda de listagem que resolveu as cinco camadas
anteriores -- listar os primeiros N nos e olhar. Barata e conclusiva nos dois
sentidos.

Gate: `PS3_TRACE_W547` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_W547_CAP` (default 24, `-1` = ilimitado -- nunca sem cap aqui).
Read-only.

Uso:  patch_2547f8_walk_list_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "W547-LIST-PROBE"

NEEDLE = (
    "loc_002547F8:\n"
    "        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x8);\n"
    "        ctx->gpr[31] = vm_read32(ctx->gpr[11] + 0x0);\n"
)

REPL = (
    "loc_002547F8:\n"
    "        /* " + MARKER + ": os primeiros nos do walk de func_002547AC */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_W547\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_W547_CAP\"); _cap=(_c&&*_c)?atoi(_c):24; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _cur=(uint32_t)ctx->gpr[11];\n"
    "            int _ok=(_cur>=0x10000u && _cur<0x4F000000u);\n"
    "            fprintf(stderr,\"[W547] #%d no=0x%08X proximo=0x%08X sent=0x%08X\\n\",\n"
    "              _n,_cur, _ok?vm_read32(_cur):0u, (uint32_t)ctx->gpr[30]);\n"
    "            fflush(stderr); } } }\n"
    "        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x8);\n"
    "        ctx->gpr[31] = vm_read32(ctx->gpr[11] + 0x0);\n"
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
        print("MISSING  agulha do walk de func_002547AC nao encontrada", file=sys.stderr)
        return 2
    print("patch_2547f8_walk_list_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
