#!/usr/bin/env python3
"""E116 -- conta entradas em func_00240F4C, func_00244E40, func_0024771C (callees de func_00248630 que no console
levam ao effect init) e em func_0031FDE0 (penultimo antes do trampolim func_0041ADC8). Gate PS3_TRACE_REG."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
def blk(tag):
    return ('        /* E116-'+tag+' */\n        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!=\'0\')?1:0; }\n'
            '          if(_on){ static long _c=0; _c++; if(_c<=3 || (_c%200)==0) fprintf(stderr,"[EFX-'+tag+'] #%ld r3=0x%08X r4=0x%08X lr=0x%08X\\n",_c,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4],(uint32_t)ctx->lr); fflush(stderr); } }\n')
FUNCS={"void func_00240F4C(ppu_context* ctx) {\n":"240F4C","void func_00244E40(ppu_context* ctx) {\n":"244E40","void func_0024771C(ppu_context* ctx) {\n":"24771C","void func_0031FDE0(ppu_context* ctx) {\n":"31FDE0","void func_0041ADC8(ppu_context* ctx) {\n":"41ADC8"}
def main():
    done=0
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace'); ch=False
        for fn,tag in FUNCS.items():
            if fn in s and f"E116-{tag}" not in s: s=s.replace(fn,fn+blk(tag),1); ch=True; done+=1
        if ch: f.write_text(s); print(f"E116: {f.name}")
    print("E116: ALREADY" if not done else f"E116: {done} sitios"); return 0
if __name__=="__main__": raise SystemExit(main())
