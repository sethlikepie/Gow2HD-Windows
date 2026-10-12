#!/usr/bin/env python3
"""E153 -- entrada de func_00461FE8 (update do stream de som): +0x1f0 (float), +0x1b8, +0x22, +0x1b4, +0x148, +0x154, e o valor da
constante TOC[0x1af]. Gate PS3_TRACE_REG. Diagnostico."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); h="void func_00461FE8(ppu_context* ctx) {"
        if h in s:
            if "E153-STRUPD" in s: print("E153: ALREADY"); return 0
            s=s.replace(h,h+"\n        /* E153-STRUPD */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<30){ uint32_t _s=(uint32_t)ctx->gpr[3]; int _ok=(_s>=0x10000u&&_s<0x4F000000u); union{uint32_t u; float f;} _a,_c; _a.u=_ok?vm_read32(_s+0x1f0u):0u; uint32_t _cp=vm_read32(0x541178u+0x1afu*4u); _c.u=(_cp>=0x10000u&&_cp<0x4F000000u)?vm_read32(_cp):0u;
            uint32_t _B=0x47CA9B80u; fprintf(stderr,"[BLK] %08X %08X %08X %08X | %08X %08X %08X %08X\n",vm_read32(_B),vm_read32(_B+4),vm_read32(_B+8),vm_read32(_B+12),vm_read32(_B+16),vm_read32(_B+20),vm_read32(_B+24),vm_read32(_B+28)); fprintf(stderr,"[STRUPD] lr=0x%08X stream=0x%08X +0x1f0=%g const=%g +0x1b8=%u +0x22=0x%X +0x1b4=%u +0x148=%u +0x154=%u +0x1c4=0x%X\n",(uint32_t)ctx->lr,_s,_a.f,_c.f,_ok?vm_read32(_s+0x1b8u):0u,_ok?vm_read8(_s+0x22u):0u,_ok?vm_read32(_s+0x1b4u):0u,_ok?vm_read32(_s+0x148u):0u,_ok?vm_read32(_s+0x154u):0u,_ok?vm_read32(_s+0x1c4u):0u); } } }''',1)
            f.write_text(s); print("E153: instrumentado em",f.name); return 0
    print("E153: nao achei"); return 2
if __name__=="__main__": raise SystemExit(main())
