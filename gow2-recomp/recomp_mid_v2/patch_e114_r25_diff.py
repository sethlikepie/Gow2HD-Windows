#!/usr/bin/env python3
"""E114 -- em func_002DAF04, no ponto onde r25 e' computado (0x2DB02C: r25=rldicl(r9)), imprime r25, *(r25) (word0
do no'), r15 (base do array de 0x27CAC8), *(r15) (contagem), r31, r0, r10 -- para o diff com o oraculo. Gate PS3_TRACE_REG."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
NEEDLE = "        ctx->gpr[25] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
BLOCK = NEEDLE + r'''        /* E114-R25DIFF */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<10){ uint32_t _r25=(uint32_t)ctx->gpr[25],_r15=(uint32_t)ctx->gpr[15];
            fprintf(stderr,"[R25DIFF] r25=0x%08X *r25(word0)=0x%08X r15(arraybase)=0x%08X *r15(cont)=0x%08X r31=0x%08X r0=0x%08X r10=0x%08X\n",
              _r25,(_r25>=0x10000u&&_r25<0x4F000000u)?vm_read32(_r25):0xDEADu,_r15,(_r15>=0x10000u&&_r15<0x4F000000u)?vm_read32(_r15):0xDEADu,
              (uint32_t)ctx->gpr[31],(uint32_t)ctx->gpr[0],(uint32_t)ctx->gpr[10]); fflush(stderr); } } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        i=s.find("void func_002DAF04(ppu_context* ctx) {")
        if i<0: continue
        j=s.find("\nvoid func_",i+10); body=s[i:j if j>0 else len(s)]
        if "E114-R25DIFF" in body: print("E114: ALREADY"); return 0
        if body.count(NEEDLE)!=1: print(f"E114: agulha x{body.count(NEEDLE)} -> recusa"); return 2
        s=s[:i]+body.replace(NEEDLE,BLOCK,1)+s[i+len(body):]; f.write_text(s); print(f"E114: {f.name}"); return 0
    print("E114: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
