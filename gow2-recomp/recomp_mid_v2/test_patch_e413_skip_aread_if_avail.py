#!/usr/bin/env python3
"""Testes de patch_e413_skip_aread_if_avail.py."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e413_skip_aread_if_avail.py"
NEEDLE = (
    '        ctx->gpr[4] = vm_read32(ctx->gpr[31] + 0x1A4);\n'
    '        { int64_t a = (int32_t)ctx->gpr[4]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n'
    '        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_002B9F54; return; }\n'
)


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text(
            'void func_002B9F00(ppu_context* ctx) {\n' + NEEDLE + '}\n'
        )
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        t = (d / "ppu_recomp_001.cpp").read_text()
        assert "E413-SKIP-AREAD" in t
        assert "_av>=0x20u" in t
        assert "func_002B9F54" in t  # fallback still present
        assert 'getenv("PS3_TRACE_BAD10")' in t
        assert "[2B9F00] inflight=0 av=" in t.split("if(_on)", 1)[1]
        h = hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest()
        r2 = _run(d)
        assert r2.returncode == 0 and "ALREADY" in r2.stdout
        assert hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest() == h
        print("[PASS] skip-aread + fallback 2B9F54 + idempotent")
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text("void f(ppu_context* ctx) {}\n")
        assert _run(d).returncode != 0
        print("[PASS] 0x -> rc!=0")
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
