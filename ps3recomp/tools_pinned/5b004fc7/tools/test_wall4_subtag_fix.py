#!/usr/bin/env python3
"""D-11.4 (Fase 11, plano 11-02): os tres testes OFFLINE que dao dentes ao fix
candidato B (11-03) ANTES de ele existir.

Contexto: `.planning/phases/11-a-parede-real-o-boot-n-o-entra-no-loop-principal/
11-CONTEXT.md` D-11.4, fundamentado em
`docs/re_sessions/2026-08-03-E1-typewalk-trace.md` (a sonda in-boot que mediu
o mecanismo) e `docs/re_sessions/2026-08-04-E2-quem-forca-low16.md` (a analise
estatica do desserializador). Nenhum dos quatro grupos abaixo depende de
correr o boot; o unico grupo que pode tocar dados do jogo
(`test_deserializer_low16_writes_real_elf_optional`) e' opcional e SKIPa de
forma limpa se `../gow2-recomp/EBOOT.ELF`/`functions.json` nao existirem --
o mesmo padrao de tools/test_ghidra_lookup.py ("sem EBOOT").

O mecanismo, em uma frase (E1 + E2): o desserializador `0x00254C40` forca
`low16 := 1` preservando o `subtag`; para um registo WAD (`subtag=3`) isso
produz `w0 = 0x40030001`; o walk exterior `0x0041FF70` despacha por `low16`
(nunca ve o `subtag`); o push `0x0041F700` le `+0x7C` como cabeca de lista,
mas num registo WAD `+0x7C` e' `matriz[0][3]` (0.0f) -- e a sonda E1 mediu
`sent_head=0` e 179 459 343 iteracoes de um `for` que nunca chega a sentinela.

Sem pytest (convencao tools/test_*.py deste repo -- so' stdlib, asserts
simples + main() que imprime PASS/FAIL por grupo). Ver tools/test_midasm_emit.py
e tools/test_recomp_config.py para o mesmo estilo.

Run: .venv/bin/python3 tools/test_wall4_subtag_fix.py
     python3 tools/test_wall4_subtag_fix.py
"""
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from ppu_disasm import disassemble_bytes  # noqa: E402


# ---------------------------------------------------------------------------
# O detector partilhado pelos grupos 1 e 2 (sintetico e ELF real)
# ---------------------------------------------------------------------------

def find_low16_force_writes(instructions, window: int = 24) -> list:
    """Detecta o padrao de escrita que forca low16 := 1 no desserializador:

        rlwinm rX, rX, 0, 4, 15   -- w0 & 0xFFF0000 (preserva o subtag)
        ...
        ori    rX, rX, 0x1        -- forca low16 := 1
        ...
        stw    rX, 0x0(rY)        -- a escrita em si

    dentro de uma pequena janela de instrucoes uma da outra (no ELF real ha
    2-5 instrucoes nao relacionadas entre cada passo -- ver
    test_deserializer_low16_writes_real_elf_optional; NAO e' um match de 3
    instrucoes consecutivas). Devolve os EAs dos `stw` que fecham um match.
    """
    n = len(instructions)
    matches: list = []
    for i, ins in enumerate(instructions):
        if ins.mnemonic != "rlwinm":
            continue
        parts = [p.strip() for p in ins.operands.split(",")]
        if len(parts) != 5:
            continue
        dest_reg, _src_reg, sh, mb, me = parts
        if not (sh == "0" and mb == "4" and me == "15"):
            continue

        ori_dest = None
        ori_index = None
        for j in range(i + 1, min(i + 1 + window, n)):
            oj = instructions[j]
            if oj.mnemonic != "ori":
                continue
            oparts = [p.strip() for p in oj.operands.split(",")]
            if len(oparts) == 3 and oparts[0] == dest_reg and oparts[2] == "0x1":
                ori_dest, ori_index = oparts[0], j
                break
        if ori_dest is None:
            continue

        for k in range(ori_index + 1, min(ori_index + 1 + window, n)):
            sk = instructions[k]
            if sk.mnemonic != "stw":
                continue
            sparts = [p.strip() for p in sk.operands.split(",")]
            if len(sparts) != 2 or sparts[0] != ori_dest:
                continue
            disp = sparts[1].split("(")[0]
            if disp == "0x0":
                matches.append(sk.addr)
                break

    return matches


