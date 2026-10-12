#!/usr/bin/env python3
"""Testes de patch_e407_441c_poll_then_epilogue.py."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e407_441c_poll_then_epilogue.py"

F4340 = (
    'void func_002B4340(ppu_context* ctx) {\n'
    '        if ((!((ctx->cr >> 0) & 2))) { /* E406-441C-CALL */ func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx); return; }\n'
    '        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x1B0);\n'
    '        ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x1A0);\n'
    '        return;\n'
    '}\n'
)
F43C0 = (
    'void func_002B43C0(ppu_context* ctx) {\n'
    '        { /* E406-441C-CALL */ func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx); return; }\n'
    '}\n'
)
F43DC = (
    'void func_002B43DC(ppu_context* ctx) {\n'
    '        { /* E406-441C-CALL */ func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx); return; }\n'
    '}\n'
)
F441C = (
    'void func_002B441C(ppu_context* ctx) {\n'
    '            ctx->gpr[3] = (_p4 != 0u || _p10 != 0u) ? 1 : 0;\n'
    '            /* E406-441C-NO-BADF4: C-return to BAD10 after-DRAIN; that site\n'
    '             * trampolines BADF4 with live frame/r31 when r3!=0. */\n'
    '        }\n'
    '        ctx->gpr[28] = _cs_28;\n'
    '        ctx->gpr[29] = _cs_29;\n'
    '        ctx->gpr[30] = _cs_30;\n'
    '        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x1B0);\n'
    '        ctx->gpr[31] = _cs_31;\n'
    '        ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x1A0);\n'
    '        ctx->lr = ctx->gpr[0];\n'
    '        ctx->gpr[3] = (int64_t)(int32_t)ctx->gpr[3];\n'
    '        return;\n'
    '}\n'
)
FBAD78 = (
    'void func_002BAD78(ppu_context* ctx) {\n'
    '        vm_write32(ctx->gpr[31] + 0x1CC, ctx->gpr[0]);\n'
    '        ctx->lr = 0x002BADA4; func_002B4340(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '        /* nop */;\n'
    '        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[28], 0, 22, 22);\n'
    '}\n'
)
FBAD10 = (
    'void func_002BAD10(ppu_context* ctx) {\n'
    '        vm_write32(ctx->gpr[31] + 0x1CC, ctx->gpr[0]);\n'
    '        ctx->lr = 0x002BADA4; func_002B4340(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '        /* nop */;\n'
    '        /* E403-BAD10-RET-41F6C already here */\n'
    '        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[28], 0, 22, 22);\n'
    '}\n'
)


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def _write(d: Path, **over: str) -> None:
    (d / "ppu_recomp_001.cpp").write_text(
        over.get("f4340", F4340) + over.get("f43c0", F43C0) + over.get("f441c", F441C) + over.get("fbad10", FBAD10)
    )
    (d / "ppu_recomp_002.cpp").write_text(over.get("f43dc", F43DC))
    (d / "ppu_recomp_005.cpp").write_text(over.get("fbad78", FBAD78))


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        _write(d)
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        c1 = (d / "ppu_recomp_001.cpp").read_text()
        c2 = (d / "ppu_recomp_002.cpp").read_text()
        c5 = (d / "ppu_recomp_005.cpp").read_text()
        assert "E407-441C-POLL" in c1
        assert "ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x1A0);" not in c1.split("void func_002B441C", 1)[1].split("void func_002BAD10", 1)[0]
        assert "E407-441C-THEN-EPILOGUE" in c1
        assert "DRAIN_TRAMPOLINE(ctx); return;" not in c1.split("THEN-EPILOGUE", 1)[1].split("\n", 1)[0]
        assert "E407-441C-THEN-439C" in c1 and "E407-441C-THEN-439C" in c2
        assert "func_002B439C" in c2
        assert "E407-BAD78-RET" in c5
        assert "via=BAD78" in c5
        assert "E407-BAD78-RET" not in c1  # BAD10 not restamped
        h = hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest()
        r2 = _run(d)
        assert r2.returncode == 0 and "ALREADY" in r2.stdout
        assert hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest() == h
        print("[PASS] install + 441C poll-only + 439C + BAD78 probe + idempotent")

    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text("void f(ppu_context* ctx) {}\n")
        assert _run(d).returncode != 0
        print("[PASS] 0x -> rc!=0")

    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
