#!/usr/bin/env python3
"""Sonda do TAG DE TIPO com que o walker do WAD escolhe a fabrica.

Porque este e' o sitio (cadeia toda medida a 2026-08-01)
--------------------------------------------------------
Quatro paredes independentes, todas a mesma doenca: um metodo de contentor a
correr sobre um objecto que nao e' um contentor.

    func_002545B0   lista intrusiva, sentinela em arg+0x7C   head=0
    func_002182A4   tabela de pools em obj+0x14              base[0]=contagem
    func_002547AC   lista intrusiva, sentinela em arg+0x7C   head=0
    func_004244C0   lista intrusiva, sentinela em this+0x24  laco infinito

E o objecto que aparece nas duas primeiras NAO TEM VTABLE. Medido, corrida
inteira sem cap:

    [0x4077AC10]=0x0   ra0=func_0024CADC+0x338     <- unica escrita da word 0

`func_0024CADC` zera-o e chama `func_0024C1F8`, o inicializador de matriz
identidade. **E' um struct simples, nao-polimorfico.** A construcao dele esta
correcta para o que ele e'; errado e' quem o entrega a metodos de contentor.

E esse sitio e' `func_0024E270`, no `0x0024E2D4` que ja' aparecia no `lr` desde
Julho:

    r0  = *(uint16*)(obj + 0x2)          // TAG DE TIPO, lido do proprio objecto
    r0  = rlwinm(r0, 2, 14, 29)          // idx = (tipo << 2) & 0x3FFFC
    r4  = obj                            // o objecto vai como argumento
    r11 = *(r23 + r0)                    // fabrica = tab[idx]
    r9  = *(r11); r10 = *(r9 + 0x18)     // metodo vt[0x18] da fabrica
    ps3_indirect_call(fabrica, obj)

`idx = (tipo << 2) & 0x3FFFC` e' a formula do registry de tipos que o CLAUDE.md
documenta -- a Parede D, agora no caminho directo entre o `main()` e o laco que
prende o boot.

O que esta sonda mede
---------------------
Para cada despacho: o objecto, o **tag** lido de `+0x2`, o indice, a base da
tabela, a fabrica escolhida, a vtable dela e o codigo do metodo. Isso separa
tres causas que dao o mesmo sintoma e que so' se distinguem com medicao:

  - **tag errado no objecto**   -> o objecto diz ser de um tipo que nao e'
  - **entrada errada na tabela** -> o tag esta certo mas o registry mente
  - **tabela nao populada**      -> a entrada esta vazia (a Parede D classica)

Imprime tambem se o objecto tem vtable (`*(obj)`), para se ver de relance quais
sao POD e quais sao polimorficos.

Gate: `PS3_TRACE_TYPETAG` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_TYPETAG_CAP` (default 80, `-1` = ilimitado). Read-only.

Uso:  patch_24e2d4_typetag_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se nenhuma agulha casou.
"""
import os
import sys
import glob

MARKER = "TYPETAG-PROBE"

NEEDLE = (
    "        ctx->gpr[0] = vm_read16(ctx->gpr[21] + 0x2);\n"
    "        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[0], 2, 14, 29);\n"
    "        ctx->gpr[4] = ctx->gpr[21] | ctx->gpr[21];\n"
    "        ctx->gpr[11] = vm_read32((ctx->gpr[23] + ctx->gpr[0]));\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": com que tag o walker escolheu a fabrica */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_TYPETAG\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_TYPETAG_CAP\"); _cap=(_c&&*_c)?atoi(_c):80; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _obj=(uint32_t)ctx->gpr[21];\n"
    "            uint32_t _fab=(uint32_t)ctx->gpr[11];\n"
    "            uint32_t _ovt=(_obj>=0x10000u&&_obj<0x4F000000u)?vm_read32(_obj):0xFFFFFFFFu;\n"
    "            uint32_t _fvt=(_fab>=0x10000u&&_fab<0x4F000000u)?vm_read32(_fab):0u;\n"
    "            uint32_t _opd=(_fvt>=0x10000u&&_fvt<0x00600000u)?vm_read32(_fvt+0x18u):0u;\n"
    "            fprintf(stderr,\"[TYPETAG] obj=0x%08X objvt=0x%08X tag=%u idx=0x%05X \"\n"
    "              \"tab=0x%08X fab=0x%08X fabvt=0x%08X code=0x%08X\\n\",\n"
    "              _obj,_ovt,(unsigned)vm_read16(_obj+2u),(uint32_t)ctx->gpr[0],\n"
    "              (uint32_t)ctx->gpr[23],_fab,_fvt,\n"
    "              _opd?vm_read32(_opd):0u);\n"
    "            fflush(stderr);\n"
    "            /* despejo UNICO da tabela do registry: qual o indice de cada\n"
    "             * fabrica. Se o `this` do consumidor (0x400C61C8) estiver num\n"
    "             * indice != 1, o walker usou o tag errado. */\n"
    "            static int _dumped=0;\n"
    "            if(!_dumped){ _dumped=1; uint32_t _tab=(uint32_t)ctx->gpr[23];\n"
    "              fprintf(stderr,\"[REGTAB] base=0x%08X (entradas nao-nulas)\\n\",_tab);\n"
    "              for(uint32_t _i=0;_i<256;_i++){\n"
    "                uint32_t _e=vm_read32(_tab+_i*4u);\n"
    "                if(_e>=0x10000u && _e<0x4F000000u){\n"
    "                  uint32_t _vt=vm_read32(_e);\n"
    "                  fprintf(stderr,\"[REGTAB]   tag=%3u idx=0x%04X fab=0x%08X vt=0x%08X%s\\n\",\n"
    "                    _i,_i*4u,_e,_vt,\n"
    "                    (_e==0x400C61C8u)?\"  <<<< o this do CONSUMIDOR\":\"\");\n"
    "                } }\n"
    "              fflush(stderr); }\n"
    "          } } }\n"
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
        print("MISSING  agulha do tag de tipo nao encontrada", file=sys.stderr)
        return 2
    print("patch_24e2d4_typetag_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
