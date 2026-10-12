#!/usr/bin/env python3
"""Sonda da DESCIDA do walk de `func_0041F700` -- qual a aresta que fecha o ciclo.

O que ja' esta medido (2026-08-01/02)
------------------------------------
Com os tres gates de diagnostico e o fix do `cellPad` (crash real do utilizador,
`ps3recomp edd3e7a`), o boot entra no loop principal e fica preso em
`func_0041F700`: **27 539 083** iteracoes do despacho interno.

E ja' se sabe o que **nao** e' o problema:

  - o laco de irmaos e' fiel (`r31 = r9` avanca) -- 4a suspeita de bug do lifter
    da sessao, 4a refutada por verificacao;
  - a lista esta bem formada: 12 irmaos e **termina na sentinela**
    (`#12 no=0x40007D0C proximo=0x42F853B0 == sent`), medido com
    `PS3_TRACE_SIBLING`;
  - as listas destes objectos sao auto-ligadas quando vazias
    (`head == sentinela`), medido na entrada.

Logo o defeito nao esta na estrutura nem na travessia: as 27,5 M de iteracoes
sao ~2,3 M de **chamadas**, cada uma a re-percorrer os mesmos 12 nos. Alguem
re-invoca o walk.

A hipotese natural, e a razao desta sonda
-----------------------------------------
O walk desce aos filhos assim:

    filho = *(no + 8)
    idx   = (*(filho + 4) << 2) & 0x3FFFC
    fabrica = tab[idx]
    fabrica->vt[0x40](fabrica, filho + 4)     // recursao

Se um filho apontar de volta para um **antepassado**, a arvore tem um ciclo
entre niveis: cada volta re-percorre os mesmos irmaos, para sempre, sem que a
lista de irmaos esteja errada.

O que esta sonda faz
--------------------
Guarda os enderecos dos filhos ja' visitados (janela de 1024, varrimento linear
-- e' diagnostico, nao caminho quente) e imprime **a primeira aresta que
revisita** um no' ja' visto: pai, filho, e a posicao em que o filho tinha
aparecido antes. Essa linha e' a aresta que fecha o ciclo.

Sem revisita nas 1024 primeiras descidas, imprime uma linha a dizer isso -- para
o zero nunca ser lido como "nao ha' ciclo" quando pode ser "a janela e' curta".

Gate: `PS3_TRACE_DESCENT` (vazio ou "0" = OFF, default). Janela por
`PS3_TRACE_DESCENT_WIN` (default 1024). Read-only.

Uso:  patch_41f7a4_descent_cycle_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "DESCENT-CYCLE-PROBE"

NEEDLE = (
    "        ctx->gpr[11] = vm_read32((ctx->gpr[28] + ctx->gpr[9]));\n"
    "        ctx->gpr[3] = ctx->gpr[11] | ctx->gpr[11];\n"
    "        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);\n"
    "        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x40);\n"
)

REPL = (
    "        ctx->gpr[11] = vm_read32((ctx->gpr[28] + ctx->gpr[9]));\n"
    "        /* " + MARKER + ": a primeira aresta que revisita um no' */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_DESCENT\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _win=-1; if(_win<0){ extern char* getenv(const char*);\n"
    "            const char* _w=getenv(\"PS3_TRACE_DESCENT_WIN\");\n"
    "            _win=(_w&&*_w)?atoi(_w):1024; if(_win>4096) _win=4096; }\n"
    "          if(_on){ static uint32_t _seen[4096]; static int _nseen=0;\n"
    "            static int _done=0;\n"
    "            uint32_t _filho=(uint32_t)ctx->gpr[4];\n"
    "            if(!_done){ int _hit=-1;\n"
    "              for(int _i=0;_i<_nseen;_i++) if(_seen[_i]==_filho){ _hit=_i; break; }\n"
    "              if(_hit>=0){ _done=1;\n"
    "                fprintf(stderr,\"[DESCENT] CICLO: pai=0x%08X -> filho=0x%08X \"\n"
    "                  \"(ja' visto na descida #%d de %d)\\n\",\n"
    "                  (uint32_t)ctx->gpr[31], _filho, _hit+1, _nseen);\n"
    "                fflush(stderr);\n"
    "              } else if(_nseen<_win){ _seen[_nseen++]=_filho;\n"
    "              } else { _done=1;\n"
    "                fprintf(stderr,\"[DESCENT] sem revisita nas %d primeiras descidas \"\n"
    "                  \"-- janela curta ou arvore genuinamente enorme\\n\", _win);\n"
    "                fflush(stderr); } } } }\n"
    "        ctx->gpr[3] = ctx->gpr[11] | ctx->gpr[11];\n"
    "        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);\n"
    "        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x40);\n"
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
        print("MISSING  agulha da descida nao encontrada", file=sys.stderr)
        return 2
    print("patch_41f7a4_descent_cycle_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
