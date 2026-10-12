#!/usr/bin/env python3
"""Detector de jump tables offline (tools/ps3_analyse.py) -- suite sintetica.

Porque existe: o `discover_jump_tables` do lifter e' a analise que corre DENTRO
do lift; quando nao chega, hoje a resposta e' um `patch_jumptable_*.py` escrito a
mao. O `ps3_analyse.py` e' a rede offline: acha as tabelas no ELF e DECLARA-as em
TOML `[[switch_table]]`, que o `recomp_config.load_switch_tables` ja' sabe ler e o
`merge_switch_tables` ja' sabe fundir. Esta suite prova o detector sem ELF, sem
SDK e sem rede -- palavras construidas a mao e memoria servida por dicionario.

O criterio que esta suite protege de verdade nao e' "achou a tabela": e' **nao
declarar lixo**. Uma jump table errada nao e' um teste que falha, e' o boot a
saltar para um endereco invalido. Por isso metade dos casos aqui verifica que
uma cadeia fraca fica `low` e **nao sai** no TOML sem `--include-low`.

Reutiliza os construtores de palavras de `test_ppu_jumptables.py` por import
(nao por copia) -- se a codificacao mudar la', muda aqui.

Correr: .venv/bin/python3 tools/test_ps3_analyse_jtables.py
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ppu_disasm
import ps3_analyse
from recomp_config import load_switch_tables
# Reuso deliberado: mesmos construtores da suite do discover_jump_tables.
from test_ppu_jumptables import (TEXT_LO, TEXT_HI, TOC, decode_all, mem_reader,
                                 w_add, w_bctr, w_cmplwi, w_lwz, w_lwzx, w_mtctr)

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ps3_analyse.py")


def w_rlwinm(ra, rs, sh, mb, me):
    return (21 << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) | (me << 1)


def w_bcctr(bo, bi):
    """Forma CONDICIONAL do bcctr (bo=20,bi=0 seria o `bctr` incondicional)."""
    return (19 << 26) | (bo << 21) | (bi << 16) | (528 << 1)


def w_nop():
    return 0x60000000                      # ori r0, r0, 0


FAILS = []


def check(name, cond, detail=""):
    if cond:
        print(f"[an] ok   {name}")
    else:
        print(f"[an] FAIL {name} {detail}")
        FAILS.append(name)


def analyse(words, mem, toc=(TOC,), text_hi=TEXT_HI):
    insns = decode_all(words)
    tables = ps3_analyse.analyse_instructions(insns, mem_reader(mem), list(toc),
                                              TEXT_LO, text_hi)
    return insns, tables


# ---------------------------------------------------------------------------
# 0. Guarda de codificacao
# ---------------------------------------------------------------------------

def test_encodings():
    """Se as palavras novas nao decodificam como esperado, o resto nao vale nada."""
    insns = decode_all([w_rlwinm(9, 9, 2, 0, 29), w_bcctr(12, 2), w_nop()])
    got = [i.mnemonic for i in insns]
    check("encodings: rlwinm/bcctr-condicional/nop",
          got[0] == "rlwinm" and got[1].startswith("b") and got[1].endswith("ctr")
          and got[1] != "bctr" and got[2] == "nop",
          f"got={got}")


# ---------------------------------------------------------------------------
# 1. Caminho feliz: cadeia completa -> high
# ---------------------------------------------------------------------------

def test_high_absoluta():
    """cmplwi + lwz base + lwzx + mtctr + bctr, com a tabela a fechar no bound."""
    words = [w_cmplwi(0, 9, 3), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
             w_mtctr(0), w_bctr()]
    table = 0x200000
    mem = {TOC + 16: table,
           table + 0: 0x11000, table + 4: 0x11004,
           table + 8: 0x11008, table + 12: 0x1100C}   # 4 alvos = count+1
    insns, tables = analyse(words, mem)
    bctr_addr = insns[-1].addr
    check("high: uma tabela encontrada", len(tables) == 1, f"tables={tables}")
    if not tables:
        return
    jt = tables[0]
    check("high: dispatch e' o EA do bctr (nao do mtctr)",
          jt.dispatch == bctr_addr,
          f"dispatch=0x{jt.dispatch:X} bctr=0x{bctr_addr:X} mtctr=0x{insns[-2].addr:X}")
    check("high: alvos correctos",
          jt.cases == [0x11000, 0x11004, 0x11008, 0x1100C], f"cases={jt.cases}")
    check("high: confianca alta", jt.confidence == "high",
          f"confidence={jt.confidence} notes={jt.notes}")
    check("high: default indirect", jt.default == "indirect", f"default={jt.default}")
    check("high: sai no TOML sem --include-low",
          ps3_analyse.render_toml(tables).count("[[switch_table]]") == 1)


# ---------------------------------------------------------------------------
# 2. Terminal bcctr condicional -> detectado, mas nunca high
# ---------------------------------------------------------------------------

def test_bcctr_condicional_e_low():
    """O bcctr condicional e' suportado pelo decoder, portanto NAO se ignora --
    mas um dispatcher condicional e' ambiguo (pode ser um branch de predicado com
    ctr carregado noutro sitio), por isso fica tapado em `low`."""
    words = [w_cmplwi(0, 9, 3), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
             w_mtctr(0), w_bcctr(12, 2)]
    table = 0x200000
    mem = {TOC + 16: table,
           table + 0: 0x11000, table + 4: 0x11004,
           table + 8: 0x11008, table + 12: 0x1100C}
    insns, tables = analyse(words, mem)
    check("bcctr: dispatcher encontrado", len(tables) == 1, f"tables={tables}")
    if not tables:
        return
    jt = tables[0]
    check("bcctr: dispatch e' o EA do bcctr", jt.dispatch == insns[-1].addr)
    check("bcctr: nunca high", jt.confidence == "low",
          f"confidence={jt.confidence} notes={jt.notes}")
    check("bcctr: fora do TOML por default",
          "[[switch_table]]" not in ps3_analyse.render_toml(tables))


# ---------------------------------------------------------------------------
# 3. Backtrack rlwinm (indice escalado) + lwzx
# ---------------------------------------------------------------------------

def test_backtrack_rlwinm():
    """Idioma real do gcc: `rlwinm rIdx,rIdx,2,0,29` (idx*4) antes do lwzx."""
    words = [w_cmplwi(0, 9, 2), w_rlwinm(9, 9, 2, 0, 29), w_lwz(11, 2, 16),
             w_lwzx(0, 9, 11), w_mtctr(0), w_bctr()]
    table = 0x200080
    mem = {TOC + 16: table,
           table + 0: 0x12000, table + 4: 0x12004, table + 8: 0x12008}
    insns, tables = analyse(words, mem)
    check("rlwinm: dispatcher encontrado", len(tables) == 1, f"tables={tables}")
    if not tables:
        return
    jt = tables[0]
    check("rlwinm: alvos correctos",
          jt.cases == [0x12000, 0x12004, 0x12008], f"cases={jt.cases}")
    check("rlwinm: escalamento do indice registado", jt.index_scaled is True,
          f"index_scaled={jt.index_scaled}")
    check("rlwinm: confianca alta", jt.confidence == "high",
          f"confidence={jt.confidence} notes={jt.notes}")


# ---------------------------------------------------------------------------
# 4. Tabela de offsets (base + deslocamento com sinal)
# ---------------------------------------------------------------------------

def test_tabela_de_offsets():
    words = [w_cmplwi(0, 9, 1), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
             w_add(0, 0, 11), w_mtctr(0), w_bctr()]
    table = 0x11000                        # base dentro do .text, idioma real
    mem = {TOC + 16: table, table + 0: 0x100, table + 4: 0x200}
    insns, tables = analyse(words, mem)
    check("offset: dispatcher encontrado", len(tables) == 1, f"tables={tables}")
    if not tables:
        return
    jt = tables[0]
    check("offset: base + deslocamento", jt.cases == [0x11100, 0x11200],
          f"cases={jt.cases}")
    check("offset: modo registado", jt.is_offset is True, f"is_offset={jt.is_offset}")


# ---------------------------------------------------------------------------
# 5. Filtro de confianca: sem bound check -> low -> fora do TOML por default
# ---------------------------------------------------------------------------

def test_filtro_sem_bound_check():
    """Cadeia parcial: sem `cmplwi`/`cmpwi` nao ha' contagem de casos, so' o
    limite de varrimento -- exactamente o caso em que dados que por acaso parecem
    enderecos de .text produzem uma tabela inventada."""
    words = [w_lwz(11, 2, 16), w_lwzx(0, 9, 11), w_mtctr(0), w_bctr()]
    table = 0x200100
    mem = {TOC + 16: table,
           table + 0: 0x13000, table + 4: 0x13004, table + 8: 0x0}
    _, tables = analyse(words, mem)
    check("sem-bound: dispatcher encontrado", len(tables) == 1, f"tables={tables}")
    if not tables:
        return
    jt = tables[0]
    check("sem-bound: confianca baixa", jt.confidence == "low",
          f"confidence={jt.confidence} notes={jt.notes}")
    check("sem-bound: a razao fica escrita",
          any("bound" in n for n in jt.notes), f"notes={jt.notes}")
    check("sem-bound: ZERO tabelas no TOML por default",
          "[[switch_table]]" not in ps3_analyse.render_toml(tables))
    low_toml = ps3_analyse.render_toml(tables, include_low=True)
    check("sem-bound: aparece com --include-low, marcada",
          low_toml.count("[[switch_table]]") == 1 and 'confidence = "low"' in low_toml,
          f"toml={low_toml}")


def test_filtro_bound_inconsistente():
    """O bound diz 4 casos (0..3) mas a 3a entrada nao valida: os dados e a
    verificacao de limites discordam -> nao e' material de producao."""
    words = [w_cmplwi(0, 9, 3), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
             w_mtctr(0), w_bctr()]
    table = 0x200180
    mem = {TOC + 16: table,
           table + 0: 0x14000, table + 4: 0x14004, table + 8: 0x0}
    _, tables = analyse(words, mem)
    check("bound-inconsistente: dispatcher encontrado", len(tables) == 1)
    if not tables:
        return
    jt = tables[0]
    check("bound-inconsistente: confianca baixa", jt.confidence == "low",
          f"confidence={jt.confidence} notes={jt.notes}")
    check("bound-inconsistente: 2 alvos lidos", jt.cases == [0x14000, 0x14004],
          f"cases={jt.cases}")


