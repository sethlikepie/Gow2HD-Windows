#!/usr/bin/env python3
"""ps3_analyse.py -- detector OFFLINE de jump tables PPU (`mtctr rX; bctr`).

Irma do lifter, nao uma flag dele. Le um ELF (ou um blob `--raw`), acha os
dispatchers de switch e escreve um ficheiro `[[switch_table]]` TOML que o
`recomp_config.load_switch_tables` ja' sabe ler e o `ppu_lifter.merge_switch_tables`
ja' sabe fundir com o `discover_jump_tables`. Esta ferramenta NAO gera codigo e
NAO toca no lifter: a emissao de `switch` para um `bctr` com tabela conhecida ja'
existe (`ppu_lifter.py:1536-1541`, `case 0x…u: goto loc_…;` com
`default: ps3_indirect_call(ctx); return;`). Aqui so' se ALIMENTA esse mecanismo.

    python3 tools/ps3_analyse.py <ELF>  -o gow2_switch_tables.toml
    python3 tools/ps3_analyse.py <blob> --raw --base 0x10000 --toc 0x11000 -o t.toml
    python3 tools/ps3_analyse.py <ELF>  -o t.toml --include-low   # so' offline!

Porque `confidence` existe
--------------------------
Uma jump table errada nao e' um teste que falha: e' o boot a saltar para um
endereco invalido. Por isso cada tabela leva um `confidence` e **por default so'
as `high` sao escritas**; as `low` exigem `--include-low` e nunca devem entrar num
TOML de producao sem revisao humana. `confidence` e' metadado do ANALISADOR --
`recomp_config._parse_switch_table` le apenas `dispatch`/`default`/`cases` e
ignora a chave sem rebentar (coberto por `test_ps3_analyse_jtables.py`).

A heuristica v1 (WINDOW_INSNS)
------------------------------
Para cada terminal `bctr`/`bcctr` (formas NAO-linkantes; `bctrl` e' chamada
indirecta, nao switch), olha para tras numa janela de `WINDOW_INSNS` instrucoes:

  1. `mtctr rC` -- a fonte do CTR (a varredura para em qualquer chamada/retorno,
     para nao atravessar fronteiras de bloco).
  2. `lwzx rD, rA, rB` -- a carga indexada da tabela. Sem ela nao se declara
     nada: `mtctr`+`bctr` sozinhos sao um ponteiro de funcao/vtable, nao um switch.
  3. A base da tabela: `lwz base, d(r2)` (um nivel) ou `lwz mid, d1(r2);
     lwz base, d2(mid)` (dois niveis, idioma PIC do gcc). QUALQUER dos dois
     operandos do `lwzx` pode ser a base -- experimentam-se ambos, e cada
     candidato a TOC (multi-TOC) -- e ganha o que valida mais alvos.
  4. `add rC, *, base` -> a tabela e' de OFFSETS com sinal a partir da base;
     senao e' de enderecos ABSOLUTOS.
  5. `rlwinm rIdx, rIdx, 2, …` -> o indice foi escalado por 4 (sinal positivo,
     registado em `index_scaled`; NAO obrigatorio -- ha' idiomas em que o indice
     ja' vem escalado).
  6. `cmplwi/cmpwi crN, rIdx, COUNT` -> a verificacao de limites; COUNT+1 e' o
     numero de casos declarado pelo proprio codigo.

Entradas lidas em big-endian; um alvo so' conta se cair em [text_lo, text_hi) e
for alinhado a 4. A leitura para no primeiro alvo invalido.

`confidence`: `high` exige TODAS estas condicoes
------------------------------------------------
  * terminal `bctr` incondicional (um `b<cond>ctr` e' ambiguo);
  * cadeia completa: `mtctr` + `lwzx` + base resolvida;
  * `>= HIGH_MIN_TARGETS` alvos validados;
  * bound check presente (`cmplwi`/`cmpwi`, 0 <= COUNT <= MAX_BOUND_COUNT);
  * **coerencia com o bound**: leram-se exactamente COUNT+1 alvos -- os dados e a
    verificacao de limites do proprio jogo concordam.
Qualquer falha destas => `low`, com a razao escrita em `notes` (e em comentario
no TOML quando sai com `--include-low`). Zero alvos => nao se emite nada.

LIMITES CONHECIDOS da heuristica v1 (escritos, nao implicitos)
--------------------------------------------------------------
  * **Falso positivo = salto para lixo.** E' por isso que o default e' so' `high`
    e que a falta de bound check basta para despromover: sem COUNT, a varredura
    usa `DEFAULT_SCAN_LIMIT` e dados que por acaso parecem enderecos de .text
    produzem uma tabela inventada.
  * **Falso negativo nao e' fatal**: o `discover_jump_tables` corre a' mesma
    dentro do lift e os `patch_jumptable_*.py` continuam a ser a rede.
  * Nao se apanha (v1): indice em registo carregado por `ld`/`lwzu`/`lbzx`
    (tabelas de bytes/half-words, idioma `-fpic` compacto), bases calculadas por
    `addis/addi` sem passar pelo TOC, dispatchers cuja `lwzx` fica fora da janela
    de `WINDOW_INSNS`, tabelas com buracos (para-se no primeiro alvo invalido) e
    tabelas de 64 bits (`ldx`).
  * Nao se verifica se o alvo cai DENTRO da funcao que contem o dispatcher --
    isso e' do `merge_switch_tables`, que ja' avisa (e a emissao filtra por
    `func.start_addr <= c < func.end_addr`). Uma tabela declarada com alvos fora
    da funcao e' silenciosamente encolhida la'.
  * Multi-TOC resolve-se por "quem valida mais alvos", igual ao
    `discover_jump_tables`; nao ha' TOC por funcao.
  * O modo `--raw` nao adivinha o TOC: sem `--toc` nao ha' base para resolver e o
    resultado e' zero tabelas.

Relacao com `ppu_lifter.discover_jump_tables`
---------------------------------------------
A varredura aqui segue deliberadamente a mesma logica ja' provada la' (multi-TOC,
dois niveis, modo de offsets, os dois operandos do `lwzx` a votos) e a suite
`test_ppu_jumptables.py` continua a ser a referencia dessa logica. NAO se importa
a funcao porque ela devolve so' `{addr: alvos}`, sem a proveniencia
(bound, modo, base) de que o `confidence` precisa -- e parti-la para expor isso
mudaria o lifter, o que esta fase proibe. O que se duplica e' o minimo; o que
diverge (o filtro de confianca, mais conservador) e' o ponto da ferramenta.

Auto-teste: .venv/bin/python3 tools/test_ps3_analyse_jtables.py
"""
from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ppu_disasm import disassemble_bytes  # noqa: E402

