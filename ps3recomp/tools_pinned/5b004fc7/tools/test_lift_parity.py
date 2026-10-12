#!/usr/bin/env python3
"""Testes de lift_parity.py — em especial: os sinais tem dentes?

Um sinal de paridade que nunca dispara e' pior que nao existir, porque da' uma
falsa sensacao de cobertura. Estes testes usam o caso REAL que custou o boot em
2026-07-26 (`func_0037C384`, truncada a 2 instrucoes quando o ELF tem 12, com o
`bl 0x00379C70` do heap no meio) e exigem que o sinal o apanhe.

Uso: python3 test_lift_parity.py   (rc=0 verde, rc=1 falha)
"""
from __future__ import annotations

import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lift_parity as P  # noqa: E402

FAILS: list[str] = []


def check(name: str, got, want) -> None:
    if got != want:
        FAILS.append(f"{name}\n    got : {got!r}\n    want: {want!r}")


def levels(sigs):
    return {(lvl, name) for lvl, name, _ in sigs}


# --- classify() ------------------------------------------------------------
# elf[ea] = (n_insns, n_calls, end_ea)

def test_truncated_apanha_o_caso_real() -> None:
    """func_0037C384: ELF 12 insns, lift emite 3 linhas. O bug de 2026-07-26."""
    ea = 0x0037C384
    lift = {ea: {"lines": 3, "calls": 0, "tail_to": 0x0037C3B4, "chunk": "001"}}
    elf = {ea: (12, 1, 0x0037C3B4)}
    sigs = levels(P.classify(ea, lift, elf))
    check("TRUNCATED dispara no caso real", ("RED", "TRUNCATED") in sigs, True)
    check("CALL_MISMATCH tambem (o bl do heap sumiu)",
          ("YELLOW", "CALL_MISMATCH") in sigs, True)


def test_funcao_sa_nao_dispara() -> None:
    """A mesma funcao inteira: 12 insns, 14 linhas, 1 chamada. Silencio."""
    ea = 0x0037C384
    lift = {ea: {"lines": 14, "calls": 1, "tail_to": None, "chunk": "001"}}
    elf = {ea: (12, 1, 0x0037C3B4)}
    check("funcao sa nao dispara nada", P.classify(ea, lift, elf), [])


def test_funcao_trivial_nao_e_truncated() -> None:
    """Uma funcao a serio pequena nao pode ser RED — senao o sinal e' ruido."""
    ea = 0x1000
    lift = {ea: {"lines": 3, "calls": 0, "tail_to": None, "chunk": "000"}}
    elf = {ea: (4, 0, 0x1010)}
    check("4 insns / 3 linhas nao e' truncada",
          ("RED", "TRUNCATED") in levels(P.classify(ea, lift, elf)), False)


def test_no_body() -> None:
    ea = 0x2000
    check("funcao no ELF e ausente do lift",
          ("RED", "NO_BODY") in levels(P.classify(ea, {}, {ea: (10, 2, 0x2028)})), True)


def test_call_mismatch_grande_e_red() -> None:
    """Perder 4+ chamadas e' RED. Foi o que aconteceu em func_0032E200 (7)."""
    ea = 0x0032E200
    lift = {ea: {"lines": 200, "calls": 0, "tail_to": None, "chunk": "003"}}
    elf = {ea: (250, 7, 0x0032E900)}
    check("perder 7 chamadas e' RED",
          ("RED", "CALL_MISMATCH") in levels(P.classify(ea, lift, elf)), True)


def test_call_mismatch_pequeno_e_yellow() -> None:
    ea = 0x3000
    lift = {ea: {"lines": 40, "calls": 3, "tail_to": None, "chunk": "000"}}
    elf = {ea: (50, 4, 0x30C8)}
    check("perder 1 chamada e' YELLOW",
          ("YELLOW", "CALL_MISMATCH") in levels(P.classify(ea, lift, elf)), True)


def test_lift_com_mais_chamadas_nao_dispara() -> None:
    """O lift pode ter MAIS chamadas que o ELF (probes, helpers). Nao e' erro."""
    ea = 0x4000
    lift = {ea: {"lines": 40, "calls": 9, "tail_to": None, "chunk": "000"}}
    elf = {ea: (50, 4, 0x40C8)}
    check("mais chamadas no lift nao dispara",
          any(n == "CALL_MISMATCH" for _, n in levels(P.classify(ea, lift, elf))), False)


# --- scan_lift() -----------------------------------------------------------

def test_scan_lift_conta_chamadas_e_trampolim() -> None:
    src = (
        'void func_0037C384(ppu_context* ctx) {\n'
        '        ctx->gpr[4] = ctx->gpr[3] | ctx->gpr[3];\n'
        '        ctx->lr = 0x0037C39C; func_00379C70(ctx); DRAIN_TRAMPOLINE(ctx);\n'
        '        ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]);\n'
        '        return;\n'
        '}\n'
        'void func_0037C3B4(ppu_context* ctx) {\n'
        '        { g_trampoline_fn = (void(*)(void*))func_0037C400; return; }\n'
        '}\n'
    )
    with tempfile.TemporaryDirectory() as td:
        Path(td, "ppu_recomp_001.cpp").write_text(src, encoding="utf-8")
        got = P.scan_lift(td)
    check("duas funcoes lidas", sorted(got), [0x0037C384, 0x0037C3B4])
    check("chamadas contadas (func_ + ps3_call_opd)", got[0x0037C384]["calls"], 2)
    check("sem trampolim na 1a", got[0x0037C384]["tail_to"], None)
    check("trampolim lido na 2a", got[0x0037C3B4]["tail_to"], 0x0037C400)


