#!/usr/bin/env python3
"""Self-check do MECANISMO de weak override (Fase 19, plano 19-01; XEN-04 + XEN-06).

O que o mecanismo e': substituir uma funcao guest INTEIRA por codigo host, sem
editar o `.cpp` gerado e sem um `patch_*.py` que tenha de ser reaplicado a cada
re-lift. O mid-asm da Fase 17 corre AO LADO de uma instrucao; isto ENVOLVE a
funcao toda -- sao mecanismos diferentes para problemas diferentes.

Esquema de simbolos fixado pelo 19-CONTEXT (e medido aqui, nao assumido):

  | simbolo                | linkage | papel                                  |
  |------------------------|---------|----------------------------------------|
  | `__imp_func_XXXXXXXX`  | forte   | corpo liftado (implementacao)          |
  | `func_XXXXXXXX`        | FRACO   | wrapper fino: chama `__imp_func_...`   |
  | `func_XXXXXXXX` (host) | forte   | override que GANHA o link              |

O RISCO CENTRAL desta fase, e a razao de este ficheiro existir: **weak symbols
no `ld` da Apple**. O 19-CONTEXT e' explicito -- "o risco a medir, nao a
assumir (...). Se o override nao ganhar, isso e' o resultado, e o mecanismo fica
declarado inviavel em vez de fingido". Por isso a seccao 6 abaixo NAO se fica
por inspeccionar texto gerado: compila e LIGA com o `clang++` real desta
maquina, le os simbolos com `nm`, e CORRE o binario para ver que corpo executou.

Um teste que so' olhasse para o `.cpp` gerado passaria com um mecanismo que nao
liga -- e' exactamente o falso verde que o CLAUDE.md proibe ("NUNCA forjar
resultados").

Cobertura:

  1. `runtime/ppu/ppc_hooks.h`: `PPC_FUNC` / `PPC_FUNC_IMPL`, ramo Clang com
     `__attribute__((weak))`, ramo nao-Clang que degrada para simbolo FORTE.
  2. GOLDEN de nao-regressao: sem a flag o output e' byte-a-byte igual ao
     `tools/testdata/midasm_golden_noconfig.c` (o MESMO golden da Fase 17 --
     reutiliza-lo prova de uma vez que nem a Fase 17 nem a 19 mexeram no
     caminho por omissao). Tres variantes: sem `--config`; com `--config` e
     `[[functions_override]]` declarado mas flag OFF; flag ON com lista vazia.
  3. Forma da emissao com a flag ON: corpo em `PPC_FUNC_IMPL(...)`, wrapper
     `PPC_FUNC(...)` a chamar `__imp_...`, e o corpo do `__imp_` identico
     (linha a linha) ao corpo que o golden tem em `func_...`.
  4. `function_table` e chamadas directas continuam a apontar para o nome
     SUBSTITUIVEL (`func_...`), nunca para `__imp_...` -- senao o override
     ficaria invisivel para quem chama.
  5. EA orfao em `[[functions_override]]` -> rc=2, EA listado, nenhum lift
     escrito (mesmo contrato de recusa do mid-asm orfao, 17-01).
  6. LINK REAL (a medicao que decide a fase), com `clang++` + `nm` + execucao:
     L1 `nm` do `.o`: wrapper `W`/`w`, corpo `T`;
     L2 sem override: corre o corpo liftado;
     L3 com override forte NOUTRA TU: o override ganha (chamada directa);
     L4 `function_table[].func` (referencia na MESMA TU do wrapper fraco):
        o override ganha? -- medido, nao assumido;
     L5 chamada directa de OUTRA funcao liftada na mesma TU: idem;
     L6 CONTROLO por mutacao: o mesmo par SEM o atributo fraco -> o link tem
        de FALHAR com duplicate symbol. Prova que o `weak` e' quem carrega o
        mecanismo, e nao um acidente de ordem de link.
  7. `-j1` vs `-j2`: output identico com a flag ON. E' a guarda contra "o
     override desaparece sob `-j`", o defeito silencioso que a Fase 17 apanhou
     no mid-asm.

Sem pytest (convencao `tools/test_*.py` deste repo -- so' stdlib, asserts
simples + `main()`).

Run:  .venv/bin/python3 tools/test_weak_override_symbols.py
      .venv/bin/python3 tools/test_weak_override_symbols.py --measure-cost
        (mede o custo de emitir wrappers para TODAS as funcoes vs so' para as
         declaradas -- a decisao que o 19-CONTEXT deixou a' discricao do
         executor "medir o custo antes de decidir"; lento, fora do PASS)
"""
import difflib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import textwrap
import time
from pathlib import Path

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, TOOLS_DIR)

FAILS: list[str] = []

GOLDEN_PATH = Path(TOOLS_DIR) / "testdata" / "midasm_golden_noconfig.c"
HOOKS_HEADER = Path(REPO_ROOT) / "runtime" / "ppu" / "ppc_hooks.h"

CTX_TYPE = "ppu_context"


def check(label: str, cond: bool, detail: str = "") -> None:
    if cond:
        print(f"[weak] ok   {label}")
    else:
        print(f"[weak] FAIL {label}  {detail}")
        FAILS.append(label)


def w(*words: int) -> bytes:
    """Empacota palavras de 32 bits big-endian (o PPU aqui e' sempre BE)."""
    return b"".join(struct.pack(">I", x & 0xFFFFFFFF) for x in words)


