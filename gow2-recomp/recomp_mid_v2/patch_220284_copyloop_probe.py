#!/usr/bin/env python3
"""Sonda do laco de copia de `func_00220284` — o `this` e' sao ou tambem e' lixo?

A cadeia ate aqui (medido a 2026-08-01)
---------------------------------------
As 16 escritas por ponteiro mau que aparecem antes da parede do func_002545D4
vem de um laco de copia de 4 floats por elemento em `func_00220284`:

    r31 = r3                    // this
    r30 = *(this + 0x68)        // contador de elementos
    r8  = *(this + 0x60)        // destino
    r0  = *(*(this+0x50) + 0x20C)   // origem
    if (r8 == 0) salta          // <- so' guarda contra ZERO, nao contra lixo
    por elemento: le 4 floats da origem, escreve 4 no destino, avanca 0x10

O `[vm] UNCOMMITTED write32` bate certo: quatro `vm_write32` distintos por
iteracao (offsets host +0xF78/+0xFA8/+0xFD8/+0x1008), enderecos guest a subir
de 4 em 4 e o grupo de 0x10 em 0x10.

Logo `*(this + 0x60)` contem texto. **A pergunta que decide o rumo** e' se o
`this` em si esta sao:

  - `this` sao, so' o campo +0x60 mau  -> problema de DADOS/campo por preencher
  - `this` tambem lixo                 -> problema de DESPACHO (metodo a correr
                                          sobre o objecto errado), que e' o
                                          padrao ja medido em tres sitios hoje
                                          (rec=0, objecto-matriz, pool=5)

O que mede
----------
`this` e os cinco campos que o laco usa (+0x50, +0x5C, +0x60, +0x64, +0x68),
uma linha por entrada. Sem filtro: sao poucas chamadas e o que interessa e' ver
as boas ao lado da ma, como aconteceu no `[E545B0]`.

Gate: `PS3_TRACE_CPY284` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_CPY284_CAP` (default 40).

Uso:  patch_220284_copyloop_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "CPY284-PROBE"

NEEDLE = (
    "        ctx->gpr[9] = vm_read32(ctx->gpr[31] + 0x50);\n"
    "        ctx->gpr[8] = vm_read32(ctx->gpr[31] + 0x60);\n"
    "        ctx->gpr[5] = vm_read32(ctx->gpr[31] + 0x5C);\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": this e os campos do laco de copia */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_CPY284\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_CPY284_CAP\"); _cap=(_c&&*_c)?atoi(_c):40; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _t=(uint32_t)ctx->gpr[31], _d=(uint32_t)ctx->gpr[8];\n"
    "            int _tmau=(_t<0x10000u||_t>=0x50000000u);\n"
    "            int _dmau=(_d!=0u)&&(_d<0x10000u||_d>=0x50000000u);\n"
    "            fprintf(stderr,\"[CPY284] #%d this=0x%08X%s n=%u p50=0x%08X dst60=0x%08X%s p5C=0x%08X p64=0x%08X\\n\",\n"
    "              _n, _t, _tmau?\" <THIS-MAU>\":\"\", (unsigned)(uint32_t)ctx->gpr[30],\n"
    "              (uint32_t)ctx->gpr[9], _d, _dmau?\" <DST-MAU>\":\"\",\n"
    "              (uint32_t)ctx->gpr[5], (uint32_t)ctx->gpr[3]);\n"
    "            fflush(stderr); } } }\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY"
    if t.count(NEEDLE) != 1:
        return t, "MISSING"
    return t.replace(NEEDLE, REPL, 1), "APPLIED"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not chunks:
        print("ERRO: nenhum ppu_recomp_*.cpp em %s" % lift, file=sys.stderr)
        return 2
    applied = already = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        out, state = patch_text(src)
        if state == "APPLIED":
            with open(path, "w") as fh:
                fh.write(out)
            applied += 1
            print("APPLIED  %s" % os.path.basename(path))
        elif state == "ALREADY":
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  agulha do laco de copia 00220284 nao encontrada", file=sys.stderr)
        return 2
    print("patch_220284_copyloop_probe: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
