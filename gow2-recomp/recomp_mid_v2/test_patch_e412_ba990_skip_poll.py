#!/usr/bin/env python3
"""Testes de patch_e412_ba990_skip_poll.py."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e412_ba990_skip_poll.py"

F990 = (
    'void func_002BA990(ppu_context* ctx) {\n'
    '        ctx->gpr[3] = ctx->gpr[31] + (int64_t)(0x64);\n'
    '        ctx->lr = 0x002BA998; func_002B4224(ctx); DRAIN_TRAMPOLINE(ctx);\n'
    '}\n'
)
F42EC = (
    'void func_002B42EC(ppu_context* ctx) {\n'
    '        vm_write32(ctx->gpr[31] + 0x10, ctx->gpr[11]);\n'
    '        /* E410-42EC-KEEP-OP: 42B4 deletes io; pop 4224 frame, keep +8 */\n'
    '        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n'
    '          const char* _e=getenv("PS3_TRACE_BAD10"); _on=(_e&&*_e&&*_e!=\'0\')?1:0; }\n'
    '          if(_on){ fprintf(stderr,"[42EC] KEEP-OP rem=0x%08X io=0x%08X fo=0x%08X\\n",\n'
    '            (uint32_t)ctx->gpr[11], vm_read32((uint32_t)ctx->gpr[31]+8u),\n'
    '            (uint32_t)ctx->gpr[9]); fflush(stderr);} }\n'
    '        { uint64_t _cs_31 = vm_read64(ctx->gpr[1] + 0x88);\n'
    '          ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xA0);\n'
    '          ctx->gpr[3] = (int64_t)(int32_t)(1);\n'
    '          ctx->gpr[31] = _cs_31;\n'
    '          ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x90);\n'
    '          ctx->gpr[3] = (int64_t)(int32_t)ctx->gpr[3];\n'
    '          ctx->lr = ctx->gpr[0];\n'
    '          return; }\n'
    '}\n'
)


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text(F990)
        (d / "ppu_recomp_003.cpp").write_text(F42EC)
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        c1 = (d / "ppu_recomp_001.cpp").read_text()
        c3 = (d / "ppu_recomp_003.cpp").read_text()
        assert "E412-BA990-SKIP-POLL" in c1
        assert "g_trampoline_fn = (void(*)(void*))func_002BA9BC" in c1
        assert "func_002B4224" in c1  # original poll kept as else
        assert "E412-42EC-RESTORE" in c3
        assert "func_002B42B4" in c3
        assert "KEEP-OP rem=" not in c3
        assert 'getenv("PS3_TRACE_BAD10")' in c1
        h = hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest()
        r2 = _run(d)
        assert r2.returncode == 0 and "ALREADY" in r2.stdout
        assert hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest() == h
        print("[PASS] skip-poll + 42B4 restore + idempotent")
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text("void f(ppu_context* ctx) {}\n")
        assert _run(d).returncode != 0
        print("[PASS] 0x -> rc!=0")
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
