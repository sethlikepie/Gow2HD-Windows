#!/usr/bin/env python3
"""E135 -- func_00307C8C (alloc de op FIOS): entrada (sched, attr r4, attr+0x14, freelist) so' quando chamado do close
(lr==0x30D0EC), e marca o caminho 'heap' (loc_00307F0C) e o resultado do CAS. Gate PS3_TRACE_REG. Diagnostico."""
import sys,re
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
def main():
    n=0
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); o=s
        hdr="void func_00307C8C(ppu_context* ctx) {"
        if hdr in s and "E135-ALLOC-ENTRY" not in s:
            s=s.replace(hdr,hdr+"\n        /* E135-ALLOC-ENTRY */ "+GATE+r''' if(_on && (uint32_t)ctx->lr==0x0030D0ECu){ static int _k=0; if(_k++<8){ uint32_t _s=(uint32_t)ctx->gpr[3],_a=(uint32_t)ctx->gpr[4]; int _ok=(_a>=0x10000u&&_a<0x4F000000u);
            fprintf(stderr,"[ALLOC-ENTRY] sched=0x%08X attr=0x%08X attr+0x14=0x%08X attr[0..3]=%08X %08X %08X %08X freelist=0x%08X\n",_s,_a,_ok?vm_read32(_a+0x14u):0u,_ok?vm_read32(_a):0u,_ok?vm_read32(_a+4u):0u,_ok?vm_read32(_a+8u):0u,_ok?vm_read32(_a+12u):0u,vm_read32(_s+0x200u)); } } }''',1); n+=1
        lab="\nloc_00307F0C:"
        if lab in s and "E135-HEAPPATH" not in s:
            s=s.replace(lab,lab+"\n        /* E135-HEAPPATH */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<8) fprintf(stderr,"[ALLOC-HEAPPATH] r3=0x%08X (head==0 ou flag bit11)\n",(uint32_t)ctx->gpr[3]); }''',1); n+=1
        if s!=o: f.write_text(s)
    print("E135: %d sitios"%n); return 0 if n else 2
if __name__=="__main__": raise SystemExit(main())
