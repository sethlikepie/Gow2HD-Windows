#!/usr/bin/env python3
"""E117 -- entrada de func_00042294 (loader state machine, r3=obj=P3): estado +0x550, idx +0x480, +0x554,
e para cada WAD slot +0x454[i] (i<12): o objecto e o seu +0x20 (raiz/dados). Gate PS3_TRACE_REG (cap 40, so' quando o estado muda)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_00042294(ppu_context* ctx) {\n"
BLOCK = FUNC + r'''        /* E117-WADSLOTS */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static uint32_t _last=0xFFFFFFFFu; static int _n=0; uint32_t _o=(uint32_t)ctx->gpr[3]; if(_o>=0x10000u&&_o<0x4F000000u){ uint32_t _st=vm_read32(_o+0x550u);
            if(_st!=_last && _n++<40){ _last=_st; uint32_t _idx=vm_read32(_o+0x480u); char _b[400]; int _w=0; _b[0]=0;
              for(int _i=0;_i<12;_i++){ uint32_t _wo=vm_read32(_o+0x454u+_i*4u); _w+=snprintf(_b+_w,sizeof(_b)-_w,"[%d]=%08X/+20=%08X ",_i,_wo,(_wo>=0x10000u&&_wo<0x4F000000u)?vm_read32(_wo+0x20u):0xDEADu); }
              fprintf(stderr,"[WADSLOTS] obj=0x%08X estado=%u idx=%u +0x554=0x%08X +0x558=%u  %s\n",_o,_st,_idx,vm_read32(_o+0x554u),vm_read32(_o+0x558u),_b); fflush(stderr); } } } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if FUNC not in s: continue
        if "E117-WADSLOTS" in s: print("E117: ALREADY"); return 0
        f.write_text(s.replace(FUNC,BLOCK,1)); print(f"E117: aplicado em {f.name}"); return 0
    print("E117: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
