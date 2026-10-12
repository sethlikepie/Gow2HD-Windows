#!/usr/bin/env python3
"""upstream_delta.py -- inventario medido do delta upstream->nos (Fase 22 / UPST-01).

Porque existe: nenhuma das Fases 23-26 sabe que commit fazer cherry-pick sem um
ficheiro que tenha, do lado deles, o **sha do upstream** e, do nosso lado, a
**ancora `ficheiro:linha`**. Sem as duas colunas cada fase refazia a comparacao
por conta propria -- e uma leitura contra `upstream/master` (congelado a
2026-07-16) daria uma resposta desactualizada por semanas.

O ficheiro nao e' um documento, e' um artefacto **medido e defendido**:

  measure   le o git real (`for-each-ref` + `rev-list`) e reescreve SO' as
            linhas de cabecalho `#`. O cabecalho nunca se escreve a mao.
  validate  recusa linha sem ancora, ancora deslocada, decisao fora do
            vocabulario e cabecalho cuja base de comparacao seja o master.
  rows      cospe as linhas de dados sem cabecalho -- e' o que as Fases 23-26
            consomem para saber o sha a fazer cherry-pick.

Ambito: o inventario e' do motor (o fork inteiro), nao do port GoW2, por isso
vive em `docs/UPSTREAM_DELTA.tsv` e nao em `games/gow2/lift_baseline/`.

A raiz do repo sai de `__file__`, nunca do cwd. Todos os ficheiros sao abertos
com `encoding='utf-8'` (a decisao `já-temos` tem acento e a CI corre tambem em
Windows, onde o default do Python nao e' UTF-8).

Uso:
  .venv/bin/python3 tools/upstream_delta.py measure  [--tsv <p>] [--fetch]
  .venv/bin/python3 tools/upstream_delta.py validate [--tsv <p>] [--min-linhas N]
  .venv/bin/python3 tools/upstream_delta.py rows     [--tsv <p>] [--decisao D] [--area A]
"""
from __future__ import annotations

import argparse
import datetime
import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TSV = os.path.join(REPO_ROOT, "docs", "UPSTREAM_DELTA.tsv")

# Ordem exacta das colunas. As Fases 23-26 consomem por posicao (`cut -f2` = sha,
# `cut -f5` = ancora), portanto reordenar isto parte os consumidores.
COLUNAS = [
    "area", "sha_upstream", "ramo_upstream", "caminho_upstream", "ancora_nossa",
    "ancora_prova", "decisao", "fase_destino", "evidencia", "razao",
]
# Os cinco obrigatorios do criterio 1 do ROADMAP: uma linha sem ancora e' uma
# decisao sobre codigo que ninguem localizou.
OBRIGATORIAS = ["area", "sha_upstream", "ancora_nossa", "decisao", "razao"]

DECISOES = ["adoptar", "rejeitar", "já-temos", "contribuir-de-volta"]
EVIDENCIAS = ["medido", "nao-exercitado"]
AREAS = ["SPUI", "GCMF", "LV2S", "SPRS", "LIFT", "TOOLS", "ORACLE"]

CHAVES_CABECALHO = [
    "medido_em", "fetch", "ramo_upstream", "ramo_head_sha", "ramo_head_data",
    "delta_commits", "comando", "nosso_head", "nosso_delta_commits",
    "master_congelado", "nivel_evidencia",
]

ANCORA_RE = re.compile(r"^[^:]+:\d+(-\d+)?$")
CHAVE_RE = re.compile(r"^#\s*([a-z_]+)=(.*)$")
MIN_LINHAS_DEFAULT = 4


