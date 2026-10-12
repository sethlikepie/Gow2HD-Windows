#!/usr/bin/env python3
"""E134 -- func_0030D0AC (closeFile): sched+0x16C, freelist sched+0x200, fh; e o r3 devolvido por func_00307C8C (alloc de op).
Gate PS3_TRACE_REG. Diagnostico."""
import sys,re
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
def main():
    n=0
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); o=s
        hdr="void func_0030D0AC(ppu_context* ctx) {"
        if hdr in s and "E134-CLOSE-ENTRY" not in s:
            s=s.replace(hdr,hdr+"\n        /* E134-CLOSE-ENTRY */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<10){ uint32_t _s=(uint32_t)ctx->gpr[3],_fh=(uint32_t)ctx->gpr[5]; int _ok=(_s>=0x10000u&&_s<0x4F000000u), _fok=(_fh>=0x10000u&&_fh<0x4F000000u);
            fprintf(stderr,"[CLOSE-ENTRY] sched=0x%08X +0x16C=%u freelist(+0x200)=0x%08X fh=0x%08X fh+8=0x%08X fh+0x10=0x%08X fh+0x14=0x%08X fh+0x18=0x%08X fh+0x28=0x%08X lr=0x%08X\n",_s,_ok?vm_read32(_s+0x16Cu):0u,_ok?vm_read32(_s+0x200u):0u,_fh,_fok?vm_read32(_fh+8u):0u,_fok?vm_read32(_fh+0x10u):0u,_fok?vm_read32(_fh+0x14u):0u,_fok?vm_read32(_fh+0x18u):0u,_fok?vm_read32(_fh+0x28u):0u,(uint32_t)ctx->lr); } } }''',1); n+=1
        # o bl func_00307C8C dentro de func_0030D0AC: 0x30D0AC + prologo... procurar a primeira call a func_00307C8C apos o cabecalho
        i=s.find(hdr)
        if i>=0:
            j=s.find("func_00307C8C(ctx); DRAIN_TRAMPOLINE(ctx);",i); e=s.find("\n}\n",i)
            if 0<=j<e and "E134-OPALLOC" not in s[i:e]:
                k=s.find("\n",j); s=s[:k]+"\n        /* E134-OPALLOC */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<10) fprintf(stderr,"[CLOSE-OPALLOC] op(r3)=0x%08X\n",(uint32_t)ctx->gpr[3]); } }'''+s[k:]; n+=1
        if s!=o: f.write_text(s)
    print("E134: %d sitios"%n); return 0 if n else 2
if __name__=="__main__": raise SystemExit(main())
