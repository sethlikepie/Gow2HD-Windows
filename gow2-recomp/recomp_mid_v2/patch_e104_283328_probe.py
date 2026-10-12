#!/usr/bin/env python3
"""E104 -- sondas a entrada de func_00283328 (r3=filho, r4, r5) e func_000912A0 (r3, r4): imprime os args,
w0 e +4 (parent) de r3/r4, e lr. Gate PS3_TRACE_REG (cap 12 cada)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
TEMPLATE = r'''        /* E104-TAG */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ static int _n=0; if(_n++<12){ uint32_t _a=(uint32_t)ctx->gpr[3], _c=(uint32_t)ctx->gpr[4];
            int _ao=(_a>=0x10000u&&_a<0x4F000000u), _co=(_c>=0x10000u&&_c<0x4F000000u);
            fprintf(stderr,"[TAG] r3=0x%08X[+0=0x%08X,+4=0x%08X] r4=0x%08X[+0=0x%08X,+4=0x%08X] r5=0x%08X r6=0x%08X lr=0x%08X\n",
              _a,_ao?vm_read32(_a):0u,_ao?vm_read32(_a+4u):0u,_c,_co?vm_read32(_c):0u,_co?vm_read32(_c+4u):0u,(uint32_t)ctx->gpr[5],(uint32_t)ctx->gpr[6],(uint32_t)ctx->lr); fflush(stderr); } } }
'''
FUNCS = {"void func_00283328(ppu_context* ctx) {\n": "283328", "void func_000912A0(ppu_context* ctx) {\n": "912A0"}
def main():
    done=0
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace'); ch=False
        for fn,tag in FUNCS.items():
            if fn in s and f"E104-{tag}" not in s: s=s.replace(fn, fn+TEMPLATE.replace("TAG",tag),1); ch=True; done+=1
        if ch: f.write_text(s); print(f"E104: {f.name}")
    print("E104: ALREADY" if not done else f"E104: {done} sitios"); return 0
if __name__=="__main__": raise SystemExit(main())