# --- constantes da heuristica (nomeadas de proposito: ver docstring) --------
WINDOW_INSNS = 30          # instrucoes olhadas para tras a partir do terminal
HIGH_MIN_TARGETS = 2       # alvos validados minimos para `high`
MAX_BOUND_COUNT = 4096     # COUNT acima disto nao e' um bound check plausivel
DEFAULT_SCAN_LIMIT = 256   # entradas varridas quando nao ha' bound check

# Terminais que interessam: `bctr` incondicional e as formas condicionais
# `b<cond>ctr`. As linkantes (`bctrl`, `b<cond>ctrl`) sao CHAMADAS indirectas --
# nunca dispatchers de switch -- e ficam de fora.
_BARRIERS = ("blr", "blrl", "bl", "bctrl", "rfid", "rfi")


def is_dispatch_terminal(mnemonic: str) -> bool:
    """`bctr` ou uma forma condicional `b…ctr` (nao-linkante)."""
    return (mnemonic.startswith("b") and mnemonic.endswith("ctr")
            and mnemonic != "mtctr")


@dataclass
class JumpTable:
    """Uma tabela detectada. `dispatch` e' o EA do PROPRIO `bctr` -- nao o do
    `mtctr` anterior -- porque e' assim que o lifter indexa `self.jump_tables`
    (`ppu_lifter.py:1534`) e e' o que o `recomp_config.SwitchTable` documenta."""
    dispatch: int
    cases: list[int] = field(default_factory=list)
    default: str = "indirect"
    confidence: str = "low"
    notes: list[str] = field(default_factory=list)
    # proveniencia (diagnostico; nao vai para o lifter)
    mtctr_addr: int | None = None
    table_base: int | None = None
    is_offset: bool = False
    index_scaled: bool = False
    bound_count: int | None = None
    terminal: str = "bctr"