def _pack_words(words) -> bytes:
    return b"".join(struct.pack(">I", w & 0xFFFFFFFF) for w in words)


# ori r0,r0,0x0 -- decode() em ppu_disasm.py trata este caso especial como "nop".
NOP = 0x60000000


# ---------------------------------------------------------------------------
# Grupo 1: o desassemblador encontra as duas escritas, buffer sintetico
# ---------------------------------------------------------------------------

def test_deserializer_low16_writes_synthetic() -> None:
    # As palavras EXACTAS medidas nesta sessao (2026-08-04) nos dois sitios de
    # 0x00254C40 que forcam low16 (docs/re_sessions/2026-08-04-E2-quem-forca-
    # low16.md). Verificadas byte a byte contra o ELF real em
    # test_deserializer_low16_writes_real_elf_optional, abaixo.
    words_correct = [
        0x5400011E,  # rlwinm r0, r0, 0, 4, 15    (w0 & 0xFFF0000)
        0x60000001,  # ori    r0, r0, 0x1
        0x90090000,  # stw    r0, 0x0(r9)          <- 1a escrita
        NOP, NOP,
        0x5529011E,  # rlwinm r9, r9, 0, 4, 15
        0x61290001,  # ori    r9, r9, 0x1
        0x912B0000,  # stw    r9, 0x0(r11)         <- 2a escrita
    ]
    base = 0x1000

    insns_correct = disassemble_bytes(_pack_words(words_correct), base)
    matches_correct = find_low16_force_writes(insns_correct)
    assert matches_correct == [base + 0x08, base + 0x1C], (
        f"esperava [0x{base + 0x08:X}, 0x{base + 0x1C:X}], obtive "
        f"{[hex(m) for m in matches_correct]}"
    )

    # Prova por mutacao: inverte o imediato do 1o `ori` de 1 para 2. Um
    # detector que nao verifica de facto o imediato (so' a forma "rlwinm,
    # ori, stw") continuaria a ver 2 matches aqui -- por isso a mutacao TEM
    # de reduzir a contagem.
    words_broken = list(words_correct)
    words_broken[1] = 0x60000002  # ori r0, r0, 0x2 (era 0x1)
    insns_broken = disassemble_bytes(_pack_words(words_broken), base)

    mutation_caught = False
    try:
        matches_broken_reassert = find_low16_force_writes(insns_broken)
        assert matches_broken_reassert == matches_correct
    except AssertionError:
        mutation_caught = True
    assert mutation_caught, (
        "buffer mutado (imediato do ori 1->2) devia derrubar o match, mas "
        "o detector continuou a ver os mesmos 2 matches -- nao e' discriminativo"
    )

    # Expectativa precisa (nao so' "diferente"): so' o 2o bloco (nao mutado)
    # continua a bater; o 1o (mutado) desaparece.
    matches_broken = find_low16_force_writes(insns_broken)
    assert matches_broken == [base + 0x1C], (
        f"esperava so' o 2o bloco [0x{base + 0x1C:X}], obtive "
        f"{[hex(m) for m in matches_broken]}"
    )


# ---------------------------------------------------------------------------
# Grupo 2: corroboracao OPCIONAL contra o ELF real (dados proprietarios)
# ---------------------------------------------------------------------------

def test_deserializer_low16_writes_real_elf_optional() -> None:
    project_root = os.path.dirname(HERE)
    gow2_root = os.path.abspath(os.path.join(project_root, "..", "gow2-recomp"))
    elf_path = os.path.join(gow2_root, "EBOOT.ELF")
    functions_path = os.path.join(gow2_root, "functions.json")

    if not (os.path.exists(elf_path) and os.path.exists(functions_path)):
        print(
            "SKIP: ../gow2-recomp/EBOOT.ELF ou functions.json ausentes -- "
            "corroboracao real-ELF nao corre (dados proprietarios do jogo, "
            "opcional per convencao de tools/test_ghidra_lookup.py; o grupo "
            "sintetico acima e' o que tem de correr e passar sempre)"
        )
        return

    from elf_parser import ELFFile, vaddr_to_offset  # import local, so' aqui

    addr = 0x00254C40
    # Tamanho medido nesta sessao (2026-08-04) via
    # `python3 tools/ghidra_lookup.py ../gow2-recomp/ghidra_out 0x00254C40 --no-c`
    # (`size=1832`). Revalida contra o ghidra_out local, se existir, para nao
    # fossilizar um numero que a ferramenta desmentiria.
    size = 1832
    ghidra_out = os.path.join(gow2_root, "ghidra_out")
    if os.path.exists(ghidra_out):
        cp = subprocess.run(
            [sys.executable, os.path.join(HERE, "ghidra_lookup.py"), ghidra_out,
             f"0x{addr:08X}", "--no-c"],
            capture_output=True, text=True,
        )
        m = re.search(r"size=(\d+)", cp.stdout)
        if m:
            size = int(m.group(1))

    elf = ELFFile(elf_path)
    elf.load()
    off = vaddr_to_offset(elf.program_headers, addr)
    assert off is not None, f"0x{addr:08X} nao mapeado por nenhum PT_LOAD"
    data = elf.raw_data[off:off + size]
    insns = disassemble_bytes(data, addr, elf.elf_header.big_endian)

    matches = find_low16_force_writes(insns)
    assert matches == [0x00254E3C, 0x00255074], (
        f"esperava [0x254E3C, 0x255074] (medidos byte a byte esta sessao com "
        f"tools/show_func.py), obtive {[hex(m) for m in matches]}"
    )


