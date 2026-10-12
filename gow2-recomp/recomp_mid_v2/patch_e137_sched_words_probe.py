#!/usr/bin/env python3
"""E137 -- func_00307C8C (alloc de op): palavras 0..7 do scheduler (r3) e +0x16C; FUN_0046577c exige *(sched+4)=='FIOS'.
Gate PS3_TRACE_REG. Diagnostico."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); hdr="void func_00307C8C(ppu_context* ctx) {"
        if hdr in s:
            if "E137-SCHED" in s: print("E137: ALREADY"); return 0
            s=s.replace(hdr,hdr+"\n        /* E137-SCHED */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<10){ uint32_t _s=(uint32_t)ctx->gpr[3]; int _ok=(_s>=0x10000u&&_s<0x4F000000u);
            fprintf(stderr,"[SCHED] r3=0x%08X w0..7=%08X %08X %08X %08X %08X %08X %08X %08X +0x16C=%u lr=0x%08X\n",_s,_ok?vm_read32(_s):0u,_ok?vm_read32(_s+4u):0u,_ok?vm_read32(_s+8u):0u,_ok?vm_read32(_s+12u):0u,_ok?vm_read32(_s+16u):0u,_ok?vm_read32(_s+20u):0u,_ok?vm_read32(_s+24u):0u,_ok?vm_read32(_s+28u):0u,_ok?vm_read32(_s+0x16Cu):0u,(uint32_t)ctx->lr); } } }''',1)
            f.write_text(s); print("E137: instrumentado em",f.name); return 0
    print("E137: nao achei"); return 2
if __name__=="__main__": raise SystemExit(main())