# Instrucoes sinteticas. A fixture principal e' EXACTAMENTE a do
# tools/test_midasm_emit.py, de proposito: e' a que gerou o golden commitado,
# portanto o golden serve as duas suites sem um segundo ficheiro de referencia.
LI_R3_1 = (14 << 26) | (3 << 21) | (0 << 16) | 1     # li r3, 1
LI_R4_2 = (14 << 26) | (4 << 21) | (0 << 16) | 2     # li r4, 2
LI_R3_7 = (14 << 26) | (3 << 21) | (0 << 16) | 7     # li r3, 7
NOP = (24 << 26)                                      # ori r0,r0,0
BLR = 0x4E800020                                      # bclr 20,0
BL_PLUS8 = (18 << 26) | (8 & 0x03FFFFFC) | 1          # bl .+8 (AA=0, LK=1)

FIX_BASE = 0x1000
FIX_BYTES = w(LI_R3_1, LI_R4_2, NOP, BLR)
FIX_END = FIX_BASE + len(FIX_BYTES)
FIX_FUNC = "func_00001000"

# Fixture de duas funcoes: A (0x1000) chama B (0x1008) com um `bl`; B e' a
# funcao substituida. Serve as assercoes de "a chamada directa continua a
# nomear o simbolo substituivel" e a medicao L5 (referencia na MESMA TU).
TWO_BASE = 0x1000
TWO_BYTES = w(BL_PLUS8, BLR, LI_R3_7, BLR)
TWO_BOUNDS = ((0x1000, 0x1008), (0x1008, 0x1010))
TWO_CALLER = "func_00001000"
TWO_CALLEE = "func_00001008"
TWO_CALLEE_EA = 0x1008

# [main] minimo de qualquer config.toml (schema da Task A1 da Fase 16). Nenhum
# destes caminhos e' aberto por este CLI -- ver o help do --config.
_DUMMY_MAIN_TOML = """
[main]
elf_path = "unused.elf"
functions_json = "unused.json"
out_directory = "unused_out"
"""


def _normalize_lift(src: str) -> str:
    """Neutraliza a UNICA linha nao-determinista do output: o carimbo
    `/* lifter-rev: <sha>[-dirty] */`. Tudo o resto entra no diff literalmente."""
    return re.sub(r"/\* lifter-rev: [^*]*\*/", "/* lifter-rev: <normalizado> */", src)


def _run_lifter_cli(input_bytes: bytes = FIX_BYTES,
                    config_toml: str | None = None,
                    functions_bounds: tuple[tuple[int, int], ...] = ((FIX_BASE, FIX_END),),
                    base: int = FIX_BASE,
                    extra_args: tuple[str, ...] = (),
                    keep_dir: str | None = None,
                    ) -> tuple["subprocess.CompletedProcess[str]", str | None]:
    """Corre o `ppu_lifter.py` REAL num tmpdir (mesma helper do
    tools/test_midasm_emit.py). `--allow-jt-failure` e' sempre passado porque
    numa fixture sintetica a descoberta de jump tables falhar e' esperado e
    irrelevante aqui. `--jobs 1` mantem o run deterministico; o caminho
    paralelo tem a sua propria guarda (seccao 7).

    `keep_dir`: copia `out/` para la' (o teste de link precisa dos ficheiros
    gerados depois de o tmpdir morrer).
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

        proc = subprocess.run(argv, capture_output=True, text=True, timeout=180)
        src_path = tmp / "out" / "ppu_recomp.c"
        src = src_path.read_text() if src_path.is_file() else None
        if keep_dir is not None and (tmp / "out").is_dir():
            shutil.copytree(tmp / "out", keep_dir, dirs_exist_ok=True)
        return proc, src


def _override_toml(*addresses: int, flag_in_toml: bool = False,
                   size: int = 0x10) -> str:
    cfg = _DUMMY_MAIN_TOML
    if flag_in_toml:
        cfg = cfg.rstrip() + "\nemit_weak_wrappers = true\n"
    for a in addresses:
        cfg += textwrap.dedent(f"""
            [[functions_override]]
            address = {a}
            size = {size}
        """)
    return cfg


def _golden_diff(src: str) -> str:
    want = _normalize_lift(GOLDEN_PATH.read_text())
    got = _normalize_lift(src)
    return "".join(difflib.unified_diff(
        want.splitlines(keepends=True), got.splitlines(keepends=True),
        fromfile=str(GOLDEN_PATH), tofile="lift actual"))


# --------------------------------------------------------------------------- #
# 1: o header
# --------------------------------------------------------------------------- #

def test_header_defines_the_macros_with_a_clang_weak_path() -> None:
    """`runtime/ppu/ppc_hooks.h` e' a UNICA fonte do atributo fraco e do nome
    `__imp_`: o lifter emite `#include "ppc_hooks.h"` e usa as macros, em vez de
    carimbar `__attribute__((weak))` no output. Se as duas definicoes
    divergissem, o build partia alto -- em vez de emitir um wrapper que parece
    fraco e nao e'."""
    check("runtime/ppu/ppc_hooks.h existe", HOOKS_HEADER.is_file(), str(HOOKS_HEADER))
    if not HOOKS_HEADER.is_file():
        return
    text = HOOKS_HEADER.read_text()
    check("define PPC_FUNC_IMPL (corpo liftado, forte)",
          re.search(r"^#\s*define\s+PPC_FUNC_IMPL\(", text, re.M) is not None, text[:400])
    check("define PPC_FUNC (wrapper fraco)",
          re.search(r"^#\s*define\s+PPC_FUNC\(", text, re.M) is not None, text[:400])
    check("PPC_FUNC_IMPL cola o prefixo __imp_ ao nome",
          re.search(r"define\s+PPC_FUNC_IMPL\(\s*\w+\s*\)[^\n]*__imp_##", text) is not None,
          text[:600])
    check("ha um ramo __clang__ com __attribute__((weak))",
          "__clang__" in text and "__attribute__((weak))" in text, text[:600])
    check("ha um ramo nao-Clang que degrada para simbolo forte "
          "(PPC_WEAK vazio) e o declara",
          re.search(r"#\s*define\s+PPC_WEAK\s*$", text, re.M) is not None, text[:900])
    check("o header nao inclui ppu_context.h (ppu_recomp.h define o seu proprio "
          "ppu_context; incluir os dois seria redefinicao)",
          '#include "ppu_context.h"' not in text, text[:900])


