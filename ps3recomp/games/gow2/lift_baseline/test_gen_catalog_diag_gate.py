#!/usr/bin/env python3
"""Testes do ramo 2b de gen_catalog.py -- gates de diagnostico auto-declarados.

O QUE ESTE RAMO CORRIGE
-----------------------
`patch_24e3d0_null_product_gate.py` estava classificado FUNCIONAL/misc pelo
ramo 4 (o "tudo o resto"), portanto DENTRO do gate de promocao. Medido a
2026-08-03, por leitura pura dos tres lifts em disco:

  - o patch exige `src.count(NEEDLE) == 1` por chunk;
  - a agulha `vm_write16(ctx->gpr[9] + 0x6, ctx->gpr[0]);` aparece **38** vezes
    em `recomp_macos_v5promo`, **38** em `recomp_macos_v4ord` e **38** em
    `recomp_macos_v2` -- a PRODUCAO;
  - logo da `MISSING`/rc=2 identicamente nos tres. Nao e' divida de re-lift:
    e' um patch que nunca aplicou a esta geracao de lift, e o seu marcador
    `24E3D0-NULLPROD-GATE` nunca entrou no MANIFEST.tsv.

E o proprio ficheiro declara o que e': abre com "GATE DE DIAGNOSTICO (nao e' um
fix)", e o bloco que injecta esta' atras de `PS3_24E3D0_KEEP_TYPE_ON_NULL` --
sem a env var e' um no-op comportamental. A classificacao e' que estava errada,
nao o patch. O patch NAO e' corrigido nem apagado por esta mudanca.

A REGRA (nao um caso especial)
------------------------------
Ramo 2b, gemeo do ramo 2 ("Diagnostic only") mas na lingua em que o corpus esta
escrito: auto-declaracao literal "GATE DE DIAGNOSTICO (nao e' um fix)" E um
gate de ambiente `getenv("PS3_...")` no corpo. A conjuncao importa -- 95 dos
140 patches tem gate de ambiente sem serem diagnosticos, e a declaracao sozinha
nao pode comprar saida do gate.

RAIO MEDIDO: 4 patches, nomeados nos testes. Tres deles estao `APPLIED` na
etapa 4 de 2026-08-03 -- saem do gate por CLASSE, nao por falharem. Isso e' uma
perda de cobertura real e esta declarada aqui e no cabecalho do PATCH_CATALOG.

Run: .venv/bin/python3 games/gow2/lift_baseline/test_gen_catalog_diag_gate.py
Exit 0 = todos passaram.
"""
from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import gen_catalog  # noqa: E402

SIBLING_ROOT = HERE.parents[3]
PATCH_DIR = (SIBLING_ROOT / "gow2-recomp" / "recomp_mid_v2").resolve()
MANIFEST_PATH = HERE / "MANIFEST.tsv"

assert PATCH_DIR.is_dir(), f"PATCH_DIR nao existe: {PATCH_DIR}"

OS_QUATRO = [
    "patch_218364_pool_null_gate.py",
    "patch_24e3d0_null_product_gate.py",
    "patch_254610_empty_list_gate.py",
    "patch_2547f8_empty_list_gate.py",
]

# Contagens medidas ANTES desta mudanca (cabecalho do PATCH_CATALOG.tsv de
# 2026-08-02): 140 patches, 88 FUNCIONAL / 52 PROBE. Depois: 84 / 56.
TOTAL = 140
FUNCIONAL_DEPOIS = 84
PROBE_DEPOIS = 56


def _read(name: str) -> str:
    return (PATCH_DIR / name).read_text(encoding="utf-8", errors="replace")


def t_24e3d0_e_probe() -> None:
    """O caso que abriu a questao: sai de FUNCIONAL/misc para PROBE."""
    src = _read("patch_24e3d0_null_product_gate.py")
    classe, subcls, no_gate = gen_catalog.classify(
        "patch_24e3d0_null_product_gate.py", src
    )
    assert classe == "PROBE", f"esperado PROBE, obtido {classe}/{subcls}"
    assert no_gate is True, "um PROBE tem de ficar fora do gate (no_gate=1)"
    print("[PASS] patch_24e3d0_null_product_gate.py -> PROBE, fora do gate")


def t_a_frase_casa_exactamente_quatro_no_corpus_real() -> None:
    """Raio da regra, medido contra os 140 patches reais -- nao estimado."""
    frase = re.compile(r"GATE DE DIAGNOSTICO \(nao e' um fix\)")
    casam = sorted(
        p.name
        for p in PATCH_DIR.glob("patch_*.py")
        if frase.search(p.read_text(encoding="utf-8", errors="replace"))
    )
    assert casam == OS_QUATRO, f"raio inesperado: {casam}"
    print(f"[PASS] a auto-declaracao casa exactamente 4 ficheiros: {', '.join(casam)}")


def t_os_quatro_sao_todos_env_gated() -> None:
    """A conjuncao e' verdadeira nos 4: declaracao E mecanismo OFF por omissao."""
    env = re.compile(r'getenv\(\\?"PS3_')
    for nome in OS_QUATRO:
        src = _read(nome)
        assert env.search(src), f"{nome} nao injecta gate de ambiente"
        classe, _subcls, no_gate = gen_catalog.classify(nome, src)
        assert classe == "PROBE" and no_gate is True, f"{nome} -> {classe}"
    print("[PASS] os 4 sao PROBE e os 4 injectam gate de ambiente (getenv PS3_*)")


