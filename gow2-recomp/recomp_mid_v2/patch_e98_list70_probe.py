#!/usr/bin/env python3
"""E98 -- sonda a entrada de func_002A466C (r3=obj): percorre (limite 60) a lista circular em obj+0x70
(next @+0; byte de estado @+10; u16 @+8) e imprime; e' onde a thread principal fica presa depois do R_Hero01. Gate PS3_TRACE_REG."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_002A466C(ppu_context* ctx) {\n"
BLOCK = FUNC + r'''        /* E98-LIST70 */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<8){ uint32_t _o=(uint32_t)ctx->gpr[3], _s=_o+0x70u; uint32_t _q=(_s>=0x10000u&&_s<0x4F000000u)?vm_read32(_s):0u; int _k=0; char _b[400]; int _w=0; _b[0]=0;
            while(_q>=0x10000u && _q<0x4F000000u && _q!=_s && _k<60){ if(_k<10) _w+=snprintf(_b+_w,sizeof(_b)-_w,"0x%08X(st=%u,f=0x%04X,next=0x%08X) ",_q,vm_read8(_q+10u),vm_read16(_q+8u),vm_read32(_q)); _q=vm_read32(_q); _k++; }
            fprintf(stderr,"[LIST70] obj=0x%08X sent=0x%08X nos=%d fim=%s(0x%08X) %s\n",_o,_s,_k,(_q==_s)?"cabeca":"NAO-cabeca",_q,_b); fflush(stderr); } } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if FUNC not in s: continue
        if "E98-LIST70" in s: print("E98: ALREADY"); return 0
        f.write_text(s.replace(FUNC,BLOCK,1)); print(f"E98: aplicado em {f.name}"); return 0
    print("E98: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
