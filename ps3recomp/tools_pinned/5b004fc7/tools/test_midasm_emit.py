#!/usr/bin/env python3
"""Self-check do CONSUMIDOR de `[[midasm_hook]]` no tools/ppu_lifter.py (XEN-01).

Fase 17, plano 17-01. O PARSER ja existia antes desta fase
(`tools/recomp_config.py:27 MidasmHook`, `_parse_midasm_hook`, `load_recomp_config`
ja devolve `midasm_hooks`) e ja rejeita `return` + `jump_address` juntos -- essa
validacao NAO e' reimplementada aqui, e' coberta por tools/test_recomp_config.py.
O que faltava era a EMISSAO: o lifter carregava a seccao e ignorava-a.

O que esta suite fixa:

  1. Emissao ANTES da instrucao (default) -- assercao de ORDEM, nao de substring:
     a chamada tem de aparecer no indice imediatamente anterior a' linha C
     traduzida do EA declarado.
  2. Emissao DEPOIS (`after_instruction = true`), mesma assercao de ordem.
  3. `return = true` -> `return;` imediatamente a seguir a' chamada do hook
     (nas duas colocacoes, antes e depois).
  4. GOLDEN de nao-regressao: sem `--config`, o `.c` gerado numa fixture pequena
     e' BYTE-A-BYTE igual ao ficheiro commitado em
     tools/testdata/midasm_golden_noconfig.c -- que foi capturado com o lifter
     ANTES desta fase (git 4bf3bf8; proveniencia em tools/testdata/README.md,
     porque o golden e' output verbatim e nao pode levar cabecalho). Unica
     normalizacao: a linha `/* lifter-rev: <sha> */`, que e' um carimbo de
     proveniencia por design nao-deterministico (muda a cada commit e ganha
     "-dirty" com a arvore suja) -- ver `_lifter_rev()` em ppu_lifter.py. Tudo
     o resto e' comparado literalmente, por diff, nao por inspeccao.
  5. Propagacao para o lift PARALELO. Sem isto o mid-asm DESAPARECE sob `-j`,
     que e' o pior tipo de defeito: silencioso e dependente da maquina. Duas
     guardas, o mesmo par que tools/test_invalid_instructions.py usa --
     `_worker_init`/`_worker_lift` directos E um `mp.Pool` real via
     `_parallel_lift` (esta segunda e' a unica que morre se o tuplo posicional
     de `initargs` dessincronizar da assinatura de `_worker_init`).
  6. `jump_address` NAO e' implementado na v1 e o lifter RECUSA-SE a liftar
     (rc=2) em vez de emitir a chamada sem o salto. Ver a docstring de
     test_jump_address_is_refused_not_half_implemented para o porque de recusar
     em vez de avisar.
  7. EA orfao (nenhuma instrucao descodificada nesse endereco, ou fora de
     qualquer funcao liftada, ou dentro de um stub HLE cujo corpo e' substituido)
     -> rc=2 com o EA listado. Um hook declarado que nao produz nada tem de ser
     ruidoso; e' o mesmo contrato de recusa do patch_ce03c_introseq_block.py
     (rc=2 quando o corpo gerado diverge do esperado).

Sem pytest (convencao tools/test_*.py deste repo -- so' stdlib, asserts simples
+ main()). Corre com o `.venv` deste repo:

Run: .venv/bin/python3 tools/test_midasm_emit.py
     .venv/bin/python3 tools/test_midasm_emit.py --update-golden   (regenera o
     golden; so' legitimo quando a mudanca de output e' INTENCIONAL e explicada
     no commit -- o golden existe precisamente para tornar isso um acto
     deliberado)
"""
import difflib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

import ppu_lifter as PL
from ppu_disasm import disassemble_bytes
from ppu_lifter import PPULifter

FAILS: list[str] = []

GOLDEN_PATH = Path(TOOLS_DIR) / "testdata" / "midasm_golden_noconfig.c"

# O prefixo do simbolo emitido. Decisao do 17-CONTEXT ("Contrato do hook"):
# `void gow2_midasm_<Name>(ppu_context* ctx)`. O tipo do contexto foi
# CONFIRMADO no codigo ja gerado (`void func_00001000(ppu_context* ctx)`), nao
# assumido.
HOOK_PREFIX = "gow2_midasm_"
CTX_TYPE = "ppu_context"


