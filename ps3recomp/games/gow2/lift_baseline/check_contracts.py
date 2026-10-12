#!/usr/bin/env python3
"""check_contracts.py -- motor de verificacao de CONTRACTS.tsv (D-4.2, 04-04-PLAN.md).

Porque existe
--------------
Um patch que se auto-declara bem-sucedido nao e' prova. `CONTRACTS.tsv` declara,
por patch, o que tem de ser VERDADE no lift DEPOIS dele correr -- contagem de um
simbolo dentro de uma funcao, presenca de uma agulha, ausencia de outra. Este
script verifica isso de forma INDEPENDENTE do que o patch imprimiu, lendo o
mesmo lift e contando as agulhas outra vez.

Foi assim que 36 conversoes OPD desapareceram sem ninguem dar por isso: os
patches imprimiam "fixed 0" seguido de "OK" e devolviam rc=0; o conteudo nao
mudava; o runner classificava ALREADY-APPLIED; o gate ficava verde. Nenhum
contrato existia para dizer "esta funcao tem de ter 7 ps3_call_opd".

Formato de CONTRACTS.tsv (TSV, 8 colunas, cabecalho comentado com #):

  id  patch  escopo  alvo  predicado  agulha  valor  razao

  escopo=ea:      `alvo` e' um endereco hex (0x0032E200); a regiao vai de
                  "void func_XXXXXXXX(" ate ao INICIO do proximo "void func_"
                  -- o MESMO delimitador que os patches OPD ja usam.
  escopo=global:  `alvo` e' "-"; conta em TODOS os ppu_recomp_*.cpp do
                  LIFT_DIR concatenados.
  predicado=count_ge:  conta(agulha) na regiao >= valor.
  predicado=count_eq:  conta(agulha) na regiao == valor.
  agulha:         literal por omissao; prefixo "re:" activa re.findall (regex).

Uso:
  check_contracts.py CONTRACTS.tsv LIFT_DIR --patch NOME_DO_PATCH [--json]

Codigos de saida (os 3 estados que o Plano 04-05 precisa para o classificador
de 6 estados):
  0  todos os contratos do patch passam
  1  pelo menos um contrato do patch falha (ancora perdida conta como falha)
  2  SEM-CONTRATO -- o patch nao tem nenhuma linha em CONTRACTS.tsv
  3  erro de uso (CONTRACTS.tsv ou LIFT_DIR nao existe / ficheiro mal formado)
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Contract:
    id: str
    patch: str
    escopo: str
    alvo: str
    predicado: str
    agulha: str
    valor: int
    razao: str


def load_contracts(path: Path) -> list[Contract]:
    """Le CONTRACTS.tsv linha a linha. Ignora vazias e comentadas com '#'.

    Valida exactamente 8 colunas por linha de dados -- senao SystemExit
    citando a linha crua (erro de uso, nao um contrato que falha).
    """
    contracts: list[Contract] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) != 8:
            raise SystemExit(
                f"CONTRACTS.tsv malformado (esperava 8 colunas, encontrei "
                f"{len(cols)}): {raw!r}"
            )
        cid, patch, escopo, alvo, predicado, agulha, valor, razao = cols
        contracts.append(
            Contract(
                id=cid, patch=patch, escopo=escopo, alvo=alvo,
                predicado=predicado, agulha=agulha, valor=int(valor),
                razao=razao,
            )
        )
    return contracts


def load_lift_text(lift_dir: Path) -> dict[str, str]:
    """{nome_do_chunk: conteudo} para todos os ppu_recomp_*.cpp de LIFT_DIR."""
    return {
        p.name: p.read_text(encoding="utf-8", errors="replace")
        for p in sorted(lift_dir.glob("ppu_recomp_*.cpp"))
    }


def ea_region(chunks: dict[str, str], alvo: str) -> str | None:
    """Fatia [inicio de func_XXXXXXXX, inicio da funcao seguinte) num chunk.

    Replica EXACTAMENTE a delimitacao que os patches OPD ja usam
    (s.find("void func_...") / s.find("void func_", i+20)). Devolve None se
    a funcao nao existir em nenhum chunk -- "ancora perdida".

    FASE 19 (XEN-04): ha' uma SEGUNDA forma de inicio de funcao. Uma funcao
    declarada em `[[functions_override]]`, com `[main].emit_weak_wrappers`,
    sai como `PPC_FUNC_IMPL(func_X) {` (corpo, simbolo __imp_func_X) seguida de
    um wrapper FRACO `PPC_FUNC(func_X) { __imp_func_X(ctx); }`.

    Isto NAO e' hipotetico: medido a 2026-08-02 contra um lift com a forma
    nova, todos os contratos `escopo=ea` de um alvo com override davam
    "ancora perdida: ... nao existe no lift" -- um VERMELHO FALSO sobre uma
    funcao que la' esta' inteira. Como os aceites `contract:` dos OPD P0 sao
    exactamente o que a Fase 19 vai migrar, deixar isto por corrigir era
    programar um vermelho falso para o plano seguinte.

    O wrapper e' incluido no delimitador de FIM (`_FUNC_STARTS`) mas nunca
    usado como INICIO: como vem logo a seguir ao corpo, tomar o wrapper como
    inicio daria uma regiao de duas linhas -- pior que a ancora perdida,
    porque um `count_ge` sobre ela falharia com um numero plausivel.
    """
    func = f"func_{int(alvo, 16):08X}"
    starts = (f"void {func}(", f"PPC_FUNC_IMPL({func})")
    ends = ("void func_", "PPC_FUNC_IMPL(func_", "PPC_FUNC(func_")
    for text in chunks.values():
        i = min((p for p in (text.find(n) for n in starts) if p >= 0), default=-1)
        if i < 0:
            continue
        j = min((p for p in (text.find(n, i + 20) for n in ends) if p >= 0),
                default=-1)
        return text[i:j] if j >= 0 else text[i:]
    return None


def count_needle(region: str, agulha: str) -> int:
    """Conta ocorrencias de `agulha` em `region`. Prefixo 're:' -> regex."""
    if agulha.startswith("re:"):
        return len(re.findall(agulha[3:], region))
    return region.count(agulha)


def evaluate(c: Contract, chunks: dict[str, str]) -> tuple[bool, str]:
    """Avalia um Contract contra o lift. Devolve (ok, detalhe humano)."""
    if c.escopo == "ea":
        region = ea_region(chunks, c.alvo)
        if region is None:
            return False, f"ancora perdida: func_{int(c.alvo, 16):08X} nao existe no lift"
    elif c.escopo == "global":
        region = "".join(chunks.values())
    else:
        return False, f"escopo desconhecido: {c.escopo!r}"

    found = count_needle(region, c.agulha)

    if c.predicado == "count_ge":
        ok = found >= c.valor
    elif c.predicado == "count_eq":
        ok = found == c.valor
    else:
        return False, f"predicado desconhecido: {c.predicado!r}"

    detalhe = (
        f"agulha={c.agulha!r} predicado={c.predicado} "
        f"esperado {c.valor} encontrado {found}"
    )
    return ok, detalhe


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("contracts", help="caminho para CONTRACTS.tsv")
    ap.add_argument("lift_dir", help="directorio do lift (ppu_recomp_*.cpp)")
    ap.add_argument("--patch", required=True, help="nome do patch_*.py a verificar")
    ap.add_argument("--json", action="store_true", help="tambem imprime um resumo JSON")
    args = ap.parse_args(argv)

    contracts_path = Path(args.contracts)
    lift_dir = Path(args.lift_dir)

    if not contracts_path.is_file():
        print(f"ERRO: CONTRACTS.tsv nao encontrado: {contracts_path}", file=sys.stderr)
        return 3
    if not lift_dir.is_dir():
        print(f"ERRO: LIFT_DIR nao encontrado: {lift_dir}", file=sys.stderr)
        return 3

    all_contracts = load_contracts(contracts_path)
    mine = [c for c in all_contracts if c.patch == args.patch]

    if not mine:
        print(f"SEM-CONTRATO: {args.patch} nao tem nenhuma linha em {contracts_path}")
        if args.json:
            print(json.dumps({"patch": args.patch, "estado": "SEM-CONTRATO", "contratos": []}))
        return 2

    chunks = load_lift_text(lift_dir)
    results = []
    all_pass = True
    for c in mine:
        ok, detalhe = evaluate(c, chunks)
        tag = "PASS" if ok else "FAIL"
        print(f"[{tag}] {c.id} ({c.patch}): {detalhe}")
        results.append({"id": c.id, "ok": ok, "detalhe": detalhe})
        if not ok:
            all_pass = False

    if args.json:
        estado = "OK" if all_pass else "FALHA"
        print(json.dumps({"patch": args.patch, "estado": estado, "contratos": results}))

    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