# --------------------------------------------------------------------------- #
# 2: golden -- sem a flag nada muda
# --------------------------------------------------------------------------- #

def test_golden_without_any_config() -> None:
    """Sem `--config` de todo: byte-a-byte igual ao golden da Fase 17."""
    proc, src = _run_lifter_cli(config_toml=None)
    check("CLI sem --config sai 0", proc.returncode == 0,
          f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    if src is not None:
        check("diff vazio contra o golden (sem --config)", _golden_diff(src) == "",
              _golden_diff(src)[:4000])
        check("zero simbolos __imp_func_ sem --config", "__imp_func_" not in src,
              src[:400])


def test_golden_holds_with_override_declared_but_flag_off() -> None:
    """A guarda que interessa: `[[functions_override]]` DECLARADO e a flag OFF
    -> o `.c` nao muda um byte. A emissao e' opt-in; nada de wrappers por
    omissao (G2, o mesmo contrato do 17-01)."""
    proc, src = _run_lifter_cli(config_toml=_override_toml(FIX_BASE))
    check("CLI com override declarado e flag off sai 0", proc.returncode == 0,
          f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    if src is not None:
        check("diff vazio contra o golden (override declarado, flag off)",
              _golden_diff(src) == "", _golden_diff(src)[:4000])
        check("zero __imp_func_ com a flag off", "__imp_func_" not in src, src[:400])
        check("zero #include de ppc_hooks.h com a flag off",
              "ppc_hooks.h" not in src, src[:400])


def test_flag_on_with_empty_override_list_emits_nothing_and_warns() -> None:
    """Flag ON e lista de overrides VAZIA -> zero wrappers (nao "todas as
    funcoes") e um aviso. Emitir para todas as 52k funcoes do lift do GoW2
    dobrava os simbolos; o 19-CONTEXT mandou medir antes de decidir, e o
    caminho seguro entretanto e' o conjunto vazio."""
    proc, src = _run_lifter_cli(config_toml=_DUMMY_MAIN_TOML,
                                extra_args=("--emit-weak-wrappers",))
    check("CLI com flag on e lista vazia sai 0", proc.returncode == 0,
          f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    if src is not None:
        check("diff vazio contra o golden (flag on, lista vazia)",
              _golden_diff(src) == "", _golden_diff(src)[:4000])
        check("zero __imp_func_ com lista vazia", "__imp_func_" not in src, src[:400])
    check("avisa que a flag esta on mas nao ha nada declarado",
          "functions_override" in (proc.stdout + proc.stderr)
          and "arning" in (proc.stdout + proc.stderr),
          proc.stdout + proc.stderr)


def test_declaring_overrides_without_the_flag_is_reported() -> None:
    """Simetrico: overrides declarados com a emissao desligada nao podem passar
    em silencio -- "declarado mas sem efeito" e' a surpresa que o
    recomp_config.py ja rejeita no `default` do [[switch_table]]. Aqui basta
    avisar (a flag off e' uma escolha legitima), mas tem de ser visivel."""
    proc, _src = _run_lifter_cli(config_toml=_override_toml(FIX_BASE))
    out = proc.stdout + proc.stderr
    check("o CLI reporta a contagem de functions_override carregados",
          "1 functions_override" in out, out)
    check("e avisa que a emissao esta desligada",
          "arning" in out and "emit-weak-wrappers" in out, out)


# --------------------------------------------------------------------------- #
# 3-4: forma da emissao
# --------------------------------------------------------------------------- #

def _golden_body_lines() -> list[str]:
    """As linhas do corpo de `func_00001000` no golden (sem a assinatura nem o
    `}` final) -- a referencia com que o corpo do `__imp_` e' comparado."""
    lines = GOLDEN_PATH.read_text().splitlines()
    i = lines.index(f"void {FIX_FUNC}({CTX_TYPE}* ctx) {{")
    j = lines.index("}", i)
    return lines[i + 1:j]


def test_flag_on_emits_imp_body_plus_weak_wrapper() -> None:
    """A forma canonica: o corpo passa a `PPC_FUNC_IMPL(func_...)` e nasce um
    `PPC_FUNC(func_...)` que so' o chama. O corpo tem de ser IDENTICO linha a
    linha ao do golden -- envolver a funcao nao pode alterar a traducao."""
    proc, src = _run_lifter_cli(config_toml=_override_toml(FIX_BASE, flag_in_toml=True))
    check("CLI com emissao on sai 0", proc.returncode == 0,
          f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    if src is None:
        check("gerou .c", False, "nao gerou")
        return
    lines = src.splitlines()
    check("o .c inclui ppc_hooks.h (uma so' vez)",
          src.count('#include "ppc_hooks.h"') == 1, src[:600])
    impl_sig = f"PPC_FUNC_IMPL({FIX_FUNC}) {{"
    wrap_sig = f"PPC_FUNC({FIX_FUNC}) {{"
    check("corpo emitido como PPC_FUNC_IMPL(func_00001000)", impl_sig in lines,
          "\n".join(l for l in lines if "PPC_FUNC" in l))
    check("wrapper emitido como PPC_FUNC(func_00001000)", wrap_sig in lines,
          "\n".join(l for l in lines if "PPC_FUNC" in l))
    check("a assinatura antiga `void func_00001000(...)` desapareceu do corpo",
          f"void {FIX_FUNC}({CTX_TYPE}* ctx) {{" not in lines, src[-800:])
    if impl_sig in lines and wrap_sig in lines:
        i = lines.index(impl_sig)
        j = lines.index(wrap_sig)
        check("o wrapper vem DEPOIS do corpo (senao chamava um simbolo por declarar)",
              i < j, f"{i=} {j=}")
        check("corpo do __imp_ identico, linha a linha, ao corpo do golden",
              lines[i + 1:lines.index("}", i)] == _golden_body_lines(),
              f"{lines[i + 1:lines.index('}', i)]}\nvs\n{_golden_body_lines()}")
        body_w = lines[j + 1:lines.index("}", j)]
        check("o wrapper e' fino: uma unica linha, a chamada ao __imp_",
              body_w == [f"    __imp_{FIX_FUNC}(ctx);"], body_w)


def test_function_table_points_at_the_overridable_name() -> None:
    """A `function_table` e' a origem de TODA a chamada indirecta do jogo. Se
    apontasse para `__imp_...`, o override ficava invisivel para o dispatcher
    indirecto -- meio mecanismo, do pior tipo (funciona nos testes directos e
    falha no jogo)."""
    _proc, src = _run_lifter_cli(config_toml=_override_toml(FIX_BASE, flag_in_toml=True))
    if src is None:
        check("gerou .c", False, "nao gerou")
        return
    entry = [l for l in src.splitlines() if "0x00001000ULL" in l]
    check("a entrada da function_table nomeia func_00001000, nao __imp_",
          len(entry) == 1 and f" {FIX_FUNC}," in entry[0] and "__imp_" not in entry[0],
          entry)


def test_direct_call_from_another_lifted_function_keeps_the_overridable_name() -> None:
    """Uma funcao liftada que chama a substituida tem de continuar a chamar
    `func_...` (o nome fraco), nao `__imp_...`: e' assim que o override fica
    visivel para quem chama."""
    _proc, src = _run_lifter_cli(input_bytes=TWO_BYTES, base=TWO_BASE,
                                 functions_bounds=TWO_BOUNDS,
                                 config_toml=_override_toml(TWO_CALLEE_EA,
                                                            flag_in_toml=True))
    if src is None:
        check("gerou .c (fixture de 2 funcoes)", False, "nao gerou")
        return
    lines = src.splitlines()
    impl_sig = f"PPC_FUNC_IMPL({TWO_CALLER}) {{"
    check("o CALLER nao foi envolvido (so' o EA declarado leva wrapper)",
          impl_sig not in lines
          and f"void {TWO_CALLER}({CTX_TYPE}* ctx) {{" in lines,
          "\n".join(l for l in lines if "PPC_FUNC" in l or TWO_CALLER in l))
    body = [l for l in lines if f"{TWO_CALLEE}(ctx);" in l and "__imp_" not in l]
    check("o `bl` do caller continua a nomear func_00001008 (o simbolo fraco)",
          any("ctx->lr" in l for l in body), body)
    check("o callee foi envolvido", f"PPC_FUNC_IMPL({TWO_CALLEE}) {{" in lines,
          "\n".join(l for l in lines if "PPC_FUNC" in l))


# --------------------------------------------------------------------------- #
# 5: EA orfao
# --------------------------------------------------------------------------- #

def test_orphan_override_ea_fails_loudly() -> None:
    """Um `[[functions_override]]` cujo `address` nao e' o INICIO de nenhuma
    funcao liftada nunca produziria wrapper nenhum. Recusa (rc=2) com o EA
    listado, como o mid-asm orfao do 17-01 -- entregar um lift sem o override
    declarado e' pior do que nao entregar lift."""
    proc, src = _run_lifter_cli(
        config_toml=_override_toml(0x00009999, flag_in_toml=True))
    check("CLI RECUSA (rc=2) um override orfao", proc.returncode == 2,
          f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    check("a mensagem lista o EA orfao", "0x00009999" in proc.stderr, proc.stderr)
    check("nao escreveu lift nenhum", src is None, "gerou .c")


def test_override_ea_inside_a_function_but_not_its_start_is_orphan() -> None:
    """Um EA no MEIO de uma funcao liftada tambem e' orfao: o weak substitui a
    funcao inteira, so' o inicio e' um alvo valido. Sem esta guarda, declarar
    `address = 0x1004` era um no-op silencioso."""
    proc, _src = _run_lifter_cli(
        config_toml=_override_toml(FIX_BASE + 4, flag_in_toml=True))
    check("EA no interior de uma funcao e' recusado (rc=2)", proc.returncode == 2,
          f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
    check("a mensagem lista o EA interior", "0x00001004" in proc.stderr, proc.stderr)


def test_orphan_check_only_runs_when_emission_is_on() -> None:
    """Com a emissao OFF um override orfao e' so' ruido de config, nao um erro:
    recusar aqui partia lifts que hoje passam (o `gow2_recomp.toml` ja tem a
    seccao e o caminho por omissao tem de continuar a correr). Guarda de
    nao-regressao do golden, nao de conveniencia."""
    proc, src = _run_lifter_cli(config_toml=_override_toml(0x00009999))
    check("override orfao com a emissao off nao parte o lift",
          proc.returncode == 0, f"rc={proc.returncode}\n{proc.stderr}")
    check("e o output continua igual ao golden",
          src is not None and _golden_diff(src) == "",
          "" if src is None else _golden_diff(src)[:2000])


# --------------------------------------------------------------------------- #
# 6: LINK REAL -- a medicao que decide a fase
# --------------------------------------------------------------------------- #

_MAIN_CPP = """\
#include "ppu_recomp.h"
#include <cstdio>

/* O preambulo do lift declara g_trampoline_fn como `extern "C" thread_local`
   no Apple (ppu_lifter.py:414); no runtime real vive no ppu_loader.cpp. Aqui
   basta uma definicao para o link fechar. */
#if defined(_MSC_VER)
extern "C" __declspec(thread) void (*g_trampoline_fn)(void*) = 0;
#elif defined(__APPLE__)
extern "C" thread_local void (*g_trampoline_fn)(void*) = 0;
#else
extern "C" __thread void (*g_trampoline_fn)(void*) = 0;
#endif

int main(void) {
    ppu_context a; a.gpr[3] = 0;
    /* L3: chamada DIRECTA a partir de outra TU. */
    func_00001000(&a);
    printf("direct=%llu\\n", (unsigned long long)a.gpr[3]);
    /* L4: chamada INDIRECTA pela function_table, cuja referencia ao simbolo
       fraco e' emitida na MESMA TU do wrapper -- o caso que decide se o
       override e' visivel para o dispatcher indirecto do jogo. */
    ppu_context b; b.gpr[3] = 0;
    function_table[0].func(&b);
    printf("table=%llu\\n", (unsigned long long)b.gpr[3]);
    return 0;
}
"""

_OVERRIDE_CPP = """\
#include "ppu_recomp.h"
/* Override HOST: mesma assinatura, mesmo linkage C++ (SEM extern "C" -- o
   lifter emite `void func_XXXXXXXX(ppu_context*)` com mangling C++, ver o
   docstring de emit_fallback_stubs). Simbolo FORTE. */
void func_00001000(ppu_context* ctx) { ctx->gpr[3] = 4242; }
"""

# Par escrito a' mao para o CONTROLO por mutacao (L6): identico ao emitido, mas
# com o wrapper FORTE. Se o link deste passar, o `weak` nao esta a fazer nada e
# o verde de L3 seria um acidente de ordem de link.
_STRONG_PAIR_CPP = """\
#include "ppu_recomp.h"
#include <cstddef>
void __imp_func_00001000(ppu_context* ctx) { ctx->gpr[3] = 1; }
void func_00001000(ppu_context* ctx) { __imp_func_00001000(ctx); }
const func_entry function_table[] = {
    { 0x00001000ULL, func_00001000, "func_00001000" },
    { 0, NULL, NULL }
};
const uint64_t function_table_count = 1;
"""


def _clangxx() -> str:
    return os.environ.get("CXX", "clang++")


def _nm(path: Path) -> dict[str, str]:
    """{simbolo demanglado -> letra do nm}. `nm -C` desmangla, o que evita
    depender do mangling exacto do Itanium ABI nas assercoes."""
    p = subprocess.run(["nm", "-C", str(path)], capture_output=True, text=True)
    out: dict[str, str] = {}
    for line in p.stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3:
            out[parts[2].strip()] = parts[1]
        elif len(parts) == 2:
            out[parts[1].strip()] = parts[0]
    return out


def _sym(nm: dict[str, str], needle: str) -> list[tuple[str, str]]:
    return [(k, v) for k, v in nm.items() if needle in k]


def _nm_macho(path: Path) -> dict[str, str]:
    """{simbolo MANGLADO -> atributo Mach-O}, via `nm -m`.

    MEDIDO durante a execucao do 19-01, e vale a pena registar porque quase
    produziu um falso vermelho: no Mach-O o `nm` simples imprime `T` para uma
    definicao FRACA, exactamente como para uma forte -- a letra `W` do ELF nao
    aparece. A fraqueza so' e' visivel com `nm -m`, que escreve
    `weak external` em vez de `external`:

        $ nm -C a.o
        000000000000001c T f(C*)               <-- FRACO, mas indistinguivel
        $ nm -m -C a.o
        ... (__TEXT,__text) weak external __Z1fP1C

    Ou seja: a verificacao "o simbolo aparece como W/w no nm" que o plano
    esbocou (escrita a pensar em ELF) daria FALHA num mecanismo que funciona.
    Por isso a assercao le o atributo Mach-O real. `nm -m` NAO desmangla mesmo
    com `-C`, por isso as chaves aqui sao os nomes manglados."""
    p = subprocess.run(["nm", "-m", str(path)], capture_output=True, text=True)
    out: dict[str, str] = {}
    for line in p.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            out[parts[-1]] = " ".join(parts[1:-1])
    return out


def _weakness(nm_m: dict[str, str], mangled_needle: str,
              exclude: str | None = None) -> list[tuple[str, str]]:
    return [(k, v) for k, v in nm_m.items()
            if mangled_needle in k and (exclude is None or exclude not in k)]


def test_link_measures_apple_ld_weak_override() -> None:
    """**A medicao que decide a Fase 19.** Compila o lift REALMENTE emitido com
    o `clang++` desta maquina, le os simbolos com `nm` e CORRE o binario.

    Se o override forte NAO ganhar, este teste FALHA com a saida literal do
    `nm` -- e o SUMMARY declara o mecanismo inviavel no toolchain da Apple. Um
    mecanismo honestamente refutado vale mais do que um fingido."""
    if shutil.which(_clangxx()) is None:
        check("clang++ disponivel (a medicao de link e' o aceite central; "
              "sem compilador nao ha aceite)", False, _clangxx())
        return

    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        gen = tmp / "gen"
        proc, src = _run_lifter_cli(
            config_toml=_override_toml(FIX_BASE, flag_in_toml=True),
            keep_dir=str(gen))
        if proc.returncode != 0 or src is None:
            check("lift para o teste de link saiu 0", False,
                  f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
            return

        inc = ["-I", str(gen), "-I", str(Path(REPO_ROOT) / "runtime" / "ppu")]
        lift_o = tmp / "lift.o"
        cc = subprocess.run([_clangxx(), "-std=c++20", "-w", "-c", *inc,
                             "-x", "c++", str(gen / "ppu_recomp.c"),
                             "-o", str(lift_o)],
                            capture_output=True, text=True)
        check("o lift com wrappers fracos COMPILA (clang++ -std=c++20)",
              cc.returncode == 0, cc.stderr[-3000:])
        if cc.returncode != 0:
            return

        # ---- L1: nm do objecto ----------------------------------------- #
        # A leitura util no Mach-O e' `nm -m` (weak external vs external); ver
        # o docstring de _nm_macho para porque o `nm` simples nao serve.
        nm_o = _nm(lift_o)
        nm_m = _nm_macho(lift_o)
        wrap = [(k, v) for k, v in _sym(nm_o, f"{FIX_FUNC}(ppu_context*)")
                if "__imp_" not in k]
        impl = _sym(nm_o, f"__imp_{FIX_FUNC}(ppu_context*)")
        wrap_m = _weakness(nm_m, FIX_FUNC, exclude="__imp_")
        impl_m = _weakness(nm_m, f"__imp_{FIX_FUNC}")
        print(f"[weak] nm    (lift.o): wrapper={wrap} impl={impl}")
        print(f"[weak] nm -m (lift.o): wrapper={wrap_m} impl={impl_m}")
        check("L1a: nm -m ve o wrapper func_00001000 como `weak external`",
              len(wrap_m) == 1 and "weak external" in wrap_m[0][1], wrap_m)
        check("L1b: nm -m ve o corpo __imp_func_00001000 como `external` "
              "FORTE (sem 'weak')",
              len(impl_m) == 1 and "external" in impl_m[0][1]
              and "weak" not in impl_m[0][1], impl_m)
        check("L1c: os dois simbolos estao em __TEXT (definidos, nao "
              "referencias por resolver)",
              all("__TEXT" in v for _k, v in wrap_m + impl_m), wrap_m + impl_m)

        main_cpp = tmp / "main.cpp"
        main_cpp.write_text(_MAIN_CPP)
        over_cpp = tmp / "override.cpp"
        over_cpp.write_text(_OVERRIDE_CPP)

        def build(name: str, sources: list[Path], objs: list[Path]):
            exe = tmp / name
            cmd = [_clangxx(), "-std=c++20", "-w", *inc,
                   *[str(s) for s in sources], *[str(o) for o in objs],
                   "-o", str(exe)]
            return exe, subprocess.run(cmd, capture_output=True, text=True)

        # ---- L2: sem override -> corre o corpo liftado ------------------ #
        exe_base, lk = build("base", [main_cpp], [lift_o])
        check("L2a: liga sem override", lk.returncode == 0, lk.stderr[-3000:])
        if lk.returncode == 0:
            run = subprocess.run([str(exe_base)], capture_output=True, text=True)
            check("L2b: sem override corre o corpo liftado (gpr[3]==1) nos dois "
                  "caminhos (directo e function_table)",
                  "direct=1" in run.stdout and "table=1" in run.stdout,
                  run.stdout + run.stderr)

        # ---- L3/L4: com override forte noutra TU ------------------------ #
        exe_ovr, lk2 = build("ovr", [main_cpp, over_cpp], [lift_o])
        check("L3a: liga COM o override forte (sem duplicate symbol)",
              lk2.returncode == 0, lk2.stderr[-3000:])
        if lk2.returncode == 0:
            nm_exe = _nm(exe_ovr)
            got = _sym(nm_exe, f"{FIX_FUNC}(ppu_context*)")
            got = [(k, v) for k, v in got if "__imp_" not in k]
            print(f"[weak] nm(exe com override): {got}")
            run2 = subprocess.run([str(exe_ovr)], capture_output=True, text=True)
            print(f"[weak] run(exe com override): {run2.stdout.strip()!r}")
            check("L3b: o override FORTE ganha na chamada directa (gpr[3]==4242)",
                  "direct=4242" in run2.stdout, run2.stdout + run2.stderr)
            check("L4: o override ganha TAMBEM via function_table (referencia "
                  "emitida na mesma TU do wrapper fraco)",
                  "table=4242" in run2.stdout, run2.stdout + run2.stderr)

        # ---- L6: controlo por mutacao ----------------------------------- #
        strong_cpp = tmp / "strong_pair.cpp"
        strong_cpp.write_text(_STRONG_PAIR_CPP)
        _exe_ctl, lk3 = build("ctl", [main_cpp, over_cpp, strong_cpp], [])
        check("L6: CONTROLO -- o MESMO par com o wrapper FORTE tem de FALHAR o "
              "link (duplicate symbol). Se passasse, o `weak` nao era quem "
              "carregava o mecanismo e L3 seria um acidente.",
              lk3.returncode != 0 and "duplicate" in lk3.stderr.lower(),
              lk3.stderr[-2000:])


def test_link_measures_same_tu_direct_call() -> None:
    """L5: a funcao substituida chamada por OUTRA funcao liftada da MESMA TU.
    E' o caso mais exposto a que o compilador ligue a referencia directamente
    a' definicao fraca local, curto-circuitando o override. Medido, nao
    assumido."""
    if shutil.which(_clangxx()) is None:
        check("L5: clang++ disponivel", False, _clangxx())
        return
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        gen = tmp / "gen"
        proc, src = _run_lifter_cli(
            input_bytes=TWO_BYTES, base=TWO_BASE, functions_bounds=TWO_BOUNDS,
            config_toml=_override_toml(TWO_CALLEE_EA, flag_in_toml=True),
            keep_dir=str(gen))
        if proc.returncode != 0 or src is None:
            check("L5: lift da fixture de 2 funcoes saiu 0", False,
                  f"rc={proc.returncode}\n{proc.stdout}\n{proc.stderr}")
            return
        inc = ["-I", str(gen), "-I", str(Path(REPO_ROOT) / "runtime" / "ppu")]
        (tmp / "main5.cpp").write_text(textwrap.dedent("""\
            #include "ppu_recomp.h"
            #include <cstdio>
            #if defined(__APPLE__)
            extern "C" thread_local void (*g_trampoline_fn)(void*) = 0;
            #else
            extern "C" __thread void (*g_trampoline_fn)(void*) = 0;
            #endif
            /* override forte do CALLEE, noutra TU */
            void func_00001008(ppu_context* ctx) { ctx->gpr[3] = 4242; }
            int main(void) {
                ppu_context a; a.gpr[3] = 0; a.lr = 0;
                func_00001000(&a);   /* caller liftado -> bl -> callee */
                printf("via_caller=%llu\\n", (unsigned long long)a.gpr[3]);
                return 0;
            }
        """))
        exe = tmp / "l5"
        lk = subprocess.run([_clangxx(), "-std=c++20", "-w", *inc, "-x", "c++",
                             str(gen / "ppu_recomp.c"), "-x", "c++",
                             str(tmp / "main5.cpp"), "-o", str(exe)],
                            capture_output=True, text=True)
        check("L5a: liga o lift de 2 funcoes com o override do callee",
              lk.returncode == 0, lk.stderr[-3000:])
        if lk.returncode == 0:
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            print(f"[weak] run(L5): {run.stdout.strip()!r}")
            check("L5b: o override ganha na chamada directa DENTRO da mesma TU "
                  "(via_caller==4242, nao 7)",
                  "via_caller=4242" in run.stdout, run.stdout + run.stderr)

        # ---- L7: o mesmo, com o lift OPTIMIZADO ------------------------ #
        # O build_macos.sh compila os chunks com LIFT_OPT, que por omissao e'
        # -O0 mas suporta -O1/-O2 (e o repo tem um `boot_gow2_O1`). Se o
        # compilador pudesse INLINE do wrapper fraco no sitio da chamada, o
        # override era silenciosamente contornado NA MESMA TU -- um mecanismo
        # que funciona a -O0 e falha a -O1 seria a pior armadilha possivel.
        # Medido em vez de assumido (a garantia formal existe: um simbolo de
        # linkage `weak` do LLVM pode ser substituido no link, por isso nao e'
        # inlinavel -- mas quem manda aqui e' a medicao).
        for opt in ("-O1", "-O2"):
            exe_o = tmp / f"l5{opt}"
            lk_o = subprocess.run(
                [_clangxx(), "-std=c++20", "-w", opt, *inc, "-x", "c++",
                 str(gen / "ppu_recomp.c"), "-x", "c++", str(tmp / "main5.cpp"),
                 "-o", str(exe_o)], capture_output=True, text=True)
            if lk_o.returncode != 0:
                check(f"L7{opt}: liga com o lift a {opt}", False, lk_o.stderr[-2000:])
                continue
            run_o = subprocess.run([str(exe_o)], capture_output=True, text=True)
            print(f"[weak] run(L7 {opt}): {run_o.stdout.strip()!r}")
            check(f"L7{opt}: com o lift compilado a {opt} o override CONTINUA a "
                  f"ganhar na mesma TU (o wrapper fraco nao foi inlined no "
                  f"sitio da chamada)",
                  "via_caller=4242" in run_o.stdout, run_o.stdout + run_o.stderr)


# --------------------------------------------------------------------------- #
# 7: -j1 vs -jN
# --------------------------------------------------------------------------- #

def test_serial_and_parallel_outputs_are_identical() -> None:
    """A guarda contra "o override desaparece sob `-j`". A emissao vive no
    processo principal (`_function_def_lines`), nao nos workers -- mas isso e'
    uma afirmacao sobre a implementacao, e o que interessa e' o output. Compara
    os dois `.c` inteiros, byte a byte."""
    cfg = _override_toml(FIX_BASE, flag_in_toml=True)
    _p1, s1 = _run_lifter_cli(config_toml=cfg)
    _p2, s2 = _run_lifter_cli(config_toml=cfg, extra_args=("--jobs", "2"))
    check("-j2 tambem gerou lift", s2 is not None, "nao gerou")
    if s1 is not None and s2 is not None:
        d = "".join(difflib.unified_diff(
            _normalize_lift(s1).splitlines(keepends=True),
            _normalize_lift(s2).splitlines(keepends=True),
            fromfile="-j1", tofile="-j2"))
        check("output identico entre -j1 e -j2 com a emissao ON", d == "", d[:3000])
        check("-j2 tambem emitiu o wrapper fraco",
              f"PPC_FUNC({FIX_FUNC}) {{" in s2, s2[-800:])


# --------------------------------------------------------------------------- #
# medicao de custo (fora do PASS): todas as funcoes vs so' as declaradas
# --------------------------------------------------------------------------- #

def _measure_cost() -> int:
    """Custo de emitir wrappers para TODAS as funcoes vs so' para as
    declaradas. O 19-CONTEXT deixou a decisao a' discricao do executor, mas
    exigiu medir. Mede sobre uma fixture de N funcoes reais liftadas: bytes de
    fonte, tempo de compilacao e tamanho do objecto, com e sem wrapper em cada
    funcao. Depois extrapola para as funcoes do lift do GoW2."""
    n = 2000
    words: list[int] = []
    bounds: list[tuple[int, int]] = []
    base = 0x1000
    for i in range(n):
        words += [LI_R3_1, LI_R4_2, NOP, BLR]
        bounds.append((base + i * 16, base + i * 16 + 16))
    data = w(*words)

    results = {}
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        for label, addrs in (("declared(0)", ()), ("all", tuple(s for s, _ in bounds))):
            gen = tmp / f"gen_{label.replace('(', '').replace(')', '')}"
            cfg = _override_toml(*addrs, flag_in_toml=True) if addrs else \
                _override_toml(flag_in_toml=True)
            t0 = time.time()
            proc, src = _run_lifter_cli(input_bytes=data, base=base,
                                        functions_bounds=tuple(bounds),
                                        config_toml=cfg, keep_dir=str(gen))
            t_lift = time.time() - t0
            if proc.returncode != 0 or src is None:
                print(f"  {label}: lift falhou rc={proc.returncode}\n{proc.stderr[-2000:]}")
                return 1
            src_bytes = len(src)
            inc = ["-I", str(gen), "-I", str(Path(REPO_ROOT) / "runtime" / "ppu")]
            obj = tmp / f"{label}.o"
            t0 = time.time()
            cc = subprocess.run([_clangxx(), "-std=c++20", "-w", "-O1", "-c", *inc,
                                 "-x", "c++", str(gen / "ppu_recomp.c"),
                                 "-o", str(obj)], capture_output=True, text=True)
            t_cc = time.time() - t0
            if cc.returncode != 0:
                print(f"  {label}: compilacao falhou\n{cc.stderr[-2000:]}")
                return 1
            nsym = len(subprocess.run(["nm", str(obj)], capture_output=True,
                                      text=True).stdout.splitlines())
            results[label] = dict(src_bytes=src_bytes, lift_s=t_lift, cc_s=t_cc,
                                  obj_bytes=obj.stat().st_size, nm_lines=nsym)
            print(f"  {label:<12} fonte={src_bytes:>10} B  lift={t_lift:5.1f}s  "
                  f"cc={t_cc:5.1f}s  obj={obj.stat().st_size:>9} B  nm={nsym}")

    a, b = results["declared(0)"], results["all"]
    print(f"\n  N={n} funcoes sinteticas (4 instrucoes cada)")
    for k, unit in (("src_bytes", "B"), ("cc_s", "s"), ("obj_bytes", "B"),
                    ("nm_lines", "sym")):
        d_abs = b[k] - a[k]
        pct = (b[k] / a[k] - 1) * 100 if a[k] else float("nan")
        print(f"  {k:<10} {a[k]:>12.2f} -> {b[k]:>12.2f} {unit}  "
              f"({d_abs:+.2f}, {pct:+.1f}%)")
    per_fn = (b["src_bytes"] - a["src_bytes"]) / n
    print(f"\n  custo por wrapper: {per_fn:.1f} B de fonte")
    print(f"  extrapolado a 52068 funcoes do lift GoW2: "
          f"{per_fn * 52068 / 1e6:.1f} MB de fonte extra, "
          f"+{52068} simbolos")
    return 0


# --------------------------------------------------------------------------- #

def main() -> int:
    if "--measure-cost" in sys.argv:
        return _measure_cost()

    test_header_defines_the_macros_with_a_clang_weak_path()

    test_golden_without_any_config()
    test_golden_holds_with_override_declared_but_flag_off()
    test_flag_on_with_empty_override_list_emits_nothing_and_warns()
    test_declaring_overrides_without_the_flag_is_reported()

    test_flag_on_emits_imp_body_plus_weak_wrapper()
    test_function_table_points_at_the_overridable_name()
    test_direct_call_from_another_lifted_function_keeps_the_overridable_name()

    test_orphan_override_ea_fails_loudly()
    test_override_ea_inside_a_function_but_not_its_start_is_orphan()
    test_orphan_check_only_runs_when_emission_is_on()

    test_link_measures_apple_ld_weak_override()
    test_link_measures_same_tu_direct_call()

    test_serial_and_parallel_outputs_are_identical()

    print()
    if FAILS:
        print(f"[weak] FAILED ({len(FAILS)}): {', '.join(FAILS)}")
        return 1
    print("ok: ppc_hooks.h (macros + ramo Clang/nao-Clang), golden sem a flag "
          "por diff, forma __imp_/wrapper fraco, function_table e chamadas "
          "directas no nome substituivel, recusa de EA orfao, LINK REAL medido "
          "com clang++/nm/execucao (weak + override forte ganha, com controlo "
          "por mutacao) e -j1 == -j2")
    return 0


if __name__ == "__main__":
    sys.exit(main())
