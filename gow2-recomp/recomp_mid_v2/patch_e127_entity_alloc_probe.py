#!/usr/bin/env python3
"""E127 -- em func_000912A0: (a) o produto devolvido por TABLE[22]->vt[0x48] (r3 antes do bl func_0009B964: w0,+0x44,+0x9c,(+0x9c).w0)
e (b) a entidade devolvida por func_0009B964 (r3 depois). Gate PS3_TRACE_REG. Diagnostico, nao e' fix."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
CALL = "        ctx->lr = 0x000913B4; func_0009B964(ctx); DRAIN_TRAMPOLINE(ctx);"
GATE = r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
PRE = "        /* E127-ENTITY-ALLOC */\n        "+GATE+r''' if(_on){ static int _n=0; if(_n++<6){ uint32_t _p=(uint32_t)ctx->gpr[3]; int _ok=(_p>=0x10000u&&_p<0x4F000000u); uint32_t _ar=_ok?vm_read32(_p+0x9cu):0u; int _aok=(_ar>=0x10000u&&_ar<0x4F000000u);
            fprintf(stderr,"[ENTALLOC] produto(vt48 TABLE[22])=0x%08X w0=0x%08X w1=0x%08X +0x44=0x%08X +0x9c=0x%08X (+0x9c).w0=0x%08X\n",_p,_ok?vm_read32(_p):0u,_ok?vm_read32(_p+4u):0u,_ok?vm_read32(_p+0x44u):0u,_ar,_aok?vm_read32(_ar):0u); } } }
'''
POST = "\n        "+GATE+r''' if(_on){ static int _n=0; if(_n++<6){ uint32_t _e=(uint32_t)ctx->gpr[3]; int _ok=(_e>=0x10000u&&_e<0x4F000000u);
            fprintf(stderr,"[ENTALLOC] entidade(9B964)=0x%08X w0=0x%08X w1=0x%08X\n",_e,_ok?vm_read32(_e):0u,_ok?vm_read32(_e+4u):0u); } } }'''
def main():
    f=ROOT/"ppu_recomp_000.cpp"; s=f.read_text(errors='replace')
    if "E127-ENTITY-ALLOC" in s: print("E127: ALREADY"); return 0
    if s.count(CALL)!=1: print("E127: sitio nao unico:",s.count(CALL)); return 2
    f.write_text(s.replace(CALL,PRE+CALL+POST,1)); print("E127: instrumentado"); return 0
if __name__=="__main__": raise SystemExit(main())