def _mem_disp(op: str) -> int | None:
    try:
        return int(op.split("(")[0].strip(), 0)
    except ValueError:
        return None


def _decode_targets(read_u32, table_base, is_offset, limit, text_lo, text_hi):
    """Le ate' `limit` entradas big-endian; para no primeiro alvo invalido."""
    targets: list[int] = []
    for k in range(limit):
        v = read_u32((table_base + k * 4) & 0xFFFFFFFF)
        if v is None:
            break
        if is_offset:
            off = v - (1 << 32) if (v & 0x80000000) else v
            t = (table_base + off) & 0xFFFFFFFF
        else:
            t = v
        if text_lo <= t < text_hi and t % 4 == 0:
            targets.append(t)
        else:
            break
    return targets


def analyse_instructions(all_insns, read_u32, toc, text_lo, text_hi) -> list[JumpTable]:
    """Corre a heuristica descrita no docstring do modulo sobre instrucoes ja'
    desassembladas. Devolve as tabelas ordenadas por `dispatch` -- TODAS,
    incluindo as `low`; o filtro e' de quem escreve (`render_toml`)."""
    toc_list = [t for t in (toc if isinstance(toc, (list, tuple)) else [toc]) if t]
    out: list[JumpTable] = []

    for i, insn in enumerate(all_insns):
        if not is_dispatch_terminal(insn.mnemonic):
            continue
        win = all_insns[max(0, i - WINDOW_INSNS):i]

        # 1) fonte do CTR
        rC = None
        mtctr_addr = None
        for w in reversed(win):
            if w.mnemonic == "mtctr":
                rC = w.operands.strip()
                mtctr_addr = w.addr
                break
            if is_dispatch_terminal(w.mnemonic) or w.mnemonic in _BARRIERS:
                break
        if rC is None:
            continue

        # 2) carga indexada
        lwzx = next((w for w in reversed(win) if w.mnemonic == "lwzx"), None)
        if lwzx is None:
            continue
        p = [x.strip() for x in lwzx.operands.split(",")]
        if len(p) != 3:
            continue

        def _lwz_of(reg):
            """`lwz reg, disp(rA)` mais recente na janela -> (disp, rA)."""
            for w in reversed(win):
                if w.mnemonic == "lwz":
                    a = [x.strip() for x in w.operands.split(",")]
                    if len(a) == 2 and a[0] == reg and "(" in a[1]:
                        d = _mem_disp(a[1])
                        rA = a[1].split("(")[1].rstrip(")").strip()
                        return (d, rA)
            return None

        # 3) base da tabela: os DOIS operandos do lwzx sao candidatos, e cada TOC
        #    tambem (a escolha so' se sabe depois de decodificar os alvos).
        base_candidates: list[int] = []
        r_base_cands: list[str] = []
        for cand in (p[1], p[2]):
            ld = _lwz_of(cand)
            if ld is None:
                continue
            d_base, rA = ld
            if d_base is None:
                continue
            deu_base = False
            for toc_cand in toc_list:
                tb = None
                if rA == "r2":                       # um nivel: lwz base, d(r2)
                    tb = read_u32((toc_cand + d_base) & 0xFFFFFFFF)
                else:                                # dois niveis (PIC)
                    mid = _lwz_of(rA)
                    if mid is not None and mid[0] is not None and mid[1] == "r2":
                        midval = read_u32((toc_cand + mid[0]) & 0xFFFFFFFF)
                        if midval is not None:
                            tb = read_u32((midval + d_base) & 0xFFFFFFFF)
                if tb is not None:
                    deu_base = True
                    if tb not in base_candidates:
                        base_candidates.append(tb)
            if deu_base and cand not in r_base_cands:
                r_base_cands.append(cand)
        if not base_candidates:
            continue

        # 4) modo (absoluto vs offsets), um por candidato a registo-base
        offset_modes: list[bool] = []
        for rb in r_base_cands:
            m = any(w.mnemonic == "add"
                    and [x.strip() for x in w.operands.split(",")][0] == rC
                    and rb in [x.strip() for x in w.operands.split(",")][1:]
                    for w in win)
            if m not in offset_modes:
                offset_modes.append(m)
        if not offset_modes:
            offset_modes = [False]
        offset_modes.sort(reverse=True)   # em empate ganha o modo mais especifico

        # 5) escalamento do indice (sinal, nao requisito)
        idx_regs = [r for r in (p[1], p[2]) if r not in r_base_cands]
        index_scaled = any(
            w.mnemonic.startswith("rlwinm")
            and [x.strip() for x in w.operands.split(",")][0] in (idx_regs or [p[1], p[2]])
            for w in win)

        # 6) bound check
        bound_count = None
        for w in reversed(win):
            if w.mnemonic in ("cmplwi", "cmpwi"):
                try:
                    c = int(w.operands.split(",")[-1].strip(), 0)
                except ValueError:
                    c = None
                if c is not None and 0 <= c <= MAX_BOUND_COUNT:
                    bound_count = c
                break
        limit = (bound_count + 1) if bound_count is not None else DEFAULT_SCAN_LIMIT

        best: list[int] = []
        best_base = None
        best_offset = False
        for table_base in base_candidates:
            if table_base is None:
                continue
            for is_offset in offset_modes:
                targets = _decode_targets(read_u32, table_base, is_offset, limit,
                                          text_lo, text_hi)
                if len(targets) > len(best):
                    best, best_base, best_offset = targets, table_base, is_offset
        if not best:
            continue

        cases = sorted(set(best))
        notes: list[str] = []
        if insn.mnemonic != "bctr":
            notes.append(f"terminal condicional `{insn.mnemonic}` (ambiguo: o CTR "
                         f"pode vir de outro caminho)")
        if bound_count is None:
            notes.append("sem bound check (cmplwi/cmpwi) na janela; contagem de "
                         f"casos assumida ({DEFAULT_SCAN_LIMIT})")
        elif len(best) != bound_count + 1:
            notes.append(f"incoerente com o bound check: COUNT={bound_count} "
                         f"(=> {bound_count + 1} casos) mas so' {len(best)} "
                         f"entradas validaram")
        if len(cases) < HIGH_MIN_TARGETS:
            notes.append(f"apenas {len(cases)} alvo(s) distinto(s) "
                         f"(minimo {HIGH_MIN_TARGETS} para `high`)")
        confidence = "high" if not notes else "low"

        out.append(JumpTable(
            dispatch=insn.addr, cases=cases, default="indirect",
            confidence=confidence, notes=notes, mtctr_addr=mtctr_addr,
            table_base=best_base, is_offset=best_offset,
            index_scaled=index_scaled, bound_count=bound_count,
            terminal=insn.mnemonic,
        ))

    out.sort(key=lambda t: t.dispatch)
    return out


