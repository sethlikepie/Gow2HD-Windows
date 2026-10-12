#!/usr/bin/env python3
"""E130 -- sequencia de estados do level loader (func_00042294, jump table em 0x4234C): sonda na ENTRADA de cada
handler (func_<alvo>) que imprime `[LDRSTATE] N t=...` so' quando o estado muda (com contagem do anterior).
Compara-se com o oraculo RPCS3 (bp no bctr 0x42348, estado=r9/4). Gate PS3_TRACE_REG. Diagnostico, nao e' fix."""
import sys,struct,re,glob
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
ELF  = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "EBOOT.ELF"
TABLE=0x0004234C; NSTATES=32
def main():
    b=ELF.read_bytes(); e_phoff,=struct.unpack_from('>Q',b,0x20); n,=struct.unpack_from('>H',b,0x38); segs=[]
    for i in range(n):
        o=e_phoff+i*56; t,=struct.unpack_from('>I',b,o); off,va=struct.unpack_from('>QQ',b,o+8); fsz,=struct.unpack_from('>Q',b,o+32)
        if t==1 and fsz: segs.append((va,off,fsz))
    def rd(ea):
        for va,off,fsz in segs:
            if va<=ea<va+fsz: return struct.unpack_from('>i',b,off+ea-va)[0]
    targets={}
    for st in range(NSTATES):
        rel=rd(TABLE+st*4); tgt=(TABLE+rel)&0xffffffff
        if 0x10000<=tgt<0x50C7E0: targets.setdefault(tgt,[]).append(st)
    files={p:open(p,errors='replace').read() for p in sorted(glob.glob(str(ROOT/"ppu_recomp_00[0-5].cpp")))}
    n=0; miss=[]
    for tgt,sts in sorted(targets.items()):
        hdr="void func_%08X(ppu_context* ctx) {"%tgt; done=False
        for p,s in files.items():
            if hdr in s:
                if "E130-LDRSTATE" in s.split(hdr,1)[1][:600]: done=True; break
                probe=hdr+'\n        /* E130-LDRSTATE %s */ { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!=\'0\')?1:0; }\n          if(_on){ extern int g_e130_last, g_e130_n; if(g_e130_last!=%d){ fprintf(stderr,"[LDRSTATE] %d (anterior %%d x%%d)\\n",g_e130_last,g_e130_n); g_e130_last=%d; g_e130_n=0; } g_e130_n++; } }'%(",".join(map(str,sts)),sts[0],sts[0],sts[0])
                files[p]=s.replace(hdr,probe,1); done=True; n+=1; break
        if not done: miss.append(hex(tgt))
        # o dispatcher da jump table salta para LABELS internas (loc_<alvo>:) dentro da funcao grande;
        # as func_<alvo> sao so' as tail-entries. Instrumentar tambem cada label.
        lab="loc_%08X:"%tgt
        for p,s in files.items():
            if ("\n"+lab) in s and ("E130-LDRLABEL %08X"%tgt) not in s:
                probe=lab+'\n        /* E130-LDRLABEL %08X (estado %s) */ { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!=\'0\')?1:0; }\n          if(_on){ extern int g_e130_last, g_e130_n; if(g_e130_last!=%d){ fprintf(stderr,"[LDRSTATE] %d (anterior %%d x%%d)\\n",g_e130_last,g_e130_n); g_e130_last=%d; g_e130_n=0; } g_e130_n++; } }'%(tgt,",".join(map(str,sts)),sts[0],sts[0],sts[0])
                files[p]=s.replace("\n"+lab,"\n"+probe); n+=1
    p0=sorted(files)[0]
    if "g_e130_last=-1" not in files[p0]: files[p0]=files[p0].replace('#include "ppu_recomp.h"','#include "ppu_recomp.h"\nint g_e130_last=-1, g_e130_n=0; /* E130 */',1)
    for p,s in files.items(): Path(p).write_text(s)
    print("E130: %d handlers instrumentados; estados->alvo: %s; sem funcao: %s"%(n,{tuple(v):hex(k) for k,v in targets.items()},miss)); return 0
if __name__=="__main__": raise SystemExit(main())
