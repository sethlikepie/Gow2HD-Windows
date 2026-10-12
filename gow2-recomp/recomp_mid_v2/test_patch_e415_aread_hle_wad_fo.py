#!/usr/bin/env python3
"""Testes de patch_e415_aread_hle_wad_fo.py."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e415_aread_hle_wad_fo.py"

HLE = (
    "              unsigned _m=f2b_fo_mfd_get(_x);\n"
    "              if(!_m){ uint32_t t=vm_read32(_x+0x38u); if(t&&movie_io_is(t)) _m=t; }\n"
    "              if(_m && movie_io_is(_m)) _fd=_m;\n"
)
SKIP = (
    "        if (((ctx->cr >> 0) & 2)) {\n"
    "          /* E413-SKIP-AREAD: ring already has a header; do not AREAD-HLE */\n"
    "          uint32_t _st=vm_read32((uint32_t)ctx->gpr[31]+0x1A8u);\n"
    "          uint32_t _av=(_st>=0x10000u&&_st<0x4F000000u)?vm_read32(_st+0x10u):0u;\n"
    "          { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_BAD10\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "            if(_on){ fprintf(stderr,\"[2B9F00] inflight=0 av=%u%s\\n\", _av,\n"
    "              _av>=0x20u?\" SKIP-AREAD\":\" -> 2B9F54\"); fflush(stderr);} }\n"
    "          if(_av>=0x20u){\n"
    "            ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x90);\n"
    "            ctx->gpr[30] = _cs_30;\n"
    "            ctx->gpr[31] = _cs_31;\n"
    "            ctx->gpr[3] = (int64_t)(int32_t)(0);\n"
    "            ctx->lr = ctx->gpr[0];\n"
    "            ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x80);\n"
    "            return;\n"
    "          }\n"
    "          /* E414-SMALLWAD-EOF: whole LglScA was in the ring and parsed */\n"
    "          { uint32_t _req=vm_read32((uint32_t)ctx->gpr[31]+0x74u);\n"
    "            if(_req && _req<=65536u){\n"
    "              vm_write32((uint32_t)ctx->gpr[31]+0x1ACu, 0u);\n"
    "              { static int _on2=-1; if(_on2<0){ extern char* getenv(const char*);\n"
    "                const char* _e=getenv(\"PS3_TRACE_BAD10\"); _on2=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "                if(_on2){ fprintf(stderr,\"[2B9F00] SMALLWAD-EOF req=0x%08X av=%u\\n\", _req,_av); fflush(stderr);} }\n"
    "              ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x90);\n"
    "              ctx->gpr[30] = _cs_30;\n"
    "              ctx->gpr[31] = _cs_31;\n"
    "              ctx->gpr[3] = (int64_t)(int32_t)(0);\n"
    "              ctx->lr = ctx->gpr[0];\n"
    "              ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x80);\n"
    "              return; } }\n"
    "          g_trampoline_fn = (void(*)(void*))func_002B9F54; return; }\n"
)
RING = (
    "          /* Small WAD (LglScA 0xC00): preload ring even when 42EC already\n"
    "           * wrote rem — otherwise av=0 and state-1 never AREAD-consumes. */\n"
    "          if(_sz && _sz<=65536u && _c>=0x64u && vm_base){\n"
)

F001 = (
    "void func_002B3D1C(ppu_context* ctx) {\n"
    + HLE
    + "}\n"
    "void func_002B9F00(ppu_context* ctx) {\n"
    + SKIP
    + "}\n"
    "void func_002B441C(ppu_context* ctx) {\n"
    + RING
    + "          }\n"
    "}\n"
)


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text(F001)
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        t = (d / "ppu_recomp_001.cpp").read_text()
        assert "E415-WAD-FO-BIND" in t
        assert "movie_io_open" in t
        assert "f2b_fo_mfd_put" in t
        assert "WAD-FO-BIND fo=" in t
        assert "E413-SKIP-AREAD" not in t
        assert "E414-SMALLWAD-EOF" not in t
        assert "E415-NO-PRELOAD" in t
        assert "if(0 && _sz && _sz<=65536u" in t
        assert "g_trampoline_fn = (void(*)(void*))func_002B9F54; return; }" in t
        # still fulfil through aread_hle AFTER bind (container+8 is STATUS after 42B4)
        assert "ps3_fios_aread_hle" not in t  # fixture HLE site has no call
        h = hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest()
        r2 = _run(d)
        assert r2.returncode == 0 and "ALREADY" in r2.stdout, r2.stdout + r2.stderr
        assert hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest() == h
        print("[PASS] wad-fo-bind + undo skip/eof + no-preload + idempotent")
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text("void f(ppu_context* ctx) {}\n")
        assert _run(d).returncode != 0
        print("[PASS] 0x -> rc!=0")
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
