#!/usr/bin/env python3
"""GATE DE DIAGNOSTICO (nao e' um fix): tratar `head == 0` como lista vazia em
`func_002545D4`, para ver O QUE ESTA POR TRAS desta parede.

Leia isto antes de usar
-----------------------
Isto **NAO corrige nada** e nao deve ser apresentado como correccao. E' um
salto por cima de uma parede, ligado por env var e OFF por default, cujo unico
proposito e' responder a pergunta "esta parede e' a ultima antes do menu, ou ha
mais dez a seguir?". Qualquer resultado obtido com ele fica marcado como
**obtido com gate**, nunca como progresso natural (CLAUDE.md regras 4 e 5).

O que se sabe, medido a 2026-08-01
----------------------------------
`func_002545B0` (entrada real; `func_002545D4` e' um fragmento) e' chamada
exactamente DUAS vezes por corrida, ambas despachadas de `func_0039D764`:

    [ICALLTO] -> 0x002545B0 r3=0x400C61C8 r4=0x4063858C r5=0x41803D50 r12=0x44000024
    [ICALLTO] -> 0x002545B0 r3=0x400C61C8 r4=0x407790D0 r5=0x0000000F r12=0x88004022

    [E545B0] #1 arg=0x4063858C sent=0x40638608 head=0x40007F34   <- lista valida
    [E545B0] #2 arg=0x407790D0 sent=0x4077914C head=0x00000000   <- laco infinito

O #1 prova que o layout assumido (lista circular em `arg+0x7C`) esta CERTO. No
#2 a `head` e' 0, e o teste de vazio do jogo e' `head == sentinela` -- nunca
satisfeito por um 0 -- por isso o walk entra com um no' nulo, le' o "tipo" do
endereco guest 0x2 e despacha por `tab[lixo]=0`. Da' o
`FATAL: stuck calling 0x00514E80`, tanto com o limite 2000 como com 2 000 000.

E o `0x4077914C` do #2 e' `base+0x80` do objecto que `func_0024C1F8` inicializou
como matriz identidade -- confirmado por `PS3_WATCH_STORE`, escrita unica e
legitima (0.0). Ou seja o objecto do #2 nao e' do tipo que este metodo assume.

Porque isto e' util mesmo assim
-------------------------------
A causa verdadeira esta a montante (quem entrega `0x407790D0` a esta funcao, no
walker de registos do WAD -- `lr=0x0024E2D4`). Descobri-la custa mais rondas.
Entretanto, saber se o boot avanca ou bate logo noutra parede muda a ordem de
prioridades -- e essa informacao custa uma corrida.

Gate: `PS3_LIST254_EMPTY_IF_NULL=1`. Quando ligado, imprime uma linha
`[LIST254-GATE]` por cada vez que salta, para nunca ser silencioso.

Uso:  patch_254610_empty_list_gate.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "LIST254-EMPTY-GATE"

NEEDLE = (
    "        ctx->gpr[31] = vm_read32(ctx->gpr[9] + 0x0);\n"
    "        { int64_t a = (int32_t)ctx->gpr[30]; int64_t b = (int32_t)ctx->gpr[31]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if (((ctx->cr >> 0) & 2)) goto loc_00254684;\n"
)

REPL = (
    "        ctx->gpr[31] = vm_read32(ctx->gpr[9] + 0x0);\n"
    "        /* " + MARKER + " (DIAGNOSTICO, OFF por default, NUNCA um fix):\n"
    "         * head==0 nao e' uma lista vazia para o jogo (o teste dele e'\n"
    "         * head==sentinela), mas tambem nao e' uma lista -- e' um campo de\n"
    "         * outro tipo. Com o gate ligado saltamos o walk para ver o que ha\n"
    "         * a jusante. Barulhento de proposito: uma linha por salto. */\n"
    "        { static int _g=-1; if(_g<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_LIST254_EMPTY_IF_NULL\");\n"
    "            _g=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          if(_g && (uint32_t)ctx->gpr[31]==0u){ static int _n=0; if(_n++<32){\n"
    "            fprintf(stderr,\"[LIST254-GATE] head=0 em sent=0x%08X -> tratado como vazio \"\n"
    "              \"(GATE, nao e' comportamento natural)\\n\", (uint32_t)ctx->gpr[30]);\n"
    "            fflush(stderr); }\n"
    "            goto loc_00254684; } }\n"
    "        { int64_t a = (int32_t)ctx->gpr[30]; int64_t b = (int32_t)ctx->gpr[31]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if (((ctx->cr >> 0) & 2)) goto loc_00254684;\n"
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
        print("MISSING  agulha do teste de lista vazia nao encontrada", file=sys.stderr)
        return 2
    print("patch_254610_empty_list_gate: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
