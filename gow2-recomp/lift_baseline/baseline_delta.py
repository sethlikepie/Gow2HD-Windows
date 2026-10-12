#!/usr/bin/env python3
"""baseline_delta.py -- comparador generico de conjuntos de IDs opacos, por DELTA.

Porque existe (D-4.5, 04-CONTEXT.md)
-------------------------------------
Ja' provamos duas vezes nesta fase que um limiar ABSOLUTO pune melhorias:

  1. O marcador `PREAMBLE ps3_indirect_call` passou a FALHAR quando o lift
     MELHOROU -- as conversoes OPD repostas transformaram 24 `ps3_indirect_call`
     em `ps3_call_opd`, e um gate por contagem minima acusou regressao onde
     havia progresso.
  2. O grupo `opd-dispatch` acusa -319 que, medido por funcao, e' na verdade
     -619 de lixo do lift antigo (funcoes que o lifter desassemblou a partir
     de DADOS) MAIS +291 de codigo real que o `truncated-bounds repair`
     recuperou. Um limiar absoluto ve' so' o -319 e falha um gate que deveria
     estar verde.

A segunda licao, tao importante como a primeira: um corpus GRANDE nunca da'
zero achados (51917 funcoes no lift, 20750 ranges curados -- ha' sempre ruido
de fundo). Um gate absoluto sobre esse corpus ou fica cronicamente vermelho
ou e' afrouxado ate' nao servir -- o mesmo padrao que ja fez o `TRUNCATE_RATIO`
de tools/lift_parity.py ser relaxado (citado no proprio docstring desse
script).

Este script resolve os dois problemas ao mesmo tempo: compara dois conjuntos
de IDs opacos (BASELINE congelado vs CURRENT medido agora) e falha SO' por
achado NOVO (em CURRENT, ausente de BASELINE). Um achado RESOLVIDO (em
BASELINE, ausente de CURRENT) e' informativo e NUNCA belisca o rc -- essa e'
a propriedade central provada pelo Teste 3 em test_baseline_delta.py.

Uso:
  baseline_delta.py CURRENT_IDS.json BASELINE_IDS.json [--label NOME]

Formato dos dois ficheiros: {"ids": [...]} -- uma lista plana de strings
opacas a este script (o chamador decide o que um id significa: "ea/signal"
para paridade, "I1:a|b"/"I3:func" para fronteiras, etc.).

rc=0  nenhum achado novo (delta limpo -- pode haver achados resolvidos)
rc=1  ha' pelo menos um achado novo face ao baseline
rc=2  erro de uso (ficheiro em falta, JSON invalido, chave "ids" ausente)
"""
from __future__ import annotations

import argparse
import json
import sys


def load_ids(path: str) -> set[str] | None:
    """Le' {"ids": [...]} de `path` e devolve um set[str]. None em erro de uso."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            d = json.load(f)
        return set(d["ids"])
    except (FileNotFoundError, json.JSONDecodeError, KeyError, TypeError):
        return None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("current", help="ficheiro JSON com os ids medidos AGORA")
    ap.add_argument("baseline", help="ficheiro JSON com os ids congelados")
    ap.add_argument("--label", default="delta", help="nome deste gate no output")
    args = ap.parse_args(argv)

    current = load_ids(args.current)
    if current is None:
        print(f"[{args.label}] ERRO: nao foi possivel ler ids de current: {args.current}",
              file=sys.stderr)
        return 2

    baseline = load_ids(args.baseline)
    if baseline is None:
        print(f"[{args.label}] ERRO: nao foi possivel ler ids de baseline: {args.baseline}",
              file=sys.stderr)
        return 2

    novos = sorted(current - baseline)
    resolvidos = sorted(baseline - current)

    print(f"[{args.label}] current={len(current)} baseline={len(baseline)} "
          f"novos={len(novos)} resolvidos={len(resolvidos)}")

    if resolvidos:
        print(f"[{args.label}] RESOLVIDO (melhoria, nao falha o gate):")
        for rid in resolvidos[:10]:
            print(f"  RESOLVIDO  {rid}")
        if len(resolvidos) > 10:
            print(f"  ... +{len(resolvidos) - 10}")

    if novos:
        print(f"[{args.label}] NOVO (regressao face ao baseline):")
        for nid in novos[:20]:
            print(f"  NOVO  {nid}")
        if len(novos) > 20:
            print(f"  ... +{len(novos) - 20}")
        return 1

    print(f"[{args.label}] DELTA OK -- nenhum achado novo face ao baseline")
    return 0


if __name__ == "__main__":
    sys.exit(main())
