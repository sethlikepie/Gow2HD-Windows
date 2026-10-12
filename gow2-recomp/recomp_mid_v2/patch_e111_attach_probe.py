#!/usr/bin/env python3
"""E111 -- conta os attaches por classe em func_0024C3EC (r3=parent, r4=child; classe = u16 @child+2).
Imprime cada attach de classe 15 (MDLX) com parent/child, e um resumo periodico. Gate PS3_TRACE_REG."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_0024C3EC(ppu_context* ctx) {\n"
BLOCK = FUNC + r'''        /* E111-ATTACH */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static long _cls[64]={0}; static long _tot=0; uint32_t _c=(uint32_t)ctx->gpr[4]; int _k=(_c>=0x10000u&&_c<0x4F000000u)?(vm_read16(_c+2u)&0x3F):-1; if(_k>=0)_cls[_k]++; _tot++;
            if(_k==15){ static int _n15=0; if(_n15++<12) fprintf(stderr,"[ATTACH15] parent=0x%08X child=0x%08X (attach classe 15 MDLX)\n",(uint32_t)ctx->gpr[3],_c); }
            if((_tot%400)==0){ char _b[300]; int _w=0; _b[0]=0; for(int _i=0;_i<40;_i++) if(_cls[_i]) _w+=snprintf(_b+_w,sizeof(_b)-_w,"%d:%ld ",_i,_cls[_i]); fprintf(stderr,"[ATTACH] por classe: %s\n",_b); } fflush(stderr); } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if FUNC not in s: continue
        if "E111-ATTACH" in s: print("E111: ALREADY"); return 0
        f.write_text(s.replace(FUNC,BLOCK,1)); print(f"E111: aplicado em {f.name}"); return 0
    print("E111: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
