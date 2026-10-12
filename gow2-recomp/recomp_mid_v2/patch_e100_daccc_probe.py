#!/usr/bin/env python3
"""E100 -- sonda a entrada de func_002DACCC(head=r3, key=r4): tamanho da lista (limite 300), circular?, chave
encontrada?, e o chamador (lr). E' onde a thread principal fica presa depois do E99. Gate PS3_TRACE_REG (cap 40)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_002DACCC(ppu_context* ctx) {\n"
BLOCK = FUNC + r'''        /* E100-DACCC */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<40){ uint32_t _h=(uint32_t)ctx->gpr[3], _k=(uint32_t)ctx->gpr[4]; uint32_t _q=(_h>=0x10000u&&_h<0x4F000000u)?vm_read32(_h):0u; int _j=0, _f=-1; uint32_t _first=_q;
            while(_q>=0x10000u && _q<0x4F000000u && _q!=_h && _j<300){ if(_f<0 && vm_read32(_q+8u)==_k) _f=_j; _q=vm_read32(_q); _j++; }
            fprintf(stderr,"[DACCC] head=0x%08X key=0x%08X nos=%d circular=%s found_at=%d first=0x%08X fim=0x%08X lr=0x%08X\n",_h,_k,_j,(_q==_h)?"sim":"NAO",_f,_first,_q,(uint32_t)ctx->lr); fflush(stderr); } } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if FUNC not in s: continue
        if "E100-DACCC" in s: print("E100: ALREADY"); return 0
        f.write_text(s.replace(FUNC,BLOCK,1)); print(f"E100: aplicado em {f.name}"); return 0
    print("E100: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
