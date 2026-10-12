#!/usr/bin/env python3
"""Sonda do alocador virtual em `func_00210878` -- quem devolve o texto?

Porque existe (E24, 2026-08-05)
-------------------------------
`func_00210ED4` (o laco do `lr=0x00210F8C`) percorre uma lista circular com
sentinela em `this+0x18` e tem DOIS desfechos:

    loc_00210F70   lista esgotada -> r3 = func_00210878(this, 0x40)   ALOCA
    loc_00210F80   no' encontrado -> r3 = o no'
                   lr = 0x00210F8C; func_00227528(r3, ...)

Medido: 12 chamadas com `this` sao e passo `0x34` (sao nos da lista) e UMA com
`this = 0x2F725F70`, que lido como bytes e' `"/r_p"`. Logo o valor mau vem do
ramo da alocacao.

E dentro de `func_00210878` a alocacao e' uma **chamada virtual**:

    r9  = *(this + 0x0)          objecto alocador
    r11 = *(r9 + 0x10)           slot OPD
    ctr = *(r11 + 0);  r2 = *(r11 + 4)
    ps3_indirect_call(ctx)       <- a alocacao
    r10 = r3 + 4                 o no' e' alocacao+4

O valor devolvido e' o que esse alocador devolver. Falta saber **qual funcao
e'** e confirmar o valor.

O que mede
----------
Uma linha por alocacao: o alvo do despacho (`ctr`, antes da chamada), o `this`
do alocador (`r9`), o slot OPD (`r11`) e o valor devolvido (`r3`). Com o alvo
em mao, o nome sai de `nm`/da tabela de funcoes.

Gate: `PS3_TRACE_ALLOC878` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_ALLOC878_CAP` (default 200).

Nota sobre a agulha: a sequencia curta em volta do `ps3_indirect_call` aparece
**5 vezes** no lift. A agulha usada tem 7 linhas e inclui o `+0x18` da
sentinela, o que a torna unica (verificado a 2026-08-05). Uma agulha curta
teria patchado quatro sitios sem relacao nenhuma com isto.

Uso:  patch_210878_alloc_probe.py [DIR_DE_LIFT]
rc: 0 aplicado ou ja' aplicado; 2 se a agulha nao casou (SEM-EFEITO).
"""
import os
import sys
import glob

MARKER = "ALLOC878-PROBE"

NEEDLE = (
    "        ctx->gpr[29] = ctx->gpr[31] + (int64_t)(0x18);\n"
    "        ctx->gpr[9] = vm_read32(ctx->gpr[31] + 0x0);\n"
    "        ctx->gpr[11] = vm_read32(ctx->gpr[9] + 0x10);\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[11] + 0x0);\n"
    "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
    "        ctx->ctr = (uint32_t)ctx->gpr[0];\n"
    "        ctx->gpr[2] = vm_read32(ctx->gpr[11] + 0x4);\n"
)

# Guarda o alvo e o contexto ANTES da chamada; imprime DEPOIS, ja' com o r3.
REPL = NEEDLE + (
    "        /* " + MARKER + ": alvo e resultado do alocador virtual.\n"
    "         * Ver docs/re_sessions/2026-08-05-E24-o-caminho-de-alocacao.md */\n"
    "        uint32_t _a878_tgt = (uint32_t)ctx->ctr;\n"
    "        uint32_t _a878_obj = (uint32_t)ctx->gpr[9];\n"
    "        uint32_t _a878_slot = (uint32_t)ctx->gpr[11];\n"
)

# O bloco de impressao vai depois do proprio ps3_indirect_call, que e' a linha
# imediatamente a seguir a' agulha.
AFTER_NEEDLE = "        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"

AFTER_REPL = AFTER_NEEDLE + (
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_ALLOC878\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_ALLOC878_CAP\"); _cap=(_c&&*_c)?atoi(_c):200; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _r=(uint32_t)ctx->gpr[3];\n"
    "            char _asc[5]; for(int _b=0;_b<4;_b++){ unsigned _c2=(_r>>(8*(3-_b)))&0xFFu;\n"
    "              _asc[_b]=(_c2>=32&&_c2<127)?(char)_c2:'.'; } _asc[4]='\\0';\n"
    "            fprintf(stderr,\"[ALLOC878] #%d alvo=0x%08X obj=0x%08X slot=0x%08X -> r3=0x%08X \\\"%s\\\"\\n\",\n"
    "              _n, _a878_tgt, _a878_obj, _a878_slot, _r, _asc);\n"
    "            fflush(stderr); } } }\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ja"
    if t.count(NEEDLE) != 1:
        return t, "nao-casou"
    novo = t.replace(NEEDLE, REPL, 1)
    # A chamada indirecta tem de vir LOGO a seguir ao bloco que acabamos de
    # inserir; so' patchamos essa ocorrencia, nao as outras quatro do lift.
    i = novo.find(REPL)
    j = novo.find(AFTER_NEEDLE, i)
    if j < 0 or j != i + len(REPL):
        return t, "nao-casou"
    novo = novo[:j] + AFTER_REPL + novo[j + len(AFTER_NEEDLE):]
    return novo, "ok"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(here), "recomp_macos_v2")

    alvos = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not alvos:
        print("SEM-EFEITO: nenhum ppu_recomp_*.cpp em %s" % lift)
        return 2

    aplicados = ja = 0
    for f in alvos:
        with open(f, encoding="utf-8", errors="replace") as fh:
            src = fh.read()
        novo, estado = patch_text(src)
        if estado == "ja":
            ja += 1
            print("JA-APLICADO  %s" % os.path.basename(f))
        elif estado == "ok":
            with open(f, "w", newline="\n", encoding="utf-8") as fh:
                fh.write(novo)
            aplicados += 1
            print("CONVERTIDO   %s" % os.path.basename(f))

    if aplicados == 0 and ja == 0:
        print("SEM-EFEITO: agulha do alocador de func_00210878 nao casou em %s" % lift)
        return 2

    print("patch_210878_alloc_probe: aplicados=%d ja=%d" % (aplicados, ja))
    return 0


if __name__ == "__main__":
    sys.exit(main())
