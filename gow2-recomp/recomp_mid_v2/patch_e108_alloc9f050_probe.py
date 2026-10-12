#!/usr/bin/env python3
"""E108 -- em func_0009EC0C, antes e depois do `bl 0xE1B78` (lr=0x0009F054): r3 (produto -> chave +0x9c) antes,
e r3 (objecto alocado) depois, com w0/+4 e a pool (FUN_002abfa4(PTR_DAT_0053b6ac, chave)). Gate PS3_TRACE_REG (cap 6)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
NEEDLE = "        ctx->lr = 0x0009F054; func_000E1B78(ctx); DRAIN_TRAMPOLINE(ctx);\n"
BLOCK = r'''        /* E108-ALLOC9F050 */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<6){ uint32_t _p=(uint32_t)ctx->gpr[3]; fprintf(stderr,"[ALLOC9F050] antes: produto=0x%08X w0=0x%08X chave+0x9c=0x%08X r20=0x%08X\n",_p,(_p>=0x10000u&&_p<0x4F000000u)?vm_read32(_p):0u,(_p>=0x10000u&&_p<0x4F000000u)?vm_read32(_p+0x9Cu):0u,(uint32_t)ctx->gpr[20]); fflush(stderr); } } }
        ctx->lr = 0x0009F054; func_000E1B78(ctx); DRAIN_TRAMPOLINE(ctx);
        { static int _on2=-1; if(_on2<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on2=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on2){ static int _n2=0; if(_n2++<6){ uint32_t _o=(uint32_t)ctx->gpr[3]; fprintf(stderr,"[ALLOC9F050] depois: alocado=0x%08X w0=0x%08X +4=0x%08X (r20=0x%08X)\n",_o,(_o>=0x10000u&&_o<0x4F000000u)?vm_read32(_o):0u,(_o>=0x10000u&&_o<0x4F000000u)?vm_read32(_o+4u):0u,(uint32_t)ctx->gpr[20]); fflush(stderr); } } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if NEEDLE not in s: continue
        if "E108-ALLOC9F050" in s: print("E108: ALREADY"); return 0
        if s.count(NEEDLE)!=1: print("E108: agulha ambigua"); return 2
        f.write_text(s.replace(NEEDLE,BLOCK,1)); print(f"E108: aplicado em {f.name}"); return 0
    print("E108: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
