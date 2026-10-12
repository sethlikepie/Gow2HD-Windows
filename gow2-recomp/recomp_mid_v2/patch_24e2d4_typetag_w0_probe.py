#!/usr/bin/env python3
"""Sonda COMPLEMENTAR ao TYPETAG-PROBE: le o w0 INTEIRO do objecto (nao so' o
low16 de +0x2), para a sessao E6 (docs/re_sessions/2026-08-05-E6-*.md).

Porque isto falta
------------------
`patch_24e2d4_typetag_probe.py` (2026-08-01/02) ja' mede `tag=*(obj+2)` (o
low16 que decide o indice da fabrica) e confirma, por argumento indirecto
(`PS3_WATCH_STORE` + o inicializador de matriz `func_0024C1F8`), que o
objecto problematico e' um registo WAD. Esta sonda LE directamente o `w0`
completo (`vm_read32(obj+0)`) no MESMO ponto de despacho, e aplica o mesmo
predicado de poda ja' provado para a parede 4
(`tools/test_wall4_subtag_fix.py::prune`), SEM o reimplementar aqui -- so'
imprime os campos para o predicado ser avaliado offline/pela sonda de censo.

Conta tambem, por corrida, quantos despachos teriam sido podados por esse
predicado (`low16==1 and subtag!=1`) -- e' o censo D-equivalente ao D-11.2
da parede 4, agora para este sitio.

Gate: `PS3_TRACE_TYPETAG_W0` (vazio ou "0" = OFF, default). Read-only,
mesmo cap de `PS3_TRACE_TYPETAG_CAP` (default 80, -1 = ilimitado) para nao
divergir do irmao.

Uso:  patch_24e2d4_typetag_w0_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "TYPETAG-W0-PROBE"

NEEDLE = (
    "        ctx->gpr[0] = vm_read16(ctx->gpr[21] + 0x2);\n"
    "        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[0], 2, 14, 29);\n"
    "        ctx->gpr[4] = ctx->gpr[21] | ctx->gpr[21];\n"
    "        ctx->gpr[11] = vm_read32((ctx->gpr[23] + ctx->gpr[0]));\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": w0 completo do objecto, para separar low16 de subtag */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_TYPETAG_W0\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_TYPETAG_CAP\"); _cap=(_c&&*_c)?atoi(_c):80; }\n"
    "          static uint32_t _seen=0, _would_prune=0;\n"
    "          uint32_t _obj=(uint32_t)ctx->gpr[21];\n"
    "          uint32_t _w0=(_obj>=0x10000u&&_obj<0x4F000000u)?vm_read32(_obj):0xFFFFFFFFu;\n"
    "          uint32_t _low16=_w0&0xFFFFu; uint32_t _subtag=(_w0>>16)&0xFFFu;\n"
    "          int _prune=(_low16==1u && _subtag!=1u);\n"
    "          _seen++; if(_prune) _would_prune++;\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            fprintf(stderr,\"[TYPETAG-W0] obj=0x%08X w0=0x%08X low16=0x%04X subtag=0x%03X"
    " prune=%d seen=%u would_prune=%u\\n\",\n"
    "              _obj,_w0,_low16,_subtag,_prune,_seen,_would_prune);\n"
    "            fflush(stderr); } }\n"
    "        }\n"
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
        print("MISSING  agulha do w0 nao encontrada", file=sys.stderr)
        return 2
    print("patch_24e2d4_typetag_w0_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
