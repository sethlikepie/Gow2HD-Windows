#!/usr/bin/env python3
"""Sonda do indice de classe de tamanho em `func_00227788` — o ponto onde os
sintomas #3 e #4 convergem.

A convergencia (lida do lift, 2026-08-01)
-----------------------------------------
`func_00227788(r3 = obj)` faz duas chamadas virtuais e depois usa o resultado
da segunda como INDICE numa tabela de free-lists do resultado da primeira:

    r29 = <resultado da 1a icall>          // o alocador
    r31 = <resultado da 2a icall>          // o INDICE de classe de tamanho
    r9  = r29 + (r31 << 2) + 0x80
    r3  = *(r9 + 0xC)                      // o slot da free-list
    func_00263554(r3)                      // POP -> valor de retorno da funcao

Isto explica os dois sintomas de uma vez:

  #3  `[FLHEAD] TEXTO pool=0x00000005` -- o `pool` que chega ao pop e' esse
      `*(r29 + (r31<<2) + 0x8C)`. Valer 5 quer dizer que o indice caiu fora da
      tabela, ou que a tabela nao esta inicializada.
  #4  `[CPY284] #13 this=0x00000000` -- se o pop devolve 0/lixo, e' isso que
      `func_00227788` devolve, e `func_002182A4` passa-o sem verificar a
      `func_002210BC`, que o poe em `r26` e daí em `this`.

Ou seja **nao sao quatro defeitos, sao dois sintomas do mesmo indice errado** --
e os outros dois (rec=0 em func_002545D4, objecto-matriz em func_002545B0) sao
os consumidores a jusante do objecto que nunca foi alocado.

O que mede
----------
`r29` (o alocador), `r31` (o indice), o endereco calculado e o `r3` que sai do
slot. Uma linha por passagem, marcada quando o slot nao e' um ponteiro
plausivel. Com isto sabe-se se o defeito e' o indice (r31 absurdo) ou a tabela
(r29 sem free-lists inicializadas).

Gate: `PS3_TRACE_SZCLASS` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_SZCLASS_CAP` (default 60).

Uso:  patch_227788_sizeclass_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "SZCLASS-PROBE"

NEEDLE = (
    "        ctx->gpr[9] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[31], 2, 0, 29);\n"
    "        ctx->gpr[9] = ctx->gpr[9] + (int64_t)(0x80);\n"
    "        ctx->gpr[9] = (int64_t)(int32_t)ctx->gpr[9];\n"
    "        ctx->gpr[9] = ctx->gpr[29] + ctx->gpr[9];\n"
    "        ctx->gpr[3] = vm_read32(ctx->gpr[9] + 0xC);\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": alocador, indice de classe, slot e o que sai */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_SZCLASS\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_SZCLASS_CAP\"); _cap=(_c&&*_c)?atoi(_c):60; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _slot=(uint32_t)ctx->gpr[3];\n"
    "            int _mau=(_slot!=0u)&&(_slot<0x10000u||_slot>=0x50000000u);\n"
    "            fprintf(stderr,\"[SZCLASS] #%d alloc=0x%08X idx=%d ea=0x%08X slot=0x%08X%s\\n\",\n"
    "              _n, (uint32_t)ctx->gpr[29], (int)(int32_t)ctx->gpr[31],\n"
    "              (uint32_t)(ctx->gpr[9]+0xCu), _slot,\n"
    "              _mau?\"  <SLOT-MAU>\":((_slot==0u)?\"  <SLOT-ZERO>\":\"\"));\n"
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
        print("MISSING  agulha do indice de classe de tamanho nao encontrada", file=sys.stderr)
        return 2
    print("patch_227788_sizeclass_probe: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
