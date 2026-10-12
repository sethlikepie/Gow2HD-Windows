#!/usr/bin/env python3
"""Testes de patch_e403_bad10_lr_probe.py — after-return r3 no call site lr=0x41F6C.

Prova (fixture temp, applier REAL, zero valores guest hand-typed):
  GREEN 1: instala entry 1x + after-return 1x; r3 impresso e' ctx->gpr[3] apos DRAIN.
  GREEN 2: 2a corrida ALREADY, rc=0, hash identico.
  GREEN 3: agulha 0 ou 2+ -> rc!=0, nao escolhe arbitrariamente.
  GREEN 4: corpo de fprintf so' corre se PS3_TRACE_BAD10 estiver set.

Uso: python3 test_patch_e403_bad10_lr_probe.py
"""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH_PATH = HERE / "patch_e403_bad10_lr_probe.py"

# Forma literal do lift (recomp_macos_v2), so' o que a ancora precisa.
FN_BODY = (
    'void func_002BAD10(ppu_context* ctx) {\n'
    '        uint64_t _cs_28 = ctx->gpr[28];\n'
    '}\n'
)
CALL_SITE = (
    'void func_00041D5C(ppu_context* ctx) {\n'
    '        ctx->gpr[4] = ctx->gpr[4] | 0x210;\n'
    '        ctx->lr = 0x00041F6C; func_002BAD10(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '        /* nop */;\n'
    '        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x7554);\n'
    '}\n'
)
OTHER_CALL = (
    'void func_00041FC8_site(ppu_context* ctx) {\n'
    '        ctx->lr = 0x00041FCC; func_002BAD10(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '}\n'
)


def _run(lift_dir: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(PATCH_PATH), str(lift_dir)],
        capture_output=True, text=True,
    )


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _write_lift(d: Path, *, fn: str = FN_BODY, call: str = CALL_SITE) -> None:
    (d / "ppu_recomp_001.cpp").write_text(fn)
    (d / "ppu_recomp_000.cpp").write_text(call + OTHER_CALL)


def check_green1_installs_entry_and_after_return() -> None:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        _write_lift(d)
        r = _run(d)
        assert r.returncode == 0, f"rc={r.returncode} stdout={r.stdout!r} stderr={r.stderr!r}"
        c0 = (d / "ppu_recomp_000.cpp").read_text()
        c1 = (d / "ppu_recomp_001.cpp").read_text()
        assert c1.count("E403-BAD10-LR") == 1
        assert c1.count("[BAD10] #%u entry") == 1
        assert c0.count("E403-BAD10-RET-41F6C") == 1
        assert c0.count("[BAD10] ret lr=0x00041F6C r3=0x%08X") == 1
        # r3 e' o registo vivo apos DRAIN, nao uma re-leitura/recomputo
        assert "(uint32_t)ctx->gpr[3]" in c0
        assert "vm_read32" not in c0.split("E403-BAD10-RET-41F6C", 1)[1].split("}", 1)[0]
        # o outro call site (41FCC) nao foi tocado
        assert c0.count("ctx->lr = 0x00041FCC; func_002BAD10(ctx); DRAIN_TRAMPOLINE(ctx);") == 1
        assert "RET-41FCC" not in c0
        # ordem: DRAIN, depois a sonda, depois o nop original
        assert (
            "DRAIN_TRAMPOLINE(ctx);\n"
            "        /* E403-BAD10-RET-41F6C */" in c0
        )
        assert "/* nop */;" in c0
    print("[PASS] GREEN 1: entry 1x + after-return r3 1x no call site 0x41F6C")


def check_green2_idempotent() -> None:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        _write_lift(d)
        r1 = _run(d)
        assert r1.returncode == 0
        h0 = _sha(d / "ppu_recomp_000.cpp")
        h1 = _sha(d / "ppu_recomp_001.cpp")
        r2 = _run(d)
        assert r2.returncode == 0, f"2a rc={r2.returncode} {r2.stdout}{r2.stderr}"
        assert "ALREADY" in r2.stdout, r2.stdout
        assert _sha(d / "ppu_recomp_000.cpp") == h0
        assert _sha(d / "ppu_recomp_001.cpp") == h1
    print("[PASS] GREEN 2: 2a corrida ALREADY, rc=0, hash identico")


def check_green3_absent_fails() -> None:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        _write_lift(d, call="void func_00041D5C(ppu_context* ctx) {\n        ctx->gpr[3]=0;\n}\n")
        r = _run(d)
        assert r.returncode != 0, f"esperado rc!=0 (ret needle 0x), obtido {r.returncode}"
        blob = r.stdout + r.stderr
        assert "esperado 1" in blob or "FAILED" in blob or "0x" in blob
    print("[PASS] GREEN 3a: after-return ausente (0x) -> rc!=0")


def check_green3_duplicate_fails() -> None:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        dup = (
            'void func_00041D5C(ppu_context* ctx) {\n'
            '        ctx->lr = 0x00041F6C; func_002BAD10(ctx); DRAIN_TRAMPOLINE(ctx);\n'
            '        ctx->lr = 0x00041F6C; func_002BAD10(ctx); DRAIN_TRAMPOLINE(ctx);\n'
            '}\n'
        )
        _write_lift(d, call=dup)
        r = _run(d)
        assert r.returncode != 0, f"esperado rc!=0 (2x), obtido {r.returncode} {r.stdout}{r.stderr}"
        blob = r.stdout + r.stderr
        assert "2x" in blob or "esperado 1" in blob
        # nao escolheu o primeiro em silencio
        text = (d / "ppu_recomp_000.cpp").read_text()
        assert "E403-BAD10-RET-41F6C" not in text
    print("[PASS] GREEN 3b: after-return duplicada (2x) -> rc!=0, nao escreve")


def check_green4_gate_noop_without_env() -> None:
    sys.path.insert(0, str(HERE))
    import patch_e403_bad10_lr_probe as P  # noqa: E402
    for blob, fmt in (
        (P.ENTRY_PROBE, "[BAD10] #%u entry"),
        (P.RET_REPL, "[BAD10] ret lr=0x00041F6C r3="),
    ):
        assert 'getenv("PS3_TRACE_BAD10")' in blob
        assert "if(_on)" in blob
        on_block = blob.split("if(_on)", 1)[1]
        assert fmt in on_block, f"fprintf fora do if(_on): {blob!r}"
        before = blob.split("if(_on)", 1)[0]
        assert fmt not in before
    print("[PASS] GREEN 4: fprintf so' dentro de if(_on) gated por PS3_TRACE_BAD10")


def main() -> int:
    check_green1_installs_entry_and_after_return()
    check_green2_idempotent()
    check_green3_absent_fails()
    check_green3_duplicate_fails()
    check_green4_gate_noop_without_env()
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
