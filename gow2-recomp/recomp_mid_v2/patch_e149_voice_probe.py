#!/usr/bin/env python3
"""E149 -- func_00453A68 (posicao da voz do Scream): r3=voz, tabela300[v]+0x10/0x14/0x18/+0x104, tabela200[v]+0x1c/+0x24, lr.
Gate PS3_TRACE_REG. Diagnostico."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); hdr="void func_00453A68(ppu_context* ctx) {"
        if hdr in s:
            if "E149-VOICE" in s: print("E149: ALREADY"); return 0
            s=s.replace(hdr,hdr+"\n        /* E149-VOICE */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<10){ uint32_t _v=(uint32_t)ctx->gpr[3]; uint32_t _T3=vm_read32(0x5410FCu),_T2=vm_read32(0x5410F8u); uint32_t _t3=_T3+_v*0x300u,_t2=_T2+_v*0x200u; int _ok=(_v<0xC0u);
            fprintf(stderr,"[VOICE] t300=0x%08X v=%u t300+0x10=0x%X +0x14=0x%X +0x18=0x%X +0x104=%u t200+0=%u +0x1c=0x%X +0x24=%u lr=0x%08X\n",_T3,_v,_ok?vm_read32(_t3+0x10u):0u,_ok?vm_read32(_t3+0x14u):0u,_ok?vm_read32(_t3+0x18u):0u,_ok?vm_read32(_t3+0x104u):0u,_ok?vm_read32(_t2):0u,_ok?vm_read32(_t2+0x1cu):0u,_ok?vm_read32(_t2+0x24u):0u,(uint32_t)ctx->lr); } } }''',1)
            f.write_text(s); print("E149: instrumentado em",f.name); return 0
    print("E149: nao achei"); return 2
if __name__=="__main__": raise SystemExit(main())
