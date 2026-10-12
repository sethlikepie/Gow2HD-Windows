#!/usr/bin/env python3
"""Testes de gen_catalog.py (Fase 4, Plano 04-01, Tasks 1 e 2).

Task 1 (7 testes): classify() contra 6 patches reais representando os 4
ramos de classificacao, incluindo os dois casos-armadilha (nome-mente-
comportamento-manda, docstring-menciona-mas-nao-escreve) e a prova de que a
deteccao ESCREVE tem de ser por AST (um grep de substring cai no falso-
positivo medido dentro do proprio docstring de
patch_ce03c_wait_idle_f2b_movie.py).

Task 2 (4 testes): cobertura 87/87 contra o corpus real, zero indeterminados,
regressao por mutacao sintetica (fixture em tempfile.TemporaryDirectory(),
nunca no directorio real), e cruzamento com MANIFEST.tsv.

PATCH_DIR aponta para ../gow2-recomp/recomp_mid_v2 (o directorio que
apply_all_patches.sh de facto corre), NUNCA para o espelho subtree atrasado
games/gow2/recomp_mid_v2 (75/87 ficheiros).

Run directly:
  .venv/bin/python3 games/gow2/lift_baseline/test_gen_catalog.py
Exit code 0 = todas as verificacoes passaram (11 linhas [PASS]).
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import gen_catalog  # noqa: E402

# HERE = .../ps3recomp/games/gow2/lift_baseline
# HERE.parents[3] = .../PESSOAL (irmao de ps3recomp e de gow2-recomp)
MONOREPO_ROOT = HERE.parents[2]
SIBLING_ROOT = HERE.parents[3]
PATCH_DIR = (SIBLING_ROOT / "gow2-recomp" / "recomp_mid_v2").resolve()
MANIFEST_PATH = HERE / "MANIFEST.tsv"

print(f"[info] MONOREPO_ROOT resolvido: {MONOREPO_ROOT}")
print(f"[info] PATCH_DIR resolvido: {PATCH_DIR}")
assert PATCH_DIR.is_dir(), f"PATCH_DIR nao existe: {PATCH_DIR}"


def _read(name: str) -> str:
    return (PATCH_DIR / name).read_text(encoding="utf-8", errors="replace")


def check_has_write_call_ast_vs_substring_false_positive() -> None:
    """Teste 1: has_write_call() por AST, prova do falso-positivo de substring."""
    import ast

    src_wadld = _read("patch_wadld_alloc_probe.py")
    src_ce03c_stop = _read("patch_ce03c_pre_play_stop.py")
    src_ce03c_wait = _read("patch_ce03c_wait_idle_f2b_movie.py")

    assert gen_catalog.has_write_call(ast.parse(src_wadld)) is True
    assert gen_catalog.has_write_call(ast.parse(src_ce03c_stop)) is False

    # Falso-positivo medido: "write_text" aparece na PROSA do docstring de
    # patch_ce03c_wait_idle_f2b_movie.py (linha "0 write_text/open('w')"), um
    # grep ingenuo por substring dava True; o AST correcto da False.
    assert "write_text" in src_ce03c_wait, (
        "pre-condicao do teste falhou: a string 'write_text' devia estar na "
        "prosa do docstring deste ficheiro"
    )
    assert gen_catalog.has_write_call(ast.parse(src_ce03c_wait)) is False
    print(
        "[PASS] has_write_call() por AST: True em patch_wadld_alloc_probe.py, "
        "False em patch_ce03c_pre_play_stop.py e em patch_ce03c_wait_idle_f2b_movie.py "
        "(este ultimo contem 'write_text' na prosa -- prova do falso-positivo de substring)"
    )


def check_ramo1_orfao_sem_fonte() -> None:
    """Teste 2: ramo 1 (inventar comportamento) -- os dois CE03C verificadores puros."""
    for name in ("patch_ce03c_pre_play_stop.py", "patch_ce03c_wait_idle_f2b_movie.py"):
        src = _read(name)
        got = gen_catalog.classify(name, src)
        assert got == ("PROBE", "orfao-sem-fonte", True), f"{name}: {got}"

    hits = [
        p.name
        for p in PATCH_DIR.glob("patch_*.py")
        if "inventar comportamento" in p.read_text(encoding="utf-8", errors="replace")
    ]
    assert len(hits) == 2, f"esperado exactamente 2 ficheiros, obtido {hits}"
    print(
        "[PASS] ramo 1 (orfao-sem-fonte): os 2 CE03C classificam PROBE/orfao-sem-fonte/True; "
        "'inventar comportamento' casa em exactamente 2 ficheiros no corpus real"
    )


def check_ramo2_diagnostico_autodeclarado() -> None:
    """Teste 3: ramo 2 (Diagnostic only) -- patch_cc9d0_live_yield.py."""
    name = "patch_cc9d0_live_yield.py"
    src = _read(name)
    got = gen_catalog.classify(name, src)
    assert got == ("PROBE", "diagnostico", True), got

    hits = [
        p.name
        for p in PATCH_DIR.glob("patch_*.py")
        if "Diagnostic only" in p.read_text(encoding="utf-8", errors="replace")
    ]
    assert len(hits) == 1, f"esperado exactamente 1 ficheiro, obtido {hits}"
    print(
        "[PASS] ramo 2 (diagnostico autodeclarado): patch_cc9d0_live_yield.py classifica "
        "PROBE/diagnostico/True; 'Diagnostic only' casa em exactamente 1 ficheiro"
    )


def check_ramo3_positivo_probe_sem_despacho() -> None:
    """Teste 4: ramo 3 positivo -- sufixo _probe, corpo sem tokens de despacho."""
    name = "patch_wadld_alloc_probe.py"
    got = gen_catalog.classify(name, _read(name))
    assert got == ("PROBE", "diagnostico", True), got
    print(
        "[PASS] ramo 3 positivo: patch_wadld_alloc_probe.py (sufixo _probe, sem tokens "
        "de despacho no corpo) classifica PROBE/diagnostico/True"
    )


def check_ramo3_negativo_nome_mente_comportamento_manda() -> None:
    """Teste 5: ramo 3 negativo -- nome sugere probe/trace, corpo despacha de facto."""
    for name in ("patch_2b0fb4_trace.py", "patch_a1_chain_probe.py"):
        got = gen_catalog.classify(name, _read(name))
        assert got == ("FUNCIONAL", "dispatch-fix", False), f"{name}: {got}"
    print(
        "[PASS] ramo 3 negativo (nome mente, comportamento manda): "
        "patch_2b0fb4_trace.py e patch_a1_chain_probe.py classificam "
        "FUNCIONAL/dispatch-fix/False apesar do sufixo _trace/_probe"
    )


def check_protecao_docstring_menciona_mas_nao_escreve() -> None:
    """Teste 6: DISPATCH_RE nao deve reclassificar por mencao em prosa no docstring."""
    name = "patch_fios_open_probe.py"
    src = _read(name)
    assert "g_trampoline_fn =" in src, (
        "pre-condicao do teste falhou: o docstring devia mencionar g_trampoline_fn ="
    )
    got = gen_catalog.classify(name, src)
    assert got == ("PROBE", "diagnostico", True), got
    print(
        "[PASS] protecao contra falso-positivo de docstring: patch_fios_open_probe.py "
        "menciona 'g_trampoline_fn =' em prosa mas nunca no corpo -- continua "
        "PROBE/diagnostico/True, nao e' reclassificado para FUNCIONAL"
    )


def check_ramo4_subclasse_por_token_de_nome() -> None:
    """Teste 7: ramo 4 -- subclasse derivada do primeiro token do nome que casar.

    Chama classify() duas vezes com os mesmos argumentos para provar, de
    caminho, o criterio de aceitacao "classify() e' funcao pura" (sem estado
    global mutavel entre chamadas).
    """
    name = "patch_32e200_opd.py"
    src = _read(name)
    a = gen_catalog.classify(name, src)
    b = gen_catalog.classify(name, src)
    assert a == b == ("FUNCIONAL", "opd", False), (a, b)
    print(
        "[PASS] ramo 4: patch_32e200_opd.py classifica FUNCIONAL/opd/False "
        "(token 'opd' no nome); chamada duas vezes com os mesmos argumentos "
        "devolve o mesmo resultado (classify() e' pura)"
    )


# ---------------------------------------------------------------------------
# Task 2
# ---------------------------------------------------------------------------


def check_cobertura_87_87_contra_corpus_real() -> None:
    """Teste de cobertura: uma linha por patch_*.py real, nunca um 87 hardcoded morto."""
    rows = gen_catalog.build_catalog(PATCH_DIR, MANIFEST_PATH)
    esperado = len(list(PATCH_DIR.glob("patch_*.py")))
    assert len(rows) == esperado, f"esperado {esperado} linhas, obtido {len(rows)}"
    nomes_rows = {r["patch"] for r in rows}
    nomes_disco = {p.name for p in PATCH_DIR.glob("patch_*.py")}
    assert nomes_rows == nomes_disco, "cobertura incompleta ou divergente do disco"
    print(
        f"[PASS] cobertura: build_catalog() produz {len(rows)} linhas, "
        f"igual a len(list(PATCH_DIR.glob('patch_*.py'))) == {esperado} "
        "(linha de base historica 2026-07-26: 87; o numero acima e' o medido AGORA)"
    )


def check_zero_indeterminado() -> None:
    """Teste zero-indeterminado: classe sempre em {PROBE, FUNCIONAL}, subclasse/razao nunca vazias."""
    rows = gen_catalog.build_catalog(PATCH_DIR, MANIFEST_PATH)
    n_probe = 0
    n_funcional = 0
    for r in rows:
        assert r["classe"] in ("PROBE", "FUNCIONAL"), f"classe fora do dominio: {r}"
        assert r["subclasse"], f"subclasse vazia: {r}"
        if r["classe"] == "PROBE":
            assert r["razao"], f"razao vazia para PROBE: {r}"
            n_probe += 1
        else:
            n_funcional += 1
    print(
        f"[PASS] zero indeterminado: {len(rows)} linhas, classe sempre "
        f"PROBE/FUNCIONAL, subclasse nunca vazia, razao nunca vazia quando PROBE "
        f"(medido: {n_funcional} FUNCIONAL / {n_probe} PROBE)"
    )


def check_mutacao_patch_sintetico_cai_em_funcional_misc() -> None:
    """Teste de mutacao: um patch novo que nao casa nenhum dos 3 primeiros ramos
    cai em FUNCIONAL/misc -- nunca fica de fora da contagem nem vira indeterminado."""
    sintetico = (
        "#!/usr/bin/env python3\n"
        '"""Patch sintetico de teste -- nao casa nenhum dos 3 primeiros ramos."""\n'
        "def main():\n"
        "    return 0\n"
        "if __name__ == '__main__':\n"
        "    raise SystemExit(main())\n"
    )
    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        (dirp / "patch_zz_teste_cobertura.py").write_text(sintetico, encoding="utf-8")
        manifest = dirp / "MANIFEST.tsv"
        manifest.write_text("# marcador\ttipo\tmin_count\tchunks_origem\n", encoding="utf-8")

        rows = gen_catalog.build_catalog(dirp, manifest)
        assert len(rows) == 1, f"esperado 1 linha, obtido {len(rows)}"
        row = rows[0]
        assert row["patch"] == "patch_zz_teste_cobertura.py"
        assert row["classe"] == "FUNCIONAL", row
        assert row["subclasse"] == "misc", row
    print(
        "[PASS] mutacao: patch_zz_teste_cobertura.py (sintetico, fixture em "
        "tempfile.TemporaryDirectory(), fora do directorio real) cai em "
        "FUNCIONAL/misc -- entra na contagem, nunca fica indeterminado"
    )


def check_cruzamento_com_manifest() -> None:
    """Teste de cruzamento: a coluna marcador cita um TAG do MANIFEST.tsv real
    que tambem aparece no corpo do patch (D-4.3: gerado a partir dos patch_*.py
    MAIS do MANIFEST.tsv, coluna marcador nao fica sempre vazia)."""
    tags = gen_catalog.load_manifest_tags(MANIFEST_PATH)
    assert "[WADLD-ALLOC]" in tags, "MANIFEST.tsv devia ter uma linha TAG [WADLD-ALLOC]"

    rows = gen_catalog.build_catalog(PATCH_DIR, MANIFEST_PATH)
    by_name = {r["patch"]: r for r in rows}
    row = by_name["patch_wadld_alloc_probe.py"]
    assert "[WADLD-ALLOC]" in row["marcador"].split(","), (
        f"esperado [WADLD-ALLOC] na coluna marcador de patch_wadld_alloc_probe.py: {row}"
    )

    com_marcador = [r for r in rows if r["marcador"]]
    assert com_marcador, "nenhuma linha do catalogo tem a coluna marcador preenchida"
    print(
        "[PASS] cruzamento com MANIFEST: patch_wadld_alloc_probe.py cita [WADLD-ALLOC] "
        f"na coluna marcador; {len(com_marcador)}/{len(rows)} linhas do catalogo "
        "tem marcador nao-vazio"
    )


def main() -> int:
    checks = (
        check_has_write_call_ast_vs_substring_false_positive,
        check_ramo1_orfao_sem_fonte,
        check_ramo2_diagnostico_autodeclarado,
        check_ramo3_positivo_probe_sem_despacho,
        check_ramo3_negativo_nome_mente_comportamento_manda,
        check_protecao_docstring_menciona_mas_nao_escreve,
        check_ramo4_subclasse_por_token_de_nome,
        check_cobertura_87_87_contra_corpus_real,
        check_zero_indeterminado,
        check_mutacao_patch_sintetico_cai_em_funcional_misc,
        check_cruzamento_com_manifest,
    )
    for check in checks:
        check()
    return 0


if __name__ == "__main__":
    sys.exit(main())