def check(label: str, cond: bool, detail: str = "") -> None:
    if cond:
        print(f"[midasm] ok   {label}")
    else:
        print(f"[midasm] FAIL {label}  {detail}")
        FAILS.append(label)


def w(*words: int) -> bytes:
    """Empacota palavras de 32 bits big-endian (o PPU aqui e' sempre BE)."""
    return b"".join(struct.pack(">I", x & 0xFFFFFFFF) for x in words)


# Instrucoes sinteticas (nenhuma delas toca em memoria ou em CR, para a linha C
# traduzida ser curta e estavel).
LI_R3_1 = (14 << 26) | (3 << 21) | (0 << 16) | 1     # li r3, 1
LI_R4_2 = (14 << 26) | (4 << 21) | (0 << 16) | 2     # li r4, 2
NOP = (24 << 26)                                      # ori r0,r0,0
BLR = 0x4E800020                                      # bclr 20,0
B_PLUS8 = (18 << 26) | (8 & 0x03FFFFFC)               # b .+8 (AA=0, LK=0)

# Fixture principal (a mesma que gerou o golden). 4 palavras a partir de 0x1000.
FIX_BASE = 0x1000
FIX_WORDS = (LI_R3_1, LI_R4_2, NOP, BLR)
FIX_BYTES = w(*FIX_WORDS)
FIX_END = FIX_BASE + len(FIX_BYTES)
HOOK_EA = FIX_BASE + 4            # o `li r4, 2`
HOOK_EA_LINE = "ctx->gpr[4] = (int64_t)(int32_t)(2);"


class _Hook:
    """Duplo minimo de `recomp_config.MidasmHook` para os testes de API.

    Os testes de API usam isto em vez do dataclass real de proposito: fixa que
    a emissao so' depende de tres atributos (`name`, `after_instruction`,
    `returns`) e nao de nenhum detalhe do parser. Os testes de CLI abaixo usam
    o MidasmHook REAL, via TOML, portanto o tipo verdadeiro tambem esta coberto.
    """

    def __init__(self, name: str, after_instruction: bool = False,
                 returns: bool = False) -> None:
        self.name = name
        self.after_instruction = after_instruction
        self.returns = returns


def _lift_fixture(hooks_by_ea: dict | None = None,
                  data: bytes = FIX_BYTES, base: int = FIX_BASE,
                  end: int | None = None) -> list[str]:
    """Lift da fixture pela API directa; devolve body_lines."""
    lifter = PPULifter()
    if hooks_by_ea is not None:
        lifter.midasm_hooks_by_ea = hooks_by_ea
    insns = disassemble_bytes(data, base, True)
    f = lifter.lift_function(insns, base, end if end is not None else base + len(data))
    return f.body_lines


def _index_of(lines: list[str], needle: str) -> int:
    for i, line in enumerate(lines):
        if needle in line:
            return i
    return -1


def _normalize_lift(src: str) -> str:
    """Neutraliza a UNICA linha nao-determinista do output: o carimbo de
    proveniencia `/* lifter-rev: <sha>[-dirty] */` (ppu_lifter._lifter_rev, que
    corre `git rev-parse` e marca "-dirty" com a arvore suja). Tudo o resto do
    ficheiro entra no diff literalmente."""
    return re.sub(r"/\* lifter-rev: [^*]*\*/", "/* lifter-rev: <normalizado> */", src)


# [main] minimo que qualquer config.toml precisa (schema da Task A1). Nenhuma
# destas tres chaves e' lida por este CLI (ver o help do --config); os caminhos
# nunca sao abertos, portanto nao precisam de existir.
_DUMMY_MAIN_TOML = """
[main]
elf_path = "unused.elf"
functions_json = "unused.json"
out_directory = "unused_out"
"""


