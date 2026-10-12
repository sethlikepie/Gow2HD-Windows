#!/usr/bin/env python3
"""E132 -- o fh do stream de WAD: (a) entrada de func_002B3F78 (release do stream: fecha *(stream+4) se != 0);
(b) em func_002B4340 logo apos o bl func_0030D578 (openFile async; escreve *(stream+4) na conclusao): op e *(stream+4).
Gate PS3_TRACE_REG. Diagnostico, nao e' fix."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
def main():
    n=0
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); o=s
        hdr="void func_002B3F78(ppu_context* ctx) {"
        if hdr in s and "E132-RELEASE" not in s:
            s=s.replace(hdr,hdr+"\n        /* E132-RELEASE */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<12){ uint32_t _s=(uint32_t)ctx->gpr[3]; int _ok=(_s>=0x10000u&&_s<0x4F000000u);
            fprintf(stderr,"[STREAM-RELEASE] stream=0x%08X fh(+4)=0x%08X op(+8)=0x%08X lr=0x%08X\n",_s,_ok?vm_read32(_s+4u):0u,_ok?vm_read32(_s+8u):0u,(uint32_t)ctx->lr); } } }''',1); n+=1
        call="        ctx->lr = 0x002B4404; func_0030D578(ctx); DRAIN_TRAMPOLINE(ctx);"
        if call in s and "E132-OPEN" not in s:
            s=s.replace(call,call+"\n        /* E132-OPEN */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<12){ uint32_t _s=(uint32_t)ctx->gpr[31]; int _ok=(_s>=0x10000u&&_s<0x4F000000u);
            fprintf(stderr,"[STREAM-OPEN] op(r3)=0x%08X r31(stream?)=0x%08X fh(+4)=0x%08X\n",(uint32_t)ctx->gpr[3],_s,_ok?vm_read32(_s+4u):0u); } } }''',1); n+=1
        if s!=o: f.write_text(s)
    print("E132: %d sitios"%n); return 0 if n else 2
if __name__=="__main__": raise SystemExit(main())
