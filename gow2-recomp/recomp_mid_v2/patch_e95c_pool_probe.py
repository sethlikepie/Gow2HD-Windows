#!/usr/bin/env python3
"""E95c -- sonda a entrada do fragmento func_00256B64 (FUN_00256B1C: r3=mgr, r4=desc, r5=src):
imprime o teste de pool que escolhe o caminho: p=*(mgr+0x30)+0x34 -> [+4] vs [+8]
(0x256BA4..0x256BB8: iguais -> lista default @0x256C68; diferentes -> 0x256ECC). Gate PS3_TRACE_REG."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_00256B64(ppu_context* ctx) {\n"
BLOCK = FUNC + r'''        /* E95C-POOL */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ uint32_t _m=(uint32_t)ctx->gpr[3], _d=(uint32_t)ctx->gpr[4], _s=(uint32_t)ctx->gpr[5];
            uint32_t _m30=(_m>=0x10000u&&_m<0x4F000000u)?vm_read32(_m+0x30u):0u; uint32_t _p=_m30+0x34u;
            uint32_t _a4=(_p>=0x10000u&&_p<0x4F000000u)?vm_read32(_p+4u):0xDEADu, _a8=(_p>=0x10000u&&_p<0x4F000000u)?vm_read32(_p+8u):0xDEADu;
            fprintf(stderr,"[POOL] mgr=0x%08X desc=0x%08X w0=0x%08X r5=0x%08X m30=0x%08X [+4]=%u [+8]=%u %s\n",_m,_d,(_d>=0x10000u&&_d<0x4F000000u)?vm_read32(_d):0u,_s,_m30,_a4,_a8,(_a4==_a8)?"IGUAL(deflist)":"DIFERENTE(0x256ECC)"); fflush(stderr); } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if FUNC not in s: continue
        if "E95C-POOL" in s: print("E95c: ALREADY"); return 0
        if s.count(FUNC)!=1: print("E95c: agulha ambigua"); return 2
        f.write_text(s.replace(FUNC,BLOCK,1)); print(f"E95c: aplicado em {f.name}"); return 0
    print("E95c: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
