#!/usr/bin/env python3
"""Testes de patch_e406_441c_call_no_badf4.py.

GREEN 1: 4340/43C0/43DC passam a chamar 441C; BADF4 sai de 441C; r3 continua
         a ser o boolean de _op (nao recomputado).
GREEN 2: 2a corrida ALREADY, rc=0, hash identico.
GREEN 3: agulha 0 ou contagem errada -> rc!=0, nao escreve.
GREEN 4: 441C nao volta a setar g_trampoline_fn = BADF4.
"""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e406_441c_call_no_badf4.py"

F4340 = (
    'void func_002B4340(ppu_context* ctx) {\n'
    '        ctx->gpr[3] = (int64_t)(int32_t)(1);\n'
    '        if ((!((ctx->cr >> 0) & 2))) { g_trampoline_fn = (void(*)(void*))func_002B441C; return; }\n'
    '        ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x1A0);\n'
    '        return;\n'
    '}\n'
)
F43C0 = (
    'void func_002B43C0(ppu_context* ctx) {\n'
    '        ctx->gpr[3] = (int64_t)(int32_t)(1);\n'
    '        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_002B439C; return; }\n'
    '        { g_trampoline_fn = (void(*)(void*))func_002B441C; return; }\n'
    '}\n'
)
F43DC = (
    'void func_002B43DC(ppu_context* ctx) {\n'
    '        ctx->gpr[3] = (int64_t)(int32_t)(1);\n'
    '        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_002B439C; return; }\n'
    '        { g_trampoline_fn = (void(*)(void*))func_002B441C; return; }\n'
    '}\n'
)
F441C = (
    'void func_002B441C(ppu_context* ctx) {\n'
    '        uint64_t _op = ctx->gpr[31]; /* E405-441C-OP-SNAP: live op from 4340 */\n'
    '            ctx->gpr[3] = (_p4 != 0u || _p10 != 0u) ? 1 : 0;\n'
    '            if (ctx->gpr[3]) {\n'
    '                /* 441C is 4340\'s epilogue via trampoline; BAD10\'s after-DRAIN\n'
    '                 * (r3!=0 -> BADF4 sync ticks) does not run. Hand off to BADF4. */\n'
    '                g_trampoline_fn = (void(*)(void*))func_002BADF4;\n'
    '            }\n'
    '        ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x1A0);\n'
    '        return;\n'
    '}\n'
)
# BAD10's legitimate trampoline must stay.
FBAD10 = (
    'void func_002BAD10(ppu_context* ctx) {\n'
    '        if ((!((ctx->cr >> 0) & 2))) { g_trampoline_fn = (void(*)(void*))func_002BADF4; return; }\n'
    '}\n'
)


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def _sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def _write(d: Path, *, f4340=F4340, f43c0=F43C0, f43dc=F43DC, f441c=F441C) -> None:
    (d / "ppu_recomp_001.cpp").write_text(f4340 + f43c0 + f441c + FBAD10)
    (d / "ppu_recomp_002.cpp").write_text(f43dc)


def check_green1() -> None:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        _write(d)
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        c1 = (d / "ppu_recomp_001.cpp").read_text()
        c2 = (d / "ppu_recomp_002.cpp").read_text()
        assert c1.count("E406-441C-CALL") == 2  # 4340 + 43C0
        assert c2.count("E406-441C-CALL") == 1  # 43DC
        assert "func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx); return;" in c1
        assert "func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx); return;" in c2
        assert "g_trampoline_fn = (void(*)(void*))func_002B441C" not in c1
        assert "g_trampoline_fn = (void(*)(void*))func_002B441C" not in c2
        assert "E406-441C-NO-BADF4" in c1
        assert "g_trampoline_fn = (void(*)(void*))func_002BADF4" not in c1.split("void func_002B441C", 1)[1].split("void func_002BAD10", 1)[0]
        # BAD10 keeps its trampoline
        assert "g_trampoline_fn = (void(*)(void*))func_002BADF4; return;" in c1
        assert "ctx->gpr[3] = (_p4 != 0u || _p10 != 0u) ? 1 : 0;" in c1
    print("[PASS] GREEN 1: call+DRAIN, BADF4 fora de 441C, boolean _op intacto")


def check_green2() -> None:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        _write(d)
        assert _run(d).returncode == 0
        h1 = _sha(d / "ppu_recomp_001.cpp")
        h2 = _sha(d / "ppu_recomp_002.cpp")
        r2 = _run(d)
        assert r2.returncode == 0, r2.stdout + r2.stderr
        assert "ALREADY" in r2.stdout
        assert _sha(d / "ppu_recomp_001.cpp") == h1
        assert _sha(d / "ppu_recomp_002.cpp") == h2
    print("[PASS] GREEN 2: ALREADY, hash identico")


def check_green3_absent() -> None:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text("void f(ppu_context* ctx) {}\n")
        r = _run(d)
        assert r.returncode != 0
        assert "E406-441C-CALL" not in (d / "ppu_recomp_001.cpp").read_text()
    print("[PASS] GREEN 3a: 0x -> rc!=0")


def check_green3_wrong_bare_count() -> None:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        _write(d, f43dc="void func_002B43DC(ppu_context* ctx) { return; }\n")
        r = _run(d)
        assert r.returncode != 0, r.stdout + r.stderr
        blob = (d / "ppu_recomp_001.cpp").read_text() + (d / "ppu_recomp_002.cpp").read_text()
        assert "E406-441C-CALL" not in blob
    print("[PASS] GREEN 3b: 43C0/43DC !=2 -> rc!=0, nao escreve")


def check_green4() -> None:
    sys.path.insert(0, str(HERE))
    import patch_e406_441c_call_no_badf4 as P  # noqa: E402
    assert "func_002BADF4" not in P.REPL_BADF4
    assert "func_002B441C(ctx); DRAIN_TRAMPOLINE(ctx)" in P.REPL_4340
    assert "g_trampoline_fn" not in P.REPL_4340
    assert "g_trampoline_fn" not in P.REPL_BARE
    print("[PASS] GREEN 4: REPL nao reintroduz trampoline BADF4/441C")


def main() -> int:
    check_green1()
    check_green2()
    check_green3_absent()
    check_green3_wrong_bare_count()
    check_green4()
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
