#!/usr/bin/env python3
"""audit_boundaries.py -- torna as fronteiras de funcao VERIFICAVEIS.

Hoje o mapa de funcoes de um titulo (functions.json) e' um artefacto curado
que ninguem consegue auditar: nao ha nada que responda "este mapa esta bem?".
Medido no GoW2 (2026-07-25): 3.281 buracos dentro do .text somando 1.100.516
bytes, dos quais 274.438 palavras descodificam como instrucao VALIDA -- codigo
real fora de qualquer funcao. E 179 funcoes acabam SEM terminador com o `end`
a nao ser inicio de nenhuma funcao registada: nesses casos o lifter trampolina
para a funcao SEGUINTE POR INDICE e salta codigo alcancavel. E' exactamente a
classe do bug func_002550C8, que custou meses.

Invariantes verificadas:

  I1 OVERLAP        dois ranges intersectam-se
  I2 GAP-CODE       lacuna nao coberta que descodifica como codigo
  I3 TRUNCATED      ultima instrucao nao e' terminador E o `end` nao e' inicio
                    registado  -> o emissor vai trampolinar para o vizinho errado
  I4 ORACLE-MISSING funcao que um oraculo externo (Ghidra/IDA) viu e nos nao
  I5 ORACLE-END     mesmo inicio, fim diferente do oraculo

Os oraculos externos consomem o schema que tools/ghidra_analyze.py e
tools/ida_analyze.py escrevem: [{addr, size, name, thunk}]. Sao dois
detectores independentes para a mesma pergunta.

Uso:
  audit_boundaries.py functions.json --elf EBOOT.ELF
  audit_boundaries.py functions.json --elf EBOOT.ELF --oracle ghidra_out/functions.json
  audit_boundaries.py functions.json --elf EBOOT.ELF --gate I1,I3    # exit!=0
  audit_boundaries.py functions.json --elf EBOOT.ELF --json rel.json

NOTA sobre --gate com I4/I5 (Fase 20): NAO uses `--gate I4,I5` num caminho de
aceitacao. O corpus do GoW2 tem milhares de I5 historicos; um gate por CONTAGEM
ABSOLUTA nasce vermelho e ensina toda a gente a ignora-lo. O caminho suportado
e' por DELTA contra um baseline congelado:

  audit_boundaries.py functions.json --elf EBOOT.ELF --oracle gh/functions.json \
      --json /tmp/bounds.json
  oracle_manifest.py ids --report /tmp/bounds.json --out /tmp/ids.json
  baseline_delta.py /tmp/ids.json lift_baseline/ORACLE_BASELINE.json

E' exactamente o que games/gow2/verify_lift.sh corre com VERIFY_ORACLE=1
(opcional; sem a variavel o oraculo nem e' consultado). A proveniencia do
export (sha256 do ELF, data, versao) vive em ghidra_out/MANIFEST.json e e'
verificada por `oracle_manifest.py check` antes do delta.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Terminadores INCONDICIONAIS: depois deles a execucao nao continua na
# instrucao seguinte. `bl`/`bctrl`/`bclr` com link sao CHAMADAS -- voltam.
UNCOND_TERM = {"b", "ba", "blr", "bctr", "rfid", "rfi"}

MIN_GAP_INSNS = 4          # lacunas mais curtas sao provavelmente padding


def as_int(v):
    return v if isinstance(v, int) else int(str(v), 0)


def load_ranges(path):
    """functions.json curado: [{start,end}] -> [(start, end)] ordenado."""
    with open(path) as f:
        raw = json.load(f)
    out = []
    for e in raw:
        if "start" in e:
            out.append((as_int(e["start"]), as_int(e["end"])))
        else:                                   # schema de oraculo
            a = as_int(e["addr"])
            out.append((a, a + as_int(e.get("size") or 0)))
    return sorted(out)


def load_oracle(path):
    """[{addr,size,name,thunk}] -> {start: (end, name, thunk)}."""
    with open(path) as f:
        raw = json.load(f)
    out = {}
    for e in raw:
        a = as_int(e["addr"])
        out[a] = (a + as_int(e.get("size") or 0), e.get("name") or "", bool(e.get("thunk")))
    return out


class Code:
    """Bytes executaveis indexados por endereco.

    Construivel a partir de segmentos crus para que os testes nao precisem de
    forjar um ELF -- e para que o audit() seja testavel sem ficheiros.
    """

    def __init__(self, segs):
        self.segs = list(segs)
        self.lo = min((s[0] for s in self.segs), default=0)
        self.hi = max((s[1] for s in self.segs), default=0)

    @classmethod
    def from_elf(cls, elf_path):
        from elf_parser import ELFFile, PT_LOAD
        elf = ELFFile(elf_path)
        elf.load()          # o construtor nao parseia nada; sem isto vem vazio
        segs = []
        for i, ph in enumerate(elf.program_headers):
            if ph.p_type == PT_LOAD and ph.p_filesz > 0 and (ph.p_flags & 1):
                segs.append((ph.p_vaddr, ph.p_vaddr + ph.p_filesz,
                             elf.get_segment_data(i)))
        return cls(segs)

    def word(self, addr):
        for v0, v1, d in self.segs:
            if v0 <= addr and addr + 4 <= v1:
                return int.from_bytes(d[addr - v0:addr - v0 + 4], "big")
        return None

    def mnemonic(self, addr):
        import ppu_disasm
        w = self.word(addr)
        if w is None:
            return None
        try:
            return ppu_disasm.decode(w, addr).mnemonic
        except Exception:                                       # noqa: BLE001
            return None

    def is_valid_code(self, addr):
        mn = self.mnemonic(addr)
        return bool(mn) and not mn.startswith((".word", "unknown", "op"))


def audit(ranges, code, oracles, code_lo, code_hi, max_report):
    f = {k: [] for k in ("I1", "I2", "I3", "I4", "I5")}
    starts = {s for s, _ in ranges}

    # I1 -- sobreposicoes
    for i in range(len(ranges) - 1):
        s0, e0 = ranges[i]
        s1, e1 = ranges[i + 1]
        if s1 < e0:
            f["I1"].append({"a": f"0x{s0:08X}-0x{e0:08X}",
                            "b": f"0x{s1:08X}-0x{e1:08X}",
                            "overlap_bytes": e0 - s1})

    if code is not None:
        # I2 -- lacunas que descodificam como codigo
        prev_end = None
        for s, e in ranges:
            if prev_end is not None and s > prev_end:
                g0, g1 = prev_end, s
                if code_lo <= g0 and g1 <= code_hi:
                    n_code = sum(1 for a in range(g0, g1, 4) if code.is_valid_code(a))
                    n_tot = max(1, (g1 - g0) // 4)
                    if n_code >= MIN_GAP_INSNS and n_code / n_tot > 0.75:
                        f["I2"].append({"gap": f"0x{g0:08X}-0x{g1:08X}",
                                        "bytes": g1 - g0, "insns_validas": n_code})
            prev_end = max(prev_end or 0, e)

        # I3 -- fim sem terminador e sem continuacao registada
        for s, e in ranges:
            if e in starts or not (code_lo <= s < code_hi):
                continue
            mn = code.mnemonic(e - 4)
            if mn and mn not in UNCOND_TERM:
                nxt = min((x for x in starts if x > e), default=None)
                f["I3"].append({"func": f"0x{s:08X}", "end": f"0x{e:08X}",
                                "ultima": mn,
                                "salta_bytes": (nxt - e) if nxt else None,
                                "proxima": f"0x{nxt:08X}" if nxt else None})

    # I4/I5 -- contra oraculos externos
    ends = dict(ranges)
    for name, orc in oracles:
        for a, (oe, onm, thunk) in sorted(orc.items()):
            if thunk or not (code_lo <= a < code_hi):
                continue
            if a not in starts:
                inside = any(s < a < e for s, e in ranges)
                f["I4"].append({"oraculo": name, "addr": f"0x{a:08X}", "nome": onm,
                                "dentro_de_outra": inside})
            elif oe and ends.get(a) and oe != ends[a]:
                our_end = ends[a]
                # Classificar a divergencia: uma extensao que cabe antes do
                # proximo inicio registado e' mecanicamente adoptavel; uma que
                # o ultrapassa significa que o oraculo FUNDIU duas funcoes que
                # nos separamos -- isso e' juizo, nao automatismo.
                if oe > our_end:
                    nxt = min((x for x in starts if x > a), default=None)
                    kind = "extende-seguro" if (nxt is None or oe <= nxt) else "extende-funde"
                else:
                    kind = "encurta"
                f["I5"].append({"oraculo": name, "addr": f"0x{a:08X}", "nome": onm,
                                "nosso_end": f"0x{our_end:08X}", "oraculo_end": f"0x{oe:08X}",
                                "tipo": kind, "bytes": oe - our_end})
    return f


DESC = {
    "I1": "sobreposicao de ranges",
    "I2": "lacuna que descodifica como codigo",
    "I3": "fim sem terminador e sem continuacao registada (classe 2550C8)",
    "I4": "funcao que o oraculo viu e nos nao",
    "I5": "mesmo inicio, fim diferente do oraculo",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("functions", help="functions.json curado")
    ap.add_argument("--elf", help="ELF para descodificar (I2/I3 precisam)")
    ap.add_argument("--oracle", action="append", default=[],
                    metavar="FILE", help="functions.json de Ghidra/IDA (repetivel)")
    ap.add_argument("--code-lo", type=lambda x: int(x, 0), default=None)
    ap.add_argument("--code-hi", type=lambda x: int(x, 0), default=None)
    ap.add_argument("--gate", default="", help="invariantes que fazem exit!=0 (ex.: I1,I3)")
    ap.add_argument("--json", metavar="FILE", help="escrever o relatorio completo")
    ap.add_argument("--propose-ends", metavar="FILE",
                    help="escrever um functions.json com as extensoes SEGURAS do "
                         "oraculo aplicadas (para validar num lift de teste; nao "
                         "substitui o original)")
    ap.add_argument("--max-report", type=int, default=10)
    args = ap.parse_args()

    ranges = load_ranges(args.functions)
    code = Code.from_elf(args.elf) if args.elf else None
    if code is not None and not code.segs:
        sys.exit(f"ERRO: {args.elf} nao tem segmento PT_LOAD executavel")

    lo = args.code_lo if args.code_lo is not None else (code.lo if code else ranges[0][0])
    hi = args.code_hi if args.code_hi is not None else (code.hi if code else ranges[-1][1])

    oracles = [(os.path.basename(p), load_oracle(p)) for p in args.oracle]

    print(f"[bounds] {len(ranges)} ranges, janela 0x{lo:08X}-0x{hi:08X}"
          f"{'' if code else '  (sem --elf: so I1/I4/I5)'}")
    for nm, o in oracles:
        print(f"[bounds] oraculo {nm}: {len(o)} funcoes")

    f = audit(ranges, code, oracles, lo, hi, args.max_report)

    if args.propose_ends:
        # Proposta de reparacao: SO' as extensoes seguras. Nao escreve por cima
        # do functions.json -- esse alimenta o lift de producao e esta congelado.
        safe = [x for x in f["I5"] if x["tipo"] == "extende-seguro"]
        by_start = {s: e for s, e in ranges}
        for x in safe:
            by_start[as_int(x["addr"])] = as_int(x["oraculo_end"])
        out = [{"start": f"0x{s:08X}", "end": f"0x{e:08X}"} for s, e in sorted(by_start.items())]
        with open(args.propose_ends, "w") as fh:
            json.dump(out, fh, indent=1)
        tot = sum(x["bytes"] for x in safe)
        print(f"\nproposta: {args.propose_ends} "
              f"({len(safe)} fins estendidos, +{tot} bytes de codigo recuperado)")
        print("  NAO substitui o functions.json: valida com um lift de teste primeiro.")

    print()
    for k in ("I1", "I2", "I3", "I4", "I5"):
        items = f[k]
        skipped = (k in ("I2", "I3") and code is None) or (k in ("I4", "I5") and not oracles)
        mark = "-- nao avaliado" if skipped else ""
        print(f"{k} {DESC[k]:58} {len(items):6} {mark}")
        for it in items[:args.max_report]:
            print(f"      {it}")
        if len(items) > args.max_report:
            print(f"      ... +{len(items) - args.max_report}")

    if args.json:
        with open(args.json, "w") as fh:
            json.dump({k: v for k, v in f.items()}, fh, indent=1)
        print(f"\nrelatorio: {args.json}")

    gate = [g.strip().upper() for g in args.gate.split(",") if g.strip()]
    bad = [g for g in gate if f.get(g)]
    if bad:
        print(f"\n[bounds] FALHA: {', '.join(f'{g}={len(f[g])}' for g in bad)}")
        return 1
    if gate:
        print(f"\n[bounds] gate {','.join(gate)} verde")
    return 0


if __name__ == "__main__":
    sys.exit(main())
