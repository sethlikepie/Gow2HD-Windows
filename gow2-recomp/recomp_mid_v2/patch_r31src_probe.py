#!/usr/bin/env python3
"""R31SRC -- de onde vem o r31 que o E83 mediu como sendo o TOC.

Porque existe
-------------
O E83 mediu, in-boot e em 2/2 corridas, que `func_002649FC` escreve a estrutura
que devia ser do objecto por cima do rodata do jogo, porque `ctx->gpr[31]` vale
`0x00541178` -- o TOC. E `func_00264988` faz `r31 := r3`, logo o r3 que lhe
chega ja' e' o TOC. O r3 e' o retorno de `func_00264BA8`.

O disassembly do EBOOT (0x00264BA8-0x00264C00) mostra que o lift e' FIEL, e que
o retorno da fabrica e':

    lwz  r9, -0x2080(r2) ; lwz r11, 0x24(r9) ; or r3, r11, r11
    lwz  r9, 0x0(r11)    ; lwz r10, 0x48(r9) ; lwz r0, 0x0(r10)
    mtctr r0 ; lwz r2, 0x4(r10) ; bctrl        <- chamada VIRTUAL; r3 = retorno dela
    ld   r2, 0x28(r1)                          <- o nosso TOCFIX (escreve r2, valor certo)
    rldicl r3,r3,0,32 ; lwz r3, 0x48(r3)       <- r3 = *(retorno + 0x48)
    bl   0x263554                              <- r3 = func_00263554(r3)
    blr                                        <- devolve esse r3

Duas hipoteses sobrevivem, e uma corrida separa-as:
  H1  a chamada virtual (`bctrl`) nao resolve, e o `ps3_indirect_call` devolve
      sem tocar em r3 -- entao r3 fica com o `or r3,r11,r11` anterior, e o TOC
      entra por ai.
  H2  a virtual corre, e o veneno vem de `*(ret+0x48)` ou de `func_00263554`.

Desenho (o que ja custou caro noutros ledgers)
----------------------------------------------
- **CALL SITE, nao callee.** Os pontos ficam dentro de `func_00264BA8` e no sitio
  de `func_00237CDC` que chama `func_00264988` -- cada um unico -- e nao dentro
  de `func_00263554`, que tem muitos chamadores e daria etiquetas ambiguas.
- **Captura, nao recalcula:** cada ponto imprime `ctx->gpr[3]` VIVO na posicao
  exacta do fluxo; nenhum valor e' reconstruido a partir de outro registo.
- **Mesma invocacao numa linha:** o ponto P3 imprime o r3 imediatamente antes de
  `func_00264988(ctx)`, que e' o r31 que a estrutura vai usar. P1/P2 imprimem o
  antes/depois da virtual na mesma passagem.
- **Sem cap:** sao poucas chamadas (o E83 mediu DUAS escritas na corrida inteira).
- **Controle:** P3 carimba `CTRL=TOC` quando o r3 e' exactamente 0x00541178, que
  e' a condicao que o E83 mediu. Se nunca aparecer TOC, a corrida nao apanhou o
  caminho e nada abaixo se conclui.

Gate: `PS3_TRACE_R31SRC` (OFF por default).
rc: 0 aplicado/ja aplicado; 2 se algum needle nao casar exactamente 1x.
"""
import glob
import os
import re
import sys

MARKER = "R31SRC"


def p(tag, extra=""):
    return (
        '        /* ' + MARKER + '-' + tag + ' */\n'
        '        { static int _r_on = -1; if (_r_on < 0) { extern char* getenv(const char*);\n'
        '              const char* _e = getenv("PS3_TRACE_R31SRC");\n'
        '              _r_on = (_e && *_e && *_e != \'0\') ? 1 : 0; }\n'
        '          if (_r_on) { fprintf(stderr, "[R31SRC] ' + tag + ' r3=0x%08X r11=0x%08X'
        + (' %s' % extra if extra else '') + '\\n",\n'
        '                (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[11]'
        + (', ((uint32_t)ctx->gpr[3] == 0x00541178u) ? "CTRL=TOC" : "CTRL=outro"'
           if extra else '') + '); fflush(stderr); } }\n'
    )


# --- needles, cada um procurado dentro do corpo da sua funcao ---------------
FABRICA = "func_00264BA8"
# Os dois call sites de func_00264988 vivem em funcoes DIFERENTES: o lift
# partiu 0x00237CDC-0x0023807C em dois simbolos, e o 2o call site caiu no
# fragmento func_0023807C. Cobrir so um deixaria um zero por explicar se o
# caminho mudasse de corrida para corrida.
CHAMADORES = (("func_00237CDC", "00237D4C"), ("func_0023807C", "002380EC"))

