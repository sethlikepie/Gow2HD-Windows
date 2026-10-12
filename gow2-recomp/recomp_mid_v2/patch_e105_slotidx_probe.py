#!/usr/bin/env python3
"""E105 -- na entrada de func_0009EC0C imprime *(0x53AAD4) (gestor) e o seu +0x3C (indice de slot da classe),
e o +0x3C de todos os gestores da TABLE (0x868D48). Gate PS3_TRACE_REG (1 vez)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_0009EC0C(ppu_context* ctx) {\n"
BLOCK = FUNC + r'''        /* E105-SLOTIDX */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<1){ uint32_t _g=vm_read32(0x0053AAD4u); fprintf(stderr,"[SLOTIDX] *(0x53AAD4)=0x%08X +0x3C=%u\n",_g,(_g>=0x10000u&&_g<0x4F000000u)?vm_read32(_g+0x3Cu):0xFFFFu);
            for(int _i=0;_i<40;_i++){ uint32_t _m=vm_read32(0x00868D48u+_i*4u); if(_m>=0x10000u&&_m<0x4F000000u) fprintf(stderr,"[SLOTIDX] TABLE[%2d] mgr=0x%08X +0x3C=%u +0x44=%u +0x40=%u\n",_i,_m,vm_read32(_m+0x3Cu),vm_read32(_m+0x44u),vm_read32(_m+0x40u)); }
            fflush(stderr); } } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if FUNC not in s: continue
        if "E105-SLOTIDX" in s: print("E105: ALREADY"); return 0
        f.write_text(s.replace(FUNC,BLOCK,1)); print(f"E105: aplicado em {f.name}"); return 0
    print("E105: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