# --------------------------------------------------------------------------- git
def git(*args: str, check: bool = True) -> str:
    r = subprocess.run(["git", *args], cwd=REPO_ROOT, capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    if check and r.returncode != 0:
        raise SystemExit(f"[delta] FAIL GIT cmd='git {' '.join(args)}' "
                         f"rc={r.returncode} {r.stderr.strip()}")
    return r.stdout.strip()


def escolher_ramo_vivo() -> tuple[str, str, str]:
    """O ramo vivo sai da DATA DE COMMIT, nunca de um nome fixo no codigo.

    `refs/remotes/upstream` traz tambem o simbolico HEAD (refname:short =
    `upstream`) e o `upstream/master`, ambos congelados a 2026-07-16 -- usar
    qualquer um deles como base esconde os commits que o ramo vivo ja tem.
    """
    saida = git("for-each-ref", "--sort=-committerdate",
                "--format=%(committerdate:short) %(objectname:short) %(refname:short)",
                "refs/remotes/upstream")
    for linha in saida.splitlines():
        partes = linha.split(None, 2)
        if len(partes) != 3:
            continue
        data, sha, ref = partes
        if ref == "upstream" or ref.endswith("/HEAD") or ref.endswith("/master"):
            continue
        return data, sha, ref
    raise SystemExit("[delta] FAIL RAMO_VIVO nenhum ramo vivo em refs/remotes/upstream "
                     "(so' master/HEAD?) -- correr `git fetch upstream --prune`")


# ------------------------------------------------------------------------- ficheiro
def ler_tsv(caminho: str) -> tuple[dict[str, str], list[str], list[tuple[int, list[str]]]]:
    """Devolve (chaves do cabecalho, linhas nao-`#` cruas, linhas de dados numeradas)."""
    if not os.path.exists(caminho):
        raise SystemExit(f"[delta] FAIL FICHEIRO nao existe: {caminho}")
    with open(caminho, "r", encoding="utf-8") as fh:
        linhas = fh.read().splitlines()

    chaves: dict[str, str] = {}
    nao_cabecalho: list[str] = []
    dados: list[tuple[int, list[str]]] = []
    vista_coluna = False
    for n, linha in enumerate(linhas, start=1):
        if linha.startswith("#"):
            m = CHAVE_RE.match(linha)
            if m:
                chaves[m.group(1)] = m.group(2).strip()
            continue
        if not linha.strip():
            continue
        nao_cabecalho.append(linha)
        if not vista_coluna:
            vista_coluna = True          # a primeira nao-`#` e' a linha de colunas
            continue
        dados.append((n, linha.split("\t")))
    return chaves, nao_cabecalho, dados


# -------------------------------------------------------------------------- measure
def cmd_measure(args: argparse.Namespace) -> int:
    if args.fetch:
        git("fetch", "upstream", "--prune")

    data_ramo, sha_ramo, ramo = escolher_ramo_vivo()
    cmd_delta = f"git rev-list --count HEAD..{ramo}"
    delta = git("rev-list", "--count", f"HEAD..{ramo}")
    nosso_delta = git("rev-list", "--count", f"{ramo}..HEAD")
    nosso_head = git("rev-parse", "--short", "HEAD")
    master_sha = git("rev-parse", "--short", "upstream/master")
    master_data = git("log", "-1", "--format=%ad", "--date=short", "upstream/master")
    master_esconde = git("rev-list", "--count", "HEAD..upstream/master")

    hoje = datetime.date.today().isoformat()
    cabecalho = [
        "# UPSTREAM_DELTA.tsv -- inventario do delta upstream->nos (Fase 22 / UPST-01)",
        "# NUNCA editar as linhas `#` a mao: sao medidas. Regenerar com `measure`.",
        "# regenerar cabecalho: .venv/bin/python3 tools/upstream_delta.py measure --tsv docs/UPSTREAM_DELTA.tsv",
        "# validar (gate de ancoras/vocabulario): .venv/bin/python3 tools/upstream_delta.py validate --tsv docs/UPSTREAM_DELTA.tsv",
        f"# medido_em={hoje}",
        f"# fetch={'sim' if args.fetch else 'nao'}",
        f"# ramo_upstream={ramo}",
        f"# ramo_head_sha={sha_ramo}",
        f"# ramo_head_data={data_ramo}",
        f"# delta_commits={delta}",
        f"# comando={cmd_delta}",
        f"# nosso_head={nosso_head}",
        f"# nosso_delta_commits={nosso_delta}",
        f"# master_congelado={master_sha} ({master_data}) -- NAO e' a base de comparacao: "
        f"esconde {master_esconde} commits que o ramo vivo ja tem",
        "# nivel_evidencia=medido",
        "#",
        "# HONESTIDADE (ler antes de consumir este ficheiro):",
        "# - `adoptar` significa \"entra pelo portao da Fase 23+\", NAO \"ja foi adoptado\".",
        "#   O estado de adopcao e' das fases seguintes; este ficheiro nao o simula.",
        "# - Quando um bloco for adoptado a ancora nossa MUDA (o codigo muda) e a linha tem",
        "#   de ser RE-ANCORADA -- e' exactamente para isso que existe o gate de deriva",
        "#   (ANCORA_PROVA/ANCORA_DESLOCADA). Caso ja previsto: a Fase 23 altera",
        "#   runtime/syscalls/lv2_register.c:825-826 e essa linha ficara vermelha ate ser",
        "#   re-ancorada (`validate --fix-anchors` reescreve o numero da linha).",
        "# - `evidencia` e' por linha: `medido` = sha resolvido e ancora aberta e lida;",
        "#   `nao-exercitado` = a afirmacao nao foi corrida por nos. Nenhum aceite in-boot",
        "#   e' reclamado por este ficheiro.",
    ]

    if os.path.exists(args.tsv):
        _, nao_cabecalho, _ = ler_tsv(args.tsv)
    else:
        nao_cabecalho = []
    if not nao_cabecalho:
        nao_cabecalho = ["\t".join(COLUNAS)]

    os.makedirs(os.path.dirname(os.path.abspath(args.tsv)), exist_ok=True)
    with open(args.tsv, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(cabecalho + nao_cabecalho) + "\n")

    print(f"[delta] medido ramo={ramo} sha={sha_ramo} data={data_ramo} "
          f"delta_commits={delta} nosso_head={nosso_head} linhas_dados={len(nao_cabecalho) - 1}")
    return 0


# ------------------------------------------------------------------------- validate
class Validador:
    def __init__(self) -> None:
        self.falhas: list[str] = []

    def falha(self, check: str, linha: int | None, detalhe: str) -> None:
        alvo = f"linha={linha} " if linha is not None else ""
        self.falhas.append(f"[delta] FAIL {check} {alvo}{detalhe}")


def _intervalo(ancora: str) -> tuple[str, int, int]:
    caminho, _, span = ancora.rpartition(":")
    if "-" in span:
        ini, fim = span.split("-", 1)
        return caminho, int(ini), int(fim)
    return caminho, int(span), int(span)


def validar(caminho_tsv: str, min_linhas: int,
            raiz: str | None = None) -> tuple[Validador, dict, list, dict[int, str]]:
    """Valida o TSV. `raiz` resolve as ancoras (default: a raiz do repo).

    Devolve tambem `reparacoes`: {numero_da_linha_do_tsv: ancora_corrigida} para
    as ancoras que derivaram e cuja prova foi reencontrada noutro sitio -- e' o
    que `--fix-anchors` aplica. Sem a flag nada disto e' escrito.
    """
    raiz = raiz or REPO_ROOT
    v = Validador()
    reparacoes: dict[int, str] = {}
    chaves, nao_cabecalho, dados = ler_tsv(caminho_tsv)

    for chave in CHAVES_CABECALHO:
        if chave not in chaves or not chaves[chave]:
            v.falha("CABECALHO", None, f"chave ausente ou vazia: {chave}")

    ramo = chaves.get("ramo_upstream", "")
    if "master" in ramo:
        v.falha("RAMO_NAO_MASTER", None,
                f"ramo_upstream={ramo!r} -- upstream/master esta congelado e esconde "
                f"commits; a base tem de ser o ramo vivo")

    if nao_cabecalho:
        colunas = nao_cabecalho[0].split("\t")
        if colunas != COLUNAS:
            v.falha("COLUNAS", None, f"esperado={COLUNAS} obtido={colunas}")
    else:
        v.falha("COLUNAS", None, "ficheiro sem linha de colunas")

    if len(dados) < min_linhas:
        v.falha("MINIMO_LINHAS", None, f"linhas de dados={len(dados)} minimo={min_linhas}")

    for n, campos in dados:
        if len(campos) != len(COLUNAS):
            v.falha("CAMPOS", n, f"campos={len(campos)} esperado={len(COLUNAS)}")
            continue
        reg = dict(zip(COLUNAS, campos))
        for col in OBRIGATORIAS:
            if not reg[col].strip():
                v.falha("CAMPOS", n, f"coluna obrigatoria vazia: {col}")
        if reg["decisao"] not in DECISOES:
            v.falha("VOCABULARIO", n, f"decisao={reg['decisao']!r} fora de {DECISOES}")
        if reg["evidencia"] not in EVIDENCIAS:
            v.falha("VOCABULARIO", n, f"evidencia={reg['evidencia']!r} fora de {EVIDENCIAS}")
        if reg["area"] not in AREAS:
            v.falha("VOCABULARIO", n, f"area={reg['area']!r} fora de {AREAS}")

        ancora = reg["ancora_nossa"].strip()
        if not ANCORA_RE.match(ancora):
            v.falha("ANCORA_FORMATO", n,
                    f"ancora={ancora!r} nao casa `ficheiro:linha` ou `ficheiro:linha-linha`")
            continue
        rel, ini, fim = _intervalo(ancora)
        if ini < 1 or fim < ini:
            v.falha("ANCORA_FORMATO", n, f"intervalo invalido em {ancora!r}")
            continue
        abs_path = os.path.join(raiz, rel)
        if not os.path.isfile(abs_path):
            v.falha("ANCORA_FICHEIRO", n, f"ficheiro inexistente: {rel}")
            continue
        with open(abs_path, "r", encoding="utf-8", errors="replace") as fh:
            linhas_alvo = fh.read().splitlines()
        if fim > len(linhas_alvo):
            v.falha("ANCORA_FICHEIRO", n,
                    f"{rel} tem {len(linhas_alvo)} linhas, ancora pede ate {fim}")
            continue

        # --- gate de deriva -------------------------------------------------
        # Uma ancora e' um numero de linha; numeros mentem assim que alguem
        # edita o ficheiro acima. A prova (substring literal) e' o que torna a
        # ancora verificavel: se ela nao estiver DENTRO do intervalo, a linha
        # esta a apontar para codigo que ja' nao esta la'.
        prova = reg["ancora_prova"]
        if not prova.strip():
            v.falha("CAMPOS", n, "coluna ancora_prova vazia: a ancora seria so' um numero")
            continue
        if not prova.isascii():
            v.falha("PROVA_ASCII", n,
                    f"ancora_prova={prova!r} tem caracteres nao-ASCII")
            continue
        if any(prova in linhas_alvo[i] for i in range(ini - 1, fim)):
            continue
        # Fora do intervalo: procurar no ficheiro inteiro para dizer a linha
        # NOVA -- uma falha accionavel numa edicao, nao um "esta errado".
        achadas = [i + 1 for i, ltxt in enumerate(linhas_alvo) if prova in ltxt]
        if achadas:
            nova = achadas[0]
            v.falha("ANCORA_DESLOCADA", n,
                    f"{rel}: prova {prova!r} nao esta em {ini}-{fim}, "
                    f"linha_actual={nova} (corrigir com --fix-anchors)")
            reparacoes[n] = f"{rel}:{nova}"
        else:
            v.falha("ANCORA_PROVA", n,
                    f"{rel}: prova {prova!r} nao existe em lado nenhum do ficheiro "
                    f"-- o codigo ancorado desapareceu, re-ancorar a mao")

    return v, chaves, dados, reparacoes


def aplicar_reparacoes(caminho_tsv: str, reparacoes: dict[int, str]) -> None:
    """Reescreve SO' o campo `ancora_nossa` das linhas indicadas."""
    with open(caminho_tsv, "r", encoding="utf-8") as fh:
        linhas = fh.read().splitlines()
    for n, nova in reparacoes.items():
        campos = linhas[n - 1].split("\t")
        if len(campos) == len(COLUNAS):
            campos[COLUNAS.index("ancora_nossa")] = nova
            linhas[n - 1] = "\t".join(campos)
    with open(caminho_tsv, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(linhas) + "\n")


def cmd_validate(args: argparse.Namespace) -> int:
    raiz = getattr(args, "raiz", None)
    v, chaves, dados, reparacoes = validar(args.tsv, args.min_linhas, raiz)
    if reparacoes and getattr(args, "fix_anchors", False):
        aplicar_reparacoes(args.tsv, reparacoes)
        print(f"[delta] nota re-ancoradas {len(reparacoes)} linha(s): "
              + ", ".join(f"linha={n}->{a}" for n, a in sorted(reparacoes.items())))
        v, chaves, dados, _ = validar(args.tsv, args.min_linhas, raiz)
    if v.falhas:
        for f in v.falhas:
            print(f)
        return 1
    contagem = {d: 0 for d in DECISOES}
    for _, campos in dados:
        contagem[dict(zip(COLUNAS, campos))["decisao"]] += 1
    print(f"[delta] resumo linhas={len(dados)} "
          f"adoptar={contagem['adoptar']} rejeitar={contagem['rejeitar']} "
          f"ja-temos={contagem['já-temos']} "
          f"contribuir-de-volta={contagem['contribuir-de-volta']} "
          f"delta_commits={chaves.get('delta_commits', '?')} "
          f"ramo={chaves.get('ramo_upstream', '?')}")
    return 0


# ----------------------------------------------------------------------------- rows
def cmd_rows(args: argparse.Namespace) -> int:
    _, _, dados = ler_tsv(args.tsv)
    for _, campos in dados:
        reg = dict(zip(COLUNAS, campos)) if len(campos) == len(COLUNAS) else {}
        if args.decisao and reg.get("decisao") != args.decisao:
            continue
        if args.area and reg.get("area") != args.area:
            continue
        print("\t".join(campos))
    return 0


# ----------------------------------------------------------------------------- main
def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    m = sub.add_parser("measure", help="re-medir o delta e reescrever o cabecalho")
    m.add_argument("--tsv", default=DEFAULT_TSV)
    m.add_argument("--fetch", action="store_true",
                   help="correr `git fetch upstream --prune` antes de medir")
    m.set_defaults(func=cmd_measure)

    v = sub.add_parser("validate", help="gate: ancoras, vocabulario e cabecalho")
    v.add_argument("--tsv", default=DEFAULT_TSV)
    v.add_argument("--min-linhas", type=int, default=MIN_LINHAS_DEFAULT)
    v.add_argument("--raiz", default=None,
                   help="raiz para resolver as ancoras (default: raiz do repo; "
                        "so' os self-tests a usam, para correrem hermeticos)")
    v.add_argument("--fix-anchors", action="store_true",
                   help="reescreve os numeros de linha das ancoras deslocadas. "
                        "SEM esta flag, `validate` nunca escreve em ficheiro nenhum.")
    v.set_defaults(func=cmd_validate)

    r = sub.add_parser("rows", help="linhas de dados sem cabecalho (input das Fases 23-26)")
    r.add_argument("--tsv", default=DEFAULT_TSV)
    r.add_argument("--decisao")
    r.add_argument("--area")
    r.set_defaults(func=cmd_rows)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