def test_um_alvo_so_e_low():
    """Um unico alvo valido nao distingue tabela de coincidencia."""
    words = [w_cmplwi(0, 9, 0), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
             w_mtctr(0), w_bctr()]
    table = 0x200200
    mem = {TOC + 16: table, table + 0: 0x15000}
    _, tables = analyse(words, mem)
    check("1-alvo: dispatcher encontrado", len(tables) == 1)
    if not tables:
        return
    check("1-alvo: confianca baixa", tables[0].confidence == "low",
          f"confidence={tables[0].confidence} notes={tables[0].notes}")


def test_sem_alvos_nao_emite_nada():
    """Zero alvos validos = nao ha' tabela nenhuma para declarar."""
    words = [w_cmplwi(0, 9, 3), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
             w_mtctr(0), w_bctr()]
    mem = {TOC + 16: 0x200280, 0x200280: 0xDEADBEEF}
    _, tables = analyse(words, mem)
    check("sem-alvos: nada emitido", tables == [], f"tables={tables}")


def test_sem_lwzx_nao_emite_nada():
    """`mtctr` + `bctr` sem carga indexada e' uma chamada indirecta normal
    (ponteiro de funcao / vtable), nao um switch. Declarar isto seria inventar.

    A fixture e' deliberadamente RICA -- bound check, ponteiro carregado do TOC e
    memoria por tras que parece mesmo uma tabela de alvos validos -- para que
    afrouxar a exigencia do `lwzx` produza uma tabela FALSA em vez de nada. Uma
    fixture pobre passaria com ou sem a exigencia e nao provava nada (medido:
    a primeira versao deste caso sobreviveu a' mutacao "lwzx opcional")."""
    words = [w_cmplwi(0, 9, 2), w_lwz(0, 2, 16), w_mtctr(0), w_bctr()]
    mem = {TOC + 16: 0x200400,
           0x200400: 0x17000, 0x200404: 0x17004, 0x200408: 0x17008}
    _, tables = analyse(words, mem)
    check("sem-lwzx: nada emitido", tables == [], f"tables={tables}")


