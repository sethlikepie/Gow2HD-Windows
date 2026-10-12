#!/usr/bin/env python3
"""E126 -- entrada de func_0009EC0C (this = entidade cujo [1] e' o container com os slots +0xB8): imprime r3..r5, lr e r3.w0..7.
Gate PS3_TRACE_REG. Diagnostico, nao e' fix."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
HDR = "void func_0009EC0C(ppu_context* ctx) {"
PROBE = HDR + r'''
        /* E126-9EC0C-ENTRY */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<6){ uint32_t _o=(uint32_t)ctx->gpr[3]; int _ok=(_o>=0x10000u&&_o<0x4F000000u);
            fprintf(stderr,"[9EC0C-ENTRY] lr=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r3.w0..7=%08X %08X %08X %08X %08X %08X %08X %08X\n",
              (uint32_t)ctx->lr,_o,(uint32_t)ctx->gpr[4],(uint32_t)ctx->gpr[5],(uint32_t)ctx->gpr[6],
              _ok?vm_read32(_o):0u,_ok?vm_read32(_o+4u):0u,_ok?vm_read32(_o+8u):0u,_ok?vm_read32(_o+12u):0u,_ok?vm_read32(_o+16u):0u,_ok?vm_read32(_o+20u):0u,_ok?vm_read32(_o+24u):0u,_ok?vm_read32(_o+28u):0u);
          } } }'''
def main():
    f=ROOT/"ppu_recomp_000.cpp"; s=f.read_text(errors='replace')
    if "E126-9EC0C-ENTRY" in s: print("E126: ALREADY"); return 0
    if s.count(HDR)!=1: print("E126: cabecalho nao unico"); return 2
    f.write_text(s.replace(HDR,PROBE,1)); print("E126: instrumentado"); return 0
if __name__=="__main__": raise SystemExit(main())
