#!/usr/bin/env python3
"""Sonda da cadeia de tres indireccoes que produz o `arg` do walker do WAD.

O que ja' esta medido (2026-08-01)
----------------------------------
O consumidor `func_002545B0` recebe um `arg` cujo `+0x7C` ele le' como sentinela
de lista circular. Esse offset e' **provadamente** `matriz[0][3]` -- desmontado
do construtor `func_0024C1F8` (33 instrucoes, sem ramos, matriz identidade em
+0x70/+0x80/+0x90/+0xA0). O objecto esta bem construido; o consumidor e' que
esta a olhar para o objecto errado.

E a origem do `arg`, agora correctamente decodificada (a leitura anterior tinha
um bug no descodificador: em `rldicl` o destino e' rA, nao rS):

    0x0024E1F0  bl 0x003A6740        r30 = resultado
    0x0024E214  lwz r9,12(r30)       r9  = *(r30 + 0xC)
    0x0024E22C  lwz r22,8(r9)        r22 = *(r9 + 8)
    0x0024E268  clrldi r21,r22,32    r21 = r22          <- o arg
    0x0024E2C8  mr r3,r21            usado
    0x0024E2D8  lhz r0,2(r21)        e o tag sai de +0x2

Ou seja `arg = *( *(func_003A6740() + 0xC) + 8 )`.

O que esta sonda mede
---------------------
As tres indireccoes de uma so' vez, no ponto onde a ultima acontece:

  - `r30`  -- o que `func_003A6740` devolveu
  - `r9`   -- `*(r30 + 0xC)`
  - `r22`  -- `*(r9 + 8)`, que vira o `arg`
  - e, do `arg`: a word 0 (cabecalho ou vtable) e o tag em `+0x2`

Com isto vê-se em qual das tres indireccoes o valor deixa de fazer sentido --
que e' a diferenca entre "func_003A6740 devolve o objecto errado", "o campo
+0xC aponta para a estrutura errada" e "o slot +8 tem la' o objecto errado".
Tres defeitos distintos, tres fixes distintos, e so' a medicao os separa.

Gate: `PS3_TRACE_ARGCHAIN` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_ARGCHAIN_CAP` (default 60, `-1` = ilimitado). Read-only.

A agulha repete-se em fragmentos duplicados; aplica-se a todos.

Uso:  patch_24e22c_arg_chain_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se nenhuma agulha casou.
"""
import os
import sys
import glob

MARKER = "ARGCHAIN-PROBE"

NEEDLE = "        ctx->gpr[22] = vm_read32(ctx->gpr[9] + 0x8);\n"

REPL = NEEDLE + (
    "        /* " + MARKER + ": arg = *( *(func_003A6740() + 0xC) + 8 ) */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_ARGCHAIN\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_ARGCHAIN_CAP\");\n"
    "            _cap=(_c&&*_c)?atoi(_c):60; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _r30=(uint32_t)ctx->gpr[30], _r9=(uint32_t)ctx->gpr[9];\n"
    "            uint32_t _arg=(uint32_t)ctx->gpr[22];\n"
    "            int _ok=(_arg>=0x10000u && _arg<0x4F000000u);\n"
    "            fprintf(stderr,\"[ARGCHAIN] r30=0x%08X +0xC=0x%08X arg=0x%08X \"\n"
    "              \"w0=0x%08X tag=%d\\n\",\n"
    "              _r30,_r9,_arg,\n"
    "              _ok?vm_read32(_arg):0u, _ok?(int)vm_read16(_arg+2u):-1);\n"
    "            fflush(stderr); } } }\n"
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
        print("MISSING  agulha da cadeia do arg nao encontrada", file=sys.stderr)
        return 2
    print("patch_24e22c_arg_chain_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
