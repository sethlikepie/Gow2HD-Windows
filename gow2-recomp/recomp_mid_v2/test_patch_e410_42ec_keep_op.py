#!/usr/bin/env python3
"""Testes de patch_e410_42ec_keep_op.py."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e410_42ec_keep_op.py"

F42EC = (
    'void func_002B42EC(ppu_context* ctx) {\n'
    '        ctx->gpr[9] = vm_read32(ctx->gpr[31] + 0x4);\n'
    '        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0xC);\n'
    '        ctx->gpr[0] = ctx->gpr[0] | ((uint64_t)0x1 << 16);\n'
    '        ctx->gpr[11] = vm_read64(ctx->gpr[9] + 0x48);\n'
    '        vm_write32(ctx->gpr[31] + 0xC, ctx->gpr[0]);\n'
    '        vm_write32(ctx->gpr[31] + 0x10, ctx->gpr[11]);\n'
    '        { g_trampoline_fn = (void(*)(void*))func_002B42B4; return; }\n'
    '}\n'
    'void func_002B4330(ppu_context* ctx) {\n'
    '        { g_trampoline_fn = (void(*)(void*))func_002B42B4; return; }\n'
    '}\n'
)
F4274 = (
    'void func_002B4274(ppu_context* ctx) {\n'
    '            ps3_fios_sticky_publish(_io);\n'
    '            { static int _n=0; if(_n++<8){\n'
    '              fprintf(stderr,"[FIOSOPEN] F2B-RESTATUS fo=0x%08X io=0x%08X was+44=0x%08X → 0\\n",\n'
    '                _fo, _io, _was44);\n'
    '              fflush(stderr); } }\n'
    '          }\n'
    '        }\n'
    '        /* F2B-STREAM-PUMP: host pread */\n'
    '}\n'
)


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_003.cpp").write_text(F42EC)
        (d / "ppu_recomp_001.cpp").write_text(F4274)
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        c3 = (d / "ppu_recomp_003.cpp").read_text()
        c1 = (d / "ppu_recomp_001.cpp").read_text()
        assert "E410-42EC-KEEP-OP" in c3
        body = c3.split("void func_002B42EC", 1)[1].split("void func_002B4330", 1)[0]
        assert "func_002B42B4" not in body
        assert "ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x90);" in body
        assert "vm_write32(ctx->gpr[31] + 0x8" not in body
        assert "func_0030AE58" not in body
        assert "func_002B42B4" in c3.split("void func_002B4330", 1)[1]
        assert "E410-4274-FO-SIZE" in c1
        assert "vm_write32(_fo+0x4Cu" in c1
        assert 'getenv("PS3_TRACE_BAD10")' in c3
        assert "[42EC] KEEP-OP" in c3.split("if(_on)", 1)[1]
        h = hashlib.sha256((d / "ppu_recomp_003.cpp").read_bytes()).hexdigest()
        r2 = _run(d)
        assert r2.returncode == 0 and "ALREADY" in r2.stdout
        assert hashlib.sha256((d / "ppu_recomp_003.cpp").read_bytes()).hexdigest() == h
        print("[PASS] 42EC keep-op + FO size + 4330 intact + idempotent")
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text("void f(ppu_context* ctx) {}\n")
        assert _run(d).returncode != 0
        print("[PASS] 0x -> rc!=0")
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
