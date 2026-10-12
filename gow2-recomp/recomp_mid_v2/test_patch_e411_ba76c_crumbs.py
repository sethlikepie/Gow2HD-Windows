#!/usr/bin/env python3
"""Testes de patch_e411_ba76c_crumbs.py."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e411_ba76c_crumbs.py"

BODY = (
    'void func_002BA990(ppu_context* ctx) {\n'
    '        ctx->gpr[3] = 0;\n'
    '}\n'
    'void func_002BA9BC(ppu_context* ctx) {\n'
    '        ctx->gpr[3] = 0;\n'
    '}\n'
    'void func_002B9F00(ppu_context* ctx) {\n'
    '        ctx->lr = 0x002B9F20; func_002B45D0(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '        /* nop */;\n'
    '}\n'
)


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text(BODY)
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        t = (d / "ppu_recomp_001.cpp").read_text()
        assert t.count("E411-BA76C-CRUMB") == 4  # BA990, BA9BC, pre, post
        assert "[BA76C] BA990-entry" in t
        assert "[BA76C] BA9BC-entry" in t
        assert "[BA76C] 2B9F00-pre-45D0" in t
        assert "[BA76C] 2B9F00-post-45D0" in t
        assert 'getenv("PS3_TRACE_BAD10")' in t
        h = hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest()
        r2 = _run(d)
        assert r2.returncode == 0 and "ALREADY" in r2.stdout
        assert hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest() == h
        print("[PASS] 4 crumbs + gated + idempotent")
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text("void f(ppu_context* ctx) {}\n")
        assert _run(d).returncode != 0
        print("[PASS] 0x -> rc!=0")
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
