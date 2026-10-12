#!/usr/bin/env python3
"""Sonda da resolucao do gestor em `func_00212ED0` -- a chave e o que devolve.

Porque existe (E34, 2026-08-06)
-------------------------------
Medido: numa chamada em 238, a vcall que resolve o gestor devolve
`0x400C6B50`, que **nao e' um gestor**: o slot `+0xD0` nunca foi escrito por
ninguem, e o `+0x12C` e' escrito pelo alocador `func_00263178`. E' memoria de
contabilidade do alocador, lida como gestor.

A chave da resolucao e' `*(this + 0x14)` com `this = 0x00514E80` -- o objecto
da PAREDE 1, cujo fix podou um dispatch sem reparar o objecto.

Duas hipoteses, e esta sonda separa-as:
  chave diferente nas 237 sas e na ma  -> chave invalida no objecto
  chave IGUAL e mgr diferente          -> tabela de gestores incompleta/errada

Gate: `PS3_TRACE_MGRLOOKUP` (OFF por default). Cap `PS3_TRACE_MGRLOOKUP_CAP`.
Agulha de 10 linhas, do extract da chave ate' a vcall -- verificada unica.

rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os, sys, glob

MARKER = "MGRLOOKUP-212ED0-PROBE"
NEEDLE = '        ctx->gpr[0] = vm_read32(ctx->gpr[3] + 0x14);\n        ctx->gpr[3] = ctx->gpr[11] | ctx->gpr[11];\n        ctx->gpr[4] = ctx->gpr[0] | ctx->gpr[0];\n        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);\n        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x50);\n        ctx->gpr[0] = vm_read32(ctx->gpr[10] + 0x0);\n        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n        ctx->ctr = (uint32_t)ctx->gpr[0];\n        ctx->gpr[2] = vm_read32(ctx->gpr[10] + 0x4);\n        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n'
REPL = '        ctx->gpr[0] = vm_read32(ctx->gpr[3] + 0x14);\n        ctx->gpr[3] = ctx->gpr[11] | ctx->gpr[11];\n        ctx->gpr[4] = ctx->gpr[0] | ctx->gpr[0];\n        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);\n        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x50);\n        ctx->gpr[0] = vm_read32(ctx->gpr[10] + 0x0);\n        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n        ctx->ctr = (uint32_t)ctx->gpr[0];\n        ctx->gpr[2] = vm_read32(ctx->gpr[10] + 0x4);\n        /* MGRLOOKUP-212ED0-PROBE: a chave e o gestor devolvido, dois lados\n         * da MESMA vcall. Ver docs/re_sessions/2026-08-06-E34-*.md */\n        uint32_t _ml_key = (uint32_t)ctx->gpr[4];\n        uint32_t _ml_tab = (uint32_t)ctx->gpr[3];\n        uint32_t _ml_tgt = (uint32_t)ctx->ctr;\n        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n            const char* _e=getenv("PS3_TRACE_MGRLOOKUP"); _on=(_e&&*_e&&*_e!=\'0\')?1:0; }\n          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n            const char* _c=getenv("PS3_TRACE_MGRLOOKUP_CAP"); _cap=(_c&&*_c)?atoi(_c):100000; }\n          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n            uint32_t _mgr=(uint32_t)ctx->gpr[3];\n            fprintf(stderr,"[MGRLOOKUP] #%d key=0x%08X tab=0x%08X alvo=0x%08X -> mgr=0x%08X%s\\n",\n              _n, _ml_key, _ml_tab, _ml_tgt, _mgr,\n              (_mgr==0x400C6B50u)?" <MGR-MAU>":"");\n            fflush(stderr); } } }\n'


def patch_text(t):
    if MARKER in t: return t, "ja"
    if t.count(NEEDLE) != 1: return t, "nao-casou"
    return t.replace(NEEDLE, REPL, 1), "ok"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(here), "recomp_macos_v2")
    alvos = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not alvos:
        print("SEM-EFEITO: nenhum ppu_recomp_*.cpp em %s" % lift); return 2
    ap = ja = 0
    for f in alvos:
        src = open(f, encoding="utf-8", errors="replace").read()
        novo, est = patch_text(src)
        if est == "ja": ja += 1; print("JA-APLICADO  %s" % os.path.basename(f))
        elif est == "ok":
            open(f, "w", newline="\n", encoding="utf-8").write(novo)
            ap += 1; print("CONVERTIDO   %s" % os.path.basename(f))
    if ap == 0 and ja == 0:
        print("SEM-EFEITO: agulha da vcall de resolucao nao casou em %s" % lift); return 2
    print("patch_212ed0_mgrlookup_probe: aplicados=%d ja=%d" % (ap, ja)); return 0


if __name__ == "__main__":
    sys.exit(main())
