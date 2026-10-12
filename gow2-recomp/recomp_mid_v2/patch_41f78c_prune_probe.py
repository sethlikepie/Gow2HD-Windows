#!/usr/bin/env python3
"""Sonda da PODA do walk recursivo (`func_0041F700`) -- porque nunca dispara.

Cadeia medida (2026-08-01/02)
----------------------------
Boot preso em 27 539 083 iteracoes, das quais **99,96% sao `func_0041F700`**
(contado no rasto: 27.539.083 de 27.549.345 linhas). O chamador nem aparece no
topo -- a funcao e' RECURSIVA: o despacho `tab[idx]->vt[0x40]` volta a entrar
nela para cada filho.

12 irmaos por nivel e 12^7 ~ 35M: e' **explosao exponencial por revisitar os
mesmos nos**, nao um ciclo. O grafo nao e' arvore e o walk percorre-o como se
fosse.

Sete camadas ja' eliminadas por medicao, TODAS correctas menos uma que era nossa:

  cellPadSetActDirect ......... BUG NOSSO, corrigido (EA guest desreferenciado)
  laco de irmaos .............. fiel (4a suspeita de lifter, refutada)
  listas de irmaos ............ bem formadas, head==sentinela, 12 nos, terminam
  descida da arvore ........... o filho #1 REAPARECE (revisita confirmada)
  walk exterior 0041FF70 ...... termina, 2 entradas, acaba em NULL
  walk de 002547AC ............ nunca corre com o gate (sonda deu zero)

E o walk TEM uma poda:

    r29 = (*(no    + 4) >> 16) & 0xFFF     // campo do PAI, calculado a' entrada
    r0  = (*(filho + 4) >> 16) & 0xFFF     // campo do FILHO
    if (r0 == r29) goto loc_0041F7C4;      // <- salta este filho

Se nunca coincidirem, nada e' podado e a recursao visita todas as combinacoes.

O que esta sonda mede
---------------------
Os dois lados da comparacao, e quantas vezes coincidem. Separa duas causas com
fixes diferentes:

  - **os dois lados sao sempre diferentes** -> a poda esta desenhada para outro
    campo, ou o campo `+4` dos filhos nao esta preenchido
  - **coincidem as vezes** -> a poda funciona e a explosao vem de outro lado

Imprime tambem `no` e `filho`, para se cruzar com as familias ja' identificadas
(`w0=0xC0010001` listas vs `w0=0x40030001` registos WAD com matriz em +0x70).

Gate: `PS3_TRACE_PRUNE` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_PRUNE_CAP` (default 24, `-1` = ilimitado -- **nunca sem cap**: sao
27M de passagens). Read-only.

Uso:  patch_41f78c_prune_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "PRUNE-PROBE"

NEEDLE = (
    "        ctx->gpr[9] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[0], 2, 14, 29);\n"
    "        ctx->gpr[0] = ppc_rldicl(ctx->gpr[0], 48, 52);\n"
)

REPL = (
    "        ctx->gpr[9] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[0], 2, 14, 29);\n"
    "        ctx->gpr[0] = ppc_rldicl(ctx->gpr[0], 48, 52);\n"
    "        /* " + MARKER + ": os dois lados da poda, e se alguma vez coincidem */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_PRUNE\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_PRUNE_CAP\"); _cap=(_c&&*_c)?atoi(_c):24; }\n"
    "          if(_on){ static int _n=0; static long _eq=0, _tot=0;\n"
    "            uint32_t _f=(uint32_t)ctx->gpr[0], _p=(uint32_t)ctx->gpr[29];\n"
    "            _tot++; if(_f==_p) _eq++;\n"
    "            if(_cap<0 || _n++<_cap){\n"
    "              uint32_t _filho=(uint32_t)ctx->gpr[10];\n"
    "              int _ok=(_filho>=0x10000u&&_filho<0x4F000000u);\n"
    "              uint32_t _wf=_ok?vm_read32(_filho+4u):0u;\n"
    "              uint32_t _pai_obj=(uint32_t)ctx->gpr[31];\n"
    "              int _pok=(_pai_obj>=0x10000u&&_pai_obj<0x4F000000u);\n"
    "              fprintf(stderr,\"[PRUNE] #%d filho=0x%08X w0=0x%08X +4=0x%08X \"\n"
    "                \"campo=%u | pai=0x%08X +4=0x%08X campo=%u | %s (coincidem %ld/%ld)\\n\",\n"
    "                _n,_filho,_ok?vm_read32(_filho):0u,_wf,_f,\n"
    "                _pai_obj,_pok?vm_read32(_pai_obj+4u):0u,_p,\n"
    "                (_f==_p)?\"PODA\":\"desce\", _eq,_tot);\n"
    "              fflush(stderr); } } }\n"
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
        print("MISSING  agulha da poda nao encontrada", file=sys.stderr)
        return 2
    print("patch_41f78c_prune_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