# ---------------------------------------------------------------------------
# Saida TOML
# ---------------------------------------------------------------------------

_HEADER = [
    "# Gerado por tools/ps3_analyse.py -- detector de jump tables (XEN-03).",
    "# Formato lido por recomp_config.load_switch_tables (chave [main].switch_table_file);",
    "# fundido com ppu_lifter.discover_jump_tables por merge_switch_tables (config ganha).",
    "# `dispatch` e' o EA do proprio `bctr`, NAO o do `mtctr` anterior.",
    "# `confidence` e' metadado do analisador: o parser ignora-o.",
    "# Regenerar em vez de editar a mao.",
]


def render_toml(tables, include_low: bool = False, source: str | None = None) -> str:
    """Serializa as tabelas. Por DEFAULT so' as `high`: uma tabela `low` no TOML
    de producao e' um salto para endereco invalido a' espera de acontecer."""
    lines = list(_HEADER)
    if source:
        lines.append(f"# Fonte: {source}")
    kept = [t for t in tables if include_low or t.confidence == "high"]
    dropped = len(tables) - len(kept)
    if dropped:
        lines.append(f"# {dropped} tabela(s) de baixa confianca omitida(s) "
                     f"(usar --include-low para as ver).")
    if include_low and any(t.confidence == "low" for t in kept):
        lines.append("# ATENCAO: contem tabelas `low` -- revisao humana OBRIGATORIA "
                     "antes de usar em producao.")
    lines.append("")

    for t in kept:
        modo = "offsets" if t.is_offset else "absoluta"
        base = "?" if t.table_base is None else f"0x{t.table_base:08X}"
        mt = "?" if t.mtctr_addr is None else f"0x{t.mtctr_addr:08X}"
        bound = "nenhum" if t.bound_count is None else str(t.bound_count)
        lines.append(f"# mtctr@{mt} terminal={t.terminal} base={base} "
                     f"modo={modo} bound={bound} idx_escalado={t.index_scaled}")
        for n in t.notes:
            lines.append(f"#   nota: {n}")
        lines.append("[[switch_table]]")
        lines.append(f"dispatch = 0x{t.dispatch:08X}")
        lines.append('default = "indirect"')
        lines.append(f'confidence = "{t.confidence}"')
        lines.append("cases = [")
        for c in t.cases:
            lines.append(f"  0x{c:08X},")
        lines.append("]")
        lines.append("")
    return "\n".join(lines) + "\n"


