#!/usr/bin/env python3
"""E143 -- consumidor de imagens do player de filmes: campos do objecto *(0x540054) no call de cellVdecGetPicItem
(lr 0x2C0BF4) e na entrada da funcao que o contem. Gate PS3_TRACE_REG. Diagnostico."""
import sys,re,json,bisect
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GATE=r'''{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }'''
FIELDS=r'''uint32_t _o=vm_read32(0x00540054u); fprintf(stderr,"[PLAYER-TAG] obj=0x%08X +4=%u +8=0x%08X +0x60c=%u +0x610=%u +0x620=%u +0x64c=0x%X +0x654=0x%X +0x664=%u +0x714=%u +0x718=0x%X +0x71c=0x%X\n",_o,vm_read32(_o+4),vm_read32(_o+8),vm_read32(_o+0x60c),vm_read32(_o+0x610),vm_read32(_o+0x620),vm_read32(_o+0x64c),vm_read32(_o+0x654),vm_read32(_o+0x664),vm_read32(_o+0x714),vm_read32(_o+0x718),vm_read32(_o+0x71c));'''
def main():
    n=0
    for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
        s=f.read_text(errors='replace'); o=s
        for lr,tag in (("0x002C0BF4","GETPICITEM"),("0x002C0CB4","GETPICTURE")):
            m=re.search(r'\n(\s*)ctx->lr = %s; (func_[0-9A-F]+)\(ctx\); DRAIN_TRAMPOLINE\(ctx\);'%lr, s)
            if m and ("E143-"+tag) not in s:
                probe="\n        /* E143-%s */ "%tag+GATE+" if(_on){ static int _k=0; if(_k++<12){ "+FIELDS.replace("TAG",tag)+" } } }"
                s=s[:m.start()]+probe+s[m.start():]; n+=1
        if s!=o: f.write_text(s)
    print("E143: %d sitios"%n); return 0 if n else 2
if __name__=="__main__": raise SystemExit(main())
