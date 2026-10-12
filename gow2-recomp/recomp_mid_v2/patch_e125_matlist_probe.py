#!/usr/bin/env python3
"""E125 -- na entrada de func_00283328 (r3 = objecto classe 15/MDLX) imprime a cadeia que alimenta a chave do lookup
vt[0x50] em FUN_002dae20: A=*(obj+0xd0) {vt,count,array,arena}, B=A[2] (array de descritores), key=B[0] (w0, +6=slot).
Gate PS3_TRACE_REG. Diagnostico, nao e' fix."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
HDR = "void func_00283328(ppu_context* ctx) {"
PROBE = HDR + r'''
        /* E125-MATLIST */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<6){ uint32_t _o=(uint32_t)ctx->gpr[3];
            #define _OK(x) ((x)>=0x10000u&&(x)<0x4F000000u)
            uint32_t _A=_OK(_o)?vm_read32(_o+0xd0u):0u; uint32_t _B=_OK(_A)?vm_read32(_A+8u):0u; uint32_t _K=_OK(_B)?vm_read32(_B):0u;
            fprintf(stderr,"[MATLIST] obj=0x%08X w0=0x%08X A=*(obj+0xd0)=0x%08X A[0..3]=%08X %08X %08X %08X B=A[2]=0x%08X B[0..3]=%08X %08X %08X %08X key=B[0]=0x%08X key.w0=0x%08X key+6=%u\n",
              _o,_OK(_o)?vm_read32(_o):0u,_A,_OK(_A)?vm_read32(_A):0u,_OK(_A)?vm_read32(_A+4u):0u,_OK(_A)?vm_read32(_A+8u):0u,_OK(_A)?vm_read32(_A+12u):0u,
              _B,_OK(_B)?vm_read32(_B):0u,_OK(_B)?vm_read32(_B+4u):0u,_OK(_B)?vm_read32(_B+8u):0u,_OK(_B)?vm_read32(_B+12u):0u,
              _K,_OK(_K)?vm_read32(_K):0u,_OK(_K)?(vm_read32(_K+4u)&0xffffu):0u);
            #undef _OK
          } } }'''
def main():
    f=ROOT/"ppu_recomp_000.cpp"; s=f.read_text(errors='replace')
    if "E125-MATLIST" in s: print("E125: ALREADY"); return 0
    if s.count(HDR)!=1: print("E125: cabecalho nao unico"); return 2
    f.write_text(s.replace(HDR,PROBE,1)); print("E125: instrumentado"); return 0
if __name__=="__main__": raise SystemExit(main())