def _run_lifter_cli(input_bytes: bytes = FIX_BYTES,
                    config_toml: str | None = None,
                    functions_bounds: tuple[tuple[int, int], ...] = ((FIX_BASE, FIX_END),),
                    base: int = FIX_BASE,
                    extra_args: tuple[str, ...] = ()
                    ) -> tuple["subprocess.CompletedProcess[str]", str | None]:
    """Corre o `ppu_lifter.py` REAL num tmpdir. `config_toml=None` -> sem
    `--config` de todo (o caminho do golden). `--allow-jt-failure` e' sempre
    passado: numa fixture sintetica a descoberta de jump tables falhar e'
    esperado e irrelevante para o que se mede aqui (sem a flag, main() faria
    sys.exit(3) por uma razao nao relacionada). `--jobs 1` mantem o run serial
    e deterministico -- o caminho paralelo tem guardas proprias abaixo.

    Devolve (CompletedProcess, texto_do_.c_gerado_ou_None).
    """
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        input_path = tmp / "input.bin"
        input_path.write_bytes(input_bytes)

        functions_path = tmp / "functions.json"
        functions_path.write_text(json.dumps(
            [{"start": s, "end": e} for s, e in functions_bounds]))

        argv = [sys.executable, os.path.join(TOOLS_DIR, "ppu_lifter.py"),
                str(input_path), "--raw", "--base", hex(base),
                "--functions", str(functions_path), "--single-file",
                "--output", str(tmp / "out"), "--jobs", "1",
                "--allow-jt-failure", *extra_args]
        if config_toml is not None:
            config_path = tmp / "cfg.toml"
            config_path.write_text(config_toml, encoding="utf-8")
            argv[3:3] = ["--config", str(config_path)]

        proc = subprocess.run(argv, capture_output=True, text=True, timeout=120)
        src_path = tmp / "out" / "ppu_recomp.c"
        src = src_path.read_text() if src_path.is_file() else None
        return proc, src


# --------------------------------------------------------------------------- #
# 1-3: emissao (antes / depois / return)
# --------------------------------------------------------------------------- #

def test_before_hook_lands_immediately_before_the_translated_line() -> None:
    """`after_instruction` omitido (default false) -> a chamada aparece EXACTAMENTE
    uma vez e no indice imediatamente ANTERIOR a' linha C do EA declarado.
    Assercao de posicao, nao de substring: um hook emitido no sitio errado
    passaria num teste de substring e e' precisamente o defeito que o contrato de
    recusa do 17-CONTEXT proibe."""
    lines = _lift_fixture({HOOK_EA: [_Hook("TestHook")]})
    call = f"{HOOK_PREFIX}TestHook(ctx);"
    check("hook emitido exactamente uma vez",
          sum(call in l for l in lines) == 1, lines)
    i_hook = _index_of(lines, call)
    i_insn = _index_of(lines, HOOK_EA_LINE)
    check("hook imediatamente ANTES da linha traduzida do EA",
          i_hook >= 0 and i_insn == i_hook + 1, f"{i_hook=} {i_insn=} {lines}")
    check("a chamada usa o prefixo exacto gow2_midasm_",
          any(l.strip().startswith(HOOK_PREFIX) for l in lines), lines)


def test_after_instruction_true_lands_immediately_after() -> None:
    """`after_instruction = true` -> indice imediatamente POSTERIOR."""
    lines = _lift_fixture({HOOK_EA: [_Hook("AfterHook", after_instruction=True)]})
    i_hook = _index_of(lines, f"{HOOK_PREFIX}AfterHook(ctx);")
    i_insn = _index_of(lines, HOOK_EA_LINE)
    check("hook imediatamente DEPOIS da linha traduzida do EA",
          i_hook >= 0 and i_hook == i_insn + 1, f"{i_hook=} {i_insn=} {lines}")


def test_hook_does_not_replace_the_instruction() -> None:
    """Regra do 17-CONTEXT: o hook corre AO LADO da instrucao, nao no lugar dela
    (substituir a funcao inteira e' weak override, Fase 19). As tres restantes
    linhas traduzidas tambem tem de sobreviver intactas."""
    for hook in (_Hook("TestHook"), _Hook("AfterHook", after_instruction=True)):
        lines = _lift_fixture({HOOK_EA: [hook]})
        joined = "\n".join(lines)
        check(f"{hook.name}: a instrucao ancorada continua traduzida",
              HOOK_EA_LINE in joined, joined)
        check(f"{hook.name}: as outras instrucoes continuam intactas",
              "ctx->gpr[3] = (int64_t)(int32_t)(1);" in joined
              and "/* nop */" in joined and "return;" in joined, joined)


