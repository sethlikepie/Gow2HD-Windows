#!/usr/bin/env python3
"""Descoberta de jump tables do PPU (discover_jump_tables).

Porque existe: a funcao estava MORTA no HEAD e a falha era silenciosa. O caller
passa a lista de candidatos a TOC mas o corpo usava `toc` como inteiro
(TypeError) e um segundo bloco lia `base_is_ld`/`disp`, nomes inexistentes
(NameError). A chamada vive dentro de um try/except que so' imprime em stderr,
portanto o lift saia com ZERO dispatchers descobertos e cada switch ficava um
`bctr` indirecto -- com os blocos de case potencialmente nem liftados.

Sem SDK, sem ELF, sem rede: constroi as palavras a mao e serve a memoria por um
dicionario.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ppu_disasm
from ppu_lifter import discover_jump_tables

TEXT_LO, TEXT_HI = 0x10000, 0x20000
TOC = 0x100000


def w_cmplwi(bf, ra, ui):  return (10 << 26) | (bf << 23) | (ra << 16) | (ui & 0xFFFF)
def w_lwz(rt, ra, d):      return (32 << 26) | (rt << 21) | (ra << 16) | (d & 0xFFFF)
def w_lwzx(rt, ra, rb):    return (31 << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (23 << 1)
def w_add(rt, ra, rb):     return (31 << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (266 << 1)
def w_mtctr(rs):
    spr = ((9 & 0x1F) << 5) | ((9 >> 5) & 0x1F)      # CTR = SPR 9, metades trocadas
    return (31 << 26) | (rs << 21) | (spr << 11) | (467 << 1)
def w_bctr():              return (19 << 26) | (20 << 21) | (528 << 1)
def w_cmpwi(bf, ra, si):   return (11 << 26) | (bf << 23) | (ra << 16) | (si & 0xFFFF)
def w_b(off):              return (18 << 26) | (off & 0x03FFFFFC)
def w_ble(bf, off):        return (16 << 26) | (4 << 21) | ((bf * 4 + 1) << 16) | (off & 0xFFFC)
def w_bgt(bf, off):        return (16 << 26) | (12 << 21) | ((bf * 4 + 1) << 16) | (off & 0xFFFC)


def decode_all(words, base=TEXT_LO):
    return [ppu_disasm.decode(w, base + 4 * i) for i, w in enumerate(words)]


def mem_reader(mem):
    def read_u32(a):
        return mem.get(a & 0xFFFFFFFF)
    return read_u32


FAILS = []


def check(name, cond, detail=""):
    if cond:
        print(f"[jt] ok   {name}")
    else:
        print(f"[jt] FAIL {name} {detail}")
        FAILS.append(name)


def test_encodings():
    """Guarda: se as palavras nao decodificam como esperado o resto nao vale nada."""
    insns = decode_all([w_cmplwi(0, 9, 3), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
                        w_mtctr(0), w_bctr()])
    got = [i.mnemonic for i in insns]
    check("encodings", got == ["cmplwi", "lwz", "lwzx", "mtctr", "bctr"], f"got={got}")


def test_one_level_absolute():
    """Idioma gcc de 1 nivel: lwz base,d(r2); lwzx val,idx,base; mtctr; bctr."""
    insns = decode_all([w_cmplwi(0, 9, 3), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
                        w_mtctr(0), w_bctr()])
    table = 0x200000
    mem = {TOC + 16: table,
           table + 0: 0x11000, table + 4: 0x11004, table + 8: 0x11008,
           table + 12: 0x0}          # fora do .text -> pára aqui
    tables = discover_jump_tables(insns, mem_reader(mem), [TOC], TEXT_LO, TEXT_HI)
    bctr_addr = insns[-1].addr
    check("1-nivel: dispatcher encontrado", bctr_addr in tables, f"tables={tables}")
    check("1-nivel: alvos correctos",
          tables.get(bctr_addr) == [0x11000, 0x11004, 0x11008],
          f"got={tables.get(bctr_addr)}")


def test_two_level_indirection():
    """Idioma PIC: lwz mid,d1(r2); lwz base,d2(mid); a tabela vive a 2 saltos."""
    insns = decode_all([w_cmplwi(0, 9, 2), w_lwz(10, 2, 32), w_lwz(11, 10, 8),
                        w_lwzx(0, 9, 11), w_mtctr(0), w_bctr()])
    glob, table = 0x300000, 0x200100
    mem = {TOC + 32: glob, glob + 8: table,
           table + 0: 0x12000, table + 4: 0x12004, table + 8: 0x0}
    tables = discover_jump_tables(insns, mem_reader(mem), [TOC], TEXT_LO, TEXT_HI)
    bctr_addr = insns[-1].addr
    check("2-niveis: dispatcher encontrado", bctr_addr in tables, f"tables={tables}")
    check("2-niveis: alvos correctos",
          tables.get(bctr_addr) == [0x12000, 0x12004],
          f"got={tables.get(bctr_addr)}")


def test_offset_table():
    """Tabela de offsets: entrada = deslocamento com sinal a partir da base."""
    insns = decode_all([w_cmplwi(0, 9, 2), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
                        w_add(0, 0, 11), w_mtctr(0), w_bctr()])
    table = 0x11000          # base dentro do .text, como no idioma real
    mem = {TOC + 16: table,
           table + 0: 0x100, table + 4: 0x200, table + 8: 0xFFFFFFFF}
    tables = discover_jump_tables(insns, mem_reader(mem), [TOC], TEXT_LO, TEXT_HI)
    bctr_addr = insns[-1].addr
    got = tables.get(bctr_addr)
    check("offset: dispatcher encontrado", bctr_addr in tables, f"tables={tables}")
    check("offset: base + deslocamento", got == [0x11100, 0x11200], f"got={got}")


def test_multi_toc():
    """Multi-TOC: o candidato errado le lixo; tem de vencer o que valida alvos."""
    insns = decode_all([w_cmplwi(0, 9, 2), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
                        w_mtctr(0), w_bctr()])
    bad_toc, good_toc, table = 0x400000, TOC, 0x200200
    mem = {bad_toc + 16: 0xDEADBEEF,          # aponta para fora -> 0 alvos
           good_toc + 16: table,
           table + 0: 0x13000, table + 4: 0x13004, table + 8: 0x0}
    tables = discover_jump_tables(insns, mem_reader(mem), [bad_toc, good_toc],
                                  TEXT_LO, TEXT_HI)
    got = tables.get(insns[-1].addr)
    check("multi-TOC: escolhe o candidato que valida alvos",
          got == [0x13000, 0x13004], f"got={got}")


def test_primeiro_operando_nao_ganha_sozinho():
    """O registo-base tem de ser o que VALIDA ALVOS, nao o primeiro que decodifica.

    Bug medido em 2026-07-30, herdado de `11a1c3c` e confirmado independentemente
    nas duas linhagens do upstream (PR #82 de sp00nznet chama-lhe "first
    TOC-loaded operand wins"). O corpo acumulava candidatos a table-base dos DOIS
    operandos do `lwzx` -- correcto -- mas fixava `r_base` no primeiro que
    decodificasse e nunca mais o reconsiderava. Como e' o `r_base` que decide
    `is_offset` (via `add rC, *, r_base`), um `r_base` errado faz a tabela ser
    lida como ABSOLUTA quando e' de OFFSETS: os alvos nao validam e o dispatcher
    inteiro cai em silencio.

    Cenario minimo que o expoe -- ambos os operandos do `lwzx` foram carregados
    do TOC, portanto ambos produzem candidato, mas so' r11 e' a base real (e' ele
    que aparece no `add`):

        lwz   r9,  8(r2)     <- carregado do TOC, mas NAO e' a tabela
        lwz   r11, 16(r2)    <- a base a serio
        lwzx  r0,  r9, r11   <- indice primeiro, base depois
        add   r0,  r0, r11   <- o `add` nomeia r11, nao r9
        mtctr r0 ; bctr

    Com o bug: r_base='r9' -> `add r0,*,r9` nao casa -> is_offset=False ->
    a tabela de offsets e' lida como absoluta -> 0 alvos validos.
    """
    insns = decode_all([w_cmplwi(0, 9, 2), w_lwz(9, 2, 8), w_lwz(11, 2, 16),
                        w_lwzx(0, 9, 11), w_add(0, 0, 11), w_mtctr(0), w_bctr()])
    table = 0x11000
    mem = {
        # Candidato do operando ERRADO: decodifica (por isso o bug fixava-o),
        # mas aponta para dados nao relacionados -> valida 0 alvos em qualquer
        # modo. E' o que acontece na pratica quando o indice tambem vem do TOC.
        TOC + 8:  0x18000,
        0x18000:  0xDEADBEEF,
        0x18004:  0xDEADBEEF,
        # Candidato do operando CERTO: tabela de offsets a serio.
        TOC + 16: table,
        table + 0: 0x100, table + 4: 0x200, table + 8: 0xFFFFFFFF,
    }
    tables = discover_jump_tables(insns, mem_reader(mem), [TOC], TEXT_LO, TEXT_HI)
    bctr_addr = insns[-1].addr
    got = tables.get(bctr_addr)
    check("first-operand: dispatcher NAO se perde quando o 1o operando decodifica",
          bctr_addr in tables, f"tables={tables}")
    check("first-operand: le como tabela de OFFSETS (base+desloc), nao absoluta",
          got == [0x11100, 0x11200], f"got={got}")


def test_toc_escalar_nao_rebenta():
    """Chamadores antigos passam um inteiro (ou None) em vez de lista."""
    insns = decode_all([w_cmplwi(0, 9, 1), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
                        w_mtctr(0), w_bctr()])
    mem = {TOC + 16: 0x200300, 0x200300: 0x14000, 0x200304: 0x0}
    try:
        tables = discover_jump_tables(insns, mem_reader(mem), TOC, TEXT_LO, TEXT_HI)
        ok_int = tables.get(insns[-1].addr) == [0x14000]
    except Exception as exc:                                   # noqa: BLE001
        ok_int = False
        print(f"[jt] excepcao com toc inteiro: {exc!r}")
    check("toc inteiro aceite", ok_int)
    try:
        discover_jump_tables(insns, mem_reader(mem), None, TEXT_LO, TEXT_HI)
        ok_none = True
    except Exception as exc:                                   # noqa: BLE001
        ok_none = False
        print(f"[jt] excepcao com toc None: {exc!r}")
    check("toc None nao rebenta", ok_none)


def test_limite_nao_atravessa_b_incondicional():
    """E162 (2026-09-02): o `cmp` de OUTRO caminho, antes de um `b`, nao e' o limite.

    GoW2 FUN_0043d9b0: a janela de 30 insns antes do bctr continha
    `cmpwi cr7,r0,0 ... b 0x43DECC` de um bloco irmao; o limite real
    (`cmplwi cr7,r0,7; ble 0x43E118`) vive 300 insns antes, no salto que
    ENTRA no dispatcher. count saiu 0, a tabela de 8 ficou com 1 case e os
    outros 7 indices caiam em ps3_indirect_tail -> retorno sem epilogo.
    """
    insns = decode_all([w_cmplwi(7, 9, 3), w_ble(7, 0xC),          # 0,1: limite real -> salta para 4
                        w_cmpwi(7, 0, 0), w_b(0x40),                 # 2,3: bloco irmao, termina em `b`
                        w_lwz(11, 2, 16), w_lwzx(0, 9, 11),          # 4,5: dispatcher
                        w_mtctr(0), w_bctr()])                       # 6,7
    got = [i.mnemonic for i in insns]
    check("E162: encodings", got == ["cmplwi", "ble", "cmpwi", "b", "lwz", "lwzx", "mtctr", "bctr"],
          f"got={got}")
    table = 0x200000
    mem = {TOC + 16: table,
           table + 0: 0x11000, table + 4: 0x11004, table + 8: 0x11008, table + 12: 0x1100C,
           table + 16: 0x0}
    tables = discover_jump_tables(insns, mem_reader(mem), [TOC], TEXT_LO, TEXT_HI)
    got = tables.get(insns[-1].addr)
    check("E162: 4 cases (limite do salto que entra no dispatcher, nao o cmpwi do irmao)",
          got == [0x11000, 0x11004, 0x11008, 0x1100C], f"got={got}")


def test_limite_no_caminho_de_queda_continua_a_valer():
    """Forma `cmplwi; bgt default; [dispatcher]`: o limite do caminho de queda continua a limitar."""
    insns = decode_all([w_cmplwi(7, 9, 1), w_bgt(7, 0x40),
                        w_lwz(11, 2, 16), w_lwzx(0, 9, 11), w_mtctr(0), w_bctr()])
    table = 0x200000
    mem = {TOC + 16: table,
           table + 0: 0x11000, table + 4: 0x11004, table + 8: 0x11008, table + 12: 0x1100C,
           table + 16: 0x0}
    tables = discover_jump_tables(insns, mem_reader(mem), [TOC], TEXT_LO, TEXT_HI)
    got = tables.get(insns[-1].addr)
    check("E162: limite de queda (cmplwi 1 -> 2 cases)", got == [0x11000, 0x11004], f"got={got}")


def main():
    test_encodings()
    test_one_level_absolute()
    test_two_level_indirection()
    test_offset_table()
    test_multi_toc()
    test_primeiro_operando_nao_ganha_sozinho()
    test_toc_escalar_nao_rebenta()
    test_limite_nao_atravessa_b_incondicional()
    test_limite_no_caminho_de_queda_continua_a_valer()
    print(f"\n[jt] {'FAILED: ' + ', '.join(FAILS) if FAILS else 'todos os casos passaram'}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
