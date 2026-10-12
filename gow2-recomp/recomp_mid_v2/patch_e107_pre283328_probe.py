#!/usr/bin/env python3
"""E107 -- sonda imediatamente antes do `bl 0x283328` em func_0009EC0C (lr=0x0009F0F4): r20 (param_1),
*(r20+4) (parent), idx=*(*(0x53AAD4)+0x3C), no'=*(parent+0xB8+idx*4), r29 (filho), r3. Gate PS3_TRACE_REG (cap 6)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
NEEDLE = "        ctx->lr = 0x0009F0F4; func_00283328(ctx); DRAIN_TRAMPOLINE(ctx);\n"
BLOCK = r'''        /* E107-PRE283328 */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<6){ uint32_t _p1=(uint32_t)ctx->gpr[20]; uint32_t _par=(_p1>=0x10000u&&_p1<0x4F000000u)?vm_read32(_p1+4u):0u; uint32_t _g=vm_read32(0x0053AAD4u); uint32_t _idx=(_g>=0x10000u&&_g<0x4F000000u)?vm_read32(_g+0x3Cu):0xFFFFu;
            uint32_t _node=(_par>=0x10000u&&_par<0x4F000000u&&_idx<64u)?vm_read32(_par+0xB8u+_idx*4u):0u;
            fprintf(stderr,"[PRE283328] r20=0x%08X parent=0x%08X parent_w0=0x%08X idx=%u node=0x%08X r29=0x%08X r3=0x%08X r9=0x%08X r11=0x%08X\n",_p1,_par,(_par>=0x10000u&&_par<0x4F000000u)?vm_read32(_par):0u,_idx,_node,(uint32_t)ctx->gpr[29],(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[9],(uint32_t)ctx->gpr[11]); fflush(stderr); } } }
''' + NEEDLE
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if NEEDLE not in s: continue
        if "E107-PRE283328" in s: print("E107: ALREADY"); return 0
        if s.count(NEEDLE)!=1: print("E107: agulha ambigua"); return 2
        f.write_text(s.replace(NEEDLE,BLOCK,1)); print(f"E107: aplicado em {f.name}"); return 0
    print("E107: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