# ---------------------------------------------------------------------------
# 6. Round-trip: o TOML gerado e' lido pelo parser que ja' existe
# ---------------------------------------------------------------------------

def test_round_trip_load_switch_tables():
    words = [w_cmplwi(0, 9, 3), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
             w_mtctr(0), w_bctr()]
    table = 0x200300
    mem = {TOC + 16: table,
           table + 0: 0x16000, table + 4: 0x16004,
           table + 8: 0x16008, table + 12: 0x1600C}
    insns, tables = analyse(words, mem)
    if not tables:
        check("round-trip: ha' tabela para escrever", False)
        return
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "st.toml")
        n = ps3_analyse.write_toml(out, tables)
        check("round-trip: 1 tabela escrita", n == 1, f"n={n}")
        loaded = load_switch_tables(out)
        check("round-trip: load_switch_tables le 1 entrada", len(loaded) == 1,
              f"loaded={loaded}")
        if not loaded:
            return
        st = loaded[0]
        check("round-trip: dispatch preservado", st.dispatch == insns[-1].addr,
              f"got=0x{st.dispatch:X}")
        check("round-trip: cases preservados",
              st.cases == [0x16000, 0x16004, 0x16008, 0x1600C], f"got={st.cases}")
        check("round-trip: default indirect", st.default == "indirect")
        # `confidence` e' metadado do analisador: tem de estar no ficheiro (para
        # revisao humana) e o parser tem de o IGNORAR sem rebentar.
        with open(out) as fh:
            body = fh.read()
        check("round-trip: confidence presente no ficheiro",
              'confidence = "high"' in body, f"body={body}")