# dentro de func_00264BA8: antes e depois da chamada virtual
N_PRE_V = ("        ctx->gpr[2] = vm_read32(ctx->gpr[10] + 0x4);\n"
           "        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n")

# O pre-virtual imprime tambem o ALVO. Sem ele nao se distingue "a virtual correu
# e o metodo devolveu 0" de "despachou para um stub/lixo que devolve 0" -- e a
# primeira corrida ficou exactamente nessa duvida. ctr/r9/r10 estao todos vivos
# aqui (o lift acabou de os calcular), portanto e' captura e nao reconstrucao.
PRE_V_ALVO = (
    '        /* ' + MARKER + '-pre-virtual */\n'
    '        { static int _r_on = -1; if (_r_on < 0) { extern char* getenv(const char*);\n'
    '              const char* _e = getenv("PS3_TRACE_R31SRC");\n'
    '              _r_on = (_e && *_e && *_e != \'0\') ? 1 : 0; }\n'
    '          if (_r_on) { fprintf(stderr, "[R31SRC] pre-virtual r3=0x%08X r11=0x%08X "\n'
    '                "vtable=0x%08X opd=0x%08X ctr=0x%08X toc=0x%08X\\n",\n'
    '                (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[11],\n'
    '                (uint32_t)ctx->gpr[9], (uint32_t)ctx->gpr[10],\n'
    '                (uint32_t)ctx->ctr, (uint32_t)ctx->gpr[2]); fflush(stderr); } }\n'
)

R_PRE_V = ("        ctx->gpr[2] = vm_read32(ctx->gpr[10] + 0x4);\n"
           + PRE_V_ALVO +
           "        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
           + p("pos-virtual"))

# dentro de func_00237CDC: o r3 passado a func_00264988 (dois call sites iguais)
N_CALL = "        ctx->lr = 0x%s; func_00264988(ctx); DRAIN_TRAMPOLINE(ctx);\n"


def corpo_de(src, fn):
    m = re.search(r"^void " + fn + r"\(ppu_context\* ctx\) \{\n", src, re.M)
    if not m:
        return None
    fim = src.find("\n}\n", m.end())
    return (m.start(), fim + 3) if fim >= 0 else None


def patch_text(src, erros):
    if MARKER in src:
        return src, "ALREADY"
    tocou = False
    # --- fabrica ---
    span = corpo_de(src, FABRICA)
    if span:
        a, b = span
        corpo = src[a:b]
        n = corpo.count(N_PRE_V)
        if n != 1:
            erros.append("%s: needle da virtual casou %d vezes no corpo" % (FABRICA, n))
            return src, "ERRO"
        src = src[:a] + corpo.replace(N_PRE_V, R_PRE_V, 1) + src[b:]
        tocou = True
    # --- chamadores: um call site por funcao, cada um com o seu lr unico ---
    for fn, lr in CHAMADORES:
        span = corpo_de(src, fn)
        if not span:
            continue
        a, b = span
        corpo = src[a:b]
        needle = N_CALL % lr
        n = corpo.count(needle)
        if n != 1:
            erros.append("%s: call site lr=%s casou %d vezes no corpo" % (fn, lr, n))
            return src, "ERRO"
        src = src[:a] + corpo.replace(needle, p("arg-" + fn, "%s") + needle, 1) + src[b:]
        tocou = True
    return (src, "APPLIED") if tocou else (src, "SEM-FUNCAO")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not chunks:
        print("ERRO: nenhum ppu_recomp_*.cpp em %s" % lift, file=sys.stderr)
        return 2
    erros = []
    applied = already = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        out, estado = patch_text(src, erros)
        if estado == "APPLIED":
            with open(path, "w") as fh:
                fh.write(out)
            applied += 1
            print("APPLIED  %s" % os.path.basename(path))
        elif estado == "ALREADY":
            already += 1
        elif estado == "ERRO":
            for e in erros:
                print("ERRO     %s: %s" % (os.path.basename(path), e), file=sys.stderr)
            return 2
    if applied:
        print("patch_r31src_probe: applied=%d" % applied)
        return 0
    if already:
        print("ALREADY  (%d chunk(s))" % already)
        return 0
    print("MISSING  nem %s nem %s em %s" % (FABRICA, CHAMADOR, lift), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
