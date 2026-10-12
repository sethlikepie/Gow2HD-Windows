#!/usr/bin/env python3
"""Duas sondas em `func_00227788`: quem PEDE o alocador e qual VEM.

O que estas duas medem, e porque juntas
---------------------------------------
`func_00227788` obtem o "alocador" que depois indexa por classe de tamanho
atraves de uma unica chamada virtual num singleton:

    sing = *( *(TOC-0x2A78) + 0xC )
    alloc = sing->vt[0x50]( sing, key )        // key = *( *(obj+0x24) + 0x14 )

Medido a 2026-08-01, corrida inteira:

  SING50   109 despachos, TODOS com sing=0x400C6210 vt=0x00514FA0
           opd=0x0051B0A0 code=0x0039D3C4 -- o singleton e o metodo sao
           CONSTANTES; so' a `key` varia.

  ALLOCSRC dois resultados distintos:
           alloc=0x406387E0 vt=0x00514F28 tab8C[0]=0x407719A8   (sao)
           alloc=0x400C6B50 vt=0x00515008 tab8C[0]=0x00000000   (partido)

E a correlacao, linhas consecutivas do mesmo log:

    [SING50]   #107 ... key=0x4077ED10 obj24=0x4077CAB8
    [ALLOCSRC] #2   alloc=0x400C6B50 vt=0x00515008 tab8C[0]=0
    [vm] UNCOMMITTED write32 ... ra=func_00220284+0x1244

**Uma chave concreta devolve um objecto de outra classe.** Todas as outras
devolvem a classe certa. E `code=0x0039D3C4` e' da familia 0x39Dxxx -- o
registo de tipos, o mesmo mecanismo que atravessa esta investigacao inteira.

O `obj24=0x4077CAB8` bate com o `p50=0x4077CAB8` do `[CPY284] #13`: e' o mesmo
objecto que acaba a correr com `this=0`.

Gates (ambos OFF por default):
  PS3_TRACE_SING50    / PS3_TRACE_SING50_CAP    (default 5000)
  PS3_TRACE_ALLOCSRC  / PS3_TRACE_ALLOCSRC_CAP  (default 5000)

Ambas so' imprimem quando o valor MUDA, para o log nao encher com as centenas
de despachos identicos.

Uso:  patch_227788_allocsrc_probes.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se nenhuma agulha casou.
"""
import os
import sys
import glob

MARK_A = "SINGLETON50-PROBE"
MARK_B = "ALLOCSRC-PROBE"

NEEDLE_A = (
    "        ctx->gpr[11] = vm_read32(ctx->gpr[9] + 0x0);\n"
    "        ctx->gpr[4] = vm_read32(ctx->gpr[10] + 0x14);\n"
    "        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x50);\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);\n"
)

REPL_A = NEEDLE_A + (
    "        /* " + MARK_A + ": quem pede o alocador (singleton, metodo, chave) */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_SING50\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_SING50_CAP\"); _cap=(_c&&*_c)?atoi(_c):5000; }\n"
    "          if(_on){ static uint32_t _pc=0xFFFFFFFFu, _pr=0xFFFFFFFFu; static int _n=0;\n"
    "            uint32_t _code=(uint32_t)ctx->gpr[0], _r4=(uint32_t)ctx->gpr[4];\n"
    "            if((_code!=_pc || _r4!=_pr) && (_cap<0 || _n++<_cap)){ _pc=_code; _pr=_r4;\n"
    "              fprintf(stderr,\"[SING50] #%d sing=0x%08X vt=0x%08X opd=0x%08X code=0x%08X key=0x%08X obj24=0x%08X\\n\",\n"
    "                _n, (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[11],\n"
    "                (uint32_t)ctx->gpr[9], _code, _r4, (uint32_t)ctx->gpr[10]);\n"
    "              fflush(stderr); } } }\n"
)

NEEDLE_B = (
    "        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n"
    "        ctx->gpr[11] = vm_read32(ctx->gpr[29] + 0x24);\n"
    "        ctx->gpr[29] = ppc_rldicl(ctx->gpr[3], 0, 32);\n"
)

REPL_B = (
    "        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n"
    "        /* " + MARK_B + ": qual alocador veio, e se tem tabela de pools */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_ALLOCSRC\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_ALLOCSRC_CAP\"); _cap=(_c&&*_c)?atoi(_c):5000; }\n"
    "          if(_on){ uint32_t _a=(uint32_t)ctx->gpr[3];\n"
    "            static uint32_t _prev=0xFFFFFFFFu; static int _n=0;\n"
    "            if(_a!=_prev && (_cap<0 || _n++<_cap)){ _prev=_a;\n"
    "              fprintf(stderr,\"[ALLOCSRC] #%d alloc=0x%08X vt=0x%08X tab8C[0]=0x%08X\\n\",\n"
    "                _n, _a, (_a>=0x10000u&&_a<0x50000000u)?vm_read32(_a):0u,\n"
    "                (_a>=0x10000u&&_a<0x50000000u)?vm_read32(_a+0x8Cu):0u);\n"
    "              fflush(stderr); } } }\n"
    "        ctx->gpr[11] = vm_read32(ctx->gpr[29] + 0x24);\n"
    "        ctx->gpr[29] = ppc_rldicl(ctx->gpr[3], 0, 32);\n"
)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not chunks:
        print("ERRO: nenhum ppu_recomp_*.cpp em %s" % lift, file=sys.stderr)
        return 2
    total = already = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        out, n = src, 0
        if MARK_A in out:
            already += 1
        elif NEEDLE_A in out:
            n += out.count(NEEDLE_A); out = out.replace(NEEDLE_A, REPL_A)
        if MARK_B in out:
            pass
        elif NEEDLE_B in out and out.count(NEEDLE_B) == 1:
            n += 1; out = out.replace(NEEDLE_B, REPL_B, 1)
        if n:
            with open(path, "w") as fh:
                fh.write(out)
            total += n
            print("APPLIED  %-20s sites=%d" % (os.path.basename(path), n))
    if total == 0 and already == 0:
        print("MISSING  agulhas do func_00227788 nao encontradas", file=sys.stderr)
        return 2
    print("patch_227788_allocsrc_probes: sites=%d already=%d" % (total, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
