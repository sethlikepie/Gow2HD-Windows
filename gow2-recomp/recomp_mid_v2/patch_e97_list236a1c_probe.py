#!/usr/bin/env python3
"""E97 -- sonda a entrada de func_00236A1C: percorre (com limite) a lista global em *(*0x53EA54)
(next @+0, prev @+4, chave @+8) e imprime os nos e a chave procurada (param_1+8). Gate PS3_TRACE_REG."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_00236A1C(ppu_context* ctx) {\n"
BLOCK = FUNC + r'''        /* E97-LIST236A1C */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<6){ uint32_t _p=(uint32_t)ctx->gpr[3]; uint32_t _head=vm_read32(0x0053EA54u); uint32_t _key=(_p>=0x10000u&&_p<0x4F000000u)?vm_read32(_p+8u):0u;
            fprintf(stderr,"[LIST236A1C] p=0x%08X +0x24=0x%08X key=0x%08X head=0x%08X\n",_p,(_p>=0x10000u&&_p<0x4F000000u)?vm_read32(_p+0x24u):0u,_key,_head);
            uint32_t _q=(_head>=0x10000u&&_head<0x4F000000u)?vm_read32(_head):0u; int _k=0;
            while(_q>=0x10000u && _q<0x4F000000u && _q!=_head && _k<60){ fprintf(stderr,"[LIST236A1C]   #%d node=0x%08X next=0x%08X prev=0x%08X key=0x%08X\n",_k,_q,vm_read32(_q),vm_read32(_q+4u),vm_read32(_q+8u)); _q=vm_read32(_q); _k++; }
            fprintf(stderr,"[LIST236A1C]   fim k=%d em 0x%08X %s\n",_k,_q,(_q==_head)?"cabeca":"NAO-cabeca"); fflush(stderr); } } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if FUNC not in s: continue
        if "E97-LIST236A1C" in s: print("E97: ALREADY"); return 0
        f.write_text(s.replace(FUNC,BLOCK,1)); print(f"E97: aplicado em {f.name}"); return 0
    print("E97: func_00236A1C ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
