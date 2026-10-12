#!/usr/bin/env python3
"""check_boot_health.py -- o criterio de aceite do boot: NAO PIOR QUE A PRODUCAO.

Porque existe (2026-08-03)
--------------------------
Dois criterios de aceite de um re-lift estavam escritos contra um ideal que a
propria producao NAO cumpre:

  * `ICALL-BAD = 0` -- a producao tem 12. Um candidato com 12 e' IGUAL a
    producao, e o criterio chamava-lhe falha. Um gate que nasce vermelho e' um
    gate que ninguem le (a licao ja' escrita em manifest_delta_gate.py).
  * `accept_relift.sh` perna 4 exige `elo_stopped="nenhum"` -- os 5 elos
    bloqueantes passados. A producao para no elo 4 em 100% das corridas
    medidas, e ha' uma razao medida para isso, escrita em
    `lib_boot_chain_metrics.sh:94-118`: a thread `AUTO_LOAD` so' pode ser criada
    a jusante de `func_00242C94`, o LOOP PRINCIPAL, que so' retorna em
    REQUEST_EXITGAME. **"AUTO_LOAD nunca criada" e' o comportamento CORRECTO de
    um jogo que ainda esta a correr.** O criterio, como estava, exigia que o
    jogo se FECHASSE para o lift ser aceite.

O criterio correcto para promover um re-lift nao e' "perfeito" -- e' **nao pior
do que a producao que vai substituir**. Os numeros da producao vivem em
`PRODUCTION_REFERENCE.tsv`, cada um com data e proveniencia, nunca hard-coded
aqui: um limiar sem proveniencia e' um numero que ninguem pode refutar daqui a
um mes.

O que este script NAO faz: nao mede o lift (isso e' `verify_lift.sh`), nao
aplica patches, nao decide sozinho a promocao. Le' o TSV do `smoke_chain_gate.sh`
e os logs de boot que ele produziu, e responde a uma pergunta so'.

Uso:
  check_boot_health.py --chain-tsv TSV [--log LOG ...] [--reference REF.tsv]
  check_boot_health.py --chain-tsv TSV --log LOG ... --freeze OUT.tsv

rc=0  todas as metricas bloqueantes sao NAO PIORES que a referencia
rc=1  pelo menos uma e' pior
rc=2  erro de uso (TSV ausente/ilegivel, referencia em falta)
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REFERENCE_DEFAULT = HERE / "PRODUCTION_REFERENCE.tsv"

# Escala dos elos -- a MESMA de derive_elo_stopped() (smoke_chain_gate.sh:118).
# elo_atingido = indice do elo onde a corrida parou; quanto maior, mais longe
# chegou. Nao reimplementar noutro sitio.
ELO_INDEX = {
    "intro (st620)": 1,
    "2o movie (StartSeq)": 2,
    "re-Play (NOPIC)": 3,
    "AUTO_LOAD (nunca criada)": 4,
    "AUTO_LOAD (criada, nao termina)": 4,
    "WAD (R_PermA)": 5,
    "nenhum": 6,
}

# Os greps sao os MESMOS das notas de medicao (2026-08-02/03): contagem crua de
# linhas. Mudar um destes muda o significado dos numeros congelados na
# referencia -- por isso ficam num sitio so'.
LOG_METRICS = {
    "icall_bad": "ICALL-BAD",
    "oob": "OOB",
    "ffff": "0xFFFF",
    "fatal": "FATAL",
}
FATAL_ADDR_RE = re.compile(r"FATAL: stuck calling (0x[0-9A-Fa-f]+)")


def elo_de(nome: str) -> int:
    """Indice do elo. Um nome desconhecido vale 0 -- nunca e' silenciosamente
    tratado como sucesso; um instrumento que mudou de vocabulario tem de fazer
    o gate falhar, nao passar."""
    return ELO_INDEX.get(nome.strip(), 0)


def ler_referencia(path: Path) -> dict[str, dict]:
    if not path.is_file():
        raise SystemExit(f"ERRO: referencia de producao em falta: {path}")
    ref: dict[str, dict] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split("\t")
        if parts[0] == "metrica":
            continue
        if len(parts) < 4:
            raise SystemExit(f"ERRO: linha malformada em {path}: {line!r}")
        ref[parts[0]] = {
            "valor": int(parts[1]),
            "sentido": parts[2],
            "bloqueante": parts[3].strip().lower() in ("sim", "1", "true"),
            "medido_em": parts[4] if len(parts) > 4 else "(sem data)",
            "proveniencia": parts[5] if len(parts) > 5 else "(sem proveniencia)",
        }
    return ref


def ler_chain_tsv(path: Path) -> list[dict]:
    if not path.is_file():
        raise SystemExit(f"ERRO: TSV da cadeia em falta: {path}")
    linhas = [l for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
    if not linhas:
        raise SystemExit(f"ERRO: TSV da cadeia vazio: {path}")
    cab = linhas[0].split("\t")
    corridas = []
    for l in linhas[1:]:
        campos = l.split("\t")
        corridas.append(dict(zip(cab, campos)))
    return corridas


def medir_logs(logs: list[Path]) -> tuple[dict[str, int], list[str], int]:
    """Devolve (pior valor por metrica, enderecos de FATAL vistos, n_logs).

    PIOR = maximo entre as corridas. Uma corrida que morre cedo mede 0 em tudo
    -- isso e' melhor, nao pior, e o maximo trata-o correctamente.
    """
    pior = {k: 0 for k in LOG_METRICS}
    addrs: list[str] = []
    lidos = 0
    for p in logs:
        if not p.is_file():
            continue
        lidos += 1
        texto = p.read_text(errors="replace")
        for chave, agulha in LOG_METRICS.items():
            n = sum(1 for ln in texto.splitlines() if agulha in ln)
            pior[chave] = max(pior[chave], n)
        for m in FATAL_ADDR_RE.finditer(texto):
            if m.group(1) not in addrs:
                addrs.append(m.group(1))
    return pior, addrs, lidos


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--chain-tsv", required=True, help="TSV produzido por smoke_chain_gate.sh")
    ap.add_argument("--log", action="append", default=[], help="log de boot (repetivel)")
    ap.add_argument("--reference", default=str(REFERENCE_DEFAULT))
    ap.add_argument("--freeze", metavar="OUT.tsv", default=None,
                    help="em vez de comparar, escreve o medido agora como referencia "
                         "(a proveniencia em prosa fica por completar A MAO)")
    args = ap.parse_args(argv)

    corridas = ler_chain_tsv(Path(args.chain_tsv))
    logs = [Path(x) for x in args.log]
    pior, fatal_addrs, n_logs = medir_logs(logs)

    n_runs = len(corridas)
    elos = [elo_de(c.get("elo_stopped", "")) for c in corridas]
    st620 = [int(c.get("st620", "0") or 0) for c in corridas]

    if args.freeze:
        out = Path(args.freeze)
        linhas = [
            "# GERADO por check_boot_health.py --freeze -- a PROVENIENCIA EM PROSA DE CADA",
            "# LINHA FICA POR ESCREVER A MAO. Um limiar sem proveniencia e' um numero que",
            "# ninguem pode refutar daqui a um mes; nao promova nada com este ficheiro por",
            "# completar. Ver o cabecalho de PRODUCTION_REFERENCE.tsv.",
            "metrica\tvalor\tsentido\tbloqueante\tmedido_em\tproveniencia",
            f"elo_atingido\t{min(elos) if elos else 0}\tmin\tsim\t(por datar)\t"
            f"medido em {n_runs} corridas de {args.chain_tsv}",
            f"st620_max\t3\tmin\tsim\t(por datar)\tlimiar contratual de smoke_relift_equiv.sh; "
            f"medido agora: min={min(st620) if st620 else 0}",
        ]
        for chave in LOG_METRICS:
            linhas.append(f"{chave}\t{pior[chave]}\tmax\tsim\t(por datar)\t"
                          f"pior de {n_logs} logs; enderecos FATAL vistos: "
                          f"{', '.join(fatal_addrs) or 'nenhum'}")
        out.write_text("\n".join(linhas) + "\n", encoding="utf-8")
        print(f"[REFERENCIA] congelada em {out} -- COMPLETAR a proveniencia a mao")
        return 0

    ref = ler_referencia(Path(args.reference))

    # limiar de corridas: a mesma aritmetica de smoke_chain_gate.sh:182 e
    # smoke_relift_equiv.sh -- (RUNS*2+2)/3; 6 corridas -> 4.
    limiar_corridas = (n_runs * 2 + 2) // 3

    print("=" * 62)
    print(" check_boot_health.py -- NAO PIOR QUE A PRODUCAO")
    print("=" * 62)
    print(f"referencia : {args.reference}")
    print(f"corridas   : {n_runs} (limiar de corridas boas: {limiar_corridas})")
    print(f"logs lidos : {n_logs}")
    print()

    resultados: list[tuple[str, bool, bool, str]] = []

    # --- elo_atingido: por corrida, com limiar de corridas ------------------
    if "elo_atingido" in ref:
        r = ref["elo_atingido"]
        boas = sum(1 for e in elos if e >= r["valor"])
        ok = boas >= limiar_corridas
        resultados.append(("elo_atingido", ok, r["bloqueante"],
                           f"elo >= {r['valor']} em {boas}/{n_runs} (limiar {limiar_corridas}); "
                           f"elos medidos: {elos}"))

    # --- st620_max: por corrida, mesmo limiar de corridas -------------------
    if "st620_max" in ref:
        r = ref["st620_max"]
        boas = sum(1 for s in st620 if s >= r["valor"])
        ok = boas >= limiar_corridas
        resultados.append(("st620_max", ok, r["bloqueante"],
                           f"st620 >= {r['valor']} em {boas}/{n_runs} (limiar {limiar_corridas}); "
                           f"max medido: {max(st620) if st620 else 0}"))

    # --- metricas de log: pior corrida vs referencia ------------------------
    for chave in LOG_METRICS:
        if chave not in ref:
            continue
        r = ref[chave]
        medido = pior[chave]
        if n_logs == 0:
            resultados.append((chave, False, r["bloqueante"],
                               "SEM LOGS -- nao medido (um gate sem medicao nunca passa)"))
            continue
        ok = medido <= r["valor"] if r["sentido"] == "max" else medido >= r["valor"]
        resultados.append((chave, ok, r["bloqueante"],
                           f"medido (pior corrida) = {medido}, producao = {r['valor']} "
                           f"({r['sentido']}, {r['medido_em']})"))

    largura = max(len(n) for n, _, _, _ in resultados)
    for nome, ok, bloq, detalhe in resultados:
        marca = "PASS" if ok else "FAIL"
        tag = "[BLOQUEANTE]" if bloq else "[informativo]"
        print(f"{nome:<{largura}}  {marca}  {tag}  {detalhe}")

    print()
    print(f"INFORMATIVO  enderecos de FATAL observados: {', '.join(fatal_addrs) or 'nenhum'}")
    if "fatal" in ref:
        print(f"INFORMATIVO  a referencia gateia a CONTAGEM de FATAL, nao o endereco -- ver a")
        print(f"             proveniencia da linha `fatal` em {args.reference}")
    print()

    falhas = [n for n, ok, bloq, _ in resultados if bloq and not ok]
    if falhas:
        print(f"REJEITADO: pior que a producao em: {', '.join(falhas)}")
        return 1
    print("ACEITE: nao pior que a producao em nenhuma metrica bloqueante.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
