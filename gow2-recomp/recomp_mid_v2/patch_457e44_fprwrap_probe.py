#!/usr/bin/env python3
"""FPRWRAP -- o discriminador do E82 na entrada e nos dois ciclos de `func_00457E44`.

Porque existe
-------------
O E78 mediu que o tick para em `func_00457E44` (normalizador de angulo) e deixou
o discriminador DESENHADO E NAO CORRIDO: imprimir os FPRs a entrada. Ao ler o
lift antes de o escrever (E82), aparece um facto que muda as hipoteses: os dois
valores do ciclo sao lidos **TOC-relativos** --

    vm_read32(ctx->gpr[2] + 0x10C)   -> minimo   (0x00541284 se r2 = 0x00541178)
    vm_read32(ctx->gpr[2] + 0x108)   -> periodo  (0x00541280,  = 2pi no ELF)

-- portanto um `r2` errado envenena os dois, e o ciclo compara valores que o jogo
nunca escreveu. As quatro predicoes do E82 (TOC errado / periodo zero / fpr1 muito
negativo / funcao sa e a parede e' depois dela) sao todas separadas por uma so
corrida desta sonda.

Desenho
-------
- **Captura, nunca recalcula.** Os floats impressos sao `ctx->fpr[0]`/`fpr[13]`
  VIVOS no ponto onde o ciclo os usa, com os bits tirados por memcpy do proprio
  valor -- nao uma segunda leitura de memoria que poderia ler outro estado.
- **Controle:** imprime o EA calculado dos dois loads. Se nao for
  `0x00541284`/`0x00541280`, o controle falhou e os floats nao dizem nada sobre
  `DAT_00541280` -- e a linha traz `CTRL=BAD` para isso nao passar despercebido.
- **Marcos em vez de cap** dentro dos ciclos: 1, 10, 1e3, 1e6 e depois cada 1e8.
  Um cap aqui esconderia precisamente a diferenca entre "nao converge" e "1e29
  iteracoes" (o erro dos ledgers E63/E64).
- **Entrada, saida normal e saida pelo trampolim** sao todas impressas: sem a
  saida nao se distingue "a funcao trava" de "a funcao e' so' a ultima migalha
  instrumentada" (o erro do degrau 33 no E73).

Ancoragem: todas as substituicoes sao feitas SO' dentro do corpo de
`func_00457E44` (extraido do `void func_00457E44` ate' a primeira linha `}` a
coluna 0), e cada needle e' contado ai dentro. Fora do corpo estas linhas
repetem-se as centenas no chunk -- aplicar por texto global instrumentaria
codigo nao relacionado.

Gate: `PS3_TRACE_FPRWRAP` (OFF por default; no-op no baseline).

Uso:  patch_457e44_fprwrap_probe.py [DIR_DE_LIFT]
rc: 0 aplicado / ja aplicado; 2 se a funcao ou algum needle nao casou.
"""
import glob
import os
import re
import sys

MARKER = "FPRWRAP"
FUNC = "func_00457E44"

# --- os blocos injectados ---------------------------------------------------

PROLOGO = (
    '        /* ' + MARKER + ' */\n'
    '        static int _fw_on = -1; static unsigned long long _fw_n = 0;\n'
    '        unsigned long long _fw_add = 0, _fw_sub = 0;\n'
    '        if (_fw_on < 0) { extern char* getenv(const char*);\n'
    '            const char* _e = getenv("PS3_TRACE_FPRWRAP");\n'
    '            _fw_on = (_e && *_e && *_e != \'0\') ? 1 : 0; }\n'
    '        if (_fw_on) { _fw_n++;\n'
    '            fprintf(stderr, "[FPRWRAP] entra #%llu r2=0x%08X r3=0x%08X "\n'
    '                    "p1=%.9g p2=%.9g\\n", _fw_n,\n'
    '                    (uint32_t)ctx->gpr[2], (uint32_t)ctx->gpr[3],\n'
    '                    (double)ctx->fpr[1], (double)ctx->fpr[2]); fflush(stderr); }\n'
)

# impresso depois de os DOIS loads TOC-relativos terem corrido, antes do 1o ciclo
PRE_LOOP = (
    '        /* ' + MARKER + '-PRE */\n'
    '        if (_fw_on) {\n'
    '            uint32_t _ea_min = (uint32_t)(ctx->gpr[2] + 0x10C);\n'
    '            uint32_t _ea_per = (uint32_t)(ctx->gpr[2] + 0x108);\n'
    '            float _fmin = (float)ctx->fpr[0]; uint32_t _bmin; memcpy(&_bmin, &_fmin, 4);\n'
    '            float _fper = (float)ctx->fpr[13]; uint32_t _bper; memcpy(&_bper, &_fper, 4);\n'
    '            fprintf(stderr, "[FPRWRAP] pre-loop #%llu ea_min=0x%08X min=%.9g(0x%08X) "\n'
    '                    "ea_per=0x%08X per=%.9g(0x%08X) p1=%.9g CTRL=%s\\n", _fw_n,\n'
    '                    _ea_min, (double)ctx->fpr[0], _bmin,\n'
    '                    _ea_per, (double)ctx->fpr[13], _bper, (double)ctx->fpr[1],\n'
    '                    (_ea_min == 0x00541284u && _ea_per == 0x00541280u) ? "OK" : "BAD");\n'
    '            fflush(stderr); }\n'
)


