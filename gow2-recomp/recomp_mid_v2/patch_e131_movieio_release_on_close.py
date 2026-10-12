#!/usr/bin/env python3
"""E131 -- libertar o slot do movieio quando o guest FECHA o file handle do FIOS.

Medido no console (RPCS3, 2026-09-02): cada WAD e' aberto (FUN_0030d578) e FECHADO (FUN_0030d0ac -> dtor do
fh FUN_0031f540) logo a seguir ao stream; ha' no maximo 2 fh vivos. No nosso runtime o movieio (libs/video/
movie_hle.c, MOVIE_IO_SLOTS=4) nunca era libertado -- movie_io_close() nao tinha chamador -- e o 5o WAD
(r_shella, o menu) falhava com "no free slot"; o loader ficava no estado 22 a espera dele.

Fix fiel: no submit do op de close (func_0030B058, r4 = op; op+0x40 == 0xD; op+0x98 = fh == fo do F2B) fechar o
ficheiro host associado (movie_io_close) e apagar a entrada fo->mfd. Sem forcar nada: so' espelha o close do guest.
(Primeira versao ancorava no dtor do fh, func_0031F540 -- RETRATADA no mesmo dia: o dtor so' corre no shutdown do
scheduler, em ambos os lados; a fh e' reciclada pela execucao do op 0xD.)
PS3_MOVIEIO_NO_RELEASE=1 restaura o comportamento antigo (diagnostico). Idempotente."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
HDR="void func_0030B058(ppu_context* ctx) {"
BLOCK=HDR+r'''
        /* E131-MOVIEIO-RELEASE: submit de um op FIOS de CLOSE (tipo 0xD, fh em op+0x98) para um fh que o F2B serve
         * pelo movieio -> libertar o slot host (fiel ao close do console). Medido: o dtor do fh (func_0031F540) NAO e'
         * chamado no fecho normal em nenhum dos lados; o que o guest faz e' submeter o op 0xD (FUN_0030d0ac -> 0030b058). */
        { static int _off=-1; if(_off<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_MOVIEIO_NO_RELEASE"); _off=(_e&&*_e&&*_e!='0')?1:0; }
          if(!_off){
            uint32_t _op=(uint32_t)ctx->gpr[4]; int _ok=(_op>=0x10000u&&_op<0x4F000000u); uint32_t _fh=(_ok&&vm_read32(_op+0x40u)==0xDu)?vm_read32(_op+0x98u):0u; unsigned _m=_fh?f2b_fo_mfd_get(_fh):0u;
            if(_m){ static int _tr=-1; if(_tr<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _tr=(_e&&*_e&&*_e!='0')?1:0; }
              if(_tr) fprintf(stderr,"[MOVIEIO-RELEASE] op=0x%08X fh=0x%08X mfd=0x%08X lr=0x%08X\n",_op,_fh,_m,(uint32_t)ctx->lr);
              movie_io_close(_m); f2b_fo_mfd_del(_fh); } } }'''
def main():
    f=next((x for x in sorted(ROOT.glob("ppu_recomp_00*.cpp")) if HDR in x.read_text(errors='replace')),None)
    if f is None: print("E131: func_0031F540 nao encontrado"); return 2
    s=f.read_text(errors='replace')
    if "E131-MOVIEIO-RELEASE" in s: print("E131: ALREADY"); return 0
    if s.count(HDR)!=1: print("E131: cabecalho nao unico"); return 2
    # as tabelas fo->mfd e f2b_fo_mfd_get sao `static` NESTE chunk (F2B extraido por simbolo, ver build_macos.sh):
    # definir o f2b_fo_mfd_del tambem static, logo a seguir ao f2b_fo_mfd_get, e declarar movie_io_close a nivel de ficheiro.
    import re
    if "E131-DEL" not in s:
        m=re.search(r'static unsigned f2b_fo_mfd_get\(uint32_t[^\n]*\{.*?\n\}\n',s,re.S)
        if not m: print("E131: f2b_fo_mfd_get static nao encontrado"); return 2
        DEL=('/* E131-DEL: o guest fechou o fh -> apagar a entrada fo->mfd (slot do movieio reutilizavel) */\n'
             'extern "C" void movie_io_close(unsigned);\n'
             'static void f2b_fo_mfd_del(uint32_t fo) {\n'
             '    for (int i = 0; i < g_f2b_fo_mfd_n; i++)\n'
             '        if (g_f2b_fo_mfd_fo[i] == fo) {\n'
             '            for (int j = i + 1; j < g_f2b_fo_mfd_n; j++) { g_f2b_fo_mfd_fo[j-1] = g_f2b_fo_mfd_fo[j]; g_f2b_fo_mfd_fd[j-1] = g_f2b_fo_mfd_fd[j]; g_f2b_fo_mfd_sz[j-1] = g_f2b_fo_mfd_sz[j]; }\n'
             '            g_f2b_fo_mfd_n--; return;\n'
             '        }\n'
             '}\n')
        s=s[:m.end()]+DEL+s[m.end():]
    f.write_text(s.replace(HDR,BLOCK,1)); print("E131: instrumentado em",f.name); return 0
if __name__=="__main__": raise SystemExit(main())
