#!/usr/bin/env python3
"""E151 -- conclusao de op FIOS (func_0030644C: r3=op): tipo, status, path do fh. Gate PS3_TRACE_REG. Diagnostico."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
PATH=r'''char _p[48]; _p[0]=0; if(_fh>=0x10000u&&_fh<0x4F000000u){ uint32_t _pp=vm_read32(_fh+0x30u); if(_pp>=0x10000u&&_pp<0x4F000000u){ for(int _i=0;_i<47;_i++){ _p[_i]=(char)vm_read8(_pp+_i); if(!_p[_i]) break; } _p[47]=0; } }'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); h="void func_0030644C(ppu_context* ctx) {"
        if h in s:
            if "E151-DONE" in s: print("E151: ALREADY"); return 0
            s=s.replace(h,h+"\n        /* E151-DONE */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<120){ uint32_t _op=(uint32_t)ctx->gpr[3]; int _ok=(_op>=0x10000u&&_op<0x4F000000u); uint32_t _fh=_ok?vm_read32(_op+0x98u):0u; '''+PATH+r''' fprintf(stderr,"[FIOSOP] done op=0x%08X tipo=0x%X status=0x%08X r4=%u path='%s' op+0x30=0x%08X op+0x34=0x%08X lr=0x%08X\n",_op,_ok?vm_read32(_op+0x40u):0u,_ok?vm_read32(_op+0x44u):0u,(uint32_t)ctx->gpr[4],_p, _ok?vm_read32(_op+0x30u):0u, _ok?vm_read32(_op+0x34u):0u, (uint32_t)ctx->lr); } } }''',1)
            f.write_text(s); print("E151: instrumentado em",f.name); return 0
    print("E151: nao achei"); return 2
if __name__=="__main__": raise SystemExit(main())
