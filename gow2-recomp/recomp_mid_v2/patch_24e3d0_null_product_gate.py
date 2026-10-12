#!/usr/bin/env python3
"""GATE DE DIAGNOSTICO (nao e' um fix): nao escrever o tipo quando o produto e NULL.

Leia isto antes de usar
-----------------------
OFF por default, barulhento quando liga, e **nunca deve ser apresentado como
correccao** (CLAUDE.md regras 4 e 5). Existe para responder a uma pergunta:
quanto da cadeia de quinze elos e' consequencia de UMA escrita?

O que o codigo faz hoje
-----------------------
`func_0024E3D0` pergunta a fabrica pelo produto corrente e copia o tipo dele
para o objecto:

    produto = fabrica->vt[0x48](fabrica);      // func_0039E5A8
    *(uint16*)(objecto + 6) = *(produto + 0x20);

Medido a 2026-08-01, 2/2 registos afectados por corrida:

    [FAB48] fab=0x400C6210 code=0x0039E5A8 cursor+C8=-1  <NEGATIVO>
    [TYPEASSIGN] obj=0x4077ED10 regtipo=0x3 produto=0x00000000 tipo_escrito=0

Com `produto == 0`, o `*(produto + 0x20)` le' o endereco guest `0x20` -- memoria
da base do espaco guest -- e escreve o que la esta (zero) como tipo do objecto.
**Num PS3 real isso faria fault na pagina nula.** Logo o jogo nunca pode tomar
este caminho com produto nulo: a invariante dele e' "o produto existe aqui".

O que o gate faz
----------------
Quando `produto == 0`, salta a escrita e deixa o campo como estava (medido:
valia 2, posto por func_00210720 e reafirmado por func_004117B0). Nao inventa
valor nenhum -- recusa-se a escrever um lido de `*(0x20)`.

O que se espera aprender
------------------------
Se o tipo ficar 2, o lookup do registo devolve a classe certa, o alocador tem
tabela de pools, o pop devolve um bloco e a cadeia inteira a jusante nao
acontece. Isso mede quanto dos quinze elos e' consequencia desta escrita -- e
se o boot avanca ou bate logo noutra parede.

Se NAO avancar, aprende-se que o problema real esta noutro sitio e poupa-se
trabalho na fabrica.

Gate: `PS3_24E3D0_KEEP_TYPE_ON_NULL=1`. Uma linha de log por salto.

Uso:  patch_24e3d0_null_product_gate.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "24E3D0-NULLPROD-GATE"

NEEDLE = "        vm_write16(ctx->gpr[9] + 0x6, ctx->gpr[0]);\n"

REPL = (
    "        /* " + MARKER + " (DIAGNOSTICO, OFF por default, NUNCA um fix):\n"
    "         * com produto==0 o valor a escrever vem de *(0x20), memoria da base\n"
    "         * do espaco guest. Num PS3 real isso faria fault -- o jogo nunca toma\n"
    "         * este caminho com produto nulo. O gate salta a escrita para se medir\n"
    "         * quanto da cadeia a jusante e' consequencia dela. */\n"
    "        { static int _g=-1; if(_g<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_24E3D0_KEEP_TYPE_ON_NULL\");\n"
    "            _g=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          if(_g && (uint32_t)ctx->gpr[3]==0u){ static int _n=0; if(_n++<16){\n"
    "            fprintf(stderr,\"[24E3D0-GATE] produto=NULL -> tipo do obj 0x%08X mantido em %u \"\n"
    "              \"(GATE, nao e' comportamento natural)\\n\",\n"
    "              (uint32_t)ctx->gpr[9], (unsigned)vm_read16((uint32_t)ctx->gpr[9]+6u));\n"
    "            fflush(stderr); } }\n"
    "          else vm_write16(ctx->gpr[9] + 0x6, ctx->gpr[0]); }\n"
)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    applied = already = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        if MARKER in src:
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
            continue
        if src.count(NEEDLE) != 1:
            continue
        with open(path, "w") as fh:
            fh.write(src.replace(NEEDLE, REPL, 1))
        applied += 1
        print("APPLIED  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  agulha da escrita do tipo em func_0024E3D0", file=sys.stderr)
        return 2
    print("patch_24e3d0_null_product_gate: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
