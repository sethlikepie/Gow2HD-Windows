#!/usr/bin/env python3
"""E102 -- sonda a entrada de func_0009EC0C(obj=r3, p2=r4, p3=r5): imprime obj, w0, parent=obj[1], w0 do parent,
o indice de classe *(*(0x53F58C)+0x3C) e o no' parent+0xB8+idx*4 de onde sai o filho passado a FUN_00283328. Gate PS3_TRACE_REG (cap 12)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_0009EC0C(ppu_context* ctx) {\n"
BLOCK = FUNC + r'''        /* E102-9EC0C */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<12){ uint32_t _o=(uint32_t)ctx->gpr[3]; int _ok=(_o>=0x10000u&&_o<0x4F000000u); uint32_t _p=_ok?vm_read32(_o+4u):0u; int _pk=(_p>=0x10000u&&_p<0x4F000000u);
            uint32_t _g=vm_read32(0x0053F58Cu); uint32_t _ci=(_g>=0x10000u&&_g<0x4F000000u)?vm_read32(_g+0x3Cu):0xFFFFu; uint32_t _node=(_pk&&_ci<64u)?vm_read32(_p+0xB8u+_ci*4u):0u;
            fprintf(stderr,"[9EC0C] obj=0x%08X w0=0x%08X parent=0x%08X parent_w0=0x%08X cls_idx=%u node=0x%08X child=0x%08X p2=0x%08X p3=0x%08X lr=0x%08X\n",_o,_ok?vm_read32(_o):0u,_p,_pk?vm_read32(_p):0u,_ci,_node,_node?_node-0x14u:0u,(uint32_t)ctx->gpr[4],(uint32_t)ctx->gpr[5],(uint32_t)ctx->lr);
            if(_pk){ char _sb[420]; int _sw=0; _sb[0]=0; for(int _i=0;_i<34;_i++) _sw+=snprintf(_sb+_sw,sizeof(_sb)-_sw,"%08X ",vm_read32(_p+0xB8u+_i*4u)); fprintf(stderr,"[9EC0C]   parent slots+0xB8: %s\n",_sb); }
            fflush(stderr); } } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if FUNC not in s: continue
        if "E102-9EC0C" in s and "parent slots+0xB8" in s: print("E102: ALREADY"); return 0
        if "E102-9EC0C" in s:
            i=s.find("        /* E102-9EC0C */"); j=s.find("\n",s.find("fflush(stderr); } } }",i))+1; s=s[:i]+s[j:]
        f.write_text(s.replace(FUNC,BLOCK,1)); print(f"E102: aplicado em {f.name}"); return 0
    print("E102: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
