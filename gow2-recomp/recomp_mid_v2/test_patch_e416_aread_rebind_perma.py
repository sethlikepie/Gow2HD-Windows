#!/usr/bin/env python3
"""Testes de patch_e416_aread_rebind_perma.py."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE / "patch_e416_aread_rebind_perma.py"

NEEDLE = (
    "              /* E415-WAD-FO-BIND: natural file_new FO has no mfd; open by path */\n"
    "              if(!_m){\n"
    "                uint32_t _p30=vm_read32(_x+0x30u); char _pt[96]; _pt[0]=0;\n"
    "                if(_p30>=0x10000u && _p30<0x4F000000u){\n"
    "                  for(int _i=0;_i<95;_i++){ unsigned char _ch=(unsigned char)vm_read8(_p30+_i);\n"
    "                    _pt[_i]=(char)_ch; if(!_ch) break; }\n"
    "                  _pt[95]=0; }\n"
    "                if(_pt[0] && (strstr(_pt,\"wad\")||strstr(_pt,\"WAD\")||strstr(_pt,\"lgl\")||strstr(_pt,\"Lgl\")\n"
    "                    ||strstr(_pt,\"perm\")||strstr(_pt,\"Perm\"))){\n"
    "                  unsigned _osz=0; _m=movie_io_open(_pt, &_osz);\n"
    "                  if(!_m){ const char* _b=_pt; for(const char* _s=_pt;*_s;_s++) if(*_s=='/') _b=_s+1;\n"
    "                    _m=movie_io_open(_b, &_osz); }\n"
    "                  if(_m && movie_io_is(_m)){\n"
    "                    f2b_fo_mfd_put(_x, _m, _osz);\n"
    "                    { static int _onb=-1; if(_onb<0){ extern char* getenv(const char*);\n"
    "                      const char* _e=getenv(\"PS3_TRACE_AREAD\"); if(!_e||!*_e) _e=getenv(\"PS3_TRACE_BAD10\");\n"
    "                      _onb=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "                      if(_onb){ fprintf(stderr,\"[2B3D1C] WAD-FO-BIND fo=0x%08X mfd=0x%X sz=%u path='%s'\\n\",\n"
    "                        _x,_m,_osz,_pt); fflush(stderr);} }\n"
    "                  } else _m=0; } }\n"
)

F001 = "void func_002B3D1C(ppu_context* ctx) {\n" + NEEDLE + "}\n"


def _run(d: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(PATCH), str(d)], capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text(F001)
        r = _run(d)
        assert r.returncode == 0, r.stdout + r.stderr
        t = (d / "ppu_recomp_001.cpp").read_text()
        assert "E416-REBIND-PERMA" in t
        assert "_n>_csz" in t
        assert "r_perma.wad_ps3" in t
        assert "f2b_fo_sz_get" in t
        h = hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest()
        r2 = _run(d)
        assert r2.returncode == 0 and "ALREADY" in r2.stdout
        assert hashlib.sha256((d / "ppu_recomp_001.cpp").read_bytes()).hexdigest() == h
        print("[PASS] rebind-perma + idempotent")
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "ppu_recomp_001.cpp").write_text("void f(ppu_context* ctx) {}\n")
        assert _run(d).returncode != 0
        print("[PASS] 0x -> rc!=0")
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
