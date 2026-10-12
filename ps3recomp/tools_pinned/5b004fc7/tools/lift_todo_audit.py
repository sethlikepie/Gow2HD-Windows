#!/usr/bin/env python3
"""lift_todo_audit.py -- inventário do que o lifter NÃO SABE traduzir.

Porque existe
-------------
O `ppu_lifter.py:2931` tem um caminho de último recurso:

    return f"/* TODO: {mn} {insn.operands} */;"

Quando não conhece uma instrução, emite-a como **comentário**. O código do jogo
que ali estava desaparece do binário sem que nada acuse — é a mesma classe do
bug `truncated-bounds` que custou o boot em 2026-07-26, mas ao nível da
instrução em vez da função.

O RESULTADO MEDIDO CONTRARIA A HIPÓTESE QUE MOTIVOU ESTA FERRAMENTA, e vale a
pena ficar escrito. Contando os TODO no lift, há **48409**, dos quais 47991 são
`.word` e 416 pareciam instruções reais — 207 `rld??` e 130 `vmx_x*`. Isso
sugeria um buraco grande: centenas de instruções do jogo emitidas como
comentário.

Restringindo ao que o `functions.json` declara como código, sobram **14**. E das
14, **11 têm encoding ASCII** (`0x794D6574` = "yMet", `0x782D2578` = "x-%x") e as
3 restantes são `0x10111213` — bytes sequenciais. Todas caem em três "funções"
que são tabelas de strings: `func_004C0F00`, `func_004C43B0` e `func_004C4AD8`.
Duas delas já eram acusadas como espúrias pelo `lift_parity.py`.

**Conclusão: hoje o lifter não perde código real por não saber traduzir.** Os
TODO são dados desassemblados como código. O problema que existe é outro — o
mapa de funções declara regiões de dados como funções, que é o que o
`--code-end` (nunca adoptado) atacaria.

A ferramenta fica na mesma, por duas razões: prova essa ausência de forma
repetível em vez de por inspecção, e o dia em que aparecer um opcode real por
implementar — noutro jogo, ou num troço do GoW2 ainda não coberto — dá o
encoding, o endereço e a contagem por forma, que é o que um implementador
precisa.

Esta ferramenta parte do ELF, não do lift, por uma razão: só no binário existe o
**encoding** de 32 bits. Sem ele não se implementa um opcode — o mnemónico
sozinho não diz quais são os campos.

Dois níveis distintos de "não sei", e a ferramenta separa-os porque a correcção
é noutro sítio:

  DISASM  o desassemblador não descodifica a sub-forma e devolve um nome com
          `?` (`ld?`, `std?`, `rld??`). Corrige-se em tools/ppu_disasm.py.
  LIFT    o desassemblador dá um nome, mas o lifter não tem tradução
          (`vmx_x*`, `op31_x*`). Corrige-se em tools/ppu_lifter.py.

Uso:
  lift_todo_audit.py <ELF> [--functions FILE] [--json OUT] [--limit N]
                     [--family NOME]

rc=0 sempre — é um inventário, não um gate. O gate é o `lift_parity.py`.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Um mnemónico é "desconhecido" quando o desassemblador desiste. As três formas
# que ele usa hoje, medidas em tools/ppu_disasm.py:
#   "ld?"/"std?"  -- sub-forma DS não reconhecida (:411, :419)
#   "rld??"       -- rotate-left-doubleword sem forma conhecida (:488)
#   "vmx_xNNN"    -- VMX pelo XO cru, sem tradução (:1076)
#   "op31_xNNN"   -- tabela 31 pelo XO cru
UNKNOWN_MARKERS = ("?", "vmx_x", "op31_x", "op4_x", "op19_x", "op59_x", "op63_x")


def is_unknown(mnemonic: str) -> bool:
    if not mnemonic or mnemonic.startswith("."):
        return False                      # `.word`/`.long` = dados, não ignorância
    return any(m in mnemonic for m in UNKNOWN_MARKERS)


def looks_like_text(word: int) -> bool:
    """O encoding é texto ASCII disfarçado de instrução?

    Medido em 2026-07-30: das 14 instruções desconhecidas dentro de funções
    declaradas do GoW2, **as 14** têm encoding ASCII — `0x794D6574` = "yMet",
    `0x782D2578` = "x-%x", `0x796D7075` = "ympu". Caem em três "funções" que são
    tabelas de strings (`func_004C0F00`, `func_004C43B0`, `func_004C4AD8`), duas
    das quais o `lift_parity.py` já acusa como espúrias.

    Sem este filtro, a ferramenta mandaria implementar opcodes que não existem —
    o pior desfecho possível, porque produziria código a sério para traduzir
    texto, e ninguém desconfiaria do resultado.
    """
    b = word.to_bytes(4, "big")
    printable = sum(1 for c in b if 32 <= c < 127)
    return printable >= 3


def family_of(mnemonic: str) -> str:
    """Agrupa por FORMA, não por instância: vmx_x531 e vmx_x9 são a mesma família."""
    for pref in ("vmx_x", "op31_x", "op4_x", "op19_x", "op59_x", "op63_x"):
        if mnemonic.startswith(pref):
            return pref + "*"
    return mnemonic


def load_exec_segments(elf_path):
    from elf_parser import ELFFile, PT_LOAD
    elf = ELFFile(elf_path)
    elf.load()
    segs = []
    for i, ph in enumerate(elf.program_headers):
        if ph.p_type == PT_LOAD and ph.p_filesz > 0 and (ph.p_flags & 1):
            segs.append((ph.p_vaddr, ph.p_vaddr + ph.p_filesz, elf.get_segment_data(i)))
    if not segs:
        raise SystemExit(f"ERRO: {elf_path} nao tem segmento PT_LOAD executavel")
    return segs


def scan(elf_path, in_functions=None):
    """[{ea, word, mnemonic, operands, family, nivel}] das instruções desconhecidas."""
    import ppu_disasm
    out = []
    for lo, _hi, data in load_exec_segments(elf_path):
        for ins in ppu_disasm.disassemble_bytes(data, lo):
            if not is_unknown(ins.mnemonic):
                continue
            if in_functions is not None and ins.addr not in in_functions:
                continue
            off = ins.addr - lo
            word = int.from_bytes(data[off:off + 4], "big") if off + 4 <= len(data) else 0
            out.append({
                "ea": ins.addr,
                "word": word,
                "mnemonic": ins.mnemonic,
                "operands": str(ins.operands),
                "family": family_of(ins.mnemonic),
                # o disasm desistiu (nome com '?') vs o lifter e' que nao traduz
                "nivel": "DISASM" if "?" in ins.mnemonic else "LIFT",
                "texto": looks_like_text(word),
            })
    return out


def covered_addrs(functions_json):
    """Endereços dentro de funções declaradas — o resto é provavelmente dados."""
    with open(functions_json) as f:
        fl = json.load(f)
    out = set()
    for e in fl:
        s, t = int(str(e["start"]), 0), int(str(e["end"]), 0)
        out.update(range(s, t, 4))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("--functions", help="functions.json: restringe ao que e' codigo declarado")
    ap.add_argument("--json", help="escreve o inventario completo aqui")
    ap.add_argument("--limit", type=int, default=12, help="exemplos por familia (0=todos)")
    ap.add_argument("--family", help="detalha so' esta familia")
    a = ap.parse_args()

    cov = covered_addrs(a.functions) if a.functions else None
    if cov is not None:
        print(f"[todo-audit] functions.json: {len(cov)} enderecos de codigo declarado",
              file=sys.stderr)

    items = scan(a.elf, cov)
    n_txt = sum(1 for i in items if i["texto"])
    print(f"[todo-audit] {len(items)} instrucoes que o lifter nao sabe traduzir "
          f"({n_txt} com encoding ASCII = dados, {len(items)-n_txt} candidatas reais)",
          file=sys.stderr)
    if items and n_txt == len(items):
        print("[todo-audit] TODAS tem encoding de texto -- nao ha' opcode real por "
              "implementar; o que ha' e' dados declarados como codigo.", file=sys.stderr)

    by_fam = defaultdict(list)
    for it in items:
        by_fam[it["family"]].append(it)

    print("\n=== o que o lifter nao sabe, por familia ===")
    print(f"  {'familia':<14} {'n':>5}  {'nivel':<7} {'XO distintos':>12}")
    for fam, lst in sorted(by_fam.items(), key=lambda kv: -len(kv[1])):
        xos = {it["word"] & 0x7FF for it in lst}
        niveis = {it["nivel"] for it in lst}
        ntxt = sum(1 for it in lst if it["texto"])
        marca = f"  <- {ntxt}/{len(lst)} sao TEXTO (dados)" if ntxt else ""
        print(f"  {fam:<14} {len(lst):>5}  {'/'.join(sorted(niveis)):<7} {len(xos):>12}{marca}")

    fams = [a.family] if a.family else [f for f, _ in
                                        sorted(by_fam.items(), key=lambda kv: -len(kv[1]))]
    for fam in fams:
        lst = by_fam.get(fam)
        if not lst:
            continue
        print(f"\n--- {fam} ({len(lst)} ocorrencias) ---")
        # agrupa por encoding-forma: opcode primario + XO, que e' o que define a
        # instrucao. Implementar uma forma resolve todas as suas ocorrencias.
        by_shape = defaultdict(list)
        for it in lst:
            by_shape[(it["word"] >> 26, it["word"] & 0x7FF)].append(it)
        lim = len(by_shape) if a.limit == 0 else a.limit
        for (op, xo), g in sorted(by_shape.items(), key=lambda kv: -len(kv[1]))[:lim]:
            ex = g[0]
            print(f"  op={op:<2} xo={xo:<4} n={len(g):<4} "
                  f"ex: 0x{ex['ea']:08X}  word=0x{ex['word']:08X}  "
                  f"{ex['mnemonic']} {ex['operands']}")
        if len(by_shape) > lim:
            print(f"  ... +{len(by_shape) - lim} formas")

    if a.json:
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump({"total": len(items),
                       "por_familia": {f: len(l) for f, l in by_fam.items()},
                       "itens": items}, fh, indent=1)
        print(f"\n[todo-audit] inventario em {a.json}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