def test_return_true_emits_return_right_after_the_hook_call() -> None:
    """`return = true` (chave TOML `return`, campo Python `returns`) -> `return;`
    na linha SEGUINTE a' chamada, nas duas colocacoes. Verificado por indice
    adjacente: um `return;` que ja existisse noutro sitio do corpo (o blr final
    emite um) faria passar um teste de substring."""
    for after in (False, True):
        lbl = "after=True" if after else "after=False"
        lines = _lift_fixture(
            {HOOK_EA: [_Hook("ReturnHook", after_instruction=after, returns=True)]})
        i_hook = _index_of(lines, f"{HOOK_PREFIX}ReturnHook(ctx);")
        check(f"{lbl}: hook presente", i_hook >= 0, lines)
        if i_hook >= 0:
            check(f"{lbl}: `return;` imediatamente a seguir a' chamada",
                  i_hook + 1 < len(lines) and lines[i_hook + 1].strip() == "return;",
                  f"{i_hook=} {lines}")


def test_returns_false_does_not_inject_a_return() -> None:
    """Guarda simetrica da anterior: sem `return = true`, a linha a seguir ao
    hook e' a instrucao traduzida, nao um `return;` inventado."""
    lines = _lift_fixture({HOOK_EA: [_Hook("TestHook")]})
    i_hook = _index_of(lines, f"{HOOK_PREFIX}TestHook(ctx);")
    check("sem `return=true` nao ha `return;` injectado a seguir ao hook",
          i_hook >= 0 and lines[i_hook + 1].strip() != "return;", lines)


def test_multiple_hooks_on_the_same_ea_keep_declaration_order() -> None:
    """Varios hooks no MESMO EA sao uma lista, na ordem do TOML -- last-wins
    (a regra dos duplicados de switch_table/invalid_instructions) perderia
    hooks. Os `before` saem por ordem antes da instrucao, os `after` por ordem
    depois."""
    hooks = [_Hook("First"), _Hook("Second"),
             _Hook("Third", after_instruction=True),
             _Hook("Fourth", after_instruction=True)]
    lines = _lift_fixture({HOOK_EA: hooks})
    idx = [_index_of(lines, f"{HOOK_PREFIX}{n}(ctx);")
           for n in ("First", "Second", "Third", "Fourth")]
    i_insn = _index_of(lines, HOOK_EA_LINE)
    check("os 4 hooks foram emitidos", all(i >= 0 for i in idx), f"{idx} {lines}")
    check("ordem de declaracao preservada e a instrucao no meio",
          idx == sorted(idx) and idx[1] < i_insn < idx[2],
          f"{idx=} {i_insn=} {lines}")


def test_label_stays_before_the_before_hook() -> None:
    """O `loc_XXXX:` continua a marcar a entrada REAL do bloco: um `goto` que
    salte para o EA ancorado tem de passar pelo hook, nao salta-lo. Fixture com
    um `b .+8` para o EA ancorado."""
    base = 0x2000
    data = w(B_PLUS8, LI_R3_1, LI_R4_2, BLR)
    lines = _lift_fixture({0x2008: [_Hook("TestHook")]}, data=data, base=base)
    i_label = _index_of(lines, "loc_00002008:")
    i_hook = _index_of(lines, f"{HOOK_PREFIX}TestHook(ctx);")
    i_insn = _index_of(lines, HOOK_EA_LINE)
    check("label do bloco antes do hook, hook antes da instrucao",
          0 <= i_label < i_hook < i_insn, f"{i_label=} {i_hook=} {i_insn=} {lines}")