# ---------------------------------------------------------------------------
# 7. CLI: -o e --include-low sobre um blob raw sintetico
# ---------------------------------------------------------------------------

RAW_BASE = 0x10000
RAW_TOC = 0x11000
RAW_CODE_END = 0x10024
RAW_TABLE = 0x11100


def _raw_blob():
    """Blob unico com codigo + TOC + tabela, para o modo --raw da CLI.

    0x10000 cmplwi/lwz/lwzx/mtctr/bctr   0x10014..0x10020 alvos (nops)
    0x11000 TOC (TOC+16 -> 0x11100)      0x11100 tabela de 4 alvos absolutos
    """
    code = [w_cmplwi(0, 9, 3), w_lwz(11, 2, 16), w_lwzx(0, 9, 11),
            w_mtctr(0), w_bctr(), w_nop(), w_nop(), w_nop(), w_nop()]
    blob = bytearray(RAW_TABLE + 16 - RAW_BASE)
    for i, word in enumerate(code):
        off = i * 4
        blob[off:off + 4] = word.to_bytes(4, "big")
    def put(addr, value):
        off = addr - RAW_BASE
        blob[off:off + 4] = (value & 0xFFFFFFFF).to_bytes(4, "big")
    put(RAW_TOC + 16, RAW_TABLE)
    for k, t in enumerate((0x10014, 0x10018, 0x1001C, 0x10020)):
        put(RAW_TABLE + 4 * k, t)
    return bytes(blob)


def _run_cli(args):
    return subprocess.run([sys.executable, TOOL] + args,
                          capture_output=True, text=True)


def test_cli_smoke():
    with tempfile.TemporaryDirectory() as td:
        raw = os.path.join(td, "fixture.bin")
        with open(raw, "wb") as fh:
            fh.write(_raw_blob())
        out = os.path.join(td, "out.toml")
        proc = _run_cli([raw, "--raw", "--base", hex(RAW_BASE),
                         "--toc", hex(RAW_TOC), "--code-end", hex(RAW_CODE_END),
                         "-o", out])
        check("cli: exit 0", proc.returncode == 0,
              f"rc={proc.returncode} stderr={proc.stderr[-800:]}")
        check("cli: ficheiro criado", os.path.isfile(out))
        if not os.path.isfile(out):
            return
        loaded = load_switch_tables(out)
        check("cli: 1 tabela high emitida", len(loaded) == 1, f"loaded={loaded}")
        if loaded:
            check("cli: dispatch = EA do bctr", loaded[0].dispatch == 0x10010,
                  f"got=0x{loaded[0].dispatch:X}")
            check("cli: alvos do blob",
                  loaded[0].cases == [0x10014, 0x10018, 0x1001C, 0x10020],
                  f"got={loaded[0].cases}")

        help_proc = _run_cli(["--help"])
        check("cli: --help documenta -o e --include-low",
              help_proc.returncode == 0 and "--include-low" in help_proc.stdout
              and "-o" in help_proc.stdout,
              f"rc={help_proc.returncode}")