# ---------------------------------------------------------------------------
# Grupo 3: o layout do registo WAD -- +0x7C e' matriz[0][3]
# ---------------------------------------------------------------------------

def test_wad_matrix_offset_is_0x7c() -> None:
    # As 16 escritas de func_0024C1F8, transcritas do decompilado do Ghidra
    # (`python3 tools/ghidra_lookup.py ../gow2-recomp/ghidra_out 0x0024C1F8`,
    # sessao 2026-08-04): puVar1=1.0f global em 0x70/0x84/0x98/0xAC
    # (diagonal); puVar2=0.0f global em todos os outros, incluindo 0x7C.
    stores = [
        (0x70, True), (0x74, False), (0x78, False), (0x7C, False),
        (0x80, False), (0x84, True), (0x88, False), (0x8C, False),
        (0x90, False), (0x94, False), (0x98, True), (0x9C, False),
        (0xA0, False), (0xA4, False), (0xA8, False), (0xAC, True),
    ]
    assert len(stores) == 16, "func_0024C1F8 escreve exactamente 16 campos"

    base = 0x70
    row, col = 0, 3  # h+0x80 = q+0x7C (E1/E2); q = h+4 (ver docs/re_sessions)
    computed = base + row * 0x10 + col * 4
    assert computed == 0x7C, f"matrix[0][3] deveria dar 0x7C, deu 0x{computed:X}"

    offsets = dict(stores)
    assert 0x7C in offsets, "0x7C tem de estar na lista transcrita do decompile"
    assert offsets[0x7C] is False, (
        "matrix[0][3] tem de ser uma escrita do global 0.0f, nao a diagonal 1.0f "
        "-- e' o que faz sent_head=0 quando o push le +0x7C como cabeca de lista"
    )

    diagonal_from_formula = {base + r * 0x10 + r * 4 for r in range(4)}
    assert diagonal_from_formula == {0x70, 0x84, 0x98, 0xAC}, diagonal_from_formula

    diagonal_from_transcription = {off for off, is_diag in stores if is_diag}
    assert diagonal_from_transcription == diagonal_from_formula, (
        "a formula matrix[r][r] e a lista transcrita do decompile tem de "
        "concordar sobre quais offsets sao a diagonal -- um erro de "
        "transcricao num dos dois lados tem de falhar aqui"
    )


# ---------------------------------------------------------------------------
# Grupo 4: o predicado de poda por subtag -- os dentes do fix candidato B
# ---------------------------------------------------------------------------

def prune(w0: int) -> bool:
    """O discriminador exacto que D-11.1 candidato B precisa: um no' foi
    mal-classificado na familia de listas tipo-1 por low16, apesar de ter um
    subtag real diferente. E1 mediu w0=0x40030001 como o pai do laco de
    179 459 343 iteracoes; este predicado NAO pode podar nenhum dos 12 tags
    legitimos que E1 mediu in-boot (low16==subtag, n=172, poda=0 cada)."""
    return (w0 & 0xFFFF) == 1 and ((w0 >> 16) & 0xFFF) != 1


