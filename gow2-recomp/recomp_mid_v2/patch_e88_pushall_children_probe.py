#!/usr/bin/env python3
"""E88 -- que objecto cada push-all/pop-all percorre, e que gestores toca.

Medido: FUN_0041f700/0041f924 (classe 1) e FUN_0042af18/0042a0c8 (classe 22)
empurram/despejam o proprio gestor e depois ITERAM A LISTA DE FILHOS do objecto
(obj+0x80 / obj+0x38) empurrando/despejando o gestor da classe de cada filho.
O saldo por gestor e' portanto dirigido pelos dados. Esta sonda imprime, na
entrada de cada uma, o gestor (r3), o objecto (r4 no push; o topo do stack no
pop, lido como a funcao le') e os subtags dos filhos. Gate PS3_TRACE_PUSHALL.
"""
import glob, os, sys
MARKER="E88-PUSHALL"
def blk(tag, listoff, obj_expr):
    return ('        /* %s %s */\n' % (MARKER, tag) +
      '        { static int _pa_on=-1; if(_pa_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_PUSHALL"); _pa_on=(_e&&*_e&&*_e!=\'0\')?1:0; }\n'
      '          if(_pa_on){ uint32_t _m=(uint32_t)ctx->gpr[3]; uint32_t _o=(%s); char _buf[256]; int _k=0; _buf[0]=0;\n' % obj_expr +
      '            if(_o>=0x10000u && _o<0x4F000000u){ uint32_t _h=_o+%d; uint32_t _p=vm_read32(_h); int _n=0;\n' % listoff +
      '              while(_p && _p!=_h && _n<40 && _p>=0x10000u && _p<0x4F000000u){ uint32_t _c=vm_read32(_p+8); uint32_t _w=_c?vm_read32(_c+4):0; _k+=snprintf(_buf+_k,sizeof(_buf)-_k,"%%u ",(_w>>16)&0xFFF); if(_k>200)break; _p=vm_read32(_p); _n++; } }\n'
      '            fprintf(stderr,"[PUSHALL] %s mgr=0x%%08X cursor=%%d obj=0x%%08X w0=0x%%08X filhos-subtag=[%%s]\\n", _m, (int)(int8_t)vm_read8(_m+0xC8), _o, (_o>=0x10000u&&_o<0x4F000000u)?vm_read32(_o+4):0, _buf); fflush(stderr);} }\n' % tag)
SITES={
 "func_0041F700": blk("push1", 0x80, "(uint32_t)ctx->gpr[4] ? (uint32_t)ctx->gpr[4]-4u : 0u"),
 "func_0041F924": blk("pop1",  0x80, "((int8_t)vm_read8((uint32_t)ctx->gpr[3]+0xC8)>=0) ? vm_read32((uint32_t)ctx->gpr[3]+0x48u+(uint32_t)((int8_t)vm_read8((uint32_t)ctx->gpr[3]+0xC8))*4u) : 0u"),
 "func_0042AF18": blk("push22",0x38, "(uint32_t)ctx->gpr[4] ? (uint32_t)ctx->gpr[4]-4u : 0u"),
 "func_0042A0C8": blk("pop22", 0x38, "((int8_t)vm_read8((uint32_t)ctx->gpr[3]+0xC8)>=0) ? vm_read32((uint32_t)ctx->gpr[3]+0x48u+(uint32_t)((int8_t)vm_read8((uint32_t)ctx->gpr[3]+0xC8))*4u) : 0u"),
}
def main():
    lift=os.path.abspath(sys.argv[1] if len(sys.argv)>1 else os.path.join(os.path.dirname(__file__),"..","recomp_macos_v2"))
    n=0
    for path in sorted(glob.glob(os.path.join(lift,"ppu_recomp_*.cpp"))):
        s=open(path,errors="replace").read(); t=s
        for fn,code in SITES.items():
            h="void %s(ppu_context* ctx) {\n"%fn
            if h not in t: continue
            a=t.index(h)
            if MARKER in t[a:a+3000]: n+=1; continue
            t=t[:a+len(h)]+code+t[a+len(h):]; n+=1
        if t!=s: open(path,"w").write(t)
    print("e88 sites:",n); return 0 if n==4 else 2
if __name__=="__main__": sys.exit(main())