def write_toml(path, tables, include_low: bool = False, source: str | None = None) -> int:
    """Escreve o TOML e devolve quantas tabelas foram efectivamente escritas."""
    body = render_toml(tables, include_low=include_low, source=source)
    with open(path, "w") as fh:
        fh.write(body)
    return sum(1 for t in tables if include_low or t.confidence == "high")


# ---------------------------------------------------------------------------
# Leitura da imagem (ELF ou raw)
# ---------------------------------------------------------------------------

@dataclass
class Image:
    insns: list
    read_u32: object
    toc: list[int]
    text_lo: int
    text_hi: int


def _seg_reader(seg_map, big_endian=True):
    def read_u32(a):
        for v0, v1, d in seg_map:
            if v0 <= a and a + 4 <= v1:
                return int.from_bytes(d[a - v0:a - v0 + 4],
                                      "big" if big_endian else "little")
        return None
    return read_u32


def load_raw(path, base, toc, code_start=None, code_end=None) -> Image:
    with open(path, "rb") as fh:
        data = fh.read()
    insns = disassemble_bytes(data, base, True)
    seg_map = [(base, base + len(data), data)]
    return Image(insns=insns, read_u32=_seg_reader(seg_map), toc=list(toc or []),
                 text_lo=base if code_start is None else code_start,
                 text_hi=(base + len(data)) if code_end is None else code_end)


def load_elf(path, code_start=None, code_end=None, functions=None) -> Image:
    from elf_parser import ELFFile, PT_LOAD
    elf = ELFFile(path)
    elf.load()
    big_endian = elf.elf_header.big_endian

    insns = []
    exec_lo, exec_hi = None, None
    for ph in elf.program_headers:
        if ph.p_type == PT_LOAD and (ph.p_flags & 1):
            data = elf.get_segment_data(elf.program_headers.index(ph))
            insns.extend(disassemble_bytes(data, ph.p_vaddr, big_endian))
            exec_lo = ph.p_vaddr if exec_lo is None else min(exec_lo, ph.p_vaddr)
            exec_hi = max(exec_hi or 0, ph.p_vaddr + ph.p_filesz)
    seg_map = [(ph.p_vaddr, ph.p_vaddr + ph.p_filesz,
                elf.get_segment_data(elf.program_headers.index(ph)))
               for ph in elf.program_headers
               if ph.p_type == PT_LOAD and ph.p_filesz > 0]
    read_u32 = _seg_reader(seg_map, big_endian)

    # Limites do .text: a lista de funcoes (mesma que o lifter usa) e' a mais
    # fiel; sem ela, os segmentos executaveis.
    if functions:
        import json
        with open(functions) as fh:
            flist = json.load(fh)
        bounds = [(int(str(e["start"]), 0), int(str(e["end"]), 0)) for e in flist]
        text_lo = min(s for s, _ in bounds)
        text_hi = max(e for _, e in bounds)
    else:
        text_lo, text_hi = exec_lo or 0, exec_hi or 0
    if code_start is not None:
        text_lo = code_start
    if code_end is not None:
        text_hi = min(text_hi, code_end)

    # TOC: descritor de entrada; se for bogus (repete o endereco de codigo ou 0),
    # .got + 0x8000 (bias TOC do ELFv1) -- mesmo criterio do ppu_lifter.
    toc = read_u32((elf.elf_header.e_entry + 4) & 0xFFFFFFFF) or 0
    code = read_u32(elf.elf_header.e_entry & 0xFFFFFFFF) or 0
    if toc == code or toc == 0:
        for sec in getattr(elf, "section_headers", []) or []:
            if getattr(sec, "name_str", "") == ".got":
                toc = (sec.sh_addr + 0x8000) & 0xFFFFFFFF
                break
    toc_candidates = [toc] if toc else []
    # Multi-TOC: pares OPD {codigo-no-texto, toc} frequentes nos segmentos de dados.
    from collections import Counter
    freq = Counter()
    for v0, v1, d in seg_map:
        if v0 <= text_lo < v1:
            continue
        for o in range(0, len(d) - 7, 8):
            c = int.from_bytes(d[o:o + 4], "big")
            t = int.from_bytes(d[o + 4:o + 8], "big")
            if text_lo <= c < text_hi and (c & 3) == 0 and v0 <= t < v1 and (t & 3) == 0:
                freq[t] += 1
    for t, n in freq.most_common(8):
        if n >= 64 and t not in toc_candidates:
            toc_candidates.append(t)

    return Image(insns=insns, read_u32=read_u32, toc=toc_candidates,
                 text_lo=text_lo, text_hi=text_hi)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _int0(s):
    return int(s, 0)


