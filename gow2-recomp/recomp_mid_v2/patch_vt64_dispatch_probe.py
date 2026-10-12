#!/usr/bin/env python3
"""Sonda do despacho `this->vtable[0x64](this, arg)` — os dois lados do conflito.

Estado da investigacao (2026-08-01)
-----------------------------------
A parede pos-fix do pump esta localizada ao endereco:

    func_0024E270 (walker do WAD) -> fabrica de tipos (func_0039D428/func_0039D764)
    -> func_002545B0/D4, que trata `arg` como tendo uma lista circular em arg+0x7C

E a mesma memoria tem outro dono:

    [WATCHSTORE] w32 [0x4077914C]=0x0  ra0=func_0024C1F8+0x218

`func_0024C1F8` e' um inicializador de MATRIZ IDENTIDADE (escreve 1.0/0.0 nas
quatro linhas em obj+0x70/0x80/0x90/0xA0), e `0x4077914C = 0x407790D0 + 0x7C` e'
o elemento [0][3] dessa matriz -- legitimamente 0.0.

Como o teste de "lista vazia" e' `head == sentinela`, um 0.0 nunca o satisfaz: o
walk entra com um no' nulo, le' o "tipo" do endereco guest 0x2 e despacha por
`tab[lixo]` = 0. Dai o `ctr=0x00514E80` e o laco infinito.

Ou seja: **um metodo virtual esta a correr sobre um objecto que nao e' do tipo
que ele assume** -- ou entao um dos dois lados esta a usar o offset errado.

O que esta sonda mede
---------------------
No sitio exacto onde a fabrica escolhe o metodo (`func_0039D764`, e nos outros
sitios com o mesmo idioma `*(vt+0x64)`):

    this  = r31        vt = *(this)      opd = *(vt+0x64)     code = *(opd)
    arg   = r4

Com isto sabe-se, sem inferencia, QUAL vtable escolheu o metodo e SOBRE QUE
objecto ele vai correr. Cruzando o `arg` com o `0x407790D0` conhecido e o `code`
com `0x002545B0`, o conflito fica nomeado dos dois lados -- e distingue-se
"indice de tipo errado" de "layout do objecto divergente".

Gate: `PS3_TRACE_VT64` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_VT64_CAP` (default 200). `PS3_TRACE_VT64=arg` filtra so' os despachos
cujo `arg` seja o objecto sob suspeita (`PS3_TRACE_VT64_ARG=0x407790D0`).

A agulha repete-se entre chunks (fragmentos duplicados da mesma funcao);
aplica-se a todas -- read-only.

Uso:  patch_vt64_dispatch_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se nenhuma agulha casou.
"""
import os
import sys
import glob

MARKER = "VT64-DISPATCH-PROBE"

NEEDLE = (
    "        ctx->gpr[11] = vm_read32(ctx->gpr[9] + 0x64);\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[11] + 0x0);\n"
    "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
    "        ctx->ctr = (uint32_t)ctx->gpr[0];\n"
    "        ctx->gpr[2] = vm_read32(ctx->gpr[11] + 0x4);\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": quem escolheu o metodo, e sobre que objecto vai correr */\n"
    "        { static int _on=-1; static uint32_t _flt=0;\n"
    "          if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_VT64\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0;\n"
    "            const char* _a=getenv(\"PS3_TRACE_VT64_ARG\");\n"
    "            _flt=(_a&&*_a)?(uint32_t)strtoul(_a,0,0):0u; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_VT64_CAP\"); _cap=(_c&&*_c)?atoi(_c):200; }\n"
    "          if(_on){ uint32_t _arg=(uint32_t)ctx->gpr[4];\n"
    "            if(!_flt || _arg==_flt){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "              fprintf(stderr,\"[VT64] this=0x%08X vt=0x%08X opd=0x%08X code=0x%08X arg=0x%08X\\n\",\n"
    "                (uint32_t)ctx->gpr[31], (uint32_t)ctx->gpr[9],\n"
    "                (uint32_t)ctx->gpr[11], (uint32_t)ctx->gpr[0], _arg);\n"
    "              fflush(stderr); } } } }\n"
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
        print("MISSING  agulha do despacho *(vt+0x64) nao encontrada", file=sys.stderr)
        return 2
    print("patch_vt64_dispatch_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