def test_cli_include_low():
    """Sem bound check no blob -> nada por default, uma tabela com --include-low."""
    code = [w_lwz(11, 2, 16), w_lwzx(0, 9, 11), w_mtctr(0), w_bctr(),
            w_nop(), w_nop()]
    blob = bytearray(RAW_TABLE + 16 - RAW_BASE)
    for i, word in enumerate(code):
        blob[i * 4:i * 4 + 4] = word.to_bytes(4, "big")
    def put(addr, value):
        off = addr - RAW_BASE
        blob[off:off + 4] = (value & 0xFFFFFFFF).to_bytes(4, "big")
    put(RAW_TOC + 16, RAW_TABLE)
    put(RAW_TABLE + 0, 0x10010)
    put(RAW_TABLE + 4, 0x10014)
    with tempfile.TemporaryDirectory() as td:
        raw = os.path.join(td, "weak.bin")
        with open(raw, "wb") as fh:
            fh.write(bytes(blob))
        common = [raw, "--raw", "--base", hex(RAW_BASE), "--toc", hex(RAW_TOC),
                  "--code-end", hex(RAW_CODE_END)]
        out_hi = os.path.join(td, "hi.toml")
        proc = _run_cli(common + ["-o", out_hi])
        check("cli-low: exit 0 sem --include-low", proc.returncode == 0,
              f"stderr={proc.stderr[-800:]}")
        check("cli-low: nenhuma tabela por default",
              os.path.isfile(out_hi) and load_switch_tables(out_hi) == [],
              f"loaded={load_switch_tables(out_hi) if os.path.isfile(out_hi) else 'no file'}")
        out_lo = os.path.join(td, "lo.toml")
        proc = _run_cli(common + ["--include-low", "-o", out_lo])
        check("cli-low: exit 0 com --include-low", proc.returncode == 0,
              f"stderr={proc.stderr[-800:]}")
        loaded = load_switch_tables(out_lo) if os.path.isfile(out_lo) else []
        check("cli-low: 1 tabela com --include-low", len(loaded) == 1,
              f"loaded={loaded}")


# ---------------------------------------------------------------------------
# 8. Limites da heuristica ficam ESCRITOS, nao implicitos
# ---------------------------------------------------------------------------

def test_limites_documentados():
    doc = ps3_analyse.__doc__ or ""
    check("docs: docstring do modulo declara os limites",
          "Limites" in doc or "LIMITES" in doc, "sem seccao de limites")
    check("docs: docstring nomeia a janela e o criterio de confianca",
          "WINDOW_INSNS" in doc and "confidence" in doc, f"len(doc)={len(doc)}")
    check("docs: janela e minimo de alvos sao constantes nomeadas",
          isinstance(ps3_analyse.WINDOW_INSNS, int)
          and isinstance(ps3_analyse.HIGH_MIN_TARGETS, int))


def main():
    test_encodings()
    test_high_absoluta()
    test_bcctr_condicional_e_low()
    test_backtrack_rlwinm()
    test_tabela_de_offsets()
    test_filtro_sem_bound_check()
    test_filtro_bound_inconsistente()
    test_um_alvo_so_e_low()
    test_sem_alvos_nao_emite_nada()
    test_sem_lwzx_nao_emite_nada()
    test_round_trip_load_switch_tables()
    test_cli_smoke()
    test_cli_include_low()
    test_limites_documentados()
    print(f"\n[an] {'FAILED: ' + ', '.join(FAILS) if FAILS else 'todos os casos passaram'}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