def test_scan_lift_nao_conta_a_chaveta_de_fecho() -> None:
    src = ('void func_00001000(ppu_context* ctx) {\n'
           '        return;\n'
           '}\n')
    with tempfile.TemporaryDirectory() as td:
        Path(td, "ppu_recomp_000.cpp").write_text(src, encoding="utf-8")
        got = P.scan_lift(td)
    check("uma linha de corpo", got[0x00001000]["lines"], 1)


def test_scan_lift_le_o_corpo_de_um_weak_override() -> None:
    """Fase 19 (XEN-04): o corpo de uma funcao com override sai em
    `PPC_FUNC_IMPL(func_X) {`, seguido de um wrapper FRACO `PPC_FUNC(func_X)`.

    Este teste nasceu VERMELHO de uma medicao, nao de uma previsao: o primeiro
    override real (func_00010230, o entry point do guest) fez o gate acusar
    `[RED] NO_BODY ... ausente do lift` e o lift_parity subir de 77 para 78
    sinais. O corpo estava la'; cego estava o parser.

    As duas asseveracoes sao igualmente necessarias:
      - o corpo do `_IMPL` e' lido (senao: NO_BODY falso);
      - o wrapper NAO sobrescreve a entrada (senao: TRUNCATED falso, com uma
        unica linha de corpo, que e' pior porque parece plausivel)."""
    src = (
        'PPC_FUNC_IMPL(func_00010230) {\n'
        '        ctx->gpr[2] = (int64_t)(int32_t)(0);\n'
        '        ctx->lr = 0x00010250; func_00010354(ctx); DRAIN_TRAMPOLINE(ctx);\n'
        '        { g_trampoline_fn = (void(*)(void*))func_00010254; return; }\n'
        '}\n'
        'PPC_FUNC(func_00010230) {\n'
        '    __imp_func_00010230(ctx);\n'
        '}\n'
    )
    with tempfile.TemporaryDirectory() as td:
        Path(td, "ppu_recomp_000.cpp").write_text(src, encoding="utf-8")
        got = P.scan_lift(td)
    check("a funcao com override e' vista", sorted(got), [0x00010230])
    check("corpo do __imp_ contado (3 linhas), nao o wrapper (1)",
          got[0x00010230]["lines"], 3)
    check("chamada do corpo contada", got[0x00010230]["calls"], 1)
    check("trampolim do corpo lido", got[0x00010230]["tail_to"], 0x00010254)


def test_chain_soma_os_fragmentos() -> None:
    """A unidade comparavel e' a CADEIA, nao o fragmento.

    O lifter parte uma funcao real em varios func_* encadeados por
    g_trampoline_fn. Julgar cada fragmento contra o span inteiro do ELF acusava
    truncagem em toda a fragmentacao legitima: 1139 falsos RED, medido.
    """
    lift = {
        0x1000: {"lines": 5, "calls": 1, "tail_to": 0x1100, "chunk": "000"},
        0x1100: {"lines": 6, "calls": 2, "tail_to": 0x1200, "chunk": "000"},
        0x1200: {"lines": 4, "calls": 1, "tail_to": None, "chunk": "000"},
    }
    check("soma da cadeia", P.chain_metrics(0x1000, lift), (15, 4, 3))
    check("cadeia de um so", P.chain_metrics(0x1200, lift), (4, 1, 1))


def test_chain_nao_pendura_em_ciclo() -> None:
    """Ha cadeias que voltam atras. Sem guarda de ciclo isto nao termina."""
    lift = {
        0x1000: {"lines": 3, "calls": 0, "tail_to": 0x1100, "chunk": "000"},
        0x1100: {"lines": 3, "calls": 0, "tail_to": 0x1000, "chunk": "000"},
    }
    check("ciclo termina e conta cada um uma vez",
          P.chain_metrics(0x1000, lift), (6, 0, 2))


def test_fragmentacao_legitima_nao_e_truncada() -> None:
    """3 fragmentos que juntos cobrem o span nao podem dar RED."""
    ea = 0x1000
    lift = {
        ea:      {"lines": 5, "calls": 1, "tail_to": 0x1100, "chunk": "000"},
        0x1100:  {"lines": 6, "calls": 2, "tail_to": 0x1200, "chunk": "000"},
        0x1200:  {"lines": 4, "calls": 1, "tail_to": None,   "chunk": "000"},
    }
    elf = {ea: (16, 4, 0x1240)}
    check("cadeia completa nao dispara", P.classify(ea, lift, elf), [])
    # e o mesmo caso julgado fragmento a fragmento seria RED:
    check("sem cadeia seria falso RED",
          ("RED", "TRUNCATED") in levels(P.classify(ea, lift, elf, use_chain=False)), True)


def main() -> int:
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print(f"FAIL ({len(FAILS)}):", file=sys.stderr)
        for f in FAILS:
            print("  " + f, file=sys.stderr)
        return 1
    print("[test_lift_parity] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
