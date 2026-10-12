#!/usr/bin/env python3
"""E146/E147 -- (a) entrada do callback do vdec no guest (func_002BF960): r4=msgType e obj+0x60c;
(b) em func_0045B780 depois de func_004479E8: o stream de audio do filme (r3): idx +0x1ec, +0x519, estado +0x358, samples +0x154, rate +0x364.
Gate PS3_TRACE_REG. Diagnostico."""
import sys,re
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
def main():
    n=0
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); o=s
        hdr="void func_002BF960(ppu_context* ctx) {"
        if hdr in s and "E146-CB" not in s:
            s=s.replace(hdr,hdr+"\n        /* E146-CB */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<80){ uint32_t _o=vm_read32(0x00540054u); fprintf(stderr,"[VDECCB] msg=%u +0x60c=%u +4=%u\n",(uint32_t)ctx->gpr[4],vm_read32(_o+0x60cu),vm_read32(_o+4u)); } } }''',1); n+=1
        i=s.find("void func_0045B780(ppu_context* ctx) {")
        if i>=0:
            e=s.find("\n}\n",i); j=s.find("func_004479E8(ctx); DRAIN_TRAMPOLINE(ctx);",i)
            if 0<=j<e and "E147-SND" not in s[i:e]:
                k=s.find("\n",j)
                s=s[:k]+"\n        /* E147-SND */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<40){ uint32_t _s=(uint32_t)ctx->gpr[3]; int _ok=(_s>=0x10000u&&_s<0x4F000000u); uint32_t _i=_ok?vm_read32(_s+0x1ecu):0u; uint32_t _b=_s+_i*0x308u;
            fprintf(stderr,"[SNDTIME] stream=0x%08X idx=%u +0x519=%u estado=%u samples(+0x154)=%u rate=0x%08X +0x22=0x%X +0x134=%u +0x148=%u +0x1b0=%u +0x1b4=%u +0x1b8=0x%X +0x1bc=%u +0x1c4=0x%X +0x1f0=0x%X +0x13c=%u\n",_s,_i,_ok?vm_read8(_b+0x519u):0u,_ok?vm_read32(_b+0x358u):0u,_ok?vm_read32(_s+0x154u):0u,_ok?vm_read32(_b+0x364u):0u,_ok?vm_read8(_s+0x22u):0u,_ok?vm_read32(_s+0x134u):0u,_ok?vm_read32(_s+0x148u):0u,_ok?vm_read32(_s+0x1b0u):0u,_ok?vm_read32(_s+0x1b4u):0u,_ok?vm_read32(_s+0x1b8u):0u,_ok?vm_read32(_s+0x1bcu):0u,_ok?vm_read32(_s+0x1c4u):0u,_ok?vm_read32(_s+0x1f0u):0u,_ok?vm_read32(_s+0x13cu):0u); } } }'''+s[k:]; n+=1
        if s!=o: f.write_text(s)
    print("E146/E147: %d sitios"%n); return 0 if n else 2
if __name__=="__main__": raise SystemExit(main())
