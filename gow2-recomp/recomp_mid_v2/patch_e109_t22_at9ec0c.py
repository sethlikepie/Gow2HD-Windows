#!/usr/bin/env python3
"""E109 -- na entrada de func_0009EC0C imprime o slot TABLE[22] (=*(0x868D48+0x58)) e o seu cursor (+0xC8),
mais o mgr que o vt[0x48] usa (r26=*(0x53AA38)=0x868D48, mgr=*(r26+0x58)). Gate PS3_TRACE_REG (cap 6)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_0009EC0C(ppu_context* ctx) {\n"
BLOCK = FUNC + r'''        /* E109-T22 */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<6){ uint32_t _tab=vm_read32(0x0053AA38u); uint32_t _mgr=(_tab>=0x10000u&&_tab<0x4F000000u)?vm_read32(_tab+0x58u):0u;
            int _cur=(_mgr>=0x10000u&&_mgr<0x4F000000u)?(int)(int8_t)vm_read8(_mgr+0xC8u):-99; uint32_t _prod=(_cur>=0)?vm_read32(_mgr+0x48u+(uint32_t)_cur*4u):0u;
            fprintf(stderr,"[T22] r26=0x%08X(TABLE) mgr=TABLE[22]=0x%08X cursor=%d prod=0x%08X vt48=0x%08X\n",_tab,_mgr,_cur,_prod,(_mgr>=0x10000u&&_mgr<0x4F000000u)?vm_read32(_mgr):0u); fflush(stderr); } } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if FUNC not in s: continue
        if "E109-T22" in s: print("E109: ALREADY"); return 0
        f.write_text(s.replace(FUNC,BLOCK,1)); print(f"E109: aplicado em {f.name}"); return 0
    print("E109: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