def marco(var, tag):
    """Marcos logaritmicos: 1, 10, 1e3, 1e6, depois cada 1e8. Sem cap."""
    return (
        '        /* ' + MARKER + '-' + tag + ' */\n'
        '        if (_fw_on) { ' + var + '++;\n'
        '            if (' + var + ' == 1ULL || ' + var + ' == 10ULL || ' + var + ' == 1000ULL ||\n'
        '                ' + var + ' == 1000000ULL || (' + var + ' % 100000000ULL) == 0ULL) {\n'
        '                fprintf(stderr, "[FPRWRAP] ' + tag + ' #%llu it=%llu p1=%.9g\\n",\n'
        '                        _fw_n, ' + var + ', (double)ctx->fpr[1]); fflush(stderr); } }\n'
    )


def saida_inline(tag):
    """Igual a `saida`, mas indentado para dentro do bloco do ramo do trampolim."""
    return (
        '            /* ' + MARKER + '-' + tag + ' */\n'
        '            if (_fw_on) { fprintf(stderr, "[FPRWRAP] ' + tag + ' #%llu p1=%.9g "\n'
        '                    "it_add=%llu it_sub=%llu\\n", _fw_n, (double)ctx->fpr[1],\n'
        '                    _fw_add, _fw_sub); fflush(stderr); }\n'
    )


def saida(tag):
    return (
        '        /* ' + MARKER + '-' + tag + ' */\n'
        '        if (_fw_on) { fprintf(stderr, "[FPRWRAP] ' + tag + ' #%llu p1=%.9g "\n'
        '                "it_add=%llu it_sub=%llu\\n", _fw_n, (double)ctx->fpr[1],\n'
        '                _fw_add, _fw_sub); fflush(stderr); }\n'
    )


# --- os needles, todos procurados SO' dentro do corpo -----------------------

N_PRE_LOOP = "loc_00457E54:\n"
N_LOOP_ADD = "        ctx->fpr[1] = ppu_fp_single(ppu_fadd(ctx->fpr[1], ctx->fpr[13]));\n"
N_LOOP_SUB = "        ctx->fpr[1] = ppu_fp_single(ppu_fsub(ctx->fpr[1], ctx->fpr[13]));\n"
N_TRAMP = ("        if ((!((ctx->cr >> 0) & 8))) { g_trampoline_fn = "
           "(void(*)(void*))func_00457EA4; return; }\n")
N_RET = "        vm_write32(ctx->gpr[3] + 0x8, ctx->gpr[6]);\n        return;\n"

RE_FUNC = re.compile(r"^void " + FUNC + r"\(ppu_context\* ctx\) \{\n", re.M)


def patch_corpo(corpo, erros):
    """Aplica as seis insercoes ao corpo. Cada needle tem de casar exactamente 1x."""
    def sub1(needle, novo, nome):
        n = corpo.count(needle)
        if n != 1:
            erros.append("%s: esperava 1 ocorrencia no corpo, achei %d" % (nome, n))
            return None
        return corpo.replace(needle, novo, 1)

    # prologo: logo apos a chaveta de abertura
    m = RE_FUNC.search(corpo)
    if not m:
        erros.append("abertura de %s nao casou" % FUNC)
        return corpo
    corpo = corpo[:m.end()] + PROLOGO + corpo[m.end():]

    for needle, novo, nome in (
        (N_PRE_LOOP, PRE_LOOP + N_PRE_LOOP, "pre-loop"),
        (N_LOOP_ADD, N_LOOP_ADD + marco("_fw_add", "loop-add"), "loop-add"),
        (N_LOOP_SUB, N_LOOP_SUB + marco("_fw_sub", "loop-sub"), "loop-sub"),
        (N_TRAMP,
         "        if ((!((ctx->cr >> 0) & 8))) {\n"
         + saida_inline("sai-cedo")
         + "            g_trampoline_fn = (void(*)(void*))func_00457EA4; return; }\n",
         "trampolim"),
        (N_RET, saida("sai") + N_RET, "return"),
    ):
        out = sub1(needle, novo, nome)
        if out is None:
            return corpo
        corpo = out
    return corpo


def patch_text(src, erros):
    if MARKER in src:
        return src, "ALREADY"
    m = RE_FUNC.search(src)
    if not m:
        return src, "SEM-FUNCAO"
    # corpo = da abertura ate' a primeira linha "}" a coluna 0
    fim = src.find("\n}\n", m.end())
    if fim < 0:
        erros.append("nao achei o fecho de %s" % FUNC)
        return src, "ERRO"
    corpo = src[m.start():fim + 3]
    novo = patch_corpo(corpo, erros)
    if erros:
        return src, "ERRO"
    return src[:m.start()] + novo + src[fim + 3:], "APPLIED"


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
        print("patch_457e44_fprwrap_probe: applied=%d" % applied)
        return 0
    if already:
        print("ALREADY  (%d chunk(s))" % already)
        return 0
    print("MISSING  %s nao existe em nenhum chunk de %s" % (FUNC, lift), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
