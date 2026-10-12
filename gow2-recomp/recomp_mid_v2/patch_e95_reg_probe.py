#!/usr/bin/env python3
"""E95 -- sonda do registo por gestor (FUN_0039e794): so' os registos 'default' (w0 bit31).

FUN_0039e794(mgr, desc): idx = mgr->vt[0x4c]() (stack[cursor]->+0x20, ou mgr+0xD4 se *desc<0);
factory = *( *(mgr+0x24 + *(mgr+0x44)*0xc) + idx*4 ); factory->vt[0x14](factory-4, desc).
Medido no oraculo: a raiz de um WAD recebe 12 filhos 'default' (w0=0x8000000N) via FUN_00256B64;
na raiz do R_Hero01 o nosso runtime perde o da classe 4. Esta sonda imprime, para cada registo
default, o gestor (indice na TABLE), cursor, produto corrente, idx e a entrada da typemap.
Gate PS3_TRACE_REG (OFF por default). Idempotente (marcador E95-REG).
"""
import sys, glob
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
NEEDLE = "void func_0039E794(ppu_context* ctx) {\n"
BLOCK = NEEDLE + r'''        /* E95-REG */
        { static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!='0')?1:0; }
          if(_on){ uint32_t _m=(uint32_t)ctx->gpr[3], _d=(uint32_t)ctx->gpr[4];
            uint32_t _w0=(_d>=0x10000u&&_d<0x4F000000u)?vm_read32(_d):0u;
            if(_w0 & 0x80000000u){ int _ti=-1; for(int _i=0;_i<40;_i++) if(vm_read32(0x00868D48u+_i*4u)==_m){_ti=_i;break;}
              int _cur=(int)(int8_t)vm_read8(_m+0xC8u); uint32_t _prod=(_cur>=0)?vm_read32(_m+0x48u+(uint32_t)_cur*4u):0u;
              uint32_t _idx=vm_read32(_m+0xD4u); uint32_t _tm=vm_read32(_m+0x24u+vm_read32(_m+0x44u)*0xCu);
              uint32_t _ent=(_tm>=0x10000u&&_tm<0x4F000000u)?vm_read32(_tm+_idx*4u):0xDEADu;
              fprintf(stderr,"[REG] T[%d] mgr=0x%08X desc=0x%08X w0=0x%08X cur=%d prod=0x%08X d4=%u tmidx=%u tm=0x%08X ent=0x%08X\n",
                _ti,_m,_d,_w0,_cur,_prod,_idx,vm_read32(_m+0x44u),_tm,_ent); fflush(stderr); } } }
'''
def main():
    files=[f for f in sorted(ROOT.glob("ppu_recomp_*.cpp")) if NEEDLE in f.read_text(errors='replace')]
    if len(files)!=1: print(f"E95: func_0039E794 em {len(files)} chunks -> recusa"); return 2
    f=files[0]; s=f.read_text(errors='replace')
    if "E95-REG" in s: print("E95: ALREADY"); return 0
    if s.count(NEEDLE)!=1: print("E95: agulha ambigua"); return 2
    f.write_text(s.replace(NEEDLE, BLOCK, 1)); print(f"E95: aplicado em {f.name}"); return 0
if __name__=="__main__": raise SystemExit(main())
