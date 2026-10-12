#!/usr/bin/env python3
"""E101 -- sonda a entrada de func_002DAE20(cache=r3, obj=r4, p3, p4): imprime os args, cache[0..9], e o gestor
*(*(0x53F58C)+0x3C) (indice na TABLE, cursor, stack[0]) cujo vt[0x50] alimenta a pool de onde a cache vem. Gate PS3_TRACE_REG (cap 12)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_002DAE20(ppu_context* ctx) {\n"
BLOCK = FUNC + r'''        /* E101-DAE20 */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<12){ uint32_t _c=(uint32_t)ctx->gpr[3]; char _b[200]; int _w=0; _b[0]=0;
            if(_c>=0x10000u&&_c<0x4F000000u) for(uint32_t _o=0;_o<0x28u;_o+=4u) _w+=snprintf(_b+_w,sizeof(_b)-_w,"0x%08X ",vm_read32(_c+_o));
            uint32_t _g=vm_read32(0x0053F58Cu); uint32_t _m=(_g>=0x10000u&&_g<0x4F000000u)?vm_read32(_g+0x3Cu):0u; int _ti=-1; for(int _i=0;_i<40;_i++) if(vm_read32(0x00868D48u+_i*4u)==_m){_ti=_i;break;}
            fprintf(stderr,"[DAE20] cache=0x%08X obj=0x%08X p3=0x%08X p4=0x%08X cache[0..9]=[%s] mgr=0x%08X T[%d] cursor=%d stack0=0x%08X lr=0x%08X\n",_c,(uint32_t)ctx->gpr[4],(uint32_t)ctx->gpr[5],(uint32_t)ctx->gpr[6],_b,_m,_ti,
              (_m>=0x10000u&&_m<0x4F000000u)?(int)(int8_t)vm_read8(_m+0xC8u):-99,(_m>=0x10000u&&_m<0x4F000000u)?vm_read32(_m+0x48u):0u,(uint32_t)ctx->lr); fflush(stderr); } } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if FUNC not in s: continue
        if "E101-DAE20" in s: print("E101: ALREADY"); return 0
        f.write_text(s.replace(FUNC,BLOCK,1)); print(f"E101: aplicado em {f.name}"); return 0
    print("E101: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
