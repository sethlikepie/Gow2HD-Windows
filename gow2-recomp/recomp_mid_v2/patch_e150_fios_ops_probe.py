#!/usr/bin/env python3
"""E150 -- ops FIOS: no submit (func_0030B058: r4=op) imprime tipo(+0x40), fh(+0x98), path do fh(+0x30), e na espera
(func_0030D8B8 entrada: r3, r4) o op esperado e o seu estado(+0x44)/tipo. Gate PS3_TRACE_REG. Diagnostico."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
PATH=r'''char _p[48]; _p[0]=0; if(_fh>=0x10000u&&_fh<0x4F000000u){ uint32_t _pp=vm_read32(_fh+0x30u); if(_pp>=0x10000u&&_pp<0x4F000000u){ for(int _i=0;_i<47;_i++){ _p[_i]=(char)vm_read8(_pp+_i); if(!_p[_i]) break; } _p[47]=0; } }'''
def main():
    n=0
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); o=s
        h="void func_0030B058(ppu_context* ctx) {"
        if h in s and "E150-SUBMIT" not in s:
            s=s.replace(h,h+"\n        /* E150-SUBMIT */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<120){ uint32_t _op=(uint32_t)ctx->gpr[4]; int _ok=(_op>=0x10000u&&_op<0x4F000000u); uint32_t _fh=_ok?vm_read32(_op+0x98u):0u; '''+PATH+r''' fprintf(stderr,"[FIOSOP] submit op=0x%08X tipo=0x%X fh=0x%08X path='%s' lr=0x%08X op+0x30=0x%08X op+0x34=0x%08X lr=0x%08X media=0x%08X media+0x30=0x%08X fh+0x30=0x%08X op+0x1c=0x%08X op+0x48=0x%08X\n",_op,_ok?vm_read32(_op+0x40u):0u,_fh,_p,(uint32_t)ctx->lr, _ok?vm_read32(_op+0x30u):0u, _ok?vm_read32(_op+0x34u):0u, (uint32_t)ctx->lr, (_ok&&_fh>=0x10000u)?vm_read32(_fh+0x8u):0u, (_ok&&_fh>=0x10000u&&vm_read32(_fh+0x8u)>=0x10000u)?vm_read32(vm_read32(_fh+0x8u)+0x30u):0u, (_ok&&_fh>=0x10000u)?vm_read32(_fh+0x30u):0u, _ok?vm_read32(_op+0x1Cu):0u, _ok?vm_read32(_op+0x48u):0u); if(_ok&&_fh>=0x10000u&&vm_read32(_fh+0x8u)>=0x10000u&&(vm_read32(_op+0x40u)==9u||vm_read32(_op+0x40u)==0xDu)){ uint32_t _m=vm_read32(_fh+0x8u); fprintf(stderr,"[FIOSOP] FH@0x%08X:",_fh); for(int _i=0;_i<0x30;_i++) fprintf(stderr," %08X",vm_read32(_fh+4u*_i)); fprintf(stderr,"\n"); } } } }''',1); n+=1
        h="void func_0030D8B8(ppu_context* ctx) {"
        if h in s and "E150-WAIT" not in s:
            s=s.replace(h,h+"\n        /* E150-WAIT */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<40){ uint32_t _op=(uint32_t)ctx->gpr[4]; int _ok=(_op>=0x10000u&&_op<0x4F000000u); uint32_t _fh=_ok?vm_read32(_op+0x98u):0u; '''+PATH+r''' fprintf(stderr,"[FIOSOP] wait r3=0x%08X op=0x%08X tipo=0x%X status=0x%08X fh=0x%08X path='%s' lr=0x%08X\n",(uint32_t)ctx->gpr[3],_op,_ok?vm_read32(_op+0x40u):0u,_ok?vm_read32(_op+0x44u):0u,_fh,_p,(uint32_t)ctx->lr); } } }''',1); n+=1
        if s!=o: f.write_text(s)
    print("E150: %d sitios"%n); return 0 if n else 2
if __name__=="__main__": raise SystemExit(main())
