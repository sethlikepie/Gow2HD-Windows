#!/usr/bin/env python3
"""Sonda do "produto corrente" da fabrica (`func_0039E5A8`) -- cursor, ranhura,
produto e o TAG do produto.

Porque este e' o ultimo elo (cadeia toda medida a 2026-08-01)
-------------------------------------------------------------
    main() -> func_002B2E74 -> B71 -> func_0010F5E8 (registry) -> func_0039D51C
      -> func_0039E40C -> vt[0x60]=func_00254788 -> laco infinito

e o objecto que o laco percorre chega la' por:

    func_0024E198 (walker do WAD)
      -> func_0024E3D0 -> ps3_indirect_call -> func_0039E5A8   <- ESTE
      -> r21 = produto corrente
      -> func_002545B0(this, arg=r21)   e le' arg+0x7C como sentinela de lista

O corpo, verificado contra o lift e o EBOOT:

    cursor = *(int8_t*)(fab + 0xC8);
    if (cursor < 0) return NULL;
    return *(uint32_t*)(fab + 0x48 + cursor*4);

E o que ja' esta PROVADO sobre o outro lado:

  - o registry de tipos FUNCIONA (`tab=0x00868D48`, tag -> fabrica com vt viva)
  - o objecto entregue esta BEM CONSTRUIDO (cabecalho + matriz identidade)
  - o `+0x7C` que o consumidor le' como sentinela e' `matriz[0][3]` -- provado
    desmontando o construtor `func_0024C1F8` (33 instrucoes, sem ramos)

Logo o defeito esta entre o **cursor** (`fab+0xC8`) e o **array de produtos**
(`fab+0x48`): ou o cursor aponta para a ranhura errada, ou o array tem la' um
produto de outro tipo.

O que esta sonda mede
---------------------
Para cada consulta: a fabrica, o cursor, a ranhura lida, o produto devolvido, e
o **tag do produto** (`*(uint16*)(produto+2)`, o mesmo campo que o registry usa)
mais o cabecalho `*(produto)`. Cruzar o tag com o tipo que o consumidor assume
responde a "cursor ou array" -- que sao dois defeitos com fixes diferentes.

Gate: `PS3_TRACE_PRODUCT` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_PRODUCT_CAP` (default 120, `-1` = ilimitado). Read-only.

Uso:  patch_39e5a8_product_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "PRODUCT-PROBE"

NEEDLE = (
    "        ctx->gpr[10] = vm_read32((ctx->gpr[11] + ctx->gpr[9]));\n"
    "loc_0039E5D0:\n"
)

REPL = (
    "        ctx->gpr[10] = vm_read32((ctx->gpr[11] + ctx->gpr[9]));\n"
    "        /* " + MARKER + ": cursor, ranhura, produto e o TAG do produto */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_PRODUCT\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_PRODUCT_CAP\");\n"
    "            _cap=(_c&&*_c)?atoi(_c):120; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _base=(uint32_t)ctx->gpr[11];\n"
    "            uint32_t _prod=(uint32_t)ctx->gpr[10];\n"
    "            int _ok=(_prod>=0x10000u && _prod<0x4F000000u);\n"
    "            fprintf(stderr,\"[PRODUCT] fab=0x%08X cursor=%d ranhura=0x%08X \"\n"
    "              \"produto=0x%08X hdr=0x%08X tag=%d\\n\",\n"
    "              _base-0x48u, (int)(int32_t)ctx->gpr[0],\n"
    "              (uint32_t)(_base+(uint32_t)ctx->gpr[9]), _prod,\n"
    "              _ok?vm_read32(_prod):0u, _ok?(int)vm_read16(_prod+2u):-1);\n"
    "            /* assinatura do CONSUMIDOR: base=prod-4, lista auto-ligada em\n"
    "             * base+0x80 (= prod+0x7C) e contador em base+0x78 (= prod+0x74).\n"
    "             * Se algum produto bater com isto, o tag dele e' o tag CERTO\n"
    "             * para a fabrica do consumidor -- e compara-se com o 1. */\n"
    "            if(_ok){ uint32_t _sent=_prod+0x7Cu;\n"
    "              uint32_t _head=vm_read32(_sent);\n"
    "              if(_head==_sent) fprintf(stderr,\n"
    "                \"          ^^ ASSINATURA DO CONSUMIDOR: lista auto-ligada em \"\n"
    "                \"+0x7C (tag=%d)\\n\", (int)vm_read16(_prod+2u));\n"
    "            }\n"
    "            fflush(stderr); } } }\n"
    "loc_0039E5D0:\n"
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
        print("MISSING  agulha do produto corrente nao encontrada", file=sys.stderr)
        return 2
    print("patch_39e5a8_product_probe: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
