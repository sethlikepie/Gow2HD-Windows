#!/usr/bin/env python3
"""E145 -- relogio do player de filmes: em cada `lwz r0,0x60C(r30)` (0x2C0914 / 0x2C0BD0) imprime r9 (+0x780, imagem a levantar),
r22 (frame do relogio), r29/r31 (duracao/decorrido) e obj+0x774/778/77C/76C. Gate PS3_TRACE_REG. Diagnostico."""
import sys,re
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
def main():
    n=0
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); o=s
        for m in list(re.finditer(r'^void func_002C0([5-9A-F][0-9A-F]{2})\(ppu_context\* ctx\) \{\n(.*?)^\}\n',s,re.M|re.S))[::-1]:
            body=m.group(2)
            if "E145-CLOCK" in body: continue
            nb=re.sub(r'(\n(\s*)ctx->gpr\[0\] = vm_read32\(ctx->gpr\[30\] \+ 0x60C\);)',
                      lambda mm: mm.group(1)+"\n        /* E145-CLOCK */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<40){ uint32_t _o=(uint32_t)ctx->gpr[30]; union{uint32_t u; float f;} _a,_b; _a.u=vm_read32(_o+0x778u); _b.u=vm_read32(_o+0x77Cu);
            fprintf(stderr,"[CLOCK] r9=%u r22=%u r29=%u r31=%u +0x774=%u +0x778=%.4f +0x77c=%.2f +0x76c=%u +0x60c=%u\n",(uint32_t)ctx->gpr[9],(uint32_t)ctx->gpr[22],(uint32_t)ctx->gpr[29],(uint32_t)ctx->gpr[31],vm_read32(_o+0x774u),_a.f,_b.f,vm_read32(_o+0x76Cu),vm_read32(_o+0x60Cu)); } } }''', body)
            if nb!=body: s=s[:m.start(2)]+nb+s[m.end(2):]; n+=1
        if s!=o: f.write_text(s)
    print("E145: %d fragmentos"%n); return 0 if n else 2
if __name__=="__main__": raise SystemExit(main())
