#!/usr/bin/env python3
"""E95b -- sonda da lista de filhos 'default' que FUN_00256B1C (fragmento func_00256B64) percorre
para registar os filhos de uma raiz: logo apos `r27 = *(r27+4)` (0x256C68) percorre a lista circular
(no: next @+0, desc @+8; classe = u16 @desc+2) e imprime as classes, o gestor (r24), o desc (r26) e o
objecto de origem (r28 = r5). Gate PS3_TRACE_REG. Idempotente (marcador E95B-DEFLIST)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_00256B64(ppu_context* ctx) {"
NEEDLE = "        ctx->gpr[27] = vm_read32(ctx->gpr[27] + 0x4);\n"
BLOCK = NEEDLE + r'''        /* E95B-DEFLIST */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ uint32_t _h=(uint32_t)ctx->gpr[27]; char _b[256]; int _k=0; _b[0]=0; uint32_t _n=(_h>=0x10000u&&_h<0x4F000000u)?vm_read32(_h):0u; int _s=0;
            while(_n>=0x10000u && _n<0x4F000000u && _n!=_h && _s<40){ uint32_t _d=vm_read32(_n+8u); uint32_t _c=(_d>=0x10000u&&_d<0x4F000000u)?vm_read16(_d+2u):0xFFFFu;
              _k+=snprintf(_b+_k,sizeof(_b)-_k,"%u ",_c); if(_k>230) break; _n=vm_read32(_n); _s++; }
            fprintf(stderr,"[DEFLIST] mgr=0x%08X desc=0x%08X r5=0x%08X head=0x%08X classes=[%s]\n",(uint32_t)ctx->gpr[24],(uint32_t)ctx->gpr[26],(uint32_t)ctx->gpr[28],_h,_b); fflush(stderr); } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace'); i=s.find(FUNC)
        if i<0: continue
        j=s.find("\nvoid func_", i+10); body=s[i:j if j>0 else len(s)]
        if "E95B-DEFLIST" in body: print("E95b: ALREADY"); return 0
        if body.count(NEEDLE)!=1: print(f"E95b: agulha x{body.count(NEEDLE)} em func_00256B64 -> recusa"); return 2
        s=s[:i]+body.replace(NEEDLE,BLOCK,1)+s[i+len(body):]; f.write_text(s); print(f"E95b: aplicado em {f.name}"); return 0
    print("E95b: func_00256B64 ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
