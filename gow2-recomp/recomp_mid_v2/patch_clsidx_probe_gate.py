#!/usr/bin/env python3
"""Sonda + gate de TODOS os sitios que escrevem o indice de classe a partir do
produto de uma fabrica.

Porque existe
-------------
A cadeia dos ledgers E41-E61 termina em:

    ctx->gpr[0] = vm_read32(ctx->gpr[3] + 0x20);   /* produto->campo 0x20 */
    vm_write16(ctx->gpr[N] + 0x6, ctx->gpr[0]);    /* indice de classe da vitima */

Com a fabrica vazia o produto e' NULL, le'-se `*(NULL+0x20)` = 0, e a vitima fica
com indice de classe 0 -- que a jusante da' o pool 0, o texto como cabeca de
free-list e as escritas em memoria nao commitada.

DUAS COISAS QUE ESTE PATCH ARRUMA
---------------------------------
1. COBERTURA. A sonda `VICTIM` do `patch_24e3d0_victim_probe.py` cobre **um**
   sitio (`func_0024E3D0`). O padrao aparece em **SETE**:

     func_0024E3D0  -> gpr[9]+6      func_0041C558  -> gpr[4]+6
     func_0024E430  -> gpr[21]+6     func_0041E8A4  -> gpr[4]+6
     func_004117B0  -> gpr[4]+6      func_0042C644  -> gpr[4]+6
                                     func_0042EC60  -> gpr[4]+6

   Logo o «4 escritas, 4 com produto nulo» do E59 descreve 1 de 7, e nao pode ser
   generalizado. (O E41 ja' tinha medido `func_004117B0` a escrever **2** no
   mesmo campo -- um valor bom -- o que mostra que ha' sitios que acertam.)

2. O EXPERIMENTO DISCRIMINADOR. `PS3_SKIP_NULLTYPE=1` **nao escreve** quando o
   produto e' nulo, deixando o indice de classe que la' estava. Se a cadeia for o
   que bloqueia o avanco do jogo, o loop principal (`[ALCHAIN] degrau 32`,
   `func_00411A5C`, 107 iteracoes) tem de mudar de comportamento. Se nada mudar,
   a cadeia nao e' o bloqueador e a alavanca esta' noutro sitio.

   Isto e' um GATE DE DIAGNOSTICO, nao um fix: nao escrever tambem nao e' fiel ao
   CELL. Serve para decidir onde investir, e nada mais. OFF por default.

Gates
-----
  `PS3_TRACE_CLSIDX`      liga a sonda (OFF por default). Cap `_CAP`, default 400.
  `PS3_SKIP_NULLTYPE`     salta a escrita quando o produto e' 0 (OFF por default).

Uso:  patch_clsidx_probe_gate.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se nao casar os 7 sitios esperados.
"""
import os
import re
import sys
import glob

MARKER = "CLSIDX-PROBE-GATE"
EXPECTED = 7

PAT = re.compile(
    r'(        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[3\] \+ 0x20\);\n)'
    r'((?:.*\n)??)'
    r'(        vm_write16\(ctx->gpr\[(\d+)\] \+ 0x6, ctx->gpr\[0\]\);\n)'
)


def block(fn, reg):
    return (
        '        /* ' + MARKER + ': sitio ' + fn + ', vitima em gpr[' + reg + '] */\n'
        '        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n'
        '            const char* _e=getenv("PS3_TRACE_CLSIDX");\n'
        '            _on=(_e&&*_e&&*_e!=\'0\')?1:0; }\n'
        '          static int _sk=-1; if(_sk<0){ extern char* getenv(const char*);\n'
        '            const char* _s=getenv("PS3_SKIP_NULLTYPE");\n'
        '            _sk=(_s&&*_s&&*_s!=\'0\')?1:0; }\n'
        '          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n'
        '            const char* _c=getenv("PS3_TRACE_CLSIDX_CAP");\n'
        '            _cap=(_c&&*_c)?atoi(_c):400; }\n'
        '          uint32_t _prod=(uint32_t)ctx->gpr[3];\n'
        '          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n'
        '            unsigned long ps3_dbg_tid(void);\n'
        '            fprintf(stderr,"[CLSIDX] tid=%lu sitio=' + fn + ' vitima=0x%08X "\n'
        '              "produto=0x%08X valor=0x%04X %s%s\\n", ps3_dbg_tid(),\n'
        '              (uint32_t)ctx->gpr[' + reg + '], _prod,\n'
        '              (unsigned)(ctx->gpr[0] & 0xFFFFu),\n'
        '              _prod ? "ok" : "PRODUTO-NULO",\n'
        '              (!_prod && _sk) ? " SALTADO" : "");\n'
        '            fflush(stderr); } }\n'
        '          if(!(_sk && _prod==0))\n'
        '            vm_write16(ctx->gpr[' + reg + '] + 0x6, ctx->gpr[0]);\n'
        '        }\n'
    )


def patch_text(src):
    if MARKER in src:
        return src, "ALREADY", 0
    out = []
    pos = 0
    n = 0
    for m in PAT.finditer(src):
        head = src.rfind('\nvoid func_', 0, m.start())
        fn = src[head + 6:src.find('(', head)] if head >= 0 else "?"
        out.append(src[pos:m.start()])
        out.append(m.group(1))
        out.append(m.group(2))
        out.append(block(fn, m.group(4)))
        pos = m.end()
        n += 1
    if not n:
        return src, "MISSING", 0
    out.append(src[pos:])
    return "".join(out), "APPLIED", n


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not chunks:
        print("ERRO: nenhum ppu_recomp_*.cpp em %s" % lift, file=sys.stderr)
        return 2
    applied = already = sites = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        out, state, n = patch_text(src)
        if state == "APPLIED":
            with open(path, "w") as fh:
                fh.write(out)
            applied += 1
            sites += n
            print("APPLIED  %-20s sites=%d" % (os.path.basename(path), n))
        elif state == "ALREADY":
            already += 1
    if already and not applied:
        print("ALREADY  (%d chunk(s))" % already)
        return 0
    if not applied:
        print("MISSING  padrao do indice de classe nao encontrado", file=sys.stderr)
        return 2
    # Meia cobertura mente por omissao -- foi exactamente o que aconteceu com a
    # sonda VICTIM, que cobria 1 dos 7 e me fez generalizar (E59).
    if sites != EXPECTED:
        print("AVISO: %d sitios, esperados %d -- o shape do lift mudou"
              % (sites, EXPECTED), file=sys.stderr)
        return 2
    print("patch_clsidx_probe_gate: applied=%d sites=%d" % (applied, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
