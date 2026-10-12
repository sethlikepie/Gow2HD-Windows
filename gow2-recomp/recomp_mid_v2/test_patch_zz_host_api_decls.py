#!/usr/bin/env python3
"""Testes de patch_zz_host_api_decls.py -- foco nas 13 declaracoes F2B novas.

Porque existe
-------------
patch_zz_host_api_decls.py foi extendido (02-02-PLAN.md, Task 2) com 7
protótipos de função F2B e um mecanismo novo, `GLOBAL_DECLS`/`needed_globals`/
`defines_global`, porque 6 dos 17 símbolos F2B (D-2.3/D-2.4) são globais lidas/
escritas como variável simples (`g_f2b_fill_fo = _fo;`), não como chamada -- o
`needed()`/`defines()` existentes so' reconhecem sintaxe de CHAMADA
(`\\b{sym}\\s*\\(`) e nunca detectariam isso. Este ficheiro prova, com
fixtures sintéticas em `tempfile.TemporaryDirectory()` (nada escrito fora de
directorios temporarios), que a extensao funciona: detecta o que falta
declarar, não duplica o que já está declarado, é idempotente, e o resultado
compila e LINKA contra host_gow2_f2b.c (Task 1 do mesmo plano) -- a mesma
forma do critério 4 do ROADMAP, a pequena escala.

Uso:  python3 test_patch_zz_host_api_decls.py
rc=0 todos os testes passaram (ou SKIP explícito no Teste 5 sem toolchain C++)
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
GOW2_DIR = HERE.parent
REPO = GOW2_DIR.parent.parent

sys.path.insert(0, str(HERE))
import patch_zz_host_api_decls as p  # noqa: E402

# As duas declaracoes que hoje já vivem em ppu_recomp_002.cpp:369-370
# (injected_002.cpp:1-2) -- texto BYTE A BYTE idêntico, de propósito.
DECL_ENSURE = 'extern "C" void f2b_stream_ensure(uint32_t type_sys);'
DECL_EOF = 'extern "C" void f2b_stream_eof_try_complete(uint32_t type_sys);'

FIXTURE_USED = '''#include "ppu_recomp.h"

extern "C" void call_all_f2b(ppu_context* ctx) {
    uint32_t fo = 0x1000;
    f2b_fo_mfd_put(fo, 1, 100);
    unsigned mfd = f2b_fo_mfd_get(fo);
    uint32_t sz = f2b_fo_sz_get(fo);
    f2b_stream_fill(0x2000u, 16u);
    f2b_stream_ensure(0x3000u);
    f2b_stream_eof_try_complete(0x3000u);
    f2b_stream_pre_consume(ctx);
    g_f2b_fill_fo = fo;
    g_f2b_fill_mfd = mfd;
    g_f2b_fill_sz = sz;
    g_f2b_fill_file_pos = 0;
    g_f2b_fill_stream = 0x2000u;
    g_f2b_natural_movie_fo = fo;
}
'''

FIXTURE_DECLARED = f'''#include "ppu_recomp.h"
{DECL_ENSURE}
{DECL_EOF}

extern "C" void some_func(ppu_context* ctx) {{
    f2b_stream_ensure((uint32_t)ctx->gpr[31]);
    f2b_stream_eof_try_complete((uint32_t)ctx->gpr[31]);
}}
'''

ALL_17 = [
    "f2b_fo_mfd_put", "f2b_fo_mfd_get", "f2b_fo_sz_get", "f2b_stream_fill",
    "f2b_stream_ensure", "f2b_stream_eof_try_complete", "f2b_stream_pre_consume",
    "g_f2b_fill_fo", "g_f2b_fill_mfd", "g_f2b_fill_sz", "g_f2b_fill_file_pos",
    "g_f2b_fill_stream", "g_f2b_natural_movie_fo",
]


def _which_cxx() -> str | None:
    for cand in ("clang++", "c++", "g++"):
        found = shutil.which(cand)
        if found:
            return found
    return None


def test1_writes_all_f2b_decls() -> Path:
    """Fixture sem nenhuma declaracao previa -- patch_one escreve as 13."""
    tmpdir = Path(tempfile.mkdtemp(prefix="f2b_decls_test1_"))
    fixture = tmpdir / "chunk_used.cpp"
    fixture.write_text(FIXTURE_USED, encoding="utf-8")

    wrote = p.patch_one(fixture)
    assert wrote == 1, f"esperava patch_one()==1 (escreveu), veio {wrote}"

    out = fixture.read_text(encoding="utf-8")
    assert p.MARKER in out, "bloco HOST-API-DECLS nao foi injectado"
    assert p.END_MARKER in out, "delimitador de fim do bloco ausente"

    for func in ("f2b_fo_mfd_put", "f2b_fo_mfd_get", "f2b_fo_sz_get",
                 "f2b_stream_fill", "f2b_stream_ensure",
                 "f2b_stream_eof_try_complete", "f2b_stream_pre_consume"):
        assert p.DECLS[func] in out, f"declaracao de {func} ausente do bloco"

    for glob in ("g_f2b_fill_fo", "g_f2b_fill_mfd", "g_f2b_fill_sz",
                 "g_f2b_fill_file_pos", "g_f2b_fill_stream",
                 "g_f2b_natural_movie_fo"):
        assert p.GLOBAL_DECLS[glob] in out, f"declaracao global de {glob} ausente do bloco"

    print("[PASS] Teste 1: fixture sem declaracoes previas -> patch_one escreve "
          "as 7 funcoes + 6 globais F2B no bloco HOST-API-DECLS")
    return fixture


def test2_already_declared_noop() -> None:
    """Fixture que ja' declara ensure/eof_try_complete (texto identico ao
    de ppu_recomp_002.cpp:369-370) e nao usa mais nenhum dos 17 -- no-op."""
    tmpdir = Path(tempfile.mkdtemp(prefix="f2b_decls_test2_"))
    fixture = tmpdir / "chunk_002_like.cpp"
    fixture.write_text(FIXTURE_DECLARED, encoding="utf-8")

    orig = fixture.read_text(encoding="utf-8")
    wrote = p.patch_one(fixture)
    assert wrote == 0, f"esperava patch_one()==0 (ja' declarado), veio {wrote}"
    assert fixture.read_text(encoding="utf-8") == orig, \
        "ficheiro foi alterado apesar de ja' ter as declaracoes necessarias"

    print("[PASS] Teste 2: fixture com f2b_stream_ensure/eof_try_complete "
          "byte-idênticas a ppu_recomp_002.cpp:369-370 -> patch_one() == 0 "
          "(nada escrito)")


def test3_idempotent(patched_fixture: Path) -> None:
    """Reaplicar sobre o ficheiro ja' patchado do Teste 1 e' no-op."""
    before = patched_fixture.read_text(encoding="utf-8")
    wrote = p.patch_one(patched_fixture)
    assert wrote == 0, f"segunda corrida devia ser no-op (0), veio {wrote}"
    after = patched_fixture.read_text(encoding="utf-8")
    assert before == after, "reaplicar o patch alterou um ficheiro ja' convergido"

    print("[PASS] Teste 3 (idempotencia): reaplicar patch_one() sobre o "
          "ficheiro do Teste 1 nao altera nada")


PPU_RECOMP_H_STUB = '#include "ppu_context.h"\n'


def test4_compiles_real(patched_fixture: Path, cxx: str) -> None:
    """O ficheiro patchado do Teste 1 compila com clang++ -std=c++20 contra
    ppu_context.h real (via um ppu_recomp.h stub de 1 linha no mesmo dir)."""
    stub_dir = patched_fixture.parent
    (stub_dir / "ppu_recomp.h").write_text(PPU_RECOMP_H_STUB, encoding="utf-8")

    obj = stub_dir / "chunk_used.o"
    cmd = [cxx, "-std=c++20", "-w",
           "-I", str(stub_dir),
           "-I", str(REPO / "runtime" / "ppu"),
           "-I", str(REPO / "include"),
           "-c", str(patched_fixture), "-o", str(obj)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    assert r.returncode == 0, (
        f"compilacao do ficheiro patchado falhou (rc={r.returncode})\n"
        f"cmd: {' '.join(cmd)}\nstderr:\n{r.stderr}")
    assert obj.is_file(), "objecto .o nao foi produzido"

    print("[PASS] Teste 4: ficheiro patchado do Teste 1 compila com "
          f"{cxx} -std=c++20 contra ppu_context.h real")


STUBS_C = '''#include <stdint.h>
unsigned movie_io_pread(unsigned fd, void* dst, unsigned n, unsigned pos) {
    (void)fd; (void)dst; (void)n; (void)pos;
    return 0;
}
int movie_io_is(unsigned fd) { (void)fd; return 0; }
uint32_t vm_read32(uint64_t a) { (void)a; return 0; }
void vm_write32(uint64_t a, uint32_t v) { (void)a; (void)v; }
uint8_t* vm_base = 0;
uint32_t g_wadld_eof_ea = 0;
'''

MAIN_CPP = '''#include "ppu_context.h"
extern "C" void call_all_f2b(ppu_context* ctx);
int main() {
    ppu_context ctx;
    for (int i = 0; i < 32; i++) ctx.gpr[i] = 0;
    call_all_f2b(&ctx);
    return 0;
}
'''


def test5_link_e2e(patched_fixture: Path, cxx: str, cc: str | None) -> None:
    """Prova mais forte: chunk_used.o (Teste 4) + host_gow2_f2b.o (Task 1) +
    stubs.c + main.cpp linkam e o binario corre com exit code 0."""
    if cc is None:
        print("[SKIP] Teste 5 (link E2E): nenhum compilador C encontrado no "
              "PATH -- nada para linkar, rc=0 mesmo assim")
        return

    stub_dir = patched_fixture.parent
    f2b_src = GOW2_DIR / "host_gow2_f2b.c"
    assert f2b_src.is_file(), f"host_gow2_f2b.c (Task 1) nao encontrado: {f2b_src}"

    f2b_obj = stub_dir / "host_gow2_f2b.o"
    r = subprocess.run(
        [cc, "-std=c11", "-w", "-I", str(REPO / "runtime" / "ppu"),
         "-I", str(REPO / "include"), "-c", str(f2b_src), "-o", str(f2b_obj)],
        capture_output=True, text=True)
    assert r.returncode == 0, f"compilacao de host_gow2_f2b.c falhou:\n{r.stderr}"

    stubs_c = stub_dir / "stubs.c"
    stubs_c.write_text(STUBS_C, encoding="utf-8")
    stubs_obj = stub_dir / "stubs.o"
    r = subprocess.run(
        [cc, "-std=c11", "-w", "-c", str(stubs_c), "-o", str(stubs_obj)],
        capture_output=True, text=True)
    assert r.returncode == 0, f"compilacao dos stubs falhou:\n{r.stderr}"

    main_cpp = stub_dir / "main.cpp"
    main_cpp.write_text(MAIN_CPP, encoding="utf-8")
    main_obj = stub_dir / "main.o"
    r = subprocess.run(
        [cxx, "-std=c++20", "-w",
         "-I", str(stub_dir), "-I", str(REPO / "runtime" / "ppu"),
         "-I", str(REPO / "include"),
         "-c", str(main_cpp), "-o", str(main_obj)],
        capture_output=True, text=True)
    assert r.returncode == 0, f"compilacao de main.cpp falhou:\n{r.stderr}"

    chunk_obj = stub_dir / "chunk_used.o"
    assert chunk_obj.is_file(), "chunk_used.o do Teste 4 nao encontrado"

    exe = stub_dir / "f2b_e2e"
    r = subprocess.run(
        [cxx, str(main_obj), str(chunk_obj), str(f2b_obj), str(stubs_obj),
         "-o", str(exe)],
        capture_output=True, text=True)
    assert r.returncode == 0, f"link E2E falhou:\n{r.stderr}"

    r = subprocess.run([str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, (
        f"binario E2E saiu com rc={r.returncode} (esperado 0)\n"
        f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}")

    print("[PASS] Teste 5 (link E2E): chunk_used.o + host_gow2_f2b.o + stubs "
          "+ main.cpp linkam e o binario corre com exit code 0")


def main() -> int:
    print("=== patch_zz_host_api_decls.py -- extensao F2B (02-02-PLAN.md Task 2) ===")

    # Contrato de contagem: 13 pre-existentes (factory/type15/timebase) + 3
    # fios_sticky + 2 giant_lock ja' viviam no ficheiro copiado do irmao (19,
    # medido nesta execucao -- o "13" do plano refere-se so' a factory/type15,
    # nao ao total de DECLS) + 7 F2B novas = 26. GLOBAL_DECLS e' inteiramente
    # novo: 6.
    assert len(p.GLOBAL_DECLS) == 6, f"GLOBAL_DECLS devia ter 6 entradas, tem {len(p.GLOBAL_DECLS)}"
    for sym in ("f2b_fo_mfd_put", "f2b_fo_mfd_get", "f2b_fo_sz_get",
                "f2b_stream_fill", "f2b_stream_ensure",
                "f2b_stream_eof_try_complete", "f2b_stream_pre_consume"):
        assert sym in p.DECLS, f"DECLS falta a entrada F2B {sym}"
    print(f"[PASS] Teste 0: DECLS tem {len(p.DECLS)} entradas (13 factory/type15/"
          f"timebase + 3 fios_sticky + 2 giant_lock pre-existentes + 7 F2B novas); "
          f"GLOBAL_DECLS tem {len(p.GLOBAL_DECLS)} (as 6 F2B novas)")

    patched_fixture = test1_writes_all_f2b_decls()
    test2_already_declared_noop()
    test3_idempotent(patched_fixture)

    cxx = _which_cxx()
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if cxx is None:
        print("[SKIP] Teste 4 (compilacao real): nenhum compilador C++ "
              "encontrado no PATH")
        print("[SKIP] Teste 5 (link E2E): nenhum compilador C++ encontrado "
              "no PATH")
        return 0

    test4_compiles_real(patched_fixture, cxx)
    test5_link_e2e(patched_fixture, cxx, cc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
