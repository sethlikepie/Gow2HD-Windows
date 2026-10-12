#!/usr/bin/env python3
r"""ALLOC-NULL-GUARD, 3.o sitio: func_00263680 (o "alloc+stamp header" da
chunk arena) nunca verificava o r3 devolvido por func_00263040.

Porque existe
--------------
`patch_fios_stream_guards_install.py` ja' instala 2 sitios `[ALLOC-NULL-GUARD]`
(`func_002550C8`/`func_002550E8`) que tratam o mesmo caso: o sub-alloc
`002637D8->00263040` pode devolver 0 quando a free-list da chunk arena esta
esgotada (FREELIST-TAG-GUARD aborta com r3=0), e o codigo natural do guest
carimba um cabecalho de bloco em r3 SEM verificar null primeiro -- carimbando
tags a EA 0 em vez de propagar o "sem memoria" honesto.

`func_00263680` e' outro consumidor DIRECTO de `func_00263040` (chama-o
directamente, nao via trampolim) com o MESMO padrao: logo a seguir a
`ctx->lr = 0x002636D0; func_00263040(ctx); DRAIN_TRAMPOLINE(ctx);`, sem
nenhum teste de r3, o corpo natural faz
`vm_write32(ctx->gpr[3] + 0xC, ...)`, `vm_write32(ctx->gpr[3] + 0x0, ...)`,
`vm_write32(ctx->gpr[3] + 0x10, ...)`, `vm_write16(ctx->gpr[3] + 0x16, ...)`,
`vm_write32(ctx->gpr[3] + 0x8, ...)`, `vm_write16(ctx->gpr[3] + 0x14, ...)` e
(num dos dois ramos) `vm_write32(ctx->gpr[3] + 0x4, ...)` -- carimbar um
cabecalho de chunk inteiro a EA 0 quando o allocator falha.

Encontrado por auditoria estatica (2026-07-31, sessao "a parede depois do 6.o
FREELIST-TAG-GUARD"): dos 365 call-sites directos de `func_00263040` no lift,
`func_00263680` e' o unico que faz stamp incondicional do resultado (os outros
so' guardam r3 num campo ou passam adiante sem o desreferenciar de imediato).
E' o mesmo padrao ja catalogado para 002550C8/002550E8 -- um terceiro sitio da
MESMA familia, nao um bug novo.

O que instala (1 sitio, idempotente)
-------------------------------------
  func_00263680  alloc  sub-alloc 00263040 devolveu 0 -> nao carimba o
                        cabecalho do chunk em EA 0; salta directamente para o
                        epilogo natural da propria funcao (restore dos
                        callee-saved + return), propagando r3=0 ao SEU
                        chamador -- exactamente o "sem memoria" honesto, sem
                        inventar nenhum valor plausivel para o guest seguir.

O que isto NAO resolve
------------------------
Medido no mesmo gate (`boot_gow2_f263040guard`, `PS3_TRACE_MEMORY=1`): a
rajada de OOB dominante apos o `[POSTINTRO] CB56C` (>99% dos eventos MEMORY/OOB
da corrida, todos em `func_0024C878`) NAO passa por `func_00263680` -- essa
funcao nunca aparece como `ea=0x0000000X` (a assinatura do stamp-a-zero) nas
corridas capturadas. `func_0024C878` faz o SEU proprio null-check do ponteiro
primario (salta se nulo) e diverge a percorrer um ponteiro/contador que ja
vinha corrompido de outro lado -- sintoma classificado, nao ainda a causa
raiz (ver nota da sessao). Este patch fecha um bug REAL e da MESMA familia
catalogada (nao e' invenção), mas nao e' o fix que fecha o gate de 6 corridas.

Fontes
------
- Auditoria estatica: `recomp_macos_v2.f263040guard/ppu_recomp_000.cpp`
  (func_00263680, linha ~559016-559066 na sessao de origem).
- Sessao/medicao: notas desta sessao (2026-07-31, "a parede depois do 6.o
  FREELIST-TAG-GUARD").

Uso: python3 recomp_mid_v2/patch_263680_alloc_null_guard.py [LIFT_DIR]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

SITES: list[tuple[str, str, str, str]] = [
    ('func_00263680',
     '[ALLOC-NULL-GUARD] 263680',
     '        ctx->lr = 0x002636D0; func_00263040(ctx); DRAIN_TRAMPOLINE(ctx);\n',
     '        ctx->lr = 0x002636D0; func_00263040(ctx); DRAIN_TRAMPOLINE(ctx);\n'
     '        /* ALLOC-NULL-GUARD: 00263040 can return 0 when the chunk arena\n'
     '         * free-list is exhausted (FREELIST-TAG-GUARD abort). Natural code\n'
     '         * below stamps a chunk header at r3 unconditionally (offsets\n'
     '         * 0x0/0x4/0x8/0xC/0x10/0x14/0x16) with no null-check -> tags get\n'
     '         * stamped at EA 0 instead of propagating the honest "no memory".\n'
     '         * Same class as 002550C8/002550E8 (gow2-asset-pipeline-map Task 3b).\n'
     '         * Skip the stamp and return r3=0 via this function\'s own epilogue. */\n'
     '        if ((uint32_t)ctx->gpr[3] == 0u) {\n'
     '          { static int _n=0; if(_n++<16){\n'
     '            fprintf(stderr,"[ALLOC-NULL-GUARD] 263680 sub-alloc r3=0 -> skip stamp@0\\n");\n'
     '            fflush(stderr); } }\n'
     '          ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xB0);\n'
     '          ctx->gpr[26] = _cs_26;\n'
     '          ctx->gpr[27] = _cs_27;\n'
     '          ctx->lr = ctx->gpr[0];\n'
     '          ctx->gpr[28] = _cs_28;\n'
     '          ctx->gpr[29] = _cs_29;\n'
     '          ctx->gpr[30] = _cs_30;\n'
     '          ctx->gpr[31] = _cs_31;\n'
     '          ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0xA0);\n'
     '          return;\n'
     '        }\n'),
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

    seen = {fn: False for fn, _, _, _ in SITES}
    refused: list[str] = []
    pending: list[tuple] = []
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

    for p, text in pending:
        try:
            p.write_text(text, encoding="utf-8", newline="\n")
        except TypeError:
            p.write_text(text, encoding="utf-8")
    print("[%s] ok (%d aplicado, %d ja' aplicado)" % ('263680-alloc-null-guard', applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
