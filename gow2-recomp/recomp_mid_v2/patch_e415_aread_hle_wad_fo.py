#!/usr/bin/env python3
"""E415 -- 2B3D1C AREAD-HLE binds wad/lgl/perm FO by path (FO+0x30).

2B9F54 passes the STREAM CONTAINER (ts+0x64) as r3, not a separate FIOS op.
42B4 already deleted the OPEN handle so container+8 is free for STATUS_DONE.
Natural file_new FO has no f2b mfd and FO+0x38=0, so AREAD-HLE declined and
the natural body (30FC00/3067DC) never completed. PermA then sat at
inflight=0x20000 rem=0x133C280 (E414).

Bind movie_io from the FO path and let ps3_fios_aread_hle fulfil. After 42B4,
writing STATUS_DONE=0 at container+8 is the read-op completion, not an OPEN
clobber. Do NOT re-enable PS3_FIOS_STREAM_PUMP (wall [F]).

Also: restore 2B9F00→2B9F54 (undo E413 skip-AREAD + E414 SMALLWAD-EOF) and
disable 441C RING-FILL preload so bytes arrive only as a completed AREAD.

Marker E415-WAD-FO-BIND. rc 0/2/3.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARK = "E415-WAD-FO-BIND"

NEEDLE_HLE = (
    "              unsigned _m=f2b_fo_mfd_get(_x);\n"
    "              if(!_m){ uint32_t t=vm_read32(_x+0x38u); if(t&&movie_io_is(t)) _m=t; }\n"
    "              if(_m && movie_io_is(_m)) _fd=_m;\n"
)
REPL_HLE = (
    "              unsigned _m=f2b_fo_mfd_get(_x);\n"
    "              if(!_m){ uint32_t t=vm_read32(_x+0x38u); if(t&&movie_io_is(t)) _m=t; }\n"
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
    "              if(_m && movie_io_is(_m)) _fd=_m;\n"
)

# Undo E413+E414 so inflight==0 issues the guest AREAD.
NEEDLE_SKIP = (
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
REPL_SKIP = (
    "        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_002B9F54; return; }\n"
)

NEEDLE_RING = (
    "          /* Small WAD (LglScA 0xC00): preload ring even when 42EC already\n"
    "           * wrote rem — otherwise av=0 and state-1 never AREAD-consumes. */\n"
    "          if(_sz && _sz<=65536u && _c>=0x64u && vm_base){\n"
)
REPL_RING = (
    "          /* E415-NO-PRELOAD: RING-FILL without AREAD left av=3072 wp=0 req=0xC00.\n"
    "           * Bytes must arrive via 2B9F54→2B3D1C AREAD-HLE (guest protocol). */\n"
    "          if(0 && _sz && _sz<=65536u && _c>=0x64u && vm_base){\n"
)


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    texts: dict[Path, str] = {}
    for p in paths:
        if p.is_file():
            texts[p] = p.read_text(errors="replace")
    if not texts:
        print("E415: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    if sum(s.count(MARK) for s in texts.values()):
        print("E415: ALREADY")
        return 0

    n_hle = sum(s.count(NEEDLE_HLE) for s in texts.values())
    n_skip = sum(s.count(NEEDLE_SKIP) for s in texts.values())
    n_ring = sum(s.count(NEEDLE_RING) for s in texts.values())
    if n_hle != 1:
        print(f"E415: HLE needle {n_hle}x (esperado 1)", file=sys.stderr)
        return 3 if n_hle else 2
    if n_skip != 1:
        print(f"E415: skip-AREAD needle {n_skip}x (esperado 1)", file=sys.stderr)
        return 3 if n_skip else 2
    if n_ring != 1:
        print(f"E415: RING-FILL needle {n_ring}x (esperado 1)", file=sys.stderr)
        return 3 if n_ring else 2

    for p, s in list(texts.items()):
        ns = (
            s.replace(NEEDLE_HLE, REPL_HLE, 1)
            .replace(NEEDLE_SKIP, REPL_SKIP, 1)
            .replace(NEEDLE_RING, REPL_RING, 1)
        )
        if ns != s:
            texts[p] = ns
            print(f"E415: {p.name}: APPLIED")
    for p, s in texts.items():
        if s != p.read_text(errors="replace"):
            p.write_text(s)
    print("E415: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
