#!/usr/bin/env python3
"""Sonda + saida de emergencia da travessia de lista em `func_004244C0`.

Porque existe (E75 + medicao de 2026-08-08)
-------------------------------------------
Cadeia medida ate' aqui:

    [STATE] frame=1 ... code 0x002B2DD0        loop principal despacha o frame 1
    [B71] func_002B2DD0 #001 -> func_002B2660  imprime; #002 e #003 NUNCA
    [B71] func_002B2660 #001 -> ps3_indirect_call r3=0x400C3D48 ctr=0x004244C0

Ou seja: o tick da aplicacao entra em `func_004244C0` e nao volta. E essa funcao
e', no original:

    piVar1 = param_1[9];                       /* head */
    while (param_1 + 9 != piVar1) {            /* sentinela = &obj[9] = obj+0x24 */
        piVar2 = piVar1 ? piVar1 - 2 : 0;
        piVar1 = *piVar1;                      /* next */
        if ((*(u16*)(piVar2+1) & 0x10) == 0) {
            if ((*(u16*)(piVar2+1) & 3) == 0) vt[0x1c](obj, piVar2);
            else                              vt[0x20](obj, piVar2);
        }
    }

**Travessia de lista circular, sem limite de iteracoes.** Se a lista nao fechar
no sentinela -- um `next` que nunca la' chega -- o jogo fica ali para sempre. E'
exactamente a classe de corrupcao que esta sessao andou a medir: objectos com o
slot de dono errado sao ligados a' lista do dono errado (E77).

No lift, o ciclo tem cabeca em `loc_004244EC`, sentinela em `gpr[28]`
(= obj+0x24) e no' corrente em `gpr[31]`.

O QUE ESTE PATCH FAZ
--------------------
1. `PS3_TRACE_LISTWALK=1` conta as iteracoes e imprime as primeiras 16 mais cada
   potencia de 2 (no', sentinela). Um ciclo infinito fica visivel em segundos,
   com os enderecos que o compoem.

2. `PS3_LISTWALK_LIMIT=N` forca a saida do ciclo ao fim de N iteracoes, saltando
   para o epilogo. **E' um gate de DIAGNOSTICO, nao um fix**: abandonar a
   travessia a meio deixa filhos por processar, o que nao e' fiel ao CELL. Serve
   para responder a uma pergunta e so' a uma:

       se o jogo passar dos degraus 23/32 com o ciclo cortado, a travessia E' a
       parede; se nao passar, nao e' e a parede esta' mais abaixo.

   OFF por default (limite 0 = sem corte).

Uso:  patch_4244c0_listwalk.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "LISTWALK-PROBE"

# `loc_004244EC` e' unico no lift (label = endereco guest).
NEEDLE = (
    "loc_004244EC:\n"
    "        { int64_t a = (int32_t)ctx->gpr[28]; int64_t b = (int32_t)ctx->gpr[31];"
)

BLOCK = (
    "loc_004244EC:\n"
    "        /* " + MARKER + ": travessia de lista circular sem limite.\n"
    "         * sentinela = gpr[28] (obj+0x24), no' corrente = gpr[31]. */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_LISTWALK\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _lim=-1; if(_lim<0){ extern char* getenv(const char*);\n"
    "            const char* _l=getenv(\"PS3_LISTWALK_LIMIT\");\n"
    "            _lim=(_l&&*_l)?atoi(_l):0; }\n"
    "          static unsigned long _n=0; ++_n;\n"
    "          if(_on && (_n<=16 || (_n&(_n-1))==0))\n"
    "            fprintf(stderr,\"[LISTWALK] #%lu no=0x%08X sentinela=0x%08X\\n\",\n"
    "              _n,(uint32_t)ctx->gpr[31],(uint32_t)ctx->gpr[28]), fflush(stderr);\n"
    "          if(_lim>0 && _n>(unsigned long)_lim){\n"
    "            fprintf(stderr,\"[LISTWALK] CORTE ao fim de %lu iteracoes \"\n"
    "              \"(PS3_LISTWALK_LIMIT) -- diagnostico, nao fix\\n\", _n);\n"
    "            fflush(stderr); _n=0; goto loc_00424568; } }\n"
    "        { int64_t a = (int32_t)ctx->gpr[28]; int64_t b = (int32_t)ctx->gpr[31];"
)


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    n = t.count(NEEDLE)
    if not n:
        return t, "MISSING", 0
    return t.replace(NEEDLE, BLOCK), "APPLIED", n


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
        print("MISSING  cabeca do ciclo loc_004244EC nao encontrada", file=sys.stderr)
        return 2
    print("patch_4244c0_listwalk: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
