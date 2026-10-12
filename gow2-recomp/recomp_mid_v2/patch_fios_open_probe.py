#!/usr/bin/env python3
"""Probe gated do OPEN FIOS do filme de intro (Task 2 do plano
ps3recomp/docs/superpowers/plans/2026-07-20-macos-intro-audio-open-wall.md).

PERGUNTA A MEDIR
----------------
A FSM do player de intro fica parada em `st620 == 1` e os dois handles de io
em `obj+0x630` (open) e `obj+0x634` (read) ficam a `0x00000000` o boot inteiro
(`[MOVIEOBJ]`). `Play` (`func_002C00DC`) pede o open de video atraves de
`func_002B4340`; esta probe mostra ONDE esse open morre, sem lhe tocar.

CADEIA ESTATICA (verificada no lift + desassemblagem do EBOOT, nao adivinhada)
-----------------------------------------------------------------------------
    func_002C00DC (Play)
      obj = [TOC-0x1124] = [0x00540054]           (o objecto do player)
      obj+0x740 = 0x20   se arg4(r6) == 0
      obj+0x740 = 0xA0   se arg4(r6) != 0         <- os "flags" do open
      ...
      r3 = obj+0x62C  (container da op)
      r4 = nome cru do filme ("SmLogo_v2")        <- NAO e' um caminho
      r5 = [obj+0x740]                            <- flags
      func_002B4340(r3, r4, r5)

    func_002B4340  (guest 0x2B4340, despacho por flags -- 3 ramos exclusivos)
      [container+0xC] = flags ; [container+0x14] = 0
      flags & 0x010 -> func_002B446C : sprintf("/wad/%s%s", nome, ".wad_ps3")
      flags & 0x020 -> func_002B43C0 : sprintf("/_movies/%s.m2v", nome)
      flags & 0x200 -> func_002B441C : so' busy-poll, sem open
      senao         -> return 1

    ramo do filme (func_002B43C0, cauda partilhada em func_002B43DC)
      func_00373D00  sprintf  -> buffer na stack (sp+0x70)
      func_002B3890  tolower  -> o caminho vai TODO em minusculas
      func_0030D578(r3=[[TOC-0x1460]+0x118], r4=0, r5=caminho, r6=1,
                    r7=container+4 == obj+0x630)
      [container+8] = r3      (== obj+0x634)
      flags & 0x200 == 0 -> return 1 (o intro NAO faz o busy-poll aqui)

    func_0030D578 -> func_0030D5CC   (submissao de op assincrona, estilo FIOS)
      if (r7) [r7] = 0                       <- obj+0x630 zerado a entrada
      op   = func_00307C8C(mediaobj, 0)      <- alocacao da op
      if (!op) return 0;                     <- (A) devolve 0, ambos os slots 0
      if (!caminho || caminho[0]==0) -> erro 0x80010705
      file = func_0030D29C(mediaobj, 1, caminho)
      [r7] = file                            <- obj+0x630 = objecto de ficheiro
      if (!file) -> erro 0x8001070A          <- (B) 0x630 fica 0, 0x634 = op
      op[0x40] = 9 (opcode open) ; op[0xB8] = r7 ; ... -> func_0030B058 (submete)

RESUMO DO QUE ISTO DECIDE (as classes F0..F4 do plano)
-----------------------------------------------------
  F0  nenhum `[FIOSOPEN] 002B4340`      -> Play nao chega ao open de video
  F1  ha 002B4340 mas nenhum 0030D578   -> ramo de flags errado / falha antes
  F2  ha 0030D578 com path e ret=0      -> o open FIOS falha para o membro
        F2a  op_alloc=0   -> nao ha op livre (falha ANTES de olhar ao caminho)
        F2b  file_new=0   -> a abertura do membro falha (caminho recusado)
  F3  [container+8] fica != 0 e depois volta a 0 -> open OK e teardown
  F4  o caminho pedido nao e' o do filme (ex.: so' "/gow2.psarc")

O caminho EXACTO pedido sai impresso na linha `0030D578<` -- e' esse o produto
principal desta medicao. O formato vem de [TOC-0x1418] = 0x004CA068 =
"/_movies/%s.m2v" (NAO ".vpk": o `.vpk` do EBOOT (0x4CBBC0) nao e' usado por
este caminho), e `func_002B3890` passa-o a minusculas antes do open -- ou seja
o pedido esperado e' `/_movies/smlogo_v2.m2v`, que EXISTE no psarc.

RESULTADO DA MEDICAO (2026-07-20, ./smoke_fios_open_probe.sh 8 25 AB)
--------------------------------------------------------------------
Braco A = PS3_TRACE_FIOSOPEN=1, braco B = por definir. Ambos M=8 x 25 s,
PS3_NO_RSX=1, PS3_TRACE_MOVIEOBJ=1, PS3_MOVIE_EOS=0, sem FORCE.

  Play 002C00DC entrado ......................... 8/8  (2 chamadas por corrida:
      a 2a com st620=1 passa por func_002C0498 -> Stop -> reentra o corpo)
  func_002B4340 entrado (F0 REFUTADO) ........... 8/8
      container=0x00869E04 = obj+0x62C  (obj=0x008697D8)
      flags=0x00000020 -> ramo func_002B43C0     (F1 REFUTADO) 8/8
  caminho pedido = '/_movies/smlogo_v2.m2v' ..... 8/8 (16/16 opens)
      nunca .vpk, nunca .wav, nunca /gow2.psarc  (F4 REFUTADO)
  func_0030D29C (abertura do membro) != 0 ....... 15/15 opens que la chegaram
      -> o membro NUNCA e' recusado              (F2b REFUTADO)
  func_0030D578 devolveu handle != 0 ............ 15/16 opens (7/8 corridas)
      1/16 devolveu 0 e a causa foi op_alloc=0 (pool de ops esgotado, F2a)
  [MOVIEOBJ] com open != 0 em >=1 amostra ....... 8/8 no braco A E 8/8 no B
  linhas [FIOSOPEN] com a env por definir ....... 0/8 (OFF por default provado)

CLASSIFICACAO: **F3** (5/8 no braco A, 3/8 no B). O open NAO falha: os dois
handles passam a != 0 ~3 s depois do arranque em 16/16 corridas -- obj+0x630 =
objecto de ficheiro (0x43018528), obj+0x634 = op (0x430094C0 no 1o open). Em
5/8 corridas voltam a 0 pouco depois do 2o Play (Stop -> func_002B3F78 fecha o
container); nas outras 3/8 ficam nao-nulos ate' ao fim.

ISTO REDIRECCIONA O PLANO. A premissa "obj+0x630 e obj+0x634 ficam 0x00000000 o
boot inteiro" e' FALSA -- a medicao antiga apanhou so' a janela ANTES de Play
(o 1o valor nao-nulo chega sempre por volta da amostra 14-18 de ~121). Nao ha
parede de open: a parede esta A JUSANTE, no poll do estado 1. `func_002B4224`
le `[op+0x90]` e ve 0 em TODAS as amostras das 8 corridas, apesar de o sampler
observar `[op+0x90]` != 0 tarde na corrida em 6/8 -- ou seja, a op de open E'
submetida e chega a completar, mas nunca no instante em que o estado 1 a
consulta. O produtor de conclusao da op (func_0030B058 -> fila) e' o proximo
alvo, nao o open.

NOTA DE LIFT (achado colateral, NAO corrigido aqui -- isto e' so' medicao)
-------------------------------------------------------------------------
O fallthrough do `beq 0x2B439C` em guest 0x2B4418 e' 0x2B441C, mas o fragmento
`func_002B43C0` do lift emite `g_trampoline_fn = func_002B43DC` -- salto para
TRAS que voltaria a fazer tolower+open (open duplicado). E' o mesmo padrao do
bug 2550C8 (fallthrough cross-fragment com EA menor que o site). NAO e'
exercitado pela intro: com flags 0x20/0xA0 o teste `flags & 0x200` da' zero e o
ramo tomado e' `func_002B439C`. Fica registado para a auditoria sistematica.

O QUE A PROBE FAZ
-----------------
So' LE memoria guest (vm_read8/32 tem guarda de out-of-bounds) e imprime. Nunca
escreve em `obj+0x744`, `obj+0x620`, nos handles, nem em ctx. Com
PS3_TRACE_FIOSOPEN por definir e' um no-op exacto sobre o baseline (regra 6 do
CLAUDE.md: probes gated por env, OFF por default).

Uso:  patch_fios_open_probe.py [DIR_DE_LIFT]    (default: o dir deste ficheiro)
      PS3_TRACE_FIOSOPEN=1 ./boot_gow2 EBOOT.ELF 2>&1 | grep FIOSOPEN

Reaplicado por ../apply_all_patches.sh apos cada re-lift; idempotente (a 2a
corrida nao reescreve nada -> ALREADY-APPLIED).

Ficheiros do lift tocados: ppu_recomp_001.cpp e ppu_recomp_002.cpp.

Marcadores em uso por outros patches (nao colidir): [AREAD] (patch_2b3d1c_probe),
[OOBARG] (patch_oob_ra_probe), [INTROSEQ] (patch_introseq_probe), [KICK],
[SPUBRP], [SPUHALT], [MOVIEFSM], [MOVIEOBJ], [SNDOPEN] (patch_snd_open_probe,
em func_0045E230). Esta usa [FIOSOPEN] e nenhuma dessas funcoes.

Nota de implementacao: a substituicao e' `str.replace()` textual exacta sobre a
regiao da funcao, NAO `re.sub` com string de substituicao -- essa interpreta
escapes e ja' meteu uma quebra de linha REAL dentro de um literal C, gerando
fonte que nao compila.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

MARKER = "FIOS-OPEN-PROBE"
ENV = "PS3_TRACE_FIOSOPEN"

# CORRECCAO 2026-07-25 (shape-LR)
# --------------------------------
# O lifter passou a emitir as chamadas com o link register explicito a frente:
#     ctx->lr = 0x002B4404; func_0030D578(ctx); DRAIN_TRAMPOLINE(ctx);
# (antes: so' "func_0030D578(ctx); DRAIN_TRAMPOLINE(ctx);"). Medido: a needle
# literal passou a aparecer 0x na regiao de func_002B43C0 e o script abortava.
#
# Duas mudancas, ambas conservadoras:
#  1. as needles passam por flex(): o prefixo `ctx->lr = 0x........; ` e'
#     OPCIONAL, por isso a agulha casa com o lift antigo E com o novo;
#  2. `edit()` deixou de RE-EMITIR a needle -- agora so' insere `pre` antes e
#     `post` depois do texto REALMENTE encontrado. Re-emitir o literal antigo
#     apagaria o `ctx->lr = ...` que o lifter agora emite, o que seria mudar
#     comportamento do jogo dentro de um patch que so' devia medir.
LR_OPT = r"(?:ctx->lr = 0x[0-9A-Fa-f]{8}; )?"
_CALL_RE = re.compile(r"func_[0-9A-Fa-f]{8}\(ctx\); DRAIN_TRAMPOLINE\(ctx\);")


def flex(lit: str) -> str:
    """Regex do bloco literal, com o prefixo de LR opcional nas chamadas."""
    out = []
    for line in lit.split("\n"):
        body = line.lstrip(" ")
        indent = line[: len(line) - len(body)]
        if _CALL_RE.fullmatch(body):
            out.append(re.escape(indent) + LR_OPT + re.escape(body))
        else:
            out.append(re.escape(line))
    return "\n".join(out)

# Prologo do gate por env. Repetido em cada bloco porque cada probe vive num
# escopo proprio (os `static` sao locais ao bloco instrumentado).
GATE = (
    "        { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n"
    '            const char* _e=getenv("' + ENV + "\"); _on=(_e&&*_e&&*_e!='0')?1:0;}\n"
)

# Leitor de string guest para um buffer local. Guarda de EA como no introseq.
def rdstr(var: str, ea_expr: str) -> str:
    return (
        "            char " + var + "[160]; " + var + "[0]=0;\n"
        "            { uint32_t _a=(uint32_t)(" + ea_expr + ");\n"
        "              if(_a && _a < 0x4F000000u){ int _i;\n"
        "                for(_i=0;_i<159;_i++){ unsigned char _c=(unsigned char)vm_read8(_a+_i);\n"
        "                  " + var + "[_i]=(char)_c; if(!_c) break; }\n"
        "                " + var + "[159]=0; } }\n"
    )


# --- Play: chegou-se sequer ao sitio que pede o open? ----------------------
PLAY_PROBE = (
    "        /* " + MARKER + "(play): entrada de Play. Se esta linha nao aparecer, o\n"
    "         * open de video nem e' pedido (classe F0 do plano). st620!=0 na\n"
    "         * entrada => Play sai cedo por func_002C0498 e nao abre nada. */\n"
    + GATE
    + "          if(_on){ static int _n=0; if(_n++<8){\n"
    "            uint32_t _obj=vm_read32((uint32_t)ctx->gpr[2] - 0x1124);\n"
    + rdstr("_nm", "ctx->gpr[3]")
    + '            fprintf(stderr,"[FIOSOPEN] Play 002C00DC #%d obj=0x%08X st620=%u '
    'nome=\'%s\' r4=0x%08X r5=0x%08X r6=0x%08X%s\\n",\n'
    "              _n,_obj,_obj?vm_read32(_obj+0x620):0u,_nm,\n"
    "              (uint32_t)ctx->gpr[4],(uint32_t)ctx->gpr[5],(uint32_t)ctx->gpr[6],\n"
    '              (_obj && vm_read32(_obj+0x620))?"  <-- SAI CEDO (st620!=0)":"");\n'
    "            fflush(stderr); } } }\n"
)

# --- func_002B4340: entrada + flags + ramo escolhido -----------------------
B4340_PROBE = (
    "        /* " + MARKER + "(4340): entrada do open de media. r3=container,\n"
    "         * r4=nome cru, r5=flags. Ramo: 0x10=/wad/%s%s, 0x20=/_movies/%s.m2v,\n"
    "         * 0x200=so busy-poll. container esperado = obj+0x62C. */\n"
    + GATE
    + "          if(_on){ static int _n=0; if(_n++<8){\n"
    "            uint32_t _c=(uint32_t)ctx->gpr[3], _f=(uint32_t)ctx->gpr[5];\n"
    "            uint32_t _obj=vm_read32((uint32_t)ctx->gpr[2] - 0x1124);\n"
    + rdstr("_nm", "ctx->gpr[4]")
    + "            const char* _br = (_f & 0x10u) ? \"002B446C(/wad/%s%s)\"\n"
    "                            : (_f & 0x20u) ? \"002B43C0(/_movies/%s.m2v)\"\n"
    "                            : (_f & 0x200u) ? \"002B441C(so poll)\"\n"
    '                            : "nenhum (return 1)";\n'
    '            fprintf(stderr,"[FIOSOPEN] 002B4340 #%d container=0x%08X obj=0x%08X '
    'delta=0x%X nome=\'%s\' flags=0x%08X ramo=%s\\n",\n'
    "              _n,_c,_obj,_obj?(_c-_obj):0u,_nm,_f,_br);\n"
    "            fflush(stderr); } } }\n"
)

# --- envolvente da chamada a func_0030D578 ---------------------------------
# `site` distingue as 3 origens; `creg` e' o registo NAO-volatil que guarda o
# container nessa origem (sobrevive a chamada, ao contrario de r7).
CALL_NEEDLE = "        func_0030D578(ctx); DRAIN_TRAMPOLINE(ctx);\n"


def call_pre(site: str) -> str:
    return (
        "        /* " + MARKER + "(pre-" + site + "): pedido de open FIOS. r5 e' o\n"
        "         * caminho ja' passado a minusculas por func_002B3890. */\n"
        + GATE
        + "          if(_on){ static int _n=0; if(_n++<8){\n"
        + rdstr("_pt", "ctx->gpr[5]")
        + '            fprintf(stderr,"[FIOSOPEN] 0030D578< site=' + site + ' #%d '
        'mediaobj=0x%08X r4=0x%08X path=\'%s\' r6=%u out=0x%08X\\n",\n'
        "              _n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4],_pt,\n"
        "              (uint32_t)ctx->gpr[6],(uint32_t)ctx->gpr[7]);\n"
        "            fflush(stderr); } } }\n"
    )


def call_post(site: str, creg: int) -> str:
    return (
        "        /* " + MARKER + "(pos-" + site + "): retorno do open. ret==0 => F2 do\n"
        "         * plano. [container+4]=obj+0x630 (objecto de ficheiro),\n"
        "         * [container+8]=obj+0x634 = ret (escrito logo a seguir). */\n"
        + GATE
        + "          if(_on){ static int _n=0; if(_n++<8){\n"
        "            uint32_t _c=(uint32_t)ctx->gpr[" + str(creg) + "];\n"
        '            fprintf(stderr,"[FIOSOPEN] 0030D578> site=' + site + ' #%d '
        'ret=0x%08X container=0x%08X [+4]=0x%08X [+8]_antes=0x%08X%s\\n",\n'
        "              _n,(uint32_t)ctx->gpr[3],_c,vm_read32(_c+4),vm_read32(_c+8),\n"
        '              ((uint32_t)ctx->gpr[3]==0u)?"  <-- OPEN DEVOLVEU 0 (F2)":"");\n'
        "            fflush(stderr); } } }\n"
    )


# --- func_0030D5CC: separar F2a (sem op) de F2b (ficheiro recusado) --------
ALLOC_NEEDLE = "        func_00307C8C(ctx); DRAIN_TRAMPOLINE(ctx);\n"
ALLOC_PROBE = (
    "        /* " + MARKER + "(op_alloc): alocacao da op assincrona. r3==0 faz\n"
    "         * func_0030D578 devolver 0 SEM sequer olhar ao caminho (F2a). */\n"
    + GATE
    + "          if(_on){ static int _n=0; if(_n++<8){\n"
    '            fprintf(stderr,"[FIOSOPEN] 0030D5CC op_alloc #%d r3=0x%08X%s\\n",\n'
    "              _n,(uint32_t)ctx->gpr[3],\n"
    '              ((uint32_t)ctx->gpr[3]==0u)?"  <-- SEM OP LIVRE (F2a)":"");\n'
    "            fflush(stderr); } } }\n"
)

FILE_NEEDLE = "        func_0030D29C(ctx); DRAIN_TRAMPOLINE(ctx);\n"
FILE_PROBE = (
    "        /* " + MARKER + "(file_new): abertura do membro. r3==0 => 0x8001070A e\n"
    "         * obj+0x630 fica 0 (F2b). r5/r25 e' o caminho pedido. */\n"
    + GATE
    + "          if(_on){ static int _n=0; if(_n++<8){\n"
    + rdstr("_pt", "ctx->gpr[25]")
    + '            fprintf(stderr,"[FIOSOPEN] 0030D5CC file_new #%d r3=0x%08X '
    'path=\'%s\'%s\\n",\n'
    "              _n,(uint32_t)ctx->gpr[3],_pt,\n"
    '              ((uint32_t)ctx->gpr[3]==0u)?"  <-- MEMBRO RECUSADO (F2b)":"");\n'
    "            fflush(stderr); } } }\n"
)

# --- func_002B4224: o poll do estado 1 tem alguma op para esperar? ---------
B4224_PROBE = (
    "        /* " + MARKER + "(4224): poll do estado 1. io=[container+8]; se 0 a\n"
    "         * funcao sai por func_002B4308 e nao ha nada para esperar.\n"
    "         * done=[io+0x90] e' a palavra de conclusao da op FIOS. */\n"
    + GATE
    + "          if(_on){ static int _n=0, _p=0, _d=0;\n"
    "            uint32_t _c=(uint32_t)ctx->gpr[3], _io=vm_read32(_c+8);\n"
    "            uint32_t _dn=_io?vm_read32(_io+0x90):0u;\n"
    "            /* A observacao que interessa e' a UNICA iteracao em que o poll\n"
    "             * ve [op+0x90]!=0 -- a amostragem periodica quase nunca calha\n"
    "             * nela, porque logo a seguir func_002B4274 zera [container+8]. */\n"
    "            if(_dn && _d++<4){\n"
    '              fprintf(stderr,"[FIOSOPEN] 002B4224 DONE #%d container=0x%08X '
    'io=0x%08X done=0x%08X apos %d polls\\n",\n'
    "                _d,_c,_io,_dn,_n);\n"
    "              fflush(stderr); }\n"
    "            else if((_n<4 || (_n%2048)==0) && _p++<64){\n"
    '              fprintf(stderr,"[FIOSOPEN] 002B4224 poll #%d container=0x%08X '
    'io=0x%08X done=0x%08X%s\\n",\n'
    "                _n,_c,_io,_dn,\n"
    '                _io?"":"  <-- SEM OP PARA POLLAR");\n'
    "              fflush(stderr); }\n"
    "            _n++; } }\n"
)

# --- func_002B4274: SO' e' alcancavel com [op+0x90] != 0 -------------------
# A probe de entrada de func_002B4224 NAO consegue observar o "done": le
# [op+0x90] no topo (ve 0), e o preempt do giant lock (ppu_rsv_on_store, no
# primeiro vm_write do prologo) deixa a "fios scheduler" escrever a palavra
# ENTRE a leitura da probe e a leitura do guest. func_002B4274 e' o ramo que
# o guest so' toma depois de ver != 0 -- e' a observacao sem corrida.
B4274_PROBE = (
    "        /* " + MARKER + "(4274): ramo 'done' do poll. Alcancavel apenas com\n"
    "         * [ [container+8] + 0x90 ] != 0 -- logo esta linha E' a prova de que\n"
    "         * func_002B4224 observou a palavra de conclusao. */\n"
    + GATE
    + "          if(_on){ static int _n=0; if(_n++<8){\n"
    "            uint32_t _c=(uint32_t)ctx->gpr[31], _io=vm_read32(_c+8);\n"
    '            fprintf(stderr,"[FIOSOPEN] 002B4274 DONE #%d container=0x%08X '
    'io=0x%08X done=0x%08X\\n",\n'
    "              _n,_c,_io,_io?vm_read32(_io+0x90):0u);\n"
    "            fflush(stderr); } } }\n"
)

# --- beacon do outro open que partilha func_0030D578 -----------------------
# Sem isto, as linhas de func_0030D5CC nao se conseguem atribuir a uma origem.
B4490_PROBE = (
    "        /* " + MARKER + "(4490): beacon do 2o helper (partilha 0030D578).\n"
    "         * Serve para atribuir as linhas de 0030D5CC a origem certa. */\n"
    + GATE
    + "          if(_on){ static int _n=0; if(_n++<8){\n"
    + rdstr("_nm", "ctx->gpr[4]")
    + '            fprintf(stderr,"[FIOSOPEN] 002B4490 #%d container=0x%08X '
    'nome=\'%s\' flags=0x%08X\\n",\n'
    "              _n,(uint32_t)ctx->gpr[3],_nm,(uint32_t)ctx->gpr[5]);\n"
    "            fflush(stderr); } } }\n"
)


def sig(name: str) -> str:
    return "void " + name + "(ppu_context* ctx) {\n"


def edit(root: Path, func: str, tag: str, needle: str,
         pre: str, post: str, what: str) -> str:
    """Insere `pre` antes e `post` depois de `needle`, DENTRO da regiao de `func`.

    A regiao vai da assinatura ate' a definicao seguinte -- o marcador so' e'
    procurado ai, porque a mesma needle aparece noutras funcoes.

    O guarda de idempotencia e' `MARKER(tag)`, nao `MARKER` a seco: ha funcoes
    (func_0030D5CC, func_002B43C0) com DUAS insercoes distintas, e um guarda
    por-funcao faria a segunda ser saltada para sempre.

    A needle e' casada por flex() (prefixo de LR opcional) e o texto encontrado
    e' PRESERVADO tal e qual -- ver a nota de correccao shape-LR no topo.
    """
    signature = sig(func)
    stamp = MARKER + "(" + tag + ")"
    pat = re.compile(flex(needle))
    for path in sorted(root.glob("ppu_recomp_*.cpp")):
        src = path.read_text(encoding="utf-8", errors="replace")
        i = src.find(signature)
        if i < 0:
            continue
        j = src.find("\nvoid func_", i + len(signature))
        region = src[i:j] if j > i else src[i:]
        if stamp in region:
            return f"{path.name}: {func} ({what}) ja instrumentada"
        hits = list(pat.finditer(region))
        if len(hits) != 1:
            raise SystemExit(
                f"{func}: needle de {what} aparece {len(hits)}x na "
                "regiao (esperado 1) -- o shape do lift mudou, reveja a probe"
            )
        m = hits[0]
        region = region[:m.start()] + pre + m.group(0) + post + region[m.end():]
        path.write_text(src[:i] + region + (src[j:] if j > i else ""),
                        encoding="utf-8", newline="\n")
        return f"{path.name}: {func} ({what}) instrumentada"
    raise SystemExit(
        f"{func} ausente no lift em {root} -- a cadeia do open FIOS mudou de "
        "forma; reveja a probe antes de forcar"
    )


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    if not sorted(root.glob("ppu_recomp_*.cpp")):
        print(f"FAIL: nenhum ppu_recomp_*.cpp em {root}", file=sys.stderr)
        return 1

    # (funcao, tag de idempotencia, needle, texto ANTES, texto DEPOIS, descricao)
    jobs = [
        ("func_002C00DC", "play", sig("func_002C00DC"),
         "", PLAY_PROBE, "entrada de Play"),
        ("func_002B4340", "4340", sig("func_002B4340"),
         "", B4340_PROBE, "entrada do open de media"),
        ("func_002B43C0", "pre-43C0", CALL_NEEDLE,
         call_pre("43C0"), call_post("43C0", 28),
         "chamada a 0030D578 (ramo do filme)"),
        ("func_002B43DC", "pre-43DC", CALL_NEEDLE,
         call_pre("43DC"), call_post("43DC", 28),
         "chamada a 0030D578 (cauda partilhada)"),
        ("func_002B4490", "4490", sig("func_002B4490"),
         "", B4490_PROBE, "beacon do 2o helper"),
        ("func_0030D5CC", "op_alloc", ALLOC_NEEDLE, "", ALLOC_PROBE,
         "alocacao da op"),
        ("func_0030D5CC", "file_new", FILE_NEEDLE, "", FILE_PROBE,
         "abertura do membro"),
        ("func_002B4224", "4224", sig("func_002B4224"),
         "", B4224_PROBE, "poll do estado 1"),
        ("func_002B4274", "4274", sig("func_002B4274"),
         "", B4274_PROBE, "ramo 'done' do poll"),
    ]
    for func, tag, needle, pre, post, what in jobs:
        print(edit(root, func, tag, needle, pre, post, what))
    print(f"probe {MARKER} pronta -- corra com {ENV}=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
