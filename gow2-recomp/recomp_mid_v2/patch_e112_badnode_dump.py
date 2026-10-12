#!/usr/bin/env python3
"""E112 -- trace determinístico do objeto que falha: na entrada de func_0009EC0C calcula
node = *(parent+0xB8+9*4) (slot da classe 15). Se node NÃO for um objeto plausível (word0 fora de
0x10000..0x4F000000, ou node ele-próprio parecer float 0x3E..0x41xxxxxx), despeja: parent w0, vtable,
o array +0xB8 inteiro (34 slots), e o objeto r3 — para ver que filhos ESTE parent tem e quais faltam.
Gate PS3_TRACE_REG (cap 5)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNC = "void func_0009EC0C(ppu_context* ctx) {\n"
BLOCK = FUNC + r'''        /* E112-BADNODE */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ uint32_t _o=(uint32_t)ctx->gpr[3]; uint32_t _par=(_o>=0x10000u&&_o<0x4F000000u)?vm_read32(_o+4u):0u;
            uint32_t _node=(_par>=0x10000u&&_par<0x4F000000u)?vm_read32(_par+0xB8u+9u*4u):0u;
            int _obj = (_node>=0x40000000u && _node<0x4F000000u) && (vm_read32(_node)>=0x10000u && vm_read32(_node)<0x4F000000u);
            int _bad = (_node!=0u) && !_obj;
            static long _cok=0,_cbad=0; if(_obj)_cok++; else if(_node!=0u)_cbad++; if(((_cok+_cbad)%50)==0) fprintf(stderr,"[BADNODE-CENSO] slot15 objecto=%ld float/invalido=%ld\n",_cok,_cbad);
            static int _n=0; if(_bad && _n++<8){ char _b[360]; int _w=0; _b[0]=0;
              if(_par>=0x10000u&&_par<0x4F000000u) for(int _i=0;_i<34;_i++) _w+=snprintf(_b+_w,sizeof(_b)-_w,"%08X ",vm_read32(_par+0xB8u+_i*4u));
              fprintf(stderr,"[BADNODE] obj=0x%08X parent=0x%08X parent_w0=0x%08X parent_vt=0x%08X node(slot15)=0x%08X\n[BADNODE]   parent+0xB8[0..33]: %s\n",
                _o,_par,(_par>=0x10000u&&_par<0x4F000000u)?vm_read32(_par):0u,(_par>=0x10000u&&_par<0x4F000000u)?vm_read32(_par):0u,_node,_b); fflush(stderr); } } }
'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if FUNC not in s: continue
        if "E112-BADNODE" in s: print("E112: ALREADY"); return 0
        f.write_text(s.replace(FUNC,BLOCK,1)); print(f"E112: aplicado em {f.name}"); return 0
    print("E112: ausente"); return 2
if __name__=="__main__": raise SystemExit(main())
