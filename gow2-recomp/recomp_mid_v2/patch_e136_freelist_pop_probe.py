#!/usr/bin/env python3
"""E136 -- pop lock-free da freelist de ops FIOS (func_00307CC4, lwarx/stwcx em sched+0x200): imprime ea, head lido (r10),
valor do lwarx (r0), next (r9), cr0.eq apos o stwcx, e o valor em memoria; e marca as saidas func_00307F0C (heap),
func_00307FA0/func_00307FA8 (return 0). Gate PS3_TRACE_REG. Diagnostico."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
def main():
    n=0
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); o=s
        hdr="void func_00307CC4(ppu_context* ctx) {"
        if hdr in s and "E136-POP" not in s:
            i=s.find(hdr); e=s.find("\n}\n",i)
            body=s[i:e]
            lw="{ uint64_t ea = ctx->gpr[11]; ctx->gpr[0] = ppu_res_lwarx(ctx, ea); }"
            st="{ uint64_t ea = ctx->gpr[11]; ppu_res_stwcx(ctx, ea, (uint32_t)ctx->gpr[9]); }"
            if lw in body and st in body:
                body=body.replace(lw, lw+"\n        /* E136-POP */ "+GATE+r''' if(_on){ static int _k=0; if(_k++<16) fprintf(stderr,"[FLPOP] lwarx ea=0x%08X head(r10)=0x%08X r0=0x%08X mem=0x%08X rsv_valid=%d rsv_addr=0x%08X\n",(uint32_t)ctx->gpr[11],(uint32_t)ctx->gpr[10],(uint32_t)ctx->gpr[0],vm_read32((uint32_t)ctx->gpr[11]),(int)ctx->reserve_valid,(uint32_t)ctx->reserve_addr); } }''',1)
                body=body.replace(st, st+"\n        "+GATE+r''' if(_on){ static int _k=0; if(_k++<16) fprintf(stderr,"[FLPOP] stwcx ea=0x%08X next(r9)=0x%08X cr0.eq=%d mem_after=0x%08X\n",(uint32_t)ctx->gpr[11],(uint32_t)ctx->gpr[9],(int)((ctx->cr>>28)&2?1:0),vm_read32((uint32_t)ctx->gpr[11])); } }''',1)
                s=s[:i]+body+s[e:]; n+=1
        for fn,tag in (("func_00307F0C","HEAP"),("func_00307FA0","RET0-A"),("func_00307FA8","RET0-B")):
            h="void %s(ppu_context* ctx) {"%fn
            if h in s and ("E136-"+tag) not in s:
                s=s.replace(h,h+"\n        /* E136-%s */ "%tag+GATE+r''' if(_on){ static int _k=0; if(_k++<16) fprintf(stderr,"[FLPOP] saida %s r3=0x%%08X r31=0x%%08X\n",(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[31]); } }'''%tag,1); n+=1
        if s!=o: f.write_text(s)
    print("E136: %d sitios"%n); return 0 if n else 2
if __name__=="__main__": raise SystemExit(main())
