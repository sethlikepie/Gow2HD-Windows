#!/usr/bin/env python3
"""Sonda da lista que `func_002545D4` percorre (a parede de 2026-08-01).

O que ja esta estabelecido
--------------------------
`func_002545D4` percorre uma lista circular (sentinela em `r28-4+0x80`) e, por
no', despacha um metodo virtual escolhido por um "tipo" lido do payload:

    0x00254628: rldicl r9, r31          # no'
    0x0025462C: addi   r11, r0, 0       # r11 = 0
    0x00254630: lwz    r10, 8(r9)       # payload
    0x00254634: lwz    r31, 0(r9)       # next
    0x00254638: cmpwi  cr7, r10, 0
    0x0025463C: beq    cr7 -> 0x00254644   <- salta para o CORPO, com r11=0
    0x00254640: addi   r11, r10, 4
    0x00254644: ... lhz r9,2(r11) ; rlwinm ; lwzx r11,r29,r9 ; bctrl

O `beq` foi verificado contra o PPC original no EBOOT.ELF: **o lift e' fiel**.
Nao ha bug do lifter aqui -- o jogo despacha mesmo com `r11=0` se o payload for
nulo, o que num PS3 real faria fault na pagina nula. Logo o defeito e' o no'
existir com payload nulo, e isso e' a montante.

Medido (4/4 corridas) no despacho:

    [TYPESLOT] NULO idx=0x25F7C tipo=0x97DF tab=0x00868D48 slot=0 rec=0x00000000

O que esta sonda mede
---------------------
Por no' visitado: a sentinela (`r30`), o no' (`r31` antes de avancar), o
`payload` (`*(no+8)`) e o `next` (`*(no+0)`). Isso responde:

  - o no' de payload nulo e' o PRIMEIRO da lista, ou aparece depois de N validos?
  - a lista fecha na sentinela, ou o `next` sai para lixo (o que explicaria as
    2 000 000 iteracoes do disjuntor)?
  - qual e' o objecto dono da lista (a sentinela da o endereco).

Gate: `PS3_TRACE_LIST254` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_LIST254_CAP` (default 120).

A agulha repete-se entre chunks (o lifter duplica fragmentos da mesma funcao);
aplica-se a todas -- mesmo caminho guest, sonda read-only.

Uso:  patch_254628_list_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se nenhuma agulha casou.
"""
import os
import sys
import glob

MARKER = "LIST254-PROBE"

NEEDLE = (
    "        ctx->gpr[9] = ppc_rldicl(ctx->gpr[31], 0, 32);\n"
    "        ctx->gpr[11] = (int64_t)(int32_t)(0);\n"
    "        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x8);\n"
    "        ctx->gpr[31] = vm_read32(ctx->gpr[9] + 0x0);\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": no' da lista circular -- sentinela, payload e next */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_LIST254\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_LIST254_CAP\"); _cap=(_c&&*_c)?atoi(_c):120; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            fprintf(stderr,\"[LIST254] sent=0x%08X no=0x%08X payload=0x%08X next=0x%08X%s\\n\",\n"
    "              (uint32_t)ctx->gpr[30], (uint32_t)ctx->gpr[9],\n"
    "              (uint32_t)ctx->gpr[10], (uint32_t)ctx->gpr[31],\n"
    "              ((uint32_t)ctx->gpr[10]==0u) ? \"  <-- PAYLOAD NULO\" : \"\");\n"
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
        print("MISSING  agulha do laco 0x00254628 nao encontrada", file=sys.stderr)
        return 2
    print("patch_254628_list_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
