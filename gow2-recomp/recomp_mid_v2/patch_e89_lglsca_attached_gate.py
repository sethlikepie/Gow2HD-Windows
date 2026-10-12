#!/usr/bin/env python3
"""E89 -- DIAGNOSTICO gated: entrar em FUN_000415f0 com o WAD 'anexado' (bit 4).

Medido (E88): FUN_000415f0 e' chamada uma vez no #038 com o R_LglScA (classe
22) e bit 4 limpo, toma o ramo 'nao anexado' = push-all + notify-owner
(release): entre o push1 do produto-raiz (12 gestores empurrados) e o pop1
correspondente o R_LglScA e' destruido e a lista de filhos fica vazia -> os 12
nao sao despejados -> nivel pendurado -> conjunto A acaba em -1 -> #042-#054
falham -> wrap infinito. Se no console o R_LglScA chega ao #038 ANEXADO, o
FUN_000415f0 nao faz nada e a aritmetica fecha.

Este gate poe bit 4 nas flags (p+8) de qualquer objecto de classe 22 a entrada
de func_000415F0, ANTES da leitura. So' testa a hipotese; nao e' fix -- o fix
e' descobrir quem anexa o WAD no console e por que nao o fazemos.
Gate PS3_E89_LGLSCA_ATTACHED (OFF por default).
"""
import glob, os, sys
MARKER="E89-ATTACHED-GATE"
BLK=('        /* '+MARKER+' */\n'
     '        { static int _e89=-1; if(_e89<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_E89_LGLSCA_ATTACHED"); _e89=(_e&&*_e&&*_e!=\'0\')?1:0; }\n'
     '          if(_e89){ uint32_t _p=(uint32_t)ctx->gpr[3]; if(_p>=0x10000u && vm_read16(_p+6)==0x0016u){ uint16_t _f=vm_read16(_p+8); vm_write16(_p+8, _f|4u);\n'
     '            fprintf(stderr,"[E89] p=0x%08X classe22 flags 0x%04X -> 0x%04X (bit4 forcado: ramo nao-anexado SALTADO)\\n", _p, _f, _f|4u); fflush(stderr);} } }\n')
def main():
    lift=os.path.abspath(sys.argv[1] if len(sys.argv)>1 else os.path.join(os.path.dirname(__file__),"..","recomp_macos_v2"))
    for path in sorted(glob.glob(os.path.join(lift,"ppu_recomp_*.cpp"))):
        s=open(path,errors="replace").read(); h="void func_000415F0(ppu_context* ctx) {\n"
        if h not in s: continue
        if MARKER in s: print("ALREADY"); return 0
        a=s.index(h); s=s[:a+len(h)]+BLK+s[a+len(h):]; open(path,"w").write(s); print("APPLIED",os.path.basename(path)); return 0
    print("MISSING",file=sys.stderr); return 2
if __name__=="__main__": sys.exit(main())
