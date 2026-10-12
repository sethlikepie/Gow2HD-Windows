#!/usr/bin/env python3
"""Testes de patch_e408_42ec_size_on_poll.py."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e408_42ec_size_on_poll.py"

F441C = (
    'void func_002B441C(ppu_context* ctx) {\n'
    '        /* E407-441C-POLL: no pop -- caller 4340/439C is the epilogue */\n'
    '        ctx->gpr[3] = (int64_t)(int32_t)ctx->gpr[3];\n'
    '        return;\n'
    '}\n'
)
FBAD78 = (
    'void func_002BAD78(ppu_context* ctx) {\n'
    '        /* E407-BAD78-RET */ { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n'
    '          const char* _e=getenv("PS3_TRACE_BAD10"); _on=(_e&&*_e&&*_e!=\'0\')?1:0; }\n'
    '          if(_on){ fprintf(stderr,"[BAD10] after-4340 r3=0x%08X r28=0x%08X via=BAD78\\n",\n'
    '            (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[28]); fflush(stderr);} }\n'
    '}\n'
)


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text(F441C)
        (d / "ppu_recomp_005.cpp").write_text(FBAD78)
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        c1 = (d / "ppu_recomp_001.cpp").read_text()
        c5 = (d / "ppu_recomp_005.cpp").read_text()
        assert "E408-42EC-SIZE" in c1
        assert "vm_write32(_c+0x10u, _sz)" in c1
        assert "getenv(\"PS3_TRACE_BAD10\")" in c1
        assert c1.split("if(_on)")[1].count("[441C] 42EC") == 1
        assert "E408-BAD78-REM" not in c5  # stamped via extra fields, not that marker
        assert "fo48=" in c5
        assert "vm_read32(_ts+0x74u)" in c5
        h1 = hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest()
        r2 = _run(d)
        assert r2.returncode == 0 and "ALREADY" in r2.stdout
        assert hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest() == h1
        print("[PASS] install + 42EC copy + BAD78 rem dump + idempotent")
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text("void f(ppu_context* ctx) {}\n")
        assert _run(d).returncode != 0
        print("[PASS] 0x -> rc!=0")
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
