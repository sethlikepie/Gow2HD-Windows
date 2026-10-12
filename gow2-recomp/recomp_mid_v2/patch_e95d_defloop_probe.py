#!/usr/bin/env python3
"""E95d -- sonda de cada iteracao do laco de registo dos filhos default (0x256D70) em TODOS os
fragmentos lifted que o contem (chunk 000: func_00256B64; chunk 002: func_00256BA4/C0C/...):
imprime o no (r30), o desc (node+8), a classe, e a cabeca da lista (r27). Gate PS3_TRACE_REG."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
NEEDLE = "loc_00256D70:\n        ctx->gpr[30] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
BLOCK = NEEDLE + r'''        /* E95D-DEFLOOP */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ uint32_t _n=(uint32_t)ctx->gpr[30]; uint32_t _d=(_n>=0x10000u&&_n<0x4F000000u)?vm_read32(_n+8u):0u;
            fprintf(stderr,"[DEFLOOP] %s node=0x%08X next=0x%08X desc=0x%08X cls=%u head=0x%08X mgr=0x%08X\n", __func__, _n,
              (_n>=0x10000u&&_n<0x4F000000u)?vm_read32(_n):0u, _d, (_d>=0x10000u&&_d<0x4F000000u)?vm_read16(_d+2u):0xFFFFu, (uint32_t)ctx->gpr[27], (uint32_t)ctx->gpr[24]); fflush(stderr); } }
'''
def main():
    tot=0
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace'); n=s.count(NEEDLE)
        if not n: continue
        if "E95D-DEFLOOP" in s: print(f"E95d: ALREADY em {f.name}"); tot+=n; continue
        f.write_text(s.replace(NEEDLE,BLOCK)); print(f"E95d: {n} sitios em {f.name}"); tot+=n
    return 0 if tot else 2
if __name__=="__main__": raise SystemExit(main())