def build_parser():
    p = argparse.ArgumentParser(
        prog="ps3_analyse.py",
        description="Detector offline de jump tables PPU (mtctr; bctr) -> TOML "
                    "[[switch_table]] para o --config do ppu_lifter.",
        epilog="Por default so' as tabelas de confianca ALTA sao escritas. "
               "--include-low serve para inspeccao offline: uma tabela errada "
               "faz o boot saltar para lixo, por isso nunca entra em producao "
               "sem revisao humana.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("input", help="ELF PPU (ou blob binario com --raw)")
    p.add_argument("-o", "--out", metavar="TOML",
                   help="ficheiro [[switch_table]] a escrever (default: stdout)")
    p.add_argument("--include-low", action="store_true",
                   help="inclui tambem as tabelas de baixa confianca (offline!)")
    p.add_argument("--raw", action="store_true",
                   help="tratar a entrada como blob sem cabecalho ELF")
    p.add_argument("--base", type=_int0, default=0x10000,
                   help="endereco base do blob --raw (default 0x10000)")
    p.add_argument("--toc", type=_int0, action="append", default=[],
                   help="candidato a TOC (repetivel); obrigatorio em --raw")
    p.add_argument("--code-start", type=_int0, help="inicio do .text para validar alvos")
    p.add_argument("--code-end", type=_int0, help="fim do .text para validar alvos")
    p.add_argument("--functions", metavar="JSON",
                   help="lista de funcoes (mesma do lifter) para limitar o .text")
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    if args.raw:
        img = load_raw(args.input, args.base, args.toc,
                       args.code_start, args.code_end)
        if not img.toc:
            print("ps3_analyse: --raw sem --toc: nenhuma base de tabela e' "
                  "resolvivel, zero tabelas (ver LIMITES no docstring).",
                  file=sys.stderr)
    else:
        img = load_elf(args.input, args.code_start, args.code_end, args.functions)
        if args.toc:
            for t in args.toc:
                if t not in img.toc:
                    img.toc.append(t)

    tables = analyse_instructions(img.insns, img.read_u32, img.toc,
                                  img.text_lo, img.text_hi)
    high = [t for t in tables if t.confidence == "high"]
    low = [t for t in tables if t.confidence == "low"]
    print(f"ps3_analyse: text=[0x{img.text_lo:X},0x{img.text_hi:X}) "
          f"toc={[hex(t) for t in img.toc]} insns={len(img.insns)}", file=sys.stderr)
    print(f"ps3_analyse: {len(tables)} dispatcher(s) com tabela -- "
          f"{len(high)} high, {len(low)} low"
          f"{'' if args.include_low else ' (low OMITIDAS)'}", file=sys.stderr)

    body = render_toml(tables, include_low=args.include_low, source=args.input)
    if args.out:
        with open(args.out, "w") as fh:
            fh.write(body)
        print(f"ps3_analyse: escrito {args.out} "
              f"({len(tables if args.include_low else high)} tabela(s))",
              file=sys.stderr)
    else:
        sys.stdout.write(body)
    return 0


if __name__ == "__main__":
    sys.exit(main())
