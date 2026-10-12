#!/usr/bin/env python3
"""Instala o bloco TYPE15/CB56C "prefer real product" (gate PS3_TYPE15_CB56C).

Porque existe
-------------
`patch_type15_cb56c_product.py` e' um VERIFICADOR PURO: procura o marcador
`PS3_TYPE15_CB56C` no lift e sai com rc=1 quando falta ("NOT present (apply
from gow2-recomp session notes)"). Esse marcador nunca teve escritor -- era
edicao MANUAL de sessao dentro do lift gitignored. Medido a 2026-07-25:

    grep -l PS3_TYPE15_CB56C recomp_mid_v2/*.py
        -> so' patch_type15_cb56c_product.py (verificador)
           e patch_b71_skip_icallb_reuse.py (outro verificador)
    grep -c PS3_TYPE15_CB56C <lift limpo + os 73 patches>   -> 0
    grep -c PS3_TYPE15_CB56C recomp_macos_v2/*.cpp          -> 2 (chunk 000)

O verificador ficou a verificar. Este ficheiro e' que faz o comportamento
existir; so' depois o verificador passa legitimamente.

O que instala
-------------
Em `func_000CB56C`, imediatamente antes do par `rldicl/or` que recolhe o
produto do icall1 -- exactamente a posicao que o bloco ocupa no lift de
producao -- a versao COMPLETA do "reuse product":

  * gate de ambiente `PS3_TYPE15_CB56C` (default ON, opt-out `=0`);
  * deteccao de shell da pin-zone (`0x47D00800..0x47D00C00`) alem do r3==0;
  * flag `ty15_reused` + `was_shell=%d` no log.

`patch_type15_cb56c_highbit.py` (que corre ANTES por ordem alfabetica) instala
a 1a geracao do mesmo bloco -- sem gate, so' `r3==0`, mesmo marcador
`[TYPE15] CB56C reuse product` e mesma posicao. Este script SUBSTITUI esse
bloco pelo de producao: sao a mesma insercao, nao duas. Deixar os dois faria o
reuse correr a dobrar.

Fora de ambito (de proposito)
-----------------------------
  * `[POSTINTRO] ...` e `[TYPE15] freelist +24=...` -- probes sem gate de env
    (violariam a regra 6 do CLAUDE.md) e sem escritor; nao entram no bloco.
  * bloco de attach (`[POSTINTRO] CB56C after 2A4FE4` / `SKIP_ATTACH`), que e'
    quem LE `ty15_reused`: depende de `ps3_type15_repair_if_needed` e
    `ps3_type15_product_list_reset`, host helpers que nao existem no lift
    limpo e nao tem escritor. `patch_type15_cc9d0_disc.py` documenta essa
    mesma pre-condicao em falta. Por isso `ty15_reused` fica declarada e
    atribuida sem leitor por agora -- e' o nome exacto que o futuro escritor
    do attach precisa (o build usa `-w`, nao ha' aviso).

Coexistencia com patch_b71_cb56c_reuse_block.py
-----------------------------------------------
A 2026-07-26 apareceu um segundo escritor do MESMO bloco, pelo lado do B71
(`patch_b71_cb56c_reuse_block.py`, que corre antes: 'b' < 't'). Os dois
coexistem de proposito, como `patch_ce03c_introseq_block.py` e
`patch_ce03c_movie_done_reset.py`: ambos sao guardados pelo marcador, o
primeiro a correr instala e o outro reporta ALREADY. Medido num lift limpo com
os dois presentes -- `PS3_TYPE15_CB56C` = 2 e `[TYPE15] CB56C reuse product`
= 1, exactamente como na producao: nao ha' insercao a dobrar. Este ficheiro
continua a ser o escritor que responde por `patch_type15_cb56c_product.py`
(se o do B71 for retirado ou recusar, este instala).

Como foi gerado
---------------
Metodo do `patch_ce03c_introseq_block.py`: (1) clonar o lift limpo, (2) correr
`./apply_all_patches.sh <clone>`, (3) extrair `func_000CB56C` do clone e do
lift de producao `recomp_macos_v2`, (4) diff -> o excedente da producao e' o
bloco orfao, (5) gerar este ficheiro PROGRAMATICAMENTE (script que escreve o
script) com o bloco fatiado verbatim da producao, nunca transcrito a mao.
O corpo restante da funcao NAO e' substituido em bloco: entre o lifter de
producao e o actual ha' drift (callee-save `_cs_*`, `ctx->lr = 0x...`, `stfsu`
com update de r9, casts) e copiar o corpo antigo por cima REGREDIRIA correccoes
do lifter. Insere-se so' o bloco, ancorado -- e' o mesmo metodo do
`patch_type15_cb56c_highbit.py`, ja' commitado.

Contrato de rc
--------------
  ja' instalado (marcador presente)        -> ALREADY, rc=0
  ancora encontrada 1x                     -> substitui, rc=0
  funcao presente mas ancora ausente/dupla -> RECUSA, rc=2 (nao adivinha)
  funcao ausente de todos os chunks        -> rc=2
A recusa e' ATOMICA: em rc=2 nada e' escrito no lift.

Uso: python3 recomp_mid_v2/patch_type15_cb56c_prefer_product_install.py [LIFT_DIR]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

# (funcao, marcador de idempotencia, ancora, substituicao)
SITES: list[tuple[str, str, str, str]] = [
    ('func_000CB56C',
     'PS3_TYPE15_CB56C',
     '        /* TYPE15: reuse existing product at factory+0x48 when construct returns 0. */\n        if ((uint32_t)ctx->gpr[3] == 0u) {\n          uint32_t _ent = 0x47D00000u;\n          uint32_t _vt = vm_read32(_ent);\n          if (_vt == 0x00516D70u) {\n            uint32_t _hdr = vm_read32(_ent + 0x48u);\n            if (_hdr >= 0x10000u && _hdr < 0x4F000000u) {\n              uint32_t _prod = _hdr + 4u;\n              if (_prod >= 0x10000u && _prod < 0x4F000000u) {\n                ctx->gpr[3] = _prod;\n                { static int _n=0; if(_n++<8)\n                    fprintf(stderr,"[TYPE15] CB56C reuse product hdr=0x%08X prod=0x%08X\\n",\n                      _hdr, _prod); }\n              }\n            }\n          }\n        }\n',
     '        /* TYPE15: free-list often empty after first WAD construct; pin +0x48\n         * holds the live product list. Prefer that over pin-zone freelist shells\n         * (shell vt is often non-live 0x0020xxxx). Skip icall2 attach of live\n         * products. Opt-out: PS3_TYPE15_CB56C=0. */\n        int ty15_reused = 0;\n        { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n            const char* e=getenv("PS3_TYPE15_CB56C");\n            /* Default ON. Opt-out: =0. */\n            if (!e) _on = 1;\n            else _on = (*e && *e!=\'0\') ? 1 : 0;}\n          uint32_t _prod_now = (uint32_t)ctx->gpr[3];\n          int shell_bad = (_prod_now >= 0x47D00800u && _prod_now < 0x47D00C00u);\n          if (_on && ((uint32_t)ctx->gpr[3] == 0u || shell_bad)) {\n            uint32_t _ent = 0x47D00000u;\n            uint32_t _vt = vm_read32(_ent);\n            if (_vt == 0x00516D70u) {\n              uint32_t _hdr = vm_read32(_ent + 0x48u);\n              if (_hdr >= 0x10000u && _hdr < 0x4F000000u) {\n                uint32_t _prod = _hdr + 4u;\n                if (_prod >= 0x10000u && _prod < 0x4F000000u) {\n                  ctx->gpr[3] = _prod;\n                  ty15_reused = 1;\n                  { static int _n=0; if(_n++<8)\n                      fprintf(stderr,"[TYPE15] CB56C reuse product hdr=0x%08X prod=0x%08X "\n                        "(skip icall2; was_shell=%d)\\n",\n                        _hdr, _prod, shell_bad); }\n                }\n              }\n            }\n          }\n        }\n'),
]


def func_span(text: str, fn: str):
    sig = "void %s(ppu_context* ctx) {" % fn
    i = text.find(sig)
    if i < 0:
        return None
    j = text.find("\nvoid func_", i + len(sig))
    return (i, len(text) if j < 0 else j)


def apply_site(text: str, fn: str, marker: str, anchor: str, repl: str):
    """(novo_texto, estado) com estado em {'skip','already','applied','refuse'}."""
    span = func_span(text, fn)
    if span is None:
        return text, "skip"
    b0, b1 = span
    body = text[b0:b1]
    if marker in body:
        return text, "already"
    if body.count(anchor) != 1:
        return text, "refuse"
    return text[:b0] + body.replace(anchor, repl, 1) + text[b1:], "applied"


def main() -> int:
    paths = [p for p in resolve_lift_paths(
        sys.argv[1:], str(Path(__file__).resolve().parent.parent / "recomp_macos_v2"))
        if p.is_file()]
    if not paths:
        print("ERRO: nenhum chunk de lift legivel", file=sys.stderr)
        return 2

    # 1a passagem: decide TUDO em memoria. So' se nenhum sitio recusar e' que se
    # escreve -- uma recusa nao pode deixar o lift meio-patcheado.
    seen = {fn: False for fn, _, _, _ in SITES}
    refused: list[str] = []
    pending: list[tuple] = []   # (path, texto_novo)
    applied = already = 0

    for p in paths:
        text = p.read_text(encoding="utf-8", errors="replace")
        orig = text
        for fn, marker, anchor, repl in SITES:
            text, state = apply_site(text, fn, marker, anchor, repl)
            if state == "skip":
                continue
            seen[fn] = True
            if state == "applied":
                applied += 1
                print("  APPLIED  %s :: %s" % (p.name, fn))
            elif state == "already":
                already += 1
                print("  ALREADY  %s :: %s" % (p.name, fn))
            else:
                refused.append("%s :: %s" % (p.name, fn))
                print("  REFUSE   %s :: %s (ancora ausente ou nao unica)" % (p.name, fn))
        if text != orig:
            pending.append((p, text))

    missing = sorted(fn for fn, ok in seen.items() if not ok)
    if refused or missing:
        sys.stdout.flush()
        if refused:
            print("ERRO: corpo gerado diferente do esperado em: %s" % ", ".join(refused),
                  file=sys.stderr)
            print("  O lifter mudou. Nao substituo as cegas -- revalida o bloco contra um\n"
                  "  lift de producao conhecido-bom e regenera este script.", file=sys.stderr)
        if missing:
            print("ERRO: funcao(oes) ausente(s) de todos os chunks: %s" % ", ".join(missing),
                  file=sys.stderr)
        print("  NADA foi escrito (recusa atomica).", file=sys.stderr)
        return 2

    # 2a passagem: escrever.
    for p, text in pending:
        try:
            p.write_text(text, encoding="utf-8", newline="\n")
        except TypeError:
            p.write_text(text, encoding="utf-8")
    print("[%s] ok (%d aplicado, %d ja' aplicado)" % ('type15-cb56c-prefer-product', applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
