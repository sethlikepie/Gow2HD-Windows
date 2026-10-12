#!/usr/bin/env python3
"""Testes de patch_e404_ce0a0_r31_reload.py — unique needle, idempotente, rc!=0 se 0/2+."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e404_ce0a0_r31_reload.py"

REGION = (
    'void func_000CE0A0(ppu_context* ctx) {\n'
    '        ctx->lr = 0x000CE0EC; func_00194D3C(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '        /* nop */;\n'
    '        ctx->lr = 0x000CE0F4; func_000CDE3C(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x0);\n'
    '}\n'
)


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        p = d / "ppu_recomp_000.cpp"
        p.write_text(REGION)
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        t = p.read_text()
        assert t.count("E404-CE0A0-R31") == 1
        assert "ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x5E70);" in t
        assert t.index("CDE3C(ctx); DRAIN") < t.index("gpr[31] = vm_read32")
        assert t.index("gpr[31] = vm_read32") < t.index("vm_read32(ctx->gpr[31] + 0x0)")
        h = hashlib.sha256(p.read_bytes()).hexdigest()
        r2 = _run(d)
        assert r2.returncode == 0 and "ALREADY" in r2.stdout
        assert hashlib.sha256(p.read_bytes()).hexdigest() == h
        print("[PASS] install 1x + idempotent")

    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_000.cpp").write_text("void f(ppu_context* ctx) { }\n")
        r = _run(d)
        assert r.returncode != 0
        print("[PASS] 0x -> rc!=0")

    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_000.cpp").write_text(REGION + REGION)
        r = _run(d)
        assert r.returncode != 0
        assert "E404-CE0A0-R31" not in (d / "ppu_recomp_000.cpp").read_text()
        print("[PASS] 2x -> rc!=0, no write")

    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
