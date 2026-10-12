#!/usr/bin/env python3
"""E416 -- 2B3D1C: rebind wad FO when AREAD n exceeds the cached movie_io size.

E415 bound FO 0x43018580 to r_lglsca (3072 B) and the LglScA AREAD completed
(got=3072, BAD10 ret r3=1). The same FO was then reused with limit=0x133C280
and n=131072; cached mfd still served LglScA → AREAD-HLE-NO short read.

If n > f2b_fo_sz_get(FO), drop the cached mfd and re-open FO+0x30; if that
file is still too small, open r_perma.wad_ps3. f2b_fo_mfd_put overwrites the
FO slot. Do not enable STREAM_PUMP.

Marker E416-REBIND-PERMA. rc 0/2/3.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARK = "E416-REBIND-PERMA"

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

REPL = (
    "              /* E415-WAD-FO-BIND: natural file_new FO has no mfd; open by path */\n"
    "              /* E416-REBIND-PERMA: same FO reused for PermA; n > cached sz */\n"
    "              { uint32_t _csz=f2b_fo_sz_get(_x);\n"
    "                if(_m && _csz && _n>_csz) _m=0; }\n"
    "              if(!_m){\n"
    "                uint32_t _p30=vm_read32(_x+0x30u); char _pt[96]; _pt[0]=0;\n"
    "                if(_p30>=0x10000u && _p30<0x4F000000u){\n"
    "                  for(int _i=0;_i<95;_i++){ unsigned char _ch=(unsigned char)vm_read8(_p30+_i);\n"
    "                    _pt[_i]=(char)_ch; if(!_ch) break; }\n"
    "                  _pt[95]=0; }\n"
    "                unsigned _osz=0;\n"
    "                if(_pt[0] && (strstr(_pt,\"wad\")||strstr(_pt,\"WAD\")||strstr(_pt,\"lgl\")||strstr(_pt,\"Lgl\")\n"
    "                    ||strstr(_pt,\"perm\")||strstr(_pt,\"Perm\"))){\n"
    "                  _m=movie_io_open(_pt, &_osz);\n"
    "                  if(!_m){ const char* _b=_pt; for(const char* _s=_pt;*_s;_s++) if(*_s=='/') _b=_s+1;\n"
    "                    _m=movie_io_open(_b, &_osz); } }\n"
    "                if(!_m || (_osz && _n>_osz)){\n"
    "                  const char* _alts[3]={\"/wad/r_perma.wad_ps3\",\"r_perma.wad_ps3\",\"R_PermA\"};\n"
    "                  for(int _ai=0;_ai<3 && (!_m || (_osz && _n>_osz));_ai++){\n"
    "                    unsigned _sz2=0; unsigned _m2=movie_io_open(_alts[_ai], &_sz2);\n"
    "                    if(_m2 && movie_io_is(_m2) && _sz2>=_n){ _m=_m2; _osz=_sz2;\n"
    "                      snprintf(_pt, sizeof _pt, \"%s\", _alts[_ai]); } } }\n"
    "                if(_m && movie_io_is(_m)){\n"
    "                  f2b_fo_mfd_put(_x, _m, _osz);\n"
    "                  { static int _onb=-1; if(_onb<0){ extern char* getenv(const char*);\n"
    "                    const char* _e=getenv(\"PS3_TRACE_AREAD\"); if(!_e||!*_e) _e=getenv(\"PS3_TRACE_BAD10\");\n"
    "                    _onb=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "                    if(_onb){ fprintf(stderr,\"[2B3D1C] WAD-FO-BIND fo=0x%08X mfd=0x%X sz=%u n=%u path='%s'\\n\",\n"
    "                      _x,_m,_osz,_n,_pt); fflush(stderr);} }\n"
    "                } else _m=0; }\n"
)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    texts: dict[Path, str] = {}
    for p in paths:
        if p.is_file():
            texts[p] = p.read_text(errors="replace")
    if not texts:
        print("E416: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    if sum(s.count(MARK) for s in texts.values()):
        print("E416: ALREADY")
        return 0
    n = sum(s.count(NEEDLE) for s in texts.values())
    if n != 1:
        print(f"E416: needle {n}x (esperado 1)", file=sys.stderr)
        return 3 if n else 2
    for p, s in list(texts.items()):
        if NEEDLE in s:
            texts[p] = s.replace(NEEDLE, REPL, 1)
            print(f"E416: {p.name}: APPLIED")
    for p, s in texts.items():
        if s != p.read_text(errors="replace"):
            p.write_text(s)
    print("E416: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
