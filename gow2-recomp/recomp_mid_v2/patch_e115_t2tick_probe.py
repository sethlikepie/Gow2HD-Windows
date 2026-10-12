#!/usr/bin/env python3
"""E115 -- sondas: entrada de func_00292F88 (tick de TABLE[2]: mgr=r3, cursor +0xC8, stack[cur], cabeca de lista +0x80/+0x38)
e entrada de func_00248630 (tick por objecto de nivel: conta + r3). Gate PS3_TRACE_REG."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
A = "void func_00292F88(ppu_context* ctx) {\n"
BA = A + r'''        /* E115-T2TICK */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<6){ uint32_t _m=(uint32_t)ctx->gpr[3]; int _ok=(_m>=0x10000u&&_m<0x4F000000u); int _cur=_ok?(int)(int8_t)vm_read8(_m+0xC8u):-99;
            fprintf(stderr,"[T2TICK] mgr=0x%08X cursor=%d stack0=0x%08X +0x80=0x%08X +0x38=0x%08X +0x84=0x%08X\n",_m,_cur,_ok?vm_read32(_m+0x48u):0u,_ok?vm_read32(_m+0x80u):0u,_ok?vm_read32(_m+0x38u):0u,_ok?vm_read32(_m+0x84u):0u); fflush(stderr); } } }
'''
B = "void func_00248630(ppu_context* ctx) {\n"
BB = B + r'''        /* E115-LVLTICK */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static long _c=0; _c++; if(_c<=4 || (_c%500)==0) fprintf(stderr,"[LVLTICK] #%ld r3=0x%08X lr=0x%08X\n",_c,(uint32_t)ctx->gpr[3],(uint32_t)ctx->lr); fflush(stderr); } }
'''
def main():
    done=0
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace'); ch=False
        if A in s and "E115-T2TICK" not in s: s=s.replace(A,BA,1); ch=True; done+=1
        if B in s and "E115-LVLTICK" not in s: s=s.replace(B,BB,1); ch=True; done+=1
        if ch: f.write_text(s); print(f"E115: {f.name}")
    print("E115: ALREADY" if not done else f"E115: {done} sitios"); return 0
if __name__=="__main__": raise SystemExit(main())