def test_after_hook_does_not_flip_the_terminator_detection() -> None:
    """Um hook `after_instruction` no ULTIMO `blr` nao pode fazer o lifter
    concluir que a funcao cai para a seguinte: `_last_line_is_terminator` olha
    para a ultima linha do corpo, e uma chamada de hook nao e' um terminador.
    Sem cuidado, declarar um hook mudava o fluxo de controlo da funcao -- uma
    alteracao de comportamento que ninguem pediu."""
    baseline = PPULifter()
    insns = disassemble_bytes(FIX_BYTES, FIX_BASE, True)
    f0 = baseline.lift_function(insns, FIX_BASE, FIX_END)
    lifter = PPULifter()
    lifter.midasm_hooks_by_ea = {FIX_END - 4: [_Hook("TailHook", after_instruction=True)]}
    f1 = lifter.lift_function(insns, FIX_BASE, FIX_END)
    check("fixture sanity: sem hook a funcao termina em blr (sem fallthrough)",
          not f0.fallthrough_to, f0.fallthrough_to)
    check("com hook `after` no blr o fallthrough_to nao muda",
          f1.fallthrough_to == f0.fallthrough_to,
          f"{f1.fallthrough_to} vs {f0.fallthrough_to}")


# --------------------------------------------------------------------------- #
# 4: golden de nao-regressao
# --------------------------------------------------------------------------- #

def test_api_without_hooks_is_identical_to_the_no_hook_baseline() -> None:
    """Nivel API: `midasm_hooks_by_ea` vazio produz body_lines identicos a um
    lifter que nunca ouviu falar de mid-asm."""
    a = _lift_fixture(None)
    b = _lift_fixture({})
    check("corpo identico com dict vazio vs sem sequer atribuir", a == b, f"{a}\n{b}")
    check("zero simbolos gow2_midasm_ no corpo",
          not any(HOOK_PREFIX in l for l in a), a)


def _golden_diff(src: str) -> str:
    want = _normalize_lift(GOLDEN_PATH.read_text())
    got = _normalize_lift(src)
    return "".join(difflib.unified_diff(
        want.splitlines(keepends=True), got.splitlines(keepends=True),
        fromfile=str(GOLDEN_PATH), tofile="lift actual"))


