#!/usr/bin/env python3
"""Testes de patch_e405_441c_op4_cs31.py (snapshot do op vivo)."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e405_441c_op4_cs31.py"

REGION = (
    'void func_002B441C(ppu_context* ctx) {\n'
    '        uint64_t _cs_28 = vm_read64(ctx->gpr[1] + 0x180);\n'
    '        uint64_t _cs_31 = vm_read64(ctx->gpr[1] + 0x198);\n'
    '        ctx->gpr[29] = ppc_rldicl(ctx->gpr[28], 0, 32);\n'
    '        if (((ctx->cr >> 0) & 2)) goto loc_002B4420;\n'
    '        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x4);\n'
    '        ctx->gpr[28] = _cs_28;\n'
    '}\n'
)


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        p = d / "ppu_recomp_001.cpp"
        p.write_text(REGION)
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        t = p.read_text()
        assert "E405-441C-OP-SNAP" in t
        assert "uint64_t _op = ctx->gpr[31];" in t
        assert "vm_read32((uint32_t)_op + 0x4)" in t
        h = hashlib.sha256(p.read_bytes()).hexdigest()
        r2 = _run(d)
        assert r2.returncode == 0 and "ALREADY" in r2.stdout
        assert hashlib.sha256(p.read_bytes()).hexdigest() == h
        print("[PASS] install + idempotent")
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text("void f(ppu_context* ctx) {}\n")
        assert _run(d).returncode != 0
        print("[PASS] 0x -> rc!=0")
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
