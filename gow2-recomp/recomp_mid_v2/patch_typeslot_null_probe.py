#!/usr/bin/env python3
"""Sonda do despacho `tab[(tipo<<2)]` quando o slot esta VAZIO.

Como se chegou aqui (2026-08-01)
--------------------------------
Depois do fix do F2B-STREAM-PUMP (patch_fios_f2b_pump_ring_bounds.py) o boot
aborta sempre no mesmo sitio, 6/6 no gate e tambem com o disjuntor subido para
2 000 000 (logo e' um laco real, nao o breaker a ser precipitado):

    [ppu] FATAL: stuck calling 0x00514E80 (2000 times) -- aborting run

O `lr` do guest apontava para o walker `func_0024D5BC` -- e mentia (o lift so'
escreve `lr` antes dos `bl`, nunca antes de um `bctrl`). A sonda
`PS3_TRACE_D5BC` provou o walker saudavel: 758 nos com vtable/OPD/code validos
em 3/3 corridas. Com o `ICALL-BAD` simbolizado por dladdr (ps3recomp 338706e) a
cadeia real apareceu:

    func_0025C838 -> func_002B2E74 -> func_000B71B8 -> func_0010F5E8
    -> func_0024F028 -> func_0024E270            (walker de registos do WAD)
    -> icall -> func_0039D428 -> icall -> func_0039D764   (fabrica de tipos)
    -> icall -> func_002545D4+0x5AC              <- AQUI

E o sitio, lido do lift, e' o MESMO idioma de despacho por tabela de tipos do
CB56C e do [WADLD-VT28]:

    r9  = *(uint16*)(r11 + 2)              // tipo
    r9  = (tipo << 2) & 0x3FFFC            // indice
    r11 = *(r29 + r9)                      // tab[idx]  <- objecto de tipo
    r9  = *(r11 + 0)                       // vtable
    r10 = *(r9 + 0x18)                     // OPD
    ctr = *(r10 + 0);  ps3_indirect_call

O dump traz `r3 = 0` e `r11 = 0`: **`tab[idx]` esta a zero**. O tipo que o
conteudo do WAD pede nunca foi registado. Dai `*(0)` -> `*(0x18)` -> um valor
qualquer da base da memoria guest como `ctr` (0x00514E80).

O que esta sonda mede
---------------------
`tipo`, `idx`, base da tabela (`r29`) e o conteudo `tab[idx]` -- imediatamente
antes do despacho, e SO' quando o slot esta vazio (`tab[idx] == 0`), para nao
inundar o log com os milhares de despachos saudaveis. Isso nomeia o tipo em
falta, que se pode entao cruzar com os 128 que o [WADLD-VT28] mostra
registados com OPD valido.

Um segundo modo, `PS3_TRACE_TYPESLOT=all`, regista TODOS os despachos (cap
incluido) para se ver a distribuicao de tipos pedidos.

Gate: `PS3_TRACE_TYPESLOT` (vazio ou "0" = OFF, o default). Cap por
`PS3_TRACE_TYPESLOT_CAP` (default 200).

A agulha repete-se muito entre chunks (o lifter duplica fragmentos da mesma
funcao). Aplica-se a TODAS as ocorrencias: mesmo idioma guest, sonda
read-only, e assim regista seja qual for o fragmento que corre.

Uso:  patch_typeslot_null_probe.py [DIR_DE_LIFT]   (default: ../recomp_macos_v2)
rc: 0 aplicado/ja aplicado; 2 se nenhuma agulha casou.
"""
import os
import sys
import glob

MARKER = "TYPESLOT-NULL-PROBE"

NEEDLE = (
    "        ctx->gpr[9] = vm_read16(ctx->gpr[11] + 0x2);\n"
    "        ctx->gpr[9] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[9], 2, 14, 29);\n"
    "        ctx->gpr[11] = vm_read32((ctx->gpr[29] + ctx->gpr[9]));\n"
    "        ctx->gpr[3] = ctx->gpr[11] | ctx->gpr[11];\n"
    "        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);\n"
    "        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x18);\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": tab[(tipo<<2)] vazio -> despacho por ponteiro nulo */\n"
    "        { static int _on=-1; static int _all=0;\n"
    "          if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_TYPESLOT\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0;\n"
    "            _all=(_e&&*_e=='a')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_TYPESLOT_CAP\"); _cap=(_c&&*_c)?atoi(_c):200; }\n"
    "          if(_on){ uint32_t _slot=(uint32_t)ctx->gpr[11];\n"
    "            if(_all || _slot==0u){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "              uint32_t _idx=(uint32_t)ctx->gpr[9] & 0x3FFFCu;\n"
    "              fprintf(stderr,\"[TYPESLOT] %s idx=0x%X tipo=0x%X tab=0x%08X slot=0x%08X\\n\",\n"
    "                _slot?\"ok  \":\"NULO\", _idx, _idx>>2, (uint32_t)ctx->gpr[29], _slot);\n"
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
        print("MISSING  agulha do despacho tab[(tipo<<2)] nao encontrada", file=sys.stderr)
        return 2
    print("patch_typeslot_null_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