def test_golden_no_config_output_is_byte_identical() -> None:
    """O aceite inegociavel do 17-CONTEXT: SEM `--config`, o `.c` gerado e'
    byte-a-byte igual ao golden capturado antes desta fase, provado por diff.
    Se este teste falhar, a emissao vazou para o caminho sem config."""
    proc, src = _run_lifter_cli(config_toml=None)
    check("CLI sem --config sai 0", proc.returncode == 0,
          f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    check("golden existe (regenerar com --update-golden)", GOLDEN_PATH.is_file(),
          str(GOLDEN_PATH))
    if src is not None and GOLDEN_PATH.is_file():
        d = _golden_diff(src)
        check("diff vazio contra tools/testdata/midasm_golden_noconfig.c",
              d == "", d[:4000])
        check("zero simbolos gow2_midasm_ no output sem --config",
              HOOK_PREFIX not in src, src[:500])


def test_golden_holds_with_config_that_has_no_midasm_section() -> None:
    """`--config` presente mas sem `[[midasm_hook]]` tambem nao pode mexer no
    `.c` gerado (so' no stdout, que nao entra no golden)."""
    proc, src = _run_lifter_cli(config_toml=_DUMMY_MAIN_TOML)
    check("CLI com --config sem midasm sai 0", proc.returncode == 0,
          f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    if src is not None and GOLDEN_PATH.is_file():
        d = _golden_diff(src)
        check("diff vazio contra o golden (config sem seccao midasm)",
              d == "", d[:4000])


def test_cli_with_midasm_emits_the_call_and_reports_the_count() -> None:
    """Fim-a-fim pelo CLI com o MidasmHook real (via TOML): a chamada aparece no
    `.c`, na posicao certa, e o CLI diz quantos hooks carregou (uma seccao com
    typo tem de ser visivel, nao silenciosa -- mesma convencao do
    invalid_instructions)."""
    cfg = _DUMMY_MAIN_TOML + textwrap.dedent(f"""
        [[midasm_hook]]
        name = "TestHook"
        address = {HOOK_EA}
    """)
    proc, src = _run_lifter_cli(config_toml=cfg)
    check("CLI com midasm sai 0", proc.returncode == 0,
          f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    check("CLI reporta a contagem de hooks carregados",
          "1 midasm_hook(s)" in proc.stdout, proc.stdout)
    if src is not None:
        lines = src.splitlines()
        i_hook = _index_of(lines, f"{HOOK_PREFIX}TestHook(ctx);")
        i_insn = _index_of(lines, HOOK_EA_LINE)
        check("chamada emitida no .c gerado, antes da instrucao ancorada",
              i_hook >= 0 and i_insn == i_hook + 1, f"{i_hook=} {i_insn=}")
        check("o .c gerado declara o hook (senao nao compila: chamada a funcao "
              f"nao declarada). Assinatura: void {HOOK_PREFIX}<Name>({CTX_TYPE}* ctx)",
              f"void {HOOK_PREFIX}TestHook({CTX_TYPE}* ctx);" in src,
              "\n".join(l for l in lines if HOOK_PREFIX in l))


# --------------------------------------------------------------------------- #
# 5: propagacao para o lift paralelo
# --------------------------------------------------------------------------- #

def test_worker_init_and_worker_lift_thread_midasm_hooks() -> None:
    """Guarda 1/2: `_worker_init` guarda os hooks no `_WORKER_STATE` e
    `_worker_lift` monta-os no lifter do worker. Apagar qualquer um dos dois
    parte isto."""
    hooks = {HOOK_EA: [_Hook("WorkerHook")]}
    PL._worker_init(segs=[(FIX_BASE, FIX_BYTES)], big_endian=True, name_map={},
                    prefix="", midasm_hooks_by_ea=hooks)
    check("_worker_init guarda midasm_hooks_by_ea no _WORKER_STATE",
          PL._WORKER_STATE.get("midasm_hooks_by_ea") == hooks,
          PL._WORKER_STATE.get("midasm_hooks_by_ea"))
    _idx0, results, _ct, _bt = PL._worker_lift((0, [(FIX_BASE, FIX_END)]))
    check("_worker_lift produziu exactamente uma funcao", len(results) == 1, len(results))
    if results:
        body = results[0][3]
        i_hook = _index_of(body, f"{HOOK_PREFIX}WorkerHook(ctx);")
        i_insn = _index_of(body, HOOK_EA_LINE)
        check("caminho worker emite o hook na posicao certa",
              i_hook >= 0 and i_insn == i_hook + 1, f"{i_hook=} {i_insn=} {body}")


def test_parallel_lift_real_pool_threads_midasm_hooks() -> None:
    """Guarda 2/2, a que importa: `mp.Pool` REAL via `_parallel_lift`. Executa
    literalmente o tuplo posicional de `initargs`; apagar os hooks desse tuplo,
    ou dessincroniza-lo da ordem dos parametros de `_worker_init`, parte este
    teste -- ao contrario da chamada directa acima, que contorna a fonte do
    `_parallel_lift`. E' esta a guarda contra "o mid-asm desaparece sob -j"."""
    lifter = PPULifter()
    lifter.midasm_hooks_by_ea = {HOOK_EA: [_Hook("PoolHook")]}
    PL._parallel_lift(lifter, [(FIX_BASE, FIX_END)], [(FIX_BASE, FIX_BYTES)], True, 1)
    check("lift paralelo (Pool real, jobs=1) produziu uma funcao",
          len(lifter.functions) == 1, len(lifter.functions))
    if lifter.functions:
        body = lifter.functions[0].body_lines
        i_hook = _index_of(body, f"{HOOK_PREFIX}PoolHook(ctx);")
        i_insn = _index_of(body, HOOK_EA_LINE)
        check("caminho paralelo emite o hook na posicao certa",
              i_hook >= 0 and i_insn == i_hook + 1, f"{i_hook=} {i_insn=} {body}")


# --------------------------------------------------------------------------- #
# 6: jump_address nao implementado
# --------------------------------------------------------------------------- #

def test_jump_address_is_refused_not_half_implemented() -> None:
    """`jump_address` NAO e' implementado na v1 (17-CONTEXT, seccao Deferred:
    "Deixar declarado como nao-implementado em vez de meio-implementado").

    A escolha e' RECUSAR (rc=2), nao avisar-e-continuar. Avisar deixaria no
    disco um lift que compila, corre, chama o hook e NAO salta -- exactamente a
    surpresa "declarado mas sem efeito" que o proprio recomp_config.py ja
    rejeita com erro no `default` do [[switch_table]] ("the whole point of this
    task is to stop this class of 'declared but had no effect' surprise").
    Seguir esse precedente do repo e' mais seguro do que um warning que o
    utilizador nao le no meio de um lift de 20 minutos."""
    cfg = _DUMMY_MAIN_TOML + textwrap.dedent(f"""
        [[midasm_hook]]
        name = "JumpHook"
        address = {HOOK_EA}
        jump_address = {HOOK_EA + 4}
    """)
    proc, src = _run_lifter_cli(config_toml=cfg)
    check("CLI RECUSA (rc=2) um hook com jump_address", proc.returncode == 2,
          f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    check("a mensagem nomeia jump_address e o hook",
          "jump_address" in proc.stderr and "JumpHook" in proc.stderr, proc.stderr)
    check("nao gerou lift nenhum (recusa antes de emitir)", src is None, "gerou .c")


def test_jump_address_never_invents_a_goto_at_api_level() -> None:
    """Mesma regra ao nivel da API: `build_midasm_index` levanta em vez de
    devolver um indice que emitiria a chamada sem o salto."""
    hook = _Hook("JumpHook")
    hook.address = HOOK_EA
    hook.jump_address = HOOK_EA + 4
    raised = None
    try:
        PL.build_midasm_index([hook])
    except PL.MidasmConfigError as exc:
        raised = str(exc)
    check("build_midasm_index levanta MidasmConfigError com jump_address",
          raised is not None and "jump_address" in raised, raised)


# --------------------------------------------------------------------------- #
# 7: EA orfao e duplicados
# --------------------------------------------------------------------------- #

def test_orphan_address_fails_loudly_via_cli() -> None:
    """EA declarado que nao casa com nenhuma instrucao descodificada -> rc=2 e o
    EA listado. Um hook declarado que nao produz nada nao pode ser um no-op
    silencioso (contrato de recusa do patch_ce03c_introseq_block.py, rc=2)."""
    cfg = _DUMMY_MAIN_TOML + textwrap.dedent("""
        [[midasm_hook]]
        name = "OrphanHook"
        address = 0x00009999
    """)
    proc, src = _run_lifter_cli(config_toml=cfg)
    check("CLI falha (rc=2) com um EA orfao", proc.returncode == 2,
          f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    check("a mensagem lista o EA orfao e o nome do hook",
          "0x00009999" in proc.stderr and "OrphanHook" in proc.stderr, proc.stderr)
    check("nao escreveu lift nenhum", src is None, "gerou .c")


def test_orphan_detection_distinguishes_the_three_ways_a_hook_dies() -> None:
    """`check_midasm_coverage` a nivel de unidade. Tres maneiras de um hook
    declarado nunca chegar ao output, todas silenciosas sem esta guarda:
      (a) nenhuma instrucao descodificada nesse endereco (EA errado / desalinhado);
      (b) endereco fora de qualquer funcao liftada (nao entra em nenhum
          lift_function);
      (c) endereco dentro de um stub de import HLE, cujo corpo `lift_function`
          substitui inteiro por `ps3_hle_call` antes de olhar para instrucoes.
    E o caso legitimo (d) nao pode dar falso positivo."""
    insn_addrs = {FIX_BASE, FIX_BASE + 4, FIX_BASE + 8, FIX_BASE + 12,
                  0x5000, 0x8000}
    bounds = [(FIX_BASE, FIX_END), (0x8000, 0x8020)]
    hle = {0x8000}

    def orphans(ea):
        return PL.check_midasm_coverage({ea: [_Hook("H")]}, insn_addrs, bounds,
                                        hle_stub_starts=hle)

    a = orphans(FIX_BASE + 2)
    check("(a) EA sem instrucao descodificada e' orfao",
          len(a) == 1 and "instru" in a[0][1].lower(), a)
    b = orphans(0x5000)
    check("(b) EA fora de qualquer funcao liftada e' orfao",
          len(b) == 1 and "fun" in b[0][1].lower(), b)
    c = orphans(0x8000)
    check("(c) EA dentro de um stub HLE e' orfao (o corpo e' substituido)",
          len(c) == 1 and "hle" in c[0][1].lower(), c)
    d = orphans(HOOK_EA)
    check("(d) EA legitimo NAO e' reportado como orfao", d == [], d)
    check("sem hooks nenhuns nao ha orfaos",
          PL.check_midasm_coverage({}, insn_addrs, bounds) == [], "")


def test_duplicate_name_and_address_warns_and_emits_once() -> None:
    """Duplicado exacto (mesmo `name` E mesmo `address`) e' um erro de escrita
    do TOML: emitir a chamada duas vezes correria o hook duas vezes. Segue a
    regra ja estabelecida para duplicados (`merge_switch_tables`): WARN, nunca
    silencioso -- mas aqui descarta em vez de last-wins, porque com hooks
    "last-wins" e "manter os dois" nao sao equivalentes e correr o efeito
    secundario a dobrar e' o resultado pior."""
    h1 = _Hook("Dup")
    h1.address = HOOK_EA
    h1.jump_address = None
    h2 = _Hook("Dup")
    h2.address = HOOK_EA
    h2.jump_address = None
    warnings: list[str] = []
    idx = PL.build_midasm_index([h1, h2], warn=warnings.append)
    check("o duplicado exacto foi descartado (uma so' entrada no EA)",
          len(idx.get(HOOK_EA, [])) == 1, idx)
    check("e foi AVISADO, nao descartado em silencio",
          len(warnings) == 1 and "Dup" in warnings[0], warnings)


def test_same_ea_different_names_is_not_a_duplicate() -> None:
    """Guarda simetrica: dois hooks DIFERENTES no mesmo EA sao legitimos (o
    caso multi-hook) e nao podem ser confundidos com o duplicado acima."""
    hooks = []
    for n in ("A", "B"):
        h = _Hook(n)
        h.address = HOOK_EA
        h.jump_address = None
        hooks.append(h)
    warnings: list[str] = []
    idx = PL.build_midasm_index(hooks, warn=warnings.append)
    check("dois nomes distintos no mesmo EA -> lista de 2, sem aviso",
          len(idx.get(HOOK_EA, [])) == 2 and warnings == [], f"{idx} {warnings}")


# --------------------------------------------------------------------------- #
# regeneracao do golden
# --------------------------------------------------------------------------- #

def _update_golden() -> int:
    proc, src = _run_lifter_cli(config_toml=None)
    if proc.returncode != 0 or src is None:
        print(f"nao consegui gerar o lift: rc={proc.returncode}\n"
              f"{proc.stdout}\n{proc.stderr}", file=sys.stderr)
        return 1
    GOLDEN_PATH.parent.mkdir(parents=True, exist_ok=True)
    GOLDEN_PATH.write_text(src)
    print(f"golden reescrito: {GOLDEN_PATH} ({len(src)} bytes)")
    return 0


def main() -> int:
    if "--update-golden" in sys.argv:
        return _update_golden()

    test_before_hook_lands_immediately_before_the_translated_line()
    test_after_instruction_true_lands_immediately_after()
    test_hook_does_not_replace_the_instruction()
    test_return_true_emits_return_right_after_the_hook_call()
    test_returns_false_does_not_inject_a_return()
    test_multiple_hooks_on_the_same_ea_keep_declaration_order()
    test_label_stays_before_the_before_hook()
    test_after_hook_does_not_flip_the_terminator_detection()

    test_api_without_hooks_is_identical_to_the_no_hook_baseline()
    test_golden_no_config_output_is_byte_identical()
    test_golden_holds_with_config_that_has_no_midasm_section()
    test_cli_with_midasm_emits_the_call_and_reports_the_count()

    test_worker_init_and_worker_lift_thread_midasm_hooks()
    test_parallel_lift_real_pool_threads_midasm_hooks()

    test_jump_address_is_refused_not_half_implemented()
    test_jump_address_never_invents_a_goto_at_api_level()

    test_orphan_address_fails_loudly_via_cli()
    test_orphan_detection_distinguishes_the_three_ways_a_hook_dies()
    test_duplicate_name_and_address_warns_and_emits_once()
    test_same_ea_different_names_is_not_a_duplicate()

    print()
    if FAILS:
        print(f"[midasm] FAILED ({len(FAILS)}): {', '.join(FAILS)}")
        return 1
    print("ok: emissao de [[midasm_hook]] (antes/depois/return, ordem multi-hook, "
          "label, terminador), golden sem --config por diff, wiring worker + Pool "
          "real, recusa de jump_address e de EA orfao")
    return 0


if __name__ == "__main__":
    sys.exit(main())
