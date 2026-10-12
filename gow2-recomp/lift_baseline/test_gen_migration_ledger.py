#!/usr/bin/env python3
"""Testes de gen_migration_ledger.py (Fase 16, Plano 16-01, Task 2 / LEDG-01).

O ledger PATCH_MIGRATION.tsv so' vale se a coluna `classe` for DERIVADA do
PATCH_CATALOG.tsv (nunca copiada a mao) e se o universo coberto for o corpus
que o build de facto aplica (../gow2-recomp/recomp_mid_v2), nao o espelho
subtree atrasado games/gow2/recomp_mid_v2.

Cobertura destes testes:
  1. mapa de classes catalogo->ledger contra 4 patches REAIS, um por ramo
     (PROBE, OBSOLETE, OPD, CORRECTNESS) + dominio fechado
  2. cobertura: uma linha por patch_*.py do corpus de build, sem hardcode do
     numero (o numero e' medido no disco em cada corrida)
  3. defaults seguros: PROBE -> alvo=keep-probe sem prioridade; orfaos ->
     alvo=delete + estado=todo
  4. heuristica de EA contra ancoras verificadas nos proprios scripts
  5. --merge nao apaga trabalho humano (prioridade=P0 sobrevive a um regen)
  6. cabecalho congela as contagens MEDIDAS e o TSV tem as 7 colunas de LEDG-01
  7. build_rows() e' determinista (duas corridas, mesmo resultado)

Run directly:
  .venv/bin/python3 games/gow2/lift_baseline/test_gen_migration_ledger.py
Exit code 0 = todas as verificacoes passaram.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import gen_migration_ledger as gml  # noqa: E402

# HERE = .../ps3recomp/games/gow2/lift_baseline
# HERE.parents[2] = .../ps3recomp   HERE.parents[3] = .../PESSOAL
MONOREPO_ROOT = HERE.parents[2]
SIBLING_ROOT = HERE.parents[3]
BUILD_DIR = (SIBLING_ROOT / "gow2-recomp" / "recomp_mid_v2").resolve()
MONO_DIR = (MONOREPO_ROOT / "games" / "gow2" / "recomp_mid_v2").resolve()
CATALOG = HERE / "PATCH_CATALOG.tsv"

print(f"[info] BUILD_DIR resolvido: {BUILD_DIR}")
print(f"[info] MONO_DIR resolvido: {MONO_DIR}")
assert BUILD_DIR.is_dir(), f"BUILD_DIR nao existe: {BUILD_DIR}"
assert MONO_DIR.is_dir(), f"MONO_DIR nao existe: {MONO_DIR}"
assert CATALOG.is_file(), f"PATCH_CATALOG.tsv nao existe: {CATALOG}"

CLASSES_VALIDAS = {"CORRECTNESS", "PROBE", "OPD", "BUILD", "OBSOLETE"}


def _rows() -> list[dict]:
    catalog = gml.load_catalog(CATALOG)
    return gml.build_rows(catalog, BUILD_DIR, MONO_DIR)


def check_mapa_de_classes_contra_patches_reais() -> None:
    """Teste 1: os 4 ramos do mapa, medidos contra patches reais do corpus."""
    catalog = gml.load_catalog(CATALOG)
    esperado = {
        # PROBE/diagnostico -> PROBE
        "patch_cc9d0_live_yield.py": "PROBE",
        # PROBE/orfao-sem-fonte -> OBSOLETE
        "patch_ce03c_pre_play_stop.py": "OBSOLETE",
        # FUNCIONAL/opd -> OPD
        "patch_32e200_opd.py": "OPD",
        # FUNCIONAL/<outra> -> CORRECTNESS
        "patch_fallthrough_2550c8.py": "CORRECTNESS",
    }
    for name, classe_ledger in esperado.items():
        assert name in catalog, f"{name} ausente do catalogo (regenerar Task 1?)"
        linha = catalog[name]
        got = gml.map_classe(linha["classe"], linha["subclasse"])
        assert got == classe_ledger, (
            f"{name}: catalogo={linha['classe']}/{linha['subclasse']} "
            f"-> esperado {classe_ledger}, obtido {got}"
        )

    # Dominio fechado: nenhum patch do catalogo cai fora do vocabulario LEDG-01.
    for name, linha in catalog.items():
        got = gml.map_classe(linha["classe"], linha["subclasse"])
        assert got in CLASSES_VALIDAS, f"{name}: classe fora do dominio: {got}"
    print(
        "[PASS] mapa de classes: os 4 ramos casam contra patches reais "
        "(cc9d0->PROBE, ce03c_pre_play_stop->OBSOLETE, 32e200_opd->OPD, "
        f"fallthrough_2550c8->CORRECTNESS); {len(catalog)} linhas do catalogo "
        "mapeiam sempre dentro de {CORRECTNESS,PROBE,OPD,BUILD,OBSOLETE}"
    )


def check_cobertura_uma_linha_por_patch_do_build() -> None:
    """Teste 2: uma linha por patch_*.py do corpus de build, numero medido."""
    rows = _rows()
    nomes_disco = {p.name for p in BUILD_DIR.glob("patch_*.py")}
    nomes_rows = {r["patch"] for r in rows}
    assert len(rows) == len(nomes_disco), (
        f"esperado {len(nomes_disco)} linhas, obtido {len(rows)}"
    )
    assert nomes_rows == nomes_disco, "cobertura incompleta ou divergente do disco"
    print(
        f"[PASS] cobertura: build_rows() produz {len(rows)} linhas, igual a "
        f"len(list(BUILD_DIR.glob('patch_*.py'))) == {len(nomes_disco)} "
        "(numero medido no disco, nunca hardcoded)"
    )


def check_defaults_seguros() -> None:
    """Teste 3: PROBE sem prioridade + keep-probe; orfaos delete/todo."""
    rows = _rows()
    n_probe = n_obsolete = 0
    for r in rows:
        assert r["estado"] in ("todo", "migrated", "wont"), r
        if r["classe"] == "PROBE":
            n_probe += 1
            assert r["alvo"] == "keep-probe", r
            assert r["prioridade"] == "", f"PROBE nao pode ter prioridade: {r}"
        elif r["classe"] == "OBSOLETE":
            n_obsolete += 1
            assert r["alvo"] == "delete", r
            assert r["estado"] == "todo", r
            assert r["prioridade"] == "", f"OBSOLETE nao pode ter prioridade: {r}"
        else:
            # Colunas de decisao humana nascem vazias -- 16-02 e' que as preenche.
            assert r["alvo"] == "", r
            assert r["prioridade"] == "", r
            assert r["aceite"] == "", r

    orfaos = {
        name
        for name, linha in gml.load_catalog(CATALOG).items()
        if linha["subclasse"] == "orfao-sem-fonte"
    }
    obsoletos = {r["patch"] for r in rows if r["classe"] == "OBSOLETE"}
    assert orfaos == obsoletos, f"orfaos {orfaos} != OBSOLETE {obsoletos}"
    print(
        f"[PASS] defaults seguros: {n_probe} PROBE com alvo=keep-probe e sem "
        f"prioridade, {n_obsolete} OBSOLETE com alvo=delete/estado=todo "
        "(exactamente os orfao-sem-fonte do catalogo), restantes com as 4 "
        "colunas humanas vazias"
    )


def check_heuristica_de_ea() -> None:
    """Teste 4: EA derivada, contra ancoras verificadas nos proprios scripts."""
    rows = {r["patch"]: r for r in _rows()}
    ancoras = {
        "patch_fallthrough_2550c8.py": "0x002550C8",
        "patch_32e200_opd.py": "0x0032E200",
        "patch_host_res_inflate.py": "0x001E7B50",
        "patch_ce03c_introseq_block.py": "0x000CE03C",
        "patch_14b1f0_opd.py": "0x0014B1F0",
    }
    for name, ea in ancoras.items():
        assert name in rows, f"{name} ausente do ledger"
        assert rows[name]["ea"] == ea, f"{name}: esperado {ea}, obtido {rows[name]['ea']}"

    padrao = re.compile(r"^(-|0x[0-9A-F]{8})$")
    for name, r in rows.items():
        assert padrao.match(r["ea"]), f"{name}: ea fora do formato: {r['ea']!r}"
    com_ea = sum(1 for r in rows.values() if r["ea"] != "-")

    # `ea` so' aceita EA CONFIRMADA pelo corpo do script. Quando o nome sugere
    # um endereco que o corpo nunca menciona, isso vai para `ea_pista_nome`
    # como pista NAO confirmada -- inventar a EA na coluna `ea` seria forjar
    # um numero (regra 4 do CLAUDE.md).
    r = rows["patch_254610_empty_list_gate.py"]
    assert r["ea"] == "-", r
    assert r["ea_pista_nome"] == "0x00254610", r
    # Onde a EA esta' confirmada, a pista nao se repete (ruido zero).
    assert rows["patch_fallthrough_2550c8.py"]["ea_pista_nome"] == "", rows[
        "patch_fallthrough_2550c8.py"
    ]
    padrao_pista = re.compile(r"^(|0x[0-9A-F]{8})$")
    for name, r in rows.items():
        assert padrao_pista.match(r["ea_pista_nome"]), (
            f"{name}: ea_pista_nome fora do formato: {r['ea_pista_nome']!r}"
        )
        if r["ea"] != "-":
            assert r["ea_pista_nome"] == "", f"{name}: pista redundante: {r}"
    com_pista = sum(1 for r in rows.values() if r["ea_pista_nome"])
    print(
        f"[PASS] heuristica de EA: {len(ancoras)} ancoras verificadas batem "
        f"(2550C8, 32E200, 1E7B50, CE03C, 14B1F0); {com_ea}/{len(rows)} linhas "
        "com EA confirmada pelo corpo, todas no formato 0xXXXXXXXX ou '-'; "
        f"{com_pista} linhas sem EA confirmada trazem pista do nome "
        "(nao confirmada, declarada como tal)"
    )


def check_merge_preserva_trabalho_humano() -> None:
    """Teste 5: --merge nao apaga prioridade=P0 (nem alvo/aceite/estado)."""
    catalog = gml.load_catalog(CATALOG)
    rows = gml.build_rows(catalog, BUILD_DIR, MONO_DIR)
    alvo_editado = "patch_fallthrough_2550c8.py"

    # Simula o ledger DEPOIS da 16-02: um humano editou as 4 colunas.
    editado = []
    for r in rows:
        r = dict(r)
        if r["patch"] == alvo_editado:
            r["alvo"] = "midasm"
            r["prioridade"] = "P0"
            r["aceite"] = "grep:func_00255178"
            r["estado"] = "migrated"
        editado.append(r)

    texto = gml.render_tsv(editado, gml.medir_contagens(catalog, BUILD_DIR, MONO_DIR))
    anterior = gml.parse_ledger(texto)
    assert anterior[alvo_editado]["prioridade"] == "P0", anterior[alvo_editado]

    regen = {
        r["patch"]: r
        for r in gml.build_rows(catalog, BUILD_DIR, MONO_DIR, anterior=anterior)
    }
    r = regen[alvo_editado]
    assert r["alvo"] == "midasm", r
    assert r["prioridade"] == "P0", r
    assert r["aceite"] == "grep:func_00255178", r
    assert r["estado"] == "migrated", r
    # As colunas derivadas continuam derivadas (o merge nao as congela).
    assert r["classe"] == "CORRECTNESS", r
    assert r["ea"] == "0x002550C8", r

    # Um patch nao editado nao ganha lixo do merge.
    outro = regen["patch_32e200_opd.py"]
    assert outro["prioridade"] == "", outro
    print(
        "[PASS] merge: apos render_tsv->parse_ledger->build_rows(anterior=...), "
        f"{alvo_editado} mantem alvo=midasm/prioridade=P0/aceite=grep:.../"
        "estado=migrated, mas classe e ea continuam derivadas; linhas nao "
        "editadas continuam com as colunas humanas vazias"
    )


def check_cabecalho_e_colunas() -> None:
    """Teste 6: cabecalho congela contagens medidas; as 7 colunas de LEDG-01."""
    catalog = gml.load_catalog(CATALOG)
    rows = gml.build_rows(catalog, BUILD_DIR, MONO_DIR)
    contagens = gml.medir_contagens(catalog, BUILD_DIR, MONO_DIR)
    texto = gml.render_tsv(rows, contagens)

    n_build = len(list(BUILD_DIR.glob("patch_*.py")))
    n_mono = len(list(MONO_DIR.glob("patch_*.py")))
    n_cat = len(catalog)
    assert contagens["n_build"] == n_build, contagens
    assert contagens["n_mono"] == n_mono, contagens
    assert contagens["n_cat"] == n_cat, contagens

    linhas_header = [l for l in texto.splitlines() if l.startswith("#")]
    blob = "\n".join(linhas_header)
    assert "baseline contagens" in blob, blob
    for numero in (n_build, n_mono, n_cat):
        assert str(numero) in blob, f"contagem {numero} ausente do cabecalho"
    assert str(n_build - n_mono) in blob, "divergencia build-monorepo ausente"

    # Linha de cabecalho de colunas: nao comentada, comeca por `patch\t`.
    col_line = next(l for l in texto.splitlines() if l.startswith("patch\t"))
    cols = col_line.split("\t")
    for obrigatoria in ("patch", "classe", "alvo", "ea", "prioridade", "aceite", "estado"):
        assert obrigatoria in cols, f"coluna obrigatoria ausente: {obrigatoria}"

    # `grep -cE '^patch_'` tem de contar exactamente as linhas de dados.
    dados = [l for l in texto.splitlines() if l.startswith("patch_")]
    assert len(dados) == len(rows), (len(dados), len(rows))
    for l in dados:
        assert len(l.split("\t")) == len(cols), f"largura errada: {l[:60]}"
    print(
        f"[PASS] cabecalho e colunas: as 7 colunas de LEDG-01 presentes "
        f"({len(cols)} no total); cabecalho congela n_mono={n_mono} "
        f"n_build={n_build} n_cat={n_cat} e a divergencia {n_build - n_mono}; "
        f"{len(dados)} linhas de dados, todas com {len(cols)} campos"
    )


def check_determinismo() -> None:
    """Teste 7: build_rows() duas vezes com os mesmos argumentos = mesmo TSV."""
    catalog = gml.load_catalog(CATALOG)
    contagens = gml.medir_contagens(catalog, BUILD_DIR, MONO_DIR)
    a = gml.render_tsv(gml.build_rows(catalog, BUILD_DIR, MONO_DIR), contagens)
    b = gml.render_tsv(gml.build_rows(catalog, BUILD_DIR, MONO_DIR), contagens)
    assert a == b, "render_tsv nao e' determinista"
    print("[PASS] determinismo: duas geracoes seguidas produzem TSV byte-a-byte igual")


def check_contagem_de_classificacao_e_derivada() -> None:
    """Teste 8: N_classificados/N_P0 e pilotos saem das colunas humanas.

    O cabecalho tem de recontar a cada regen. Se estes numeros fossem um
    comentario escrito a mao, o primeiro regen tornava-os mentira -- e' o
    mesmo motivo pelo qual `classe` e `ea` sao derivadas.
    """
    # Regra de "classificada": PROBE pode ter prioridade vazia; os outros nao.
    assert not gml.linha_classificada(
        {"classe": "CORRECTNESS", "alvo": "midasm", "aceite": "grep:x", "estado": "todo"}
    ), "CORRECTNESS sem prioridade nao pode contar como classificada"
    assert gml.linha_classificada(
        {"classe": "PROBE", "alvo": "keep-probe", "aceite": "grep:x", "estado": "todo"}
    ), "PROBE com prioridade vazia conta"
    assert not gml.linha_classificada(
        {"classe": "OPD", "alvo": "weak", "aceite": "", "estado": "todo",
         "prioridade": "P0"}
    ), "sem aceite nao ha' linha classificada"

    sintetico = [
        {"patch": "a.py", "classe": "CORRECTNESS", "alvo": "midasm",
         "prioridade": "P0", "aceite": "grep:x", "estado": "todo"},
        {"patch": "b.py", "classe": "CORRECTNESS", "alvo": "switch_table",
         "prioridade": "P0", "aceite": "grep:y", "estado": "todo"},
        {"patch": "c.py", "classe": "OPD", "alvo": "weak",
         "prioridade": "P1", "aceite": "contract:z", "estado": "todo"},
        {"patch": "d.py", "classe": "CORRECTNESS", "alvo": "", "prioridade": "",
         "aceite": "", "estado": "todo"},
    ]
    got = gml.contar_classificacao(sintetico)
    assert got["n_classificados"] == 3, got
    assert got["por_prioridade"] == {"P0": 2, "P1": 1}, got
    assert got["piloto_midasm"] == ["a.py"], got
    assert got["piloto_switch_table"] == ["b.py"], got

    # E o cabecalho renderizado mostra mesmo esses numeros.
    catalog = gml.load_catalog(CATALOG)
    rows = gml.build_rows(catalog, BUILD_DIR, MONO_DIR)
    texto = gml.render_tsv(rows, gml.medir_contagens(catalog, BUILD_DIR, MONO_DIR))
    blob = "\n".join(l for l in texto.splitlines() if l.startswith("#"))
    assert "N_classificados=" in blob, blob
    assert "piloto sugerido Fase 17" in blob, blob
    assert "piloto sugerido Fase 18" in blob, blob
    print(
        "[PASS] contagem de classificacao derivada: linha_classificada() aceita "
        "PROBE sem prioridade e recusa CORRECTNESS sem ela; contar_classificacao() "
        "da 3 classificados / P0=2 P1=1 e nomeia os pilotos midasm e switch_table; "
        "render_tsv() escreve N_classificados e os dois pilotos no cabecalho"
    )


def main() -> int:
    checks = (
        check_mapa_de_classes_contra_patches_reais,
        check_cobertura_uma_linha_por_patch_do_build,
        check_defaults_seguros,
        check_heuristica_de_ea,
        check_merge_preserva_trabalho_humano,
        check_cabecalho_e_colunas,
        check_determinismo,
        check_contagem_de_classificacao_e_derivada,
    )
    for check in checks:
        check()
    return 0


if __name__ == "__main__":
    sys.exit(main())
