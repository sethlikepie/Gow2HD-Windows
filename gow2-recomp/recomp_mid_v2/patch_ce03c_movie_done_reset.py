#!/usr/bin/env python3
"""Instala a chamada a movie_done_timebased_reset() em func_000CE03C.

Porque existe
-------------
O lift em producao chama `movie_done_timebased_reset()` a partir de
`func_000CE03C`, mas NENHUM script versionado instalava essa chamada -- era uma
edicao manual dentro do lift gitignored. Consequencia medida (2026-07-25): um
build a partir de codigo committado nao linkava,

    Undefined symbols for architecture arm64:
      "_movie_done_timebased_reset", referenced from:
          func_000CE03C(ppu_context*) in ppu_recomp_001.cpp.o

e o baseline do RDY-0 nao era reconstruivel. A funcao ja' foi committada
(gow2-recomp 09fa568); faltava a CHAMADA. E' isso que este script repoe.

O que faz
---------
Insere, imediatamente a seguir ao bloco de `movie_vt_clear_overlay_done()`
dentro do handler do CE03C:

    { extern void movie_done_timebased_reset(void) asm("_movie_done_timebased_reset");
      movie_done_timebased_reset(); }

Semantica: ao re-entrar no CE03C para um novo Play, o timer time-based do filme
anterior tem de ser desarmado, senao fica sticky e termina o StartSeq#2 de
imediato (ver o comentario em ../movie_eos_arm.c).

AVISO IMPORTANTE -- a ancora tambem e' orfa
-------------------------------------------
O bloco em que este patch se ancora (`movie_vt_clear_overlay_done()`, e todo o
`[INTROSEQ] CE03C ...` a volta) NAO tem escritor versionado nenhum:
`patch_ce03c_wait_idle_f2b_movie.py` e' um VERIFICADOR PURO (0 escritas). Ou
seja, num re-lift verdadeiramente limpo a ancora nao existe e este script FALHA
-- de proposito, com rc=2 e mensagem explicita, em vez de nao fazer nada em
silencio. Um "no-op silencioso" e' precisamente o que deixou o problema
escondido ate agora.

Fechar isso por completo exige um segundo patch que instale o bloco CE03C
inteiro. Fica registado em notes/2026-07-25-baseline-depende-de-wip.md.

SUBSUMIDO PELO MID-ASM (Fase 17, plano 17-03)
---------------------------------------------
O corpo host `gow2_midasm_Ce03cWaitIdle` (games/gow2/hooks/gow2_midasm_hooks.cpp)
CHAMA `movie_done_timebased_reset()` -- tal como o bloco injectado ja' fazia --
e o lifter emite esse hook sozinho quando corre com `--config`. Num lift assim,
este script nao tem ancora nenhuma para casar (o bloco `[INTROSEQ]` nunca chega
a existir no lift) e, ate' esta fase, FALHAVA com rc=2 -- medido em
/tmp/m1_f17_apply_patches.log antes desta alteracao. Um FAILED nao e' o
resultado certo: a chamada ESTA' la', so' que vinda do host.

Por isso o script passa a reconhecer o mid-asm e a devolver MIDASM/rc=0. Em
lifts ANTIGOS (sem --config) o comportamento nao muda um bit: a ancora existe e
a insercao acontece como antes.

Uso:  patch_ce03c_movie_done_reset.py [LIFT_DIR_OU_FICHEIRO...]
rc=0 aplicado, ja' aplicado ou mid-asm | rc=2 ancora ausente | rc=3 lift ilegivel
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths            # noqa: E402

MARKER = "movie_done_timebased_reset"

# Mid-asm (Fase 17): a chamada que o lifter emite dentro de func_000CE03C
# quando corre com --config. Procurada SO' na regiao dessa funcao -- o lifter
# tambem escreve a DECLARACAO no preambulo de TODAS as TUs, e um `in t` global
# daria MIDASM em chunks que nem sequer tem a funcao.
SIG_CE03C = "void func_000CE03C(ppu_context* ctx) {\n"
MIDASM_CALL = "gow2_midasm_Ce03cWaitIdle(ctx);"

# Weak override (Fase 19, plano 19-03): com [[functions_override]] declarado
# para 0x000CE03C a funcao deixa de sair na forma `void func_000CE03C(...)` e
# passa a `PPC_FUNC_IMPL(func_000CE03C)`. O tem_midasm() abaixo procura a forma
# antiga para delimitar a regiao, e sem esta constante devolveria False num
# lift onde o hook ESTA' la' -- o script iria entao procurar a ancora do bloco
# injectado, nao a encontraria, e sairia rc=2. Vermelho falso.
WEAK_IMPL = "PPC_FUNC_IMPL(func_000CE03C)"


def tem_midasm(t: str) -> bool:
    """A regiao de func_000CE03C ja' traz o hook mid-asm emitido pelo lifter?

    Conhece as DUAS formas de inicio de funcao: a de sempre e a do par
    __imp_/wrapper fraco que a Fase 19 introduziu para as funcoes declaradas em
    [[functions_override]]. A de fim tambem: o wrapper `PPC_FUNC(func_X)` vem
    logo a seguir ao corpo e nao pode ser confundido com a funcao seguinte.
    """
    i = t.find(WEAK_IMPL)
    if i >= 0:
        j = min((k for k in (t.find("\nvoid func_", i + 20),
                             t.find("\nPPC_FUNC_IMPL(func_", i + 20),
                             t.find("\nPPC_FUNC(func_", i + 20)) if k >= 0),
                default=-1)
        region = t[i:j] if j >= 0 else t[i:]
        return MIDASM_CALL in region
    i = t.find(SIG_CE03C)
    if i < 0:
        return False
    j = t.find("void func_", i + 20)
    region = t[i:j] if j >= 0 else t[i:]
    return MIDASM_CALL in region

ANCHOR = (
    '            { extern void movie_vt_clear_overlay_done(void) '
    'asm("_movie_vt_clear_overlay_done");\n'
    "              movie_vt_clear_overlay_done(); }\n"
)

INSERT = (
    '            { extern void movie_done_timebased_reset(void) '
    'asm("_movie_done_timebased_reset");\n'
    "              movie_done_timebased_reset(); }\n"
)


def patch_one(path: Path) -> int:
    """0 = aplicado, 1 = ja' aplicado, 2 = mid-asm, -1 = ancora ausente."""
    try:
        t = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"  {path.name}: ilegivel ({exc})")
        return -2
    if tem_midasm(t):
        print(f"  {path.name}: MIDASM (movie_done_timebased_reset() vem do hook host)")
        return 2
    if MARKER not in t and ANCHOR not in t:
        return -1
    if MARKER in t:
        n = t.count(f"{MARKER}();")
        print(f"  {path.name}: ALREADY ({n} chamada(s))")
        return 1
    if ANCHOR not in t:
        return -1
    n_anchor = t.count(ANCHOR)
    t = t.replace(ANCHOR, ANCHOR + INSERT)
    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:                                   # Python antigo
        path.write_text(t, encoding="utf-8")
    print(f"  {path.name}: APPLIED ({n_anchor} sitio(s))")
    return 0


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "ppu_recomp_001.cpp")
    seen = applied = already = midasm = 0
    for p in paths:
        if not p.is_file():
            continue
        seen += 1
        r = patch_one(p)
        if r == 0:
            applied += 1
        elif r == 1:
            already += 1
        elif r == 2:
            midasm += 1
        elif r == -2:
            return 3
    if not seen:
        print("ERRO: nenhum ficheiro de lift legivel", file=sys.stderr)
        return 3
    if midasm:
        print(f"[ce03c-movie-done-reset] SKIP: subsumido pelo mid-asm neste lift "
              f"({midasm} ficheiro(s) com gow2_midasm_Ce03cWaitIdle)")
        return 0
    if applied or already:
        print(f"[ce03c-movie-done-reset] ok ({applied} aplicado, {already} ja' aplicado)")
        return 0
    print("ERRO: ancora ausente em todos os ficheiros.\n"
          "  O bloco movie_vt_clear_overlay_done() do CE03C nao existe neste lift.\n"
          "  Esse bloco TAMBEM nao tem escritor versionado "
          "(patch_ce03c_wait_idle_f2b_movie.py e' verificador puro),\n"
          "  portanto num re-lift limpo tem de ser instalado primeiro.\n"
          "  Sem a chamada, o link falha em _movie_done_timebased_reset.",
          file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
