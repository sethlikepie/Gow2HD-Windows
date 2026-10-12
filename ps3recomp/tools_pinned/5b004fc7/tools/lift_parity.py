#!/usr/bin/env python3
"""lift_parity.py -- sinais de paridade entre o BINARIO e o LIFT.

Porque existe
-------------
Os gates deste projecto sao binarios: passa ou falha. Um lift pode perder
codigo real e passar toda a gente -- foi o que aconteceu duas vezes em
2026-07-26:

  1. `func_0037C384` foi emitida com 2 instrucoes quando o codigo real tem 12.
     As 10 descartadas continham `bl 0x00379C70`, a chamada que aloca o heap de
     125 MB do jogo. O boot ficava num busy-poll e nenhum gate acusou.
  2. 36 conversoes OPD desapareceram porque as agulhas dos patches deixaram de
     casar. Os patches devolveram `rc=0`, o runner classificou ALREADY-APPLIED
     e o gate ficou verde.

Nos dois casos a informacao estava disponivel: bastava comparar o que o ELF diz
que a funcao faz com o que o lift emitiu. Nenhuma ferramenta o fazia.

A ideia vem do auto-re-agent (github.com/Dryxio/auto-re-agent), que classifica
paridade entre codigo gerado e evidencia binaria em sinais RED/YELLOW/INFO em
vez de um veredicto unico. Aqui os sinais sao adaptados de "codigo C
reconstruido vs decompile" para "lift vs desassembly", que e' o nosso caso.

Sinais
------
  RED    TRUNCATED     o lift emite muito menos instrucoes do que o ELF tem
                       ate' ao terminador real (a classe do func_0037C384)
  RED    NO_BODY       a funcao existe no ELF mas nao no lift
  YELLOW CALL_MISMATCH o ELF tem N chamadas (bl/bctrl) e o lift emite menos
  YELLOW TAIL_JUMP     o corpo acaba em trampolim para a funcao seguinte por
                       endereco, saltando uma lacuna com codigo valido
  INFO   THIN          corpo muito curto sem chamadas nem fluxo (nao e'
                       necessariamente erro: ha' funcoes triviais a serio)

Uso:
  lift_parity.py <ELF> <LIFT_DIR> [--functions FILE] [--strict-exit]
                 [--only-red] [--limit N] [--json OUT]

rc=0 sempre, excepto com --strict-exit, que devolve 1 se houver algum RED.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Terminadores INCONDICIONAIS: depois deles a execucao nao continua na
# instrucao seguinte. Mesma definicao de tools/audit_boundaries.py -- manter
# alinhada, sao a mesma nocao.
UNCOND_TERM = {"b", "ba", "blr", "bctr", "rfid", "rfi"}
# Retornos: fim INEQUIVOCO da funcao. Um `b`/`ba` pode ser salto interno (fim de
# loop, else de um if), por isso e' tratado a parte em elf_spans(). Tratar todo
# o `b` como terminador cortava a funcao no primeiro loop e produzia 36087
# falsos TAIL_JUMP -- medido em 2026-07-26, na primeira calibracao deste script.
RETURN_MNEMONICS = {"blr", "bctr", "rfid", "rfi"}
# Chamadas: gravam LR e voltam. Nao terminam a funcao.
CALL_MNEMONICS = {"bl", "bla", "bctrl", "blrl"}

# Quantas instrucoes o lift pode emitir a menos antes de ser RED. Uma instrucao
# PPC vira tipicamente 1-3 linhas de C, por isso comparamos INSTRUCOES do ELF
# com CHAMADAS+terminadores do lift, nao linhas com linhas.
TRUNCATE_RATIO = 0.5      # emitiu menos de metade do que o ELF tem
TRUNCATE_MIN_GAP = 4      # e pelo menos 4 instrucoes de diferenca


def load_exec_segments(elf_path):
    """[(lo, hi, bytes)] dos PT_LOAD executaveis. Mesmo caminho do audit_boundaries."""
    from elf_parser import ELFFile, PT_LOAD
    elf = ELFFile(elf_path)
    elf.load()          # o construtor nao parseia nada; sem isto vem vazio
    segs = []
    for i, ph in enumerate(elf.program_headers):
        if ph.p_type == PT_LOAD and ph.p_filesz > 0 and (ph.p_flags & 1):
            segs.append((ph.p_vaddr, ph.p_vaddr + ph.p_filesz, elf.get_segment_data(i)))
    if not segs:
        raise SystemExit(f"ERRO: {elf_path} nao tem segmento PT_LOAD executavel")
    return segs


def scan_lift(lift_dir):
    """{ea: {'lines': n, 'calls': n, 'tail_to': ea|None, 'chunk': path}}"""
    out = {}
    # Duas formas de DEFINICAO, porque desde a Fase 19 (XEN-04) o lifter tem
    # duas. A de sempre e' `void func_X(ppu_context* ctx) {`. Para uma funcao
    # declarada em [[functions_override]], com [main].emit_weak_wrappers, o
    # corpo passa a sair em `PPC_FUNC_IMPL(func_X) {` (simbolo __imp_func_X) e
    # a seguir vem um wrapper FRACO `PPC_FUNC(func_X) { __imp_func_X(ctx); }`.
    #
    # MEDIDO, nao previsto: com so' a forma antiga, o primeiro override real
    # (func_00010230, o entry point do guest) fez o gate acusar
    # `[RED] NO_BODY func_00010230 existe no ELF, ausente do lift` e o
    # lift_parity passou de 77 para 78 sinais -- uma "regressao" que era
    # cegueira do parser, nao codigo perdido. Cada migracao da Fase 19 traria
    # mais um.
    #
    # O wrapper NAO entra aqui de proposito: `PPC_FUNC(func_X)` vem DEPOIS do
    # corpo e, se casasse, sobrescrevia a entrada com um corpo de uma linha --
    # trocando NO_BODY por um TRUNCATED igualmente falso. So' o `_IMPL` conta.
    fn_re = re.compile(
        r"^(?:void func_([0-9A-F]{8})\(ppu_context\* ctx\)"
        r"|PPC_FUNC_IMPL\(func_([0-9A-F]{8})\)) \{$")
    call_re = re.compile(r"\b(func_[0-9A-F]{8}\(ctx\)|ps3_call_opd\(|ps3_indirect_call\(|ps3_indirect_tail\()")
    tail_re = re.compile(r"g_trampoline_fn = \(void\(\*\)\(void\*\)\)func_([0-9A-F]{8});")
    for name in sorted(os.listdir(lift_dir)):
        if not (name.startswith("ppu_recomp_") and name.endswith(".cpp")):
            continue
        path = os.path.join(lift_dir, name)
        cur = None
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                m = fn_re.match(line.rstrip("\n"))
                if m:
                    cur = int(m.group(1) or m.group(2), 16)
                    out[cur] = {"lines": 0, "calls": 0, "tail_to": None, "chunk": name}
                    continue
                if cur is None:
                    continue
                if line.startswith("}"):
                    cur = None
                    continue
                rec = out[cur]
                rec["lines"] += 1
                rec["calls"] += len(call_re.findall(line))
                t = tail_re.search(line)
                if t:
                    rec["tail_to"] = int(t.group(1), 16)
    return out


def elf_spans(elf_path, starts, cut_at=None):
    """Para cada start, anda ate' ao primeiro terminador incondicional.

    Devolve {ea: (n_insns, n_calls, end_ea)}. Nao usa o `end` do functions.json
    de proposito: e' precisamente esse `end` que estava errado no bug que este
    sinal existe para apanhar.
    """
    import ppu_disasm
    by_addr = {}
    for lo, _hi, data in load_exec_segments(elf_path):
        for ins in ppu_disasm.disassemble_bytes(data, lo):
            by_addr[ins.addr] = ins
    # AUTORIDADE das fronteiras. Se `cut_at` vier, e' esse conjunto que decide
    # onde uma funcao acaba -- nunca os starts do lift.
    #
    # Porque importa: o lift emite 51917 simbolos para 20750 funcoes reais, ou
    # seja 31167 sao FRAGMENTOS que so' existem no lift. Se forem usados como
    # pontos de corte, 6086 spans reais ficam encurtados (medido em 2026-07-26)
    # -- o script passaria a julgar o ELF com fronteiras vindas do proprio
    # artefacto que quer julgar. Circular: um lift que fragmente demais esconde
    # a sua propria truncagem.
    starts_set = set(cut_at) if cut_at else set(starts)
    ordered = sorted(starts_set)
    import bisect

    def next_start(s):
        i = bisect.bisect_right(ordered, s)
        return ordered[i] if i < len(ordered) else None

    out = {}
    for s in starts:
        if s not in by_addr:
            continue
        nxt = next_start(s)
        n = ncalls = 0
        a = s
        while a in by_addr and n < 0x2000:
            ins = by_addr[a]
            n += 1
            if ins.mnemonic in CALL_MNEMONICS:
                ncalls += 1
            if ins.mnemonic in RETURN_MNEMONICS:
                a += 4
                break                       # retorno: fim inequivoco
            if ins.mnemonic in ("b", "ba"):
                # Um `b` so' termina a funcao se SAIR dela. Dentro do span e'
                # um salto interno (fim de loop, else de um if) e a funcao
                # continua. Tratar todo o `b` como terminador cortava a funcao
                # no primeiro loop e produzia 36087 falsos TAIL_JUMP -- medido.
                tgt = None
                for op in str(ins.operands).replace(",", " ").split():
                    if op.startswith("0x"):
                        try: tgt = int(op, 16)
                        except ValueError: pass
                if tgt is None or tgt < s or (nxt is not None and tgt >= nxt):
                    a += 4
                    break                   # salto para fora: tail call / fim
            a += 4
            if a in starts_set and a != s:
                break                       # encostou na funcao seguinte
        out[s] = (n, ncalls, a)
    return out


def chain_metrics(ea, lift):
    """Soma linhas e chamadas ao longo da CADEIA de trampolins a partir de `ea`.

    O lifter parte uma funcao real em varios `func_*` encadeados por
    `g_trampoline_fn`. Julgar cada fragmento isoladamente contra o span inteiro
    do ELF acusa truncagem em toda a fragmentacao legitima: medido em
    2026-07-26, 1139 falsos TRUNCATED. A unidade comparavel e' a cadeia.

    Guarda de ciclo obrigatoria: ha' cadeias que voltam atras (loops entre
    fragmentos) e sem o `seen` isto nao termina.
    """
    lines = calls = 0
    seen = set()
    cur = ea
    while cur is not None and cur in lift and cur not in seen:
        seen.add(cur)
        rec = lift[cur]
        lines += rec["lines"]
        calls += rec["calls"]
        cur = rec["tail_to"]
    return lines, calls, len(seen)


def classify(ea, lift, elf, use_chain=True):
    """Lista de (nivel, sinal, detalhe) para uma funcao."""
    sig = []
    if ea not in lift:
        sig.append(("RED", "NO_BODY", "existe no ELF, ausente do lift"))
        return sig
    L, E = lift[ea], elf.get(ea)
    if E is None:
        return sig
    n_insn, n_calls, _end = E
    if use_chain:
        c_lines, c_calls, n_frag = chain_metrics(ea, lift)
        L = dict(L, lines=c_lines, calls=c_calls)
    else:
        n_frag = 1

    emitted = L["calls"] + (1 if L["tail_to"] is None else 0)
    if n_insn - L["lines"] >= TRUNCATE_MIN_GAP and L["lines"] < n_insn * TRUNCATE_RATIO:
        sig.append(("RED", "TRUNCATED",
                    f"ELF {n_insn} insns ate' ao terminador, a cadeia emite {L['lines']} linhas"
                    + (f" ({n_frag} fragmentos)" if n_frag > 1 else "")))
    if n_calls > L["calls"]:
        lvl = "RED" if n_calls - L["calls"] >= 4 else "YELLOW"
        sig.append((lvl, "CALL_MISMATCH",
                    f"ELF tem {n_calls} chamadas, a cadeia emite {L['calls']}"))
    # TAIL_JUMP foi REMOVIDO deste script depois de medido: disparava em 35033
    # das 51917 funcoes (67%), porque o lift divide funcoes em fragmentos e o
    # alvo do trampolim raramente coincide com o fim do span. Um sinal que
    # dispara em dois tercos do universo nao informa nada.
    #
    # A pergunta que ele queria fazer -- "o trampolim salta por cima de codigo
    # valido?" -- ja' e' respondida, e melhor, pela invariante I3 do
    # tools/audit_boundaries.py, que compara contra os bounds declarados e
    # verifica se a lacuna descodifica como instrucao. Usar essa:
    #     audit_boundaries.py functions.json --elf EBOOT.ELF --gate I3
    if L["lines"] <= 3 and L["calls"] == 0 and n_insn > 6:
        sig.append(("INFO", "THIN", f"corpo de {L['lines']} linhas para {n_insn} insns"))
    return sig


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("lift_dir")
    ap.add_argument("--functions", help="functions.json (so' para escolher os starts)")
    ap.add_argument("--strict-exit", action="store_true", help="rc=1 se houver RED")
    ap.add_argument("--only-red", action="store_true")
    ap.add_argument("--limit", type=int, default=25, help="quantos mostrar (0=todos)")
    ap.add_argument("--json", help="escreve o relatorio completo aqui")
    a = ap.parse_args()

    lift = scan_lift(a.lift_dir)
    print(f"[parity] lift: {len(lift)} funcoes em {a.lift_dir}", file=sys.stderr)

    starts = sorted(lift)
    cut_at = None
    if a.functions:
        with open(a.functions) as f:
            fl = json.load(f)
        js = {int(str(e["start"]), 0) for e in fl}
        starts = sorted(js | set(lift))
        # O functions.json e' a AUTORIDADE das fronteiras; o lift so' diz o que
        # foi emitido. Sem isto os 31167 fragmentos do lift cortavam 6086 spans
        # reais e o sinal ficava cego a truncagens (ver elf_spans).
        cut_at = js
        print(f"[parity] functions.json: {len(js)} starts (autoridade das fronteiras)",
              file=sys.stderr)
    else:
        print("[parity] AVISO: sem --functions, as fronteiras vem do proprio lift "
              "(circular). Passe --functions para uma medicao valida.", file=sys.stderr)

    elf = elf_spans(a.elf, starts, cut_at)
    print(f"[parity] ELF: {len(elf)} funcoes desassembladas", file=sys.stderr)

    findings = []
    for ea in starts:
        for lvl, name, det in classify(ea, lift, elf):
            findings.append({"ea": f"0x{ea:08X}", "level": lvl, "signal": name,
                             "detail": det, "chunk": lift.get(ea, {}).get("chunk")})

    counts = defaultdict(int)
    for f in findings:
        counts[(f["level"], f["signal"])] += 1

    print("\n=== sinais de paridade lift vs ELF ===")
    for (lvl, name), n in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"  {lvl:<7} {name:<15} {n}")

    show = [f for f in findings if not a.only_red or f["level"] == "RED"]
    show.sort(key=lambda f: {"RED": 0, "YELLOW": 1, "INFO": 2}[f["level"]])
    lim = len(show) if a.limit == 0 else a.limit
    if show:
        print()
        for f in show[:lim]:
            print(f"  [{f['level']}] {f['signal']:<14} func_{f['ea'][2:]}  {f['detail']}")
        if len(show) > lim:
            print(f"  ... +{len(show) - lim}")

    if a.json:
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump({"counts": {f"{k[0]}/{k[1]}": v for k, v in counts.items()},
                       "findings": findings}, fh, indent=1)
        print(f"\n[parity] relatorio em {a.json}", file=sys.stderr)

    n_red = sum(1 for f in findings if f["level"] == "RED")
    print(f"\ntotal: {len(findings)} sinais, {n_red} RED")
    return 1 if (a.strict_exit and n_red) else 0


if __name__ == "__main__":
    sys.exit(main())
