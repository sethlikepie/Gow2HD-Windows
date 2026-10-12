"""E275 (gated diagnostico, OFF por defeito): stampa o mfd movie_io na FO natural do WAD.

Medido (E274): o open natural do R_PermA SUCEDE (file_new r3!=0, FO valida) -- o dearchiver
ACEITA o WAD -- mas a LEITURA natural entrega 0 bytes (o inflate edgezlib nao chega a' read op do
estado 22; §4ao). O host-serve FIOS-F2B-MOVIEIO so' dispara quando o open e' REJEITADO (r3==0),
logo nao ajuda aqui. Este patch: quando o open natural do WAD sucede (r3!=0), regista tambem a FO
como host-backed (movie_io_open + f2b_fo_mfd_put) para a AREAD-HLE servir os bytes do cache -- como
o e130 fazia. Caminho HOST declarado (o natural e' a meta; isto verifica [D] a jusante). Gated
PS3_WAD_HOST_READ, OFF por defeito.
"""
import sys, pathlib
ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent / "recomp_macos_e162"
ANCHOR = "        /* FIOS-F2B-MOVIEIO: when dearchiver rejects a path that lives in\n"
BLOCK = ('        /* E275 WAD-HOST-READ: FO natural do WAD sucedeu mas a leitura natural da zero bytes;\n'
  '         * registar a FO como host-backed p/ a AREAD servir do cache. Gated PS3_WAD_HOST_READ. */\n'
  '        { static int _wh=-1; if(_wh<0){const char* e=getenv("PS3_WAD_HOST_READ"); _wh=(e&&*e&&*e!=\'0\')?1:0;}\n'
  '          uint32_t _fo=(uint32_t)ctx->gpr[3];\n'
  '          if(_wh && _fo>=0x10000u && _fo<0x4F000000u){\n'
  '            char _pt[160]; _pt[0]=0; uint32_t _a=(uint32_t)(ctx->gpr[25]);\n'
  '            if(_a && _a<0x4F000000u){ for(int _i=0;_i<159;_i++){ unsigned char _c=(unsigned char)vm_read8(_a+_i); _pt[_i]=(char)_c; if(!_c) break; } _pt[159]=0; }\n'
  '            if(_pt[0] && (strstr(_pt,"wad")||strstr(_pt,"WAD"))){\n'
  '              extern unsigned movie_io_open(const char*, unsigned*); unsigned _sz=0;\n'
  '              unsigned _mfd=movie_io_open(_pt,&_sz);\n'
  '              if(!_mfd){ const char* _b=_pt; for(const char* _s=_pt; *_s; _s++) if(*_s==\'/\') _b=_s+1; _mfd=movie_io_open(_b,&_sz); }\n'
  '              if(_mfd){ f2b_fo_mfd_del(_fo); f2b_fo_mfd_put(_fo,_mfd,_sz); vm_write32(_fo+0x48u,0u); vm_write32(_fo+0x4Cu,_sz); vm_write32(_fo+0x50u,1u);\n'
  '                static int _n=0; if(_n++<8){ fprintf(stderr,"[E275] WAD-HOST-READ fo=0x%08X mfd=%u sz=%u path=\'%s\'\\n",_fo,_mfd,_sz,_pt); fflush(stderr);} } } } }\n')
n=0
for f in sorted(ROOT.glob("ppu_recomp_00*.cpp")):
    s=f.read_text(errors="replace")
    if "WAD-HOST-READ" in s: continue
    if ANCHOR in s: s=s.replace(ANCHOR, BLOCK+ANCHOR, 1); f.write_text(s); n+=1; print("E275 em",f.name)
print("E275 sitios:",n); sys.exit(0 if n else 2)