def t_declaracao_sem_env_gate_continua_funcional() -> None:
    """Mutacao: a declaracao SOZINHA nao compra saida do gate."""
    src = _read("patch_24e3d0_null_product_gate.py")
    sem_env = src.replace('getenv(\\"PS3_24E3D0_KEEP_TYPE_ON_NULL\\")', '1')
    assert 'getenv(\\"PS3_' not in sem_env, "a mutacao nao removeu o gate de ambiente"
    classe, _s, no_gate = gen_catalog.classify("patch_24e3d0_null_product_gate.py", sem_env)
    assert classe == "FUNCIONAL", f"sem env gate devia ser FUNCIONAL, obtido {classe}"
    assert no_gate is False
    print("[PASS] mutacao: declaracao sem gate de ambiente -> continua FUNCIONAL (no gate)")


def t_env_gate_sem_declaracao_continua_funcional() -> None:
    """Mutacao no outro sentido: gate de ambiente sozinho nao basta."""
    src = _read("patch_24e3d0_null_product_gate.py")
    sem_frase = src.replace("GATE DE DIAGNOSTICO (nao e' um fix)", "Correccao")
    classe, _s, no_gate = gen_catalog.classify("patch_24e3d0_null_product_gate.py", sem_frase)
    assert classe == "FUNCIONAL", f"sem a declaracao devia ser FUNCIONAL, obtido {classe}"
    assert no_gate is False
    print("[PASS] mutacao: gate de ambiente sem a auto-declaracao -> continua FUNCIONAL")


def t_um_patch_funcional_de_verdade_nao_muda() -> None:
    """Nao-regressao: um FUNCIONAL vizinho (mesmo sufixo _gate) nao sai do gate."""
    nome = "patch_263040_freelist_tag_guard.py"
    classe, _s, no_gate = gen_catalog.classify(nome, _read(nome))
    assert classe == "FUNCIONAL" and no_gate is False, f"{nome} -> {classe}"
    print(f"[PASS] nao-regressao: {nome} continua FUNCIONAL, dentro do gate")


def t_contagens_do_catalogo_completo() -> None:
    """Raio medido no catalogo inteiro: 140 = 84 FUNCIONAL + 56 PROBE."""
    rows = gen_catalog.build_catalog(PATCH_DIR, MANIFEST_PATH)
    assert len(rows) == TOTAL, f"esperado {TOTAL} linhas, obtido {len(rows)}"
    n_probe = sum(1 for r in rows if r["classe"] == "PROBE")
    n_func = sum(1 for r in rows if r["classe"] == "FUNCIONAL")
    assert (n_func, n_probe) == (FUNCIONAL_DEPOIS, PROBE_DEPOIS), (
        f"contagens inesperadas: {n_func} FUNCIONAL / {n_probe} PROBE "
        f"(esperado {FUNCIONAL_DEPOIS}/{PROBE_DEPOIS})"
    )
    por_nome = {r["patch"]: r for r in rows}
    for nome in OS_QUATRO:
        assert por_nome[nome]["classe"] == "PROBE", por_nome[nome]
        assert por_nome[nome]["no_gate"] == "1", por_nome[nome]
    print(f"[PASS] catalogo: {n_func} FUNCIONAL / {n_probe} PROBE de {len(rows)}")


def t_razao_declarada_na_linha() -> None:
    """A razao fica escrita na propria linha do TSV, nao so' numa nota."""
    rows = gen_catalog.build_catalog(PATCH_DIR, MANIFEST_PATH)
    row = {r["patch"]: r for r in rows}["patch_24e3d0_null_product_gate.py"]
    razao = row["razao"]
    assert "GATE DE DIAGNOSTICO" in razao, razao
    assert "OFF por omissao" in razao, razao
    print(f"[PASS] razao na linha: {razao}")


def t_medicao_no_cabecalho_do_tsv() -> None:
    """A medicao que motivou a reclassificacao fica RASTREAVEL no artefacto."""
    rows = gen_catalog.build_catalog(PATCH_DIR, MANIFEST_PATH)
    tsv = gen_catalog.render_tsv(rows, PATCH_DIR)
    cabecalho = "\n".join(l for l in tsv.splitlines() if l.startswith("#"))
    assert "RECLASSIFICACAO" in cabecalho, "sem bloco de reclassificacao no cabecalho"
    assert "38" in cabecalho, "a medicao (38 ocorrencias vs 1 exigida) nao esta escrita"
    assert "recomp_macos_v2" in cabecalho, "falta dizer que falha igual na PRODUCAO"
    for nome in OS_QUATRO:
        assert nome in cabecalho, f"{nome} nao esta nomeado no cabecalho"
    print("[PASS] cabecalho do PATCH_CATALOG.tsv nomeia os 4 e escreve a medicao (38 vs 1, igual na producao)")


def t_patch_sintetico_sem_nada_continua_funcional() -> None:
    """Fixture em tempfile -- nunca no directorio real (mesma regra do 04-01)."""
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "patch_zz_sintetico.py").write_text(
            '"""Corrige uma coisa."""\nimport sys\n', encoding="utf-8"
        )
        rows = gen_catalog.build_catalog(d, MANIFEST_PATH)
        assert len(rows) == 1
        assert rows[0]["classe"] == "FUNCIONAL", rows[0]
    print("[PASS] patch sintetico sem declaracao nem env gate -> FUNCIONAL")


def main() -> int:
    for check in (
        t_24e3d0_e_probe,
        t_a_frase_casa_exactamente_quatro_no_corpus_real,
        t_os_quatro_sao_todos_env_gated,
        t_declaracao_sem_env_gate_continua_funcional,
        t_env_gate_sem_declaracao_continua_funcional,
        t_um_patch_funcional_de_verdade_nao_muda,
        t_contagens_do_catalogo_completo,
        t_razao_declarada_na_linha,
        t_medicao_no_cabecalho_do_tsv,
        t_patch_sintetico_sem_nada_continua_funcional,
    ):
        check()
    return 0


if __name__ == "__main__":
    sys.exit(main())
