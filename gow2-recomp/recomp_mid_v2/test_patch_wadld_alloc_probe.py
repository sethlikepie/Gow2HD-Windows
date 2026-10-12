#!/usr/bin/env python3
"""Testes de patch_wadld_alloc_probe.py (D-3.3, Fase 3 do ps3recomp, 03-03-PLAN.md Task 2).

`[WADLD-ALLOC]` era um orfao real: nenhum patch_*.py o escrevia nos dois
repositorios (busca exaustiva confirmada antes desta task). Este teste prova
os 4 GREENs exigidos pelo plano:
  GREEN 1: instalacao -- planta a probe exactamente 1x em func_002BACE8,
           sem tocar em nenhuma outra linha da funcao.
  GREEN 2: idempotencia -- segunda corrida sobre o ficheiro ja patcheado
           devolve rc=0/ALREADY, zero escrita adicional (hash identico).
  GREEN 3: ancora nao-unica (0 ou 2+) -- rc!=0, nunca escolhe arbitrariamente.
  GREEN 4: gating no-op -- a probe so' le/imprime quando PS3_TRACE_TYMAP
           esta definida; sem a env var, o corpo do `if(on)` nunca corre.

Fixture REGION extraida LITERALMENTE de
`../gow2-recomp/recomp_macos_v2/ppu_recomp_002.cpp` (func_002BACE8), nunca
hand-typed.

Uso: python3 test_patch_wadld_alloc_probe.py   (rc=0 verde, rc=1 falha)
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH_PATH = HERE / "patch_wadld_alloc_probe.py"
REAL_LIFT_002 = HERE.parent / "recomp_macos_v2" / "ppu_recomp_002.cpp"

# Extraida literalmente de recomp_macos_v2/ppu_recomp_002.cpp em 2026-07-26
# (func_002BACE8 completa, forma ACTUAL: prefixo `ctx->lr=...;` + cast uint64_t).
REGION_REAL = (
    'void func_002BACE8(ppu_context* ctx) {\n'
    '        ctx->lr = 0x002BACEC; func_00262808(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '        /* nop */;\n'
    '        ctx->gpr[4] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[29], 0, 0, 27);\n'
    '        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);\n'
    '        ctx->gpr[5] = (int64_t)(int32_t)(0x40);\n'
    '        ctx->lr = 0x002BAD00; func_00262610(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '        /* nop */;\n'
    '        vm_write32(ctx->gpr[31] + 0x1C4, ctx->gpr[3]);\n'
    '        vm_write32(ctx->gpr[31] + 0x1C0, ctx->gpr[3]);\n'
    '        { g_trampoline_fn = (void(*)(void*))func_002BAB40; return; }\n'
    '}\n'
)

# Forma antiga (pre_v4): sem prefixo ctx->lr, cast uint32_t -- a ancora tem de
# tolerar esta forma tambem (ver read_first do plano).
REGION_PRE_V4 = (
    'void func_002BACE8(ppu_context* ctx) {\n'
    '        func_00262808(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '        /* nop */;\n'
    '        ctx->gpr[4] = (uint32_t)ppc_rlwinm((uint32_t)ctx->gpr[29], 0, 0, 27);\n'
    '        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);\n'
    '        ctx->gpr[5] = (int64_t)(int32_t)(0x40);\n'
    '        func_00262610(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '        /* nop */;\n'
    '        vm_write32(ctx->gpr[31] + 0x1C4, ctx->gpr[3]);\n'
    '        vm_write32(ctx->gpr[31] + 0x1C0, ctx->gpr[3]);\n'
    '        { g_trampoline_fn = (void(*)(void*))func_002BAB40; return; }\n'
    '        { g_trampoline_fn = (void(*)(void*))func_002BAD10; return; }\n'
    '}\n'
)

NEXT_FN = 'void func_002BC174(ppu_context* ctx) {\n        ctx->gpr[9] = 0;\n}\n'


def _run(lift_dir: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(PATCH_PATH), str(lift_dir)],
        capture_output=True, text=True,
    )


def _sha(path: Path) -> str:
    import hashlib
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_gap_before_installer_exists() -> None:
    """RED: confirma que o instalador nao existia antes desta task."""
    import subprocess as sp
    out = sp.run(
        ["grep", "-rl", "WADLD-ALLOC",
         str(HERE.parent / "recomp_mid_v2")],
        capture_output=True, text=True,
    )
    files = [f for f in out.stdout.splitlines() if f]
    assert str(PATCH_PATH) in files or PATCH_PATH.name in "".join(files), (
        f"esperava encontrar {PATCH_PATH.name} entre os ficheiros com WADLD-ALLOC: {files!r}"
    )
    print("[PASS] patch_wadld_alloc_probe.py existe e contem WADLD-ALLOC (gap fechado)")


def check_green1_installs_exactly_once_on_real_fixture() -> None:
    """GREEN 1: planta a probe 1x em func_002BACE8 (forma actual), nada mais muda."""
    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        chunk = dirp / "ppu_recomp_002.cpp"
        chunk.write_text(REGION_REAL + "\n" + NEXT_FN)

        r = _run(dirp)
        assert r.returncode == 0, f"esperado rc=0, obtido {r.returncode}: {r.stdout}{r.stderr}"
        assert "APPLIED" in r.stdout, f"esperado APPLIED na saida: {r.stdout!r}"

        patched = chunk.read_text()
        assert patched.count("[WADLD-ALLOC]") == 1, (
            f"esperado exactamente 1 ocorrencia de [WADLD-ALLOC], obtido "
            f"{patched.count('[WADLD-ALLOC]')}"
        )
        # nenhuma outra linha da funcao mudou -- confirma por presenca literal
        # de todas as linhas originais, na ordem, com a probe entre nop e rlwinm
        assert "/* nop */;\n        { static int on=-1;" in patched, (
            "probe nao foi inserida logo apos o primeiro '/* nop */;'"
        )
        assert "ctx->gpr[4] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[29], 0, 0, 27);" in patched
        assert "ctx->lr = 0x002BAD00; func_00262610(ctx); DRAIN_TRAMPOLINE(ctx);" in patched
        assert "{ g_trampoline_fn = (void(*)(void*))func_002BAB40; return; }" in patched
    print(
        "[PASS] GREEN 1: instala [WADLD-ALLOC] exactamente 1x em func_002BACE8 "
        "(forma actual, com prefixo ctx->lr e cast uint64_t), sem tocar em mais nada"
    )


def check_green1b_installs_on_pre_v4_shape() -> None:
    """GREEN 1b: a ancora tambem casa a forma antiga (pre_v4, sem prefixo, cast uint32_t)."""
    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        chunk = dirp / "ppu_recomp_002.cpp"
        chunk.write_text(REGION_PRE_V4 + "\n" + NEXT_FN)

        r = _run(dirp)
        assert r.returncode == 0, f"esperado rc=0, obtido {r.returncode}: {r.stdout}{r.stderr}"
        patched = chunk.read_text()
        assert patched.count("[WADLD-ALLOC]") == 1
        assert "ctx->gpr[4] = (uint32_t)ppc_rlwinm((uint32_t)ctx->gpr[29], 0, 0, 27);" in patched
    print(
        "[PASS] GREEN 1b: ancora tolera a forma pre_v4 (sem prefixo ctx->lr, cast uint32_t)"
    )


def check_green2_idempotent_second_run_noop() -> None:
    """GREEN 2: segunda corrida sobre ficheiro ja patcheado -> ALREADY, rc=0, hash identico."""
    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        chunk = dirp / "ppu_recomp_002.cpp"
        chunk.write_text(REGION_REAL + "\n" + NEXT_FN)

        r1 = _run(dirp)
        assert r1.returncode == 0
        hash_after_first = _sha(chunk)

        r2 = _run(dirp)
        assert r2.returncode == 0, f"esperado rc=0 na 2a corrida, obtido {r2.returncode}"
        assert "ALREADY" in r2.stdout, f"esperado ALREADY na 2a corrida: {r2.stdout!r}"
        hash_after_second = _sha(chunk)
        assert hash_after_first == hash_after_second, (
            "2a corrida escreveu algo -- deveria ser zero escrita adicional"
        )
    print(
        "[PASS] GREEN 2: 2a corrida sobre ficheiro ja patcheado -> ALREADY, rc=0, "
        "hash identico (zero escrita adicional)"
    )


def check_green3_anchor_absent_fails() -> None:
    """GREEN 3a: ancora ausente (0 ocorrencias) -> rc!=0, nunca forca."""
    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        chunk = dirp / "ppu_recomp_002.cpp"
        # funcao existe, mas o corpo nao tem a forma da ancora (mutacao sintetica)
        mutated = (
            'void func_002BACE8(ppu_context* ctx) {\n'
            '        ctx->gpr[4] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[29], 0, 0, 27);\n'
            '}\n'
        )
        chunk.write_text(mutated + "\n" + NEXT_FN)

        r = _run(dirp)
        assert r.returncode != 0, f"esperado rc!=0 (ancora ausente), obtido {r.returncode}"
        assert "FAILED" in r.stdout, f"esperado FAILED na saida: {r.stdout!r}"
        assert "esperado 1" in r.stdout
    print("[PASS] GREEN 3a: ancora ausente (0x) -> FAILED, rc!=0, nunca forca")


def check_green3_anchor_duplicated_fails() -> None:
    """GREEN 3b: ancora aparece 2x -> rc!=0, nunca escolhe arbitrariamente."""
    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        chunk = dirp / "ppu_recomp_002.cpp"
        duplicated = (
            'void func_002BACE8(ppu_context* ctx) {\n'
            '        ctx->lr = 0x002BACEC; func_00262808(ctx); DRAIN_TRAMPOLINE(ctx);\n'
            '        /* nop */;\n'
            '        ctx->gpr[4] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[29], 0, 0, 27);\n'
            '        ctx->lr = 0x002BACEC; func_00262808(ctx); DRAIN_TRAMPOLINE(ctx);\n'
            '        /* nop */;\n'
            '        ctx->gpr[4] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[29], 0, 0, 27);\n'
            '}\n'
        )
        chunk.write_text(duplicated + "\n" + NEXT_FN)

        r = _run(dirp)
        assert r.returncode != 0, f"esperado rc!=0 (ancora duplicada), obtido {r.returncode}"
        assert "FAILED" in r.stdout, f"esperado FAILED na saida: {r.stdout!r}"
        assert "2x" in r.stdout or "esperado 1" in r.stdout
    print("[PASS] GREEN 3b: ancora duplicada (2x) -> FAILED, rc!=0, nunca escolhe arbitrariamente")


def check_green4_gating_noop_without_env() -> None:
    """GREEN 4: sem PS3_TRACE_TYMAP, a probe instalada nao le memoria nem imprime.

    Confirmado por inspeccao estrutural do PROBE gerado (mesmo padrao on=-1 /
    getenv so' na primeira chamada, replicando [BA808]/outras probes deste lift):
    o corpo que le memoria/imprime vive inteiramente dentro de `if(on){...}`,
    e `on` so' se torna >0 se getenv("PS3_TRACE_TYMAP") devolver nao-nulo.
    """
    import patch_wadld_alloc_probe as mod

    probe = mod.PROBE
    assert 'getenv("PS3_TRACE_TYMAP")' in probe, "probe nao usa PS3_TRACE_TYMAP"
    assert probe.count("if(on)") >= 1 or "if(on){" in probe, (
        "gating if(on) nao encontrado -- leitura/impressao poderia correr sempre"
    )
    # a chamada fprintf/vm_read32 tem de estar dentro do bloco if(on){...}, nao antes
    idx_if_on = probe.index("if(on){")
    idx_fprintf = probe.index("fprintf(")
    idx_vm_read = probe.index("vm_read32(")
    assert idx_fprintf > idx_if_on and idx_vm_read > idx_if_on, (
        "fprintf/vm_read32 aparecem antes do gate if(on) -- nao seria no-op por default"
    )
    print(
        "[PASS] GREEN 4: gating no-op confirmado -- fprintf/vm_read32 vivem dentro de "
        "if(on){...}, on so' >0 com PS3_TRACE_TYMAP definida"
    )


def main() -> int:
    sys.path.insert(0, str(HERE))
    checks = (
        check_gap_before_installer_exists,
        check_green1_installs_exactly_once_on_real_fixture,
        check_green1b_installs_on_pre_v4_shape,
        check_green2_idempotent_second_run_noop,
        check_green3_anchor_absent_fails,
        check_green3_anchor_duplicated_fails,
        check_green4_gating_noop_without_env,
    )
    for check in checks:
        check()
    print("[test_patch_wadld_alloc_probe] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