def test_outer_walk_subtag_prune_predicate() -> None:
    # O registo WAD -- a sonda E1 mediu este w0 exacto como o pai do laco que
    # nao termina (docs/re_sessions/2026-08-03-E1-typewalk-trace.md).
    assert prune(0x40030001) is True, "o registo WAD TEM de ser podado"

    # A familia de listas legitima: subtag == low16 == 1 (o caso 169-de-176
    # que E1 mediu). Nunca pode ser podada.
    assert prune(0xC0010001) is False, "a familia de listas legitima nao pode ser podada"

    # Os 12 tags legitimos que E1 MEDIU in-boot ("As 14 chaves" -- 12
    # legitimas + 2 de lixo). Uso deliberado dos numeros de E1 (medidos e
    # duas vezes confirmados, cruzados com o ROADMAP.md), NAO da lista de
    # D-11.4 em CONTEXT.md, que tem um '12' duplicado por lapso de
    # transcricao -- E1 e' a fonte medida, nao a nota que a resume.
    legit_subtags = {3, 4, 9, 15, 16, 17, 18, 20, 21, 23, 25, 32}
    assert len(legit_subtags) == 12, "E1 mediu exactamente 12 tags legitimos"

    # Ancora pelo menos dois nos dois valores w0 literais que E1 imprimiu
    # verbatim (nao so' sinteticos).
    literal_w0 = {0x12: 0xC0120012, 0x15: 0xE0150015}
    for subtag, w0 in literal_w0.items():
        assert subtag in legit_subtags, hex(subtag)
        assert (w0 >> 16) & 0xFFF == subtag, hex(w0)
        assert w0 & 0xFFFF == subtag, hex(w0)
        assert prune(w0) is False, f"subtag 0x{subtag:X} w0=0x{w0:08X} nao pode ser podado"

    # Os restantes, construidos como (flags<<28)|(subtag<<16)|subtag -- o
    # predicado nao inspecciona flags, so' os nibbles altos variam.
    for subtag in legit_subtags:
        flags = 0xC if subtag % 2 else 0xE
        w0 = (flags << 28) | (subtag << 16) | subtag
        assert prune(w0) is False, (
            f"subtag {subtag} (0x{subtag:X}) w0=0x{w0:08X} nao pode ser podado -- "
            f"n=172, poda=0 em 6 corridas E1 (diagnostic OFF), 100% consistente"
        )

    # Prova por mutacao: uma predicado plausivel mas ERRADO (`!=` invertido
    # para `==`) tem de falhar esta mesma suite -- e' o que dá dentes ao fix
    # candidato B ANTES dele ser escrito.
    def prune_mutated(w0: int) -> bool:
        return (w0 & 0xFFFF) == 1 and ((w0 >> 16) & 0xFFF) == 1

    mutation_caught = False
    try:
        assert prune_mutated(0x40030001) is True
        assert prune_mutated(0xC0010001) is False
        for subtag in legit_subtags:
            flags = 0xC if subtag % 2 else 0xE
            w0 = (flags << 28) | (subtag << 16) | subtag
            assert prune_mutated(w0) is False
    except AssertionError:
        mutation_caught = True
    assert mutation_caught, (
        "prune_mutated (!= invertido para ==) devia falhar esta suite, mas "
        "passou -- o predicado nao e' discriminativo, e' uma tautologia"
    )


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main() -> int:
    groups = [
        ("test_deserializer_low16_writes_synthetic", test_deserializer_low16_writes_synthetic),
        ("test_deserializer_low16_writes_real_elf_optional", test_deserializer_low16_writes_real_elf_optional),
        ("test_wad_matrix_offset_is_0x7c", test_wad_matrix_offset_is_0x7c),
        ("test_outer_walk_subtag_prune_predicate", test_outer_walk_subtag_prune_predicate),
    ]

    failed: list = []
    for name, fn in groups:
        try:
            fn()
        except AssertionError as exc:
            print(f"[wall4] FAIL {name}: {exc}")
            failed.append(name)
        except Exception as exc:
            print(f"[wall4] FAIL {name}: erro inesperado {type(exc).__name__}: {exc}")
            failed.append(name)
        else:
            print(f"[wall4] PASS {name}")

    if failed:
        print(f"[wall4] FAILED ({len(failed)}): {', '.join(failed)}")
        return 1

    print(
        "[wall4] todos os grupos PASS -- D-11.4 offline (desserializador "
        "0x00254C40, layout WAD +0x7C=matriz[0][3], predicado de poda por "
        "subtag). Sem esta suite, o fix candidato B (11-03) nao tem dentes."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
