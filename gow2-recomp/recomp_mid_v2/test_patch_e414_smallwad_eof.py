#!/usr/bin/env python3
"""Testes de patch_e414_smallwad_eof.py."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e414_smallwad_eof.py"
NEEDLE = (
    "          if(_av>=0x20u){\n"
    "            ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x90);\n"
    "            ctx->gpr[30] = _cs_30;\n"
    "            ctx->gpr[31] = _cs_31;\n"
    "            ctx->gpr[3] = (int64_t)(int32_t)(0);\n"
    "            ctx->lr = ctx->gpr[0];\n"
    "            ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x80);\n"
    "            return;\n"
    "          }\n"
    "          g_trampoline_fn = (void(*)(void*))func_002B9F54; return; }\n"
)


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text(
            "void func_002B9F00(ppu_context* ctx) {\n" + NEEDLE + "}\n"
        )
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        t = (d / "ppu_recomp_001.cpp").read_text()
        assert "E414-SMALLWAD-EOF" in t
        assert "vm_write32((uint32_t)ctx->gpr[31]+0x1ACu, 0u)" in t
        assert "func_002B9F54" in t
        assert 'getenv("PS3_TRACE_BAD10")' in t
        h = hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest()
        r2 = _run(d)
        assert r2.returncode == 0 and "ALREADY" in r2.stdout
        assert hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest() == h
        print("[PASS] smallwad-eof + fallback AREAD + idempotent")
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text("void f(ppu_context* ctx) {}\n")
        assert _run(d).returncode != 0
        print("[PASS] 0x -> rc!=0")
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
