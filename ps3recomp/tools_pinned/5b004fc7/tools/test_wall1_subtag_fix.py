#!/usr/bin/env python3
"""Testes OFFLINE que dao dentes ao fix da parede 1 (E6) ANTES de o correr
in-boot como veredicto.

Contexto: docs/re_sessions/2026-08-05-E6-parede1-mesma-doenca.md. O
dispatcher `func_0024E1E8` (Ghidra `.opd.FUN_0024e198`) resolve a fabrica
de um objecto so' pelo `low16` do seu cabecalho (`idx = (low16<<2)&0x3FFFC`)
e invoca incondicionalmente `fab->vt[0x18](fab, obj)` -- a MESMA doenca da
parede 4 (docs/re_sessions/2026-08-04-E2-quem-forca-low16.md), sitio
diferente. Um registo WAD (`subtag=3`) fica indistinguivel de um container
de lista (`subtag=1`) porque ambos dao `low16=1`.

O predicado de poda e' IMPORTADO de tools/test_wall4_subtag_fix.py::prune,
nao reimplementado -- e' literalmente o mesmo bug de bits, agora aplicado a
outro dispatcher. O que este ficheiro adiciona: um conjunto NEGATIVO
proprio, MEDIDO nesta sessao (nao herdado da parede 4) -- as 26 entradas de
`[REGTAB]` que a sonda `PS3_TRACE_TYPETAG` dumpou in-boot em 3/3 corridas
(`../gow2-recomp/boot_gow2_e6wall1`, ver o ledger para os logs completos) --
e o censo `seen=1 would_prune=1` que confirma que, nesta janela de boot, o
UNICO objecto que passa por este dispatch e' o patologico.

Sem pytest (convencao tools/test_*.py deste repo). Run:
    python3 tools/test_wall1_subtag_fix.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from test_wall4_subtag_fix import prune  # noqa: E402  -- NAO reimplementado


# ---------------------------------------------------------------------------
# Grupo 1: o registo WAD medido nesta sessao (E6, 3/3 corridas) TEM de ser podado
# ---------------------------------------------------------------------------

def test_wad_register_w0_is_pruned() -> None:
    # w0 medido directamente (PS3_TRACE_TYPETAG_W0) no dispatch de
    # func_0024E1E8, 3/3 corridas, idêntico: obj varia (0x4077AC10/0x4077AC50,
    # variação normal do heap), w0 não varia.
    w0_measured = 0x40030001
    assert prune(w0_measured) is True, "o registo WAD medido pela E6 TEM de ser podado"
    low16 = w0_measured & 0xFFFF
    subtag = (w0_measured >> 16) & 0xFFF
    assert low16 == 1, hex(low16)
    assert subtag == 3, hex(subtag)


# ---------------------------------------------------------------------------
# Grupo 2: as 26 entradas de [REGTAB] medidas in-boot (3/3 corridas,
# PS3_TRACE_TYPETAG) -- nenhuma pode ser podada quando auto-consistente
# (low16 == subtag == o proprio tag do registry)
# ---------------------------------------------------------------------------

# Tags medidos verbatim em ../gow2-recomp/boot_gow2_e6wall1, 3/3 corridas
# (grep '\[REGTAB\]' | tag=...), idênticos entre corridas -- 26 entradas
# não-nulas da tabela tab=0x00868D48.
MEASURED_REGTAB_TAGS = (
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 25, 27, 32,
)


def test_measured_regtab_tags_count() -> None:
    assert len(MEASURED_REGTAB_TAGS) == 26, (
        "a sonda [REGTAB] mediu exactamente 26 entradas nao-nulas, 3/3 "
        "corridas -- se este numero mudar, a lista acima tem de ser "
        "re-medida, nao ajustada a olho"
    )
    assert len(set(MEASURED_REGTAB_TAGS)) == 26, "sem duplicados"


def test_all_measured_tags_self_consistent_never_pruned() -> None:
    """Para cada tag legitimo do registry, um objecto AUTO-CONSISTENTE
    (low16 == subtag == tag) nunca pode ser podado -- e' o conjunto
    NEGATIVO que impede o fix de cortar codigo saudavel."""
    for tag in MEASURED_REGTAB_TAGS:
        for flags_nibble in (0x4, 0x8, 0xC, 0xE):
            w0 = (flags_nibble << 28) | (tag << 16) | tag
            assert prune(w0) is False, (
                f"tag {tag} (0x{tag:X}) w0=0x{w0:08X} auto-consistente NAO "
                f"pode ser podado -- esta no [REGTAB] medido in-boot"
            )

    # O caso mais sensivel: tag=1 e' precisamente a familia de listas
    # legitima que colide com o registo WAD no low16. Um objecto REAL dessa
    # familia (subtag=1 tambem) nunca pode ser confundido com o WAD.
    assert 1 in MEASURED_REGTAB_TAGS
    assert prune(0xC0010001) is False, "familia de listas legitima (subtag=1) nao podada"
    assert prune(0x40030001) is True, "registo WAD (subtag=3, low16=1) podado"


# ---------------------------------------------------------------------------
# Grupo 3: censo medido (seen=1, would_prune=1, 3/3 corridas) -- documenta a
# garantia de blast radius, nao reexecuta o boot
# ---------------------------------------------------------------------------

def test_measured_census_matches_single_pathological_object() -> None:
    """docs/re_sessions/2026-08-05-E6-parede1-mesma-doenca.md, tabela de
    3 corridas: seen=1 would_prune=1 em TODAS. Isto e' o que torna o fix
    seguro (blast radius = exactamente o caso patologico, medido, nao
    assumido por analogia com a parede 4 -- aviso do coordenador)."""
    measured_runs = [
        {"seen": 1, "would_prune": 1, "w0": 0x40030001},
        {"seen": 1, "would_prune": 1, "w0": 0x40030001},
        {"seen": 1, "would_prune": 1, "w0": 0x40030001},
    ]
    for run in measured_runs:
        assert run["seen"] == run["would_prune"], (
            "todo objecto visto neste sitio, nesta janela de boot, e' o "
            "patologico -- nenhum objecto legitimo passa por aqui, logo "
            "nenhum risco medido de o fix cortar codigo saudavel"
        )
        assert prune(run["w0"]) is True


# ---------------------------------------------------------------------------
# Grupo 4: mutacao -- um predicado plausivel mas errado tem de falhar esta suite
# ---------------------------------------------------------------------------

def test_mutation_wrong_predicate_fails_this_suite() -> None:
    def prune_mutated(w0: int) -> bool:
        # inverte a condicao do subtag: poda quando BATE, nao quando difere
        return (w0 & 0xFFFF) == 1 and ((w0 >> 16) & 0xFFF) == 1

    caught = False
    try:
        assert prune_mutated(0x40030001) is True
        assert prune_mutated(0xC0010001) is False
        for tag in MEASURED_REGTAB_TAGS:
            w0 = (0xC << 28) | (tag << 16) | tag
            assert prune_mutated(w0) is False
    except AssertionError:
        caught = True
    assert caught, (
        "prune_mutated (== invertido) devia falhar contra o conjunto "
        "negativo medido, mas passou -- o teste nao e' discriminativo"
    )


def main() -> int:
    groups = [
        ("test_wad_register_w0_is_pruned", test_wad_register_w0_is_pruned),
        ("test_measured_regtab_tags_count", test_measured_regtab_tags_count),
        ("test_all_measured_tags_self_consistent_never_pruned",
         test_all_measured_tags_self_consistent_never_pruned),
        ("test_measured_census_matches_single_pathological_object",
         test_measured_census_matches_single_pathological_object),
        ("test_mutation_wrong_predicate_fails_this_suite",
         test_mutation_wrong_predicate_fails_this_suite),
    ]

    failed = []
    for name, fn in groups:
        try:
            fn()
        except AssertionError as exc:
            print(f"[wall1] FAIL {name}: {exc}")
            failed.append(name)
        except Exception as exc:  # noqa: BLE001
            print(f"[wall1] FAIL {name}: erro inesperado {type(exc).__name__}: {exc}")
            failed.append(name)
        else:
            print(f"[wall1] PASS {name}")

    if failed:
        print(f"[wall1] FAILED ({len(failed)}): {', '.join(failed)}")
        return 1

    print(
        "[wall1] todos os grupos PASS -- predicado de poda (reaproveitado da "
        "parede 4) validado contra o conjunto negativo medido para ESTE "
        "sitio (26 tags de [REGTAB], censo seen=1/would_prune=1). Sem esta "
        "suite, o fix da parede 1 (E6) nao tem dentes."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
