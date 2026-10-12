#!/usr/bin/env python3
"""Instala o bloco F2B de "open success" (RESTATUS + STREAM-PUMP + KEEP-DONE).

Porque existe
-------------
`patch_fios_f2b_open_success.py` e' um VERIFICADOR PURO (0 escritas): confirma
os marcadores F2B-KEEP-DONE / F2B-RESTATUS / F2B-STREAM-PUMP / F2B-FO-SIZE /
F2B-STREAM-FILL e falha se faltarem. Nenhum script versionado os instalava --
eram edicao MANUAL dentro do lift gitignored. Medido a 2026-07-25 sobre um lift
limpo do ppu_lifter.py actual com TODOS os patches aplicados:

    grep -c F2B-KEEP-DONE  <lift limpo+patches>/ppu_recomp_*.cpp   -> 0
    grep -c F2B-KEEP-DONE  recomp_macos_v2/ppu_recomp_001.cpp      -> 2

Ou seja: codigo sem dono. Este ficheiro passa a ser o dono. O verificador
continua a verificar (nao foi tocado nem enfraquecido); a partir daqui passa
legitimamente porque o comportamento existe de facto no lift.

O que instala (tudo copiado VERBATIM do lift de producao)
---------------------------------------------------------
1. Preambulo host no topo do chunk (so' se ausente):
     - externs movie_io_open / movie_io_pread / movie_io_is
     - mapa host FO->(mfd,size): g_f2b_fo_mfd_* + f2b_fo_mfd_put/get/sz_get
     - g_f2b_natural_movie_fo + globais g_f2b_fill_* + extern vm_base
     - helper `f2b_stream_fill(stream, min_need)` (refill compacto do ring
       WADLD em type_sys+0x1A8)
2. func_002B4274 (ramo 'done' do poll), imediatamente ANTES da linha gerada
   `ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x8);`:
     - F2B-RESTATUS: limpa op+44 (0x8001070A carimbado pelo re-queue do
       dearchiver em 0030B058) e re-publica +90 antes do func_00306610
     - F2B-STREAM-PUMP: movie_io_pread do ficheiro host para o ring do guest
       apos o open DONE (com a NOTE "must run even when PS3_TRACE_FIOSOPEN is
       off" -- o pump NAO pode viver dentro da probe)
     - cauda F2B-STREAM-FILL: arma o estado de fill e carrega a 1a janela
3. func_0030D5CC (F2B open), imediatamente A SEGUIR a' linha gerada
   `vm_write32(ctx->gpr[27] + 0x98, ctx->gpr[31]);`:
     - F2B-KEEP-DONE: re-afirma o sucesso host depois do setup dos campos,
       antes do trampolim para func_0030B058

O que NAO instala (territorio declarado de outros patches)
----------------------------------------------------------
- F2B-FO-SIZE  -> vive DENTRO do bloco F2B-MOVIEIO/F2B-FO-CTOR de
  `patch_fios_f2b_fo_ctor.py` (usa as locais `fea`/`sz` desse ctor); nao pode
  ser instalado sem raptar o item desse patch. Fica em falta ate' esse bloco
  ter escritor -- o verificador passa a 5/6 (limiar dele e' >= 3).
- F2B-STREAM-SEED -> marcador de `patch_2b3d1c_movie_io.py` (opt-in, OFF).
- FIOS-HOST-POP / FIOS-FREELIST-REBUILD -> `patch_fios_f2a_f2b_wad.py`.
- FO-DUMP / FREELIST-PROBE -> patches de probe com escritor proprio.
- f2b_stream_ensure / _eof_try_complete / _pre_consume e o comentario
  "F2B multi-MB fix" -> `patch_f2b_multimb_stream.py`. Por isso o
  `f2b_stream_fill` aqui vai SEM esse comentario (codigo identico ao de
  producao; so' o comentario -- que e' marcador desse outro patch -- e' que
  nao entra). A insercao do preambulo e' guardada por nome: se esse patch
  (que corre antes, por ordem alfabetica) ja' tiver posto `f2b_stream_fill`,
  este salta-o e nao ha' definicao duplicada.

Efeito em runtime, dito sem floreados
-------------------------------------
RESTATUS e KEEP-DONE so' actuam sobre FOs que estejam no mapa host
(`f2b_fo_mfd_get(fo) != 0`), e quem povoa esse mapa e' o bloco F2B-MOVIEIO
(f2b_fo_mfd_put) do `patch_fios_f2b_fo_ctor.py`. Enquanto esse bloco nao
tiver escritor, estes blocos compilam e correm mas sao no-op no boot. Isso
esta' aqui escrito de proposito: instalar o bloco NAO e' o mesmo que provar
comportamento in-boot (regra 4 do CLAUDE.md).

Como foi gerado
---------------
Nao foi transcrito a mao. Um gerador leu o lift de producao
`recomp_macos_v2/ppu_recomp_001.cpp` e copiou intervalos de linhas verbatim:

    preambulo externs   linhas 29-31
    mapa FO + globais   linhas 33-68
    f2b_stream_fill     linhas 74-171
    F2B-RESTATUS        linhas 24346-24364   (func_002B4274)
    F2B-STREAM-PUMP     linhas 24382-24464   (func_002B4274, inclui STREAM-FILL)
    F2B-KEEP-DONE       linhas 116505-116520 (func_0030D5CC)

As ancoras sao linhas GERADAS pelo lifter (nao linhas de outro patch), por isso
este script nao depende da ordem em que os outros patch_*.py correm. Em
particular, `patch_fios_sticky.py` insere o STICKY-CONSUME antes da MESMA linha
`ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x8);` e corre depois deste: o resultado
fica com a ordem de producao (RESTATUS/PUMP, depois STICKY-CONSUME).

Nao se substitui o corpo inteiro das funcoes pelo de producao (o que daria
igualdade byte-a-byte com o lift antigo) porque o lifter mudou desde entao: o
corpo de producao NAO tem `ctx->lr = 0x...` antes das chamadas, nao tem o
hoisting de callee-save (`_cs_NN`) e tem `ctx->cr = (uint32_t)ctx->gpr[12]` em
vez da mascara mtcrf. Copiar esse corpo reverteria correccoes do lifter e
partiria as ancoras de outros patches (que casam `ctx->lr = 0x...; func_X(ctx)`)
na mesma funcao. Inserido esta' o TEXTO DOS BLOCOS byte a byte igual ao de
producao; o codigo gerado a' volta fica o do lifter actual.

Contrato de rc
--------------
- 0  aplicado, ou ja' aplicado (ALREADY) -- idempotente, 2a corrida nao escreve
- 2  a funcao alvo existe mas a ancora gerada NAO -- RECUSA (o lifter mudou de
     forma; revalidar o bloco a mao antes de reaplicar), ou nenhuma das duas
     funcoes alvo existe no lift dado
- 3  nenhum ficheiro de lift legivel

Uso:  patch_fios_f2b_open_block_install.py [LIFT_DIR_OU_FICHEIRO...]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths            # noqa: E402

DEFAULT_LIFT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

INCLUDE_ANCHOR = '#include "ppu_recomp.h"\n'

# --- preambulo host (topo do chunk) -----------------------------------------
PRE_HEADERS = (
    "/* F2B open-success host helpers -- instalados por\n"
    " * recomp_mid_v2/patch_fios_f2b_open_block_install.py (copia verbatim do\n"
    " * lift de producao). Includes repetidos de proposito: este bloco fica\n"
    " * antes dos includes que outros patches poem mais abaixo. */\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
)
PRE_STICKY = 'extern "C" void ps3_fios_sticky_publish(uint32_t op);\n'
PRE_EXTERNS = 'extern "C" unsigned movie_io_open(const char* path, unsigned* out_size);\nextern "C" unsigned movie_io_pread(unsigned fd, void* dst, unsigned n, unsigned pos);\nextern "C" int movie_io_is(unsigned fd);\n'
PRE_MAP = '/* F2B FO side-channel: real FO has +0x38=0; stashing mfd there may be read\n * as a guest pointer. Keep mfd in a host map keyed by FO EA. */\nstatic uint32_t g_f2b_fo_mfd_fo[8];\nstatic unsigned g_f2b_fo_mfd_fd[8];\nstatic uint32_t g_f2b_fo_mfd_sz[8];\nstatic int g_f2b_fo_mfd_n = 0;\n/* Natural FO EA from first successful movie file_new; re-used by F2B re-open. */\nstatic uint32_t g_f2b_natural_movie_fo = 0;\nstatic void f2b_fo_mfd_put(uint32_t fo, unsigned mfd, uint32_t sz) {\n    for (int i = 0; i < g_f2b_fo_mfd_n; i++)\n        if (g_f2b_fo_mfd_fo[i] == fo) { g_f2b_fo_mfd_fd[i] = mfd; g_f2b_fo_mfd_sz[i] = sz; return; }\n    if (g_f2b_fo_mfd_n < 8) {\n        g_f2b_fo_mfd_fo[g_f2b_fo_mfd_n] = fo;\n        g_f2b_fo_mfd_fd[g_f2b_fo_mfd_n] = mfd;\n        g_f2b_fo_mfd_sz[g_f2b_fo_mfd_n] = sz;\n        g_f2b_fo_mfd_n++;\n    }\n}\nstatic unsigned f2b_fo_mfd_get(uint32_t fo) {\n    for (int i = 0; i < g_f2b_fo_mfd_n; i++)\n        if (g_f2b_fo_mfd_fo[i] == fo) return g_f2b_fo_mfd_fd[i];\n    return 0;\n}\nstatic uint32_t f2b_fo_sz_get(uint32_t fo) {\n    for (int i = 0; i < g_f2b_fo_mfd_n; i++)\n        if (g_f2b_fo_mfd_fo[i] == fo) return g_f2b_fo_mfd_sz[i];\n    return 0;\n}\n/* WADLD stream ring (type_sys+0x1A8): base@+0 cursor@+8 cap@+C avail@+10.\n * Init leaves avail=0; natural path areads FO into the ring. F2B host fill. */\nstatic uint32_t g_f2b_fill_fo = 0;\nstatic unsigned g_f2b_fill_mfd = 0;\nstatic uint32_t g_f2b_fill_sz = 0;\nstatic uint32_t g_f2b_fill_file_pos = 0;\nstatic uint32_t g_f2b_fill_stream = 0;\nextern "C" uint8_t* vm_base;\n'
PRE_FILL = 'static int f2b_stream_fill(uint32_t stream, uint32_t min_need) {\n    if (!stream || !vm_base || !g_f2b_fill_mfd || !movie_io_is(g_f2b_fill_mfd))\n        return 0;\n    if (g_f2b_fill_stream && stream != g_f2b_fill_stream\n        && g_f2b_fill_stream != 0) {\n        /* New FO: allow rebind */\n    }\n    g_f2b_fill_stream = stream;\n    int rounds = 0;\n    for (;;) {\n        uint32_t avail = vm_read32(stream + 0x10u);\n        uint32_t cursor = vm_read32(stream + 0x8u);\n        uint32_t base = vm_read32(stream + 0x0u);\n        uint32_t cap = vm_read32(stream + 0xCu);\n        if (!base || !cap || base >= 0x4F000000u || cap > 0x01000000u) return 0;\n        /* Cap min_need to ring size — multi-pass guest consumes cap-sized chunks. */\n        uint32_t target = min_need;\n        if (target == 0) target = 1; /* force at least one byte attempt when empty */\n        if (target > cap) target = cap;\n        if (avail >= target) return 1;\n        if (g_f2b_fill_file_pos >= g_f2b_fill_sz) return avail > 0 ? 1 : 0;\n        /* Compact unread to base so space = cap - avail is contiguous.\n         * Guest ring may wrap (E1480 split-copy when cursor+need > cap). Old\n         * code zeroed avail on wrap → dropped unread bytes while file_pos had\n         * already advanced past them → stream desync after multi-MB bodies\n         * (first bad header after ~010decorchest; residual body1D4 garbage). */\n        if (avail > 0 && cursor > 0 && cursor < cap && avail <= cap) {\n            uint8_t* b = vm_base + base;\n            if (cursor + avail <= cap) {\n                memmove(b, b + cursor, avail);\n            } else {\n                uint32_t len1 = cap - cursor;\n                uint32_t len2 = avail - len1;\n                if (len2 <= cap && avail <= cap) {\n                    /* One cap-sized scratch is enough (ring holds ≤cap bytes). */\n                    uint8_t* tmp = (uint8_t*)malloc((size_t)avail);\n                    if (tmp) {\n                        memcpy(tmp, b + cursor, (size_t)len1);\n                        memcpy(tmp + len1, b, (size_t)len2);\n                        memcpy(b, tmp, (size_t)avail);\n                        free(tmp);\n                        { static int _n=0; if(_n++<32)\n                            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-WRAP-COMPACT "\n                              "len1=%u len2=%u avail=%u file_pos=%u\\n",\n                              len1, len2, avail, g_f2b_fill_file_pos); }\n                    } else {\n                        { static int _n=0; if(_n++<8)\n                            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-WRAP-COMPACT-OOM "\n                              "avail=%u → drop\\n", avail); }\n                        avail = 0;\n                    }\n                } else {\n                    { static int _n=0; if(_n++<8)\n                        fprintf(stderr,"[FIOSOPEN] F2B-STREAM-WRAP-COMPACT-BAD "\n                          "cursor=%u avail=%u cap=%u → drop\\n",\n                          cursor, avail, cap); }\n                    avail = 0;\n                }\n            }\n            cursor = 0;\n            vm_write32(stream + 0x8u, 0u);\n            vm_write32(stream + 0x10u, avail);\n            vm_write32(stream + 0x4u, avail); /* write_pos @ end of compact data */\n        } else if (avail == 0) {\n            cursor = 0;\n            vm_write32(stream + 0x8u, 0u);\n            vm_write32(stream + 0x4u, 0u);\n        }\n        uint32_t space = (cap > avail) ? (cap - avail) : 0;\n        if (!space) return avail > 0 ? 1 : 0;\n        uint32_t n = g_f2b_fill_sz - g_f2b_fill_file_pos;\n        if (n > space) n = space;\n        if (!n) return avail > 0 ? 1 : 0;\n        unsigned got = movie_io_pread(g_f2b_fill_mfd, vm_base + base + avail, n,\n                                      g_f2b_fill_file_pos);\n        if (got != n) {\n            { static int _n=0; if(_n++<12)\n                fprintf(stderr,"[FIOSOPEN] F2B-STREAM-FILL short stream=0x%08X pos=%u n=%u got=%u\\n",\n                  stream, g_f2b_fill_file_pos, n, got); }\n            if (!got) return avail > 0 ? 1 : 0;\n            n = got;\n        }\n        avail += n;\n        g_f2b_fill_file_pos += n;\n        vm_write32(stream + 0x10u, avail);\n        /* Keep write_pos (+4) consistent with cursor=0 compact form: data is\n         * [0, avail). E1254/E1290 use +4 as the producer cursor. */\n        vm_write32(stream + 0x4u, avail);\n        { static int _n=0; if(_n++<64){\n            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-FILL stream=0x%08X base=0x%08X avail=%u file_pos=%u/%u need=%u\\n",\n              stream, base, avail, g_f2b_fill_file_pos, g_f2b_fill_sz, min_need);\n            fflush(stderr); } }\n        rounds++;\n        if (rounds > 64) return avail > 0 ? 1 : 0;\n        if (avail >= target) return 1;\n        /* else loop: more file data into remaining ring space */\n    }\n}\n'

# sentinelas de idempotencia do preambulo
S_MAP = "static uint32_t g_f2b_fo_mfd_fo[8];"
S_FILL = "static int f2b_stream_fill("
S_EXTERNS = 'extern "C" unsigned movie_io_pread('
S_HEADERS = "/* F2B open-success host helpers"

# --- blocos dentro das funcoes ----------------------------------------------
FN_4274 = "func_002B4274"
FN_D5CC = "func_0030D5CC"

ANCHOR_4274 = "        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x8);\n"
ANCHOR_D5CC = "        vm_write32(ctx->gpr[27] + 0x98, ctx->gpr[31]);\n"

BLK_4274 = '        /* F2B-RESTATUS: after F2B open, 0030B058 may re-queue dearch and stamp\n         * op+44=0x8001070A. 6610 then returns that error and 4274 takes the\n         * 002B4330 failure path (ICALL-BAD "/wad", freelist desync). Re-assert\n         * host-complete success for F2B FOs before 6610. Always-on. */\n        { uint32_t _c=(uint32_t)ctx->gpr[31];\n          uint32_t _fo=_c?vm_read32(_c+4u):0u;\n          uint32_t _io=_c?vm_read32(_c+8u):0u;\n          if(_fo && f2b_fo_mfd_get(_fo) && _io>=0x10000u && _io<0x4F000000u){\n            uint32_t _was44=vm_read32(_io+0x44u);\n            vm_write32(_io+0x90u, 1u);\n            vm_write32(_io+0x44u, 0u);\n            vm_write32(_io+0xCCu, 0u);\n            ps3_fios_sticky_publish(_io);\n            { static int _n=0; if(_n++<8){\n              fprintf(stderr,"[FIOSOPEN] F2B-RESTATUS fo=0x%08X io=0x%08X was+44=0x%08X → 0\\n",\n                _fo, _io, _was44);\n              fflush(stderr); } }\n          }\n        }\n        /* F2B-STREAM-PUMP: host pread into guest ring after open DONE.\n         * Guest never reaches 002B3D1C for WAD (freelist desync first).\n         * CRITICAL: do NOT call ps3_fios_aread_hle(container, …) — that\n         * helper writes STATUS_DONE=0 at op+0x08, which on the OPEN\n         * container is the live io pointer (container+8). Measured in-boot:\n         * pump then left +8=0 → 6610 returned 0x8001070A → error path.\n         * Use movie_io_pread directly; only touch +0x10/+0x14 cursor/limit.\n         * Default ON; PS3_FIOS_STREAM_PUMP=0 disables.\n         * Skip movies/*.m2v: F2B open salvages re-Play FO only; pumping the\n         * whole m2v as a WAD ring corrupted TOC (FATAL 0x005F6D6F) after\n         * StartSeq#2 (wall_after_f2b2).\n         * NOTE: must run even when PS3_TRACE_FIOSOPEN is off. */\n        { static int _pump=-1; if(_pump<0){const char* e=getenv("PS3_FIOS_STREAM_PUMP");\n            _pump=(e&&*e&&*e!=\'0\')?1:0; /* E90: default OFF */}\n          uint32_t _c=(uint32_t)ctx->gpr[31];\n          if(_pump && _c){ uint32_t _fo=vm_read32(_c+4u);\n          unsigned _mfd=f2b_fo_mfd_get(_fo);\n          uint32_t _sz=f2b_fo_sz_get(_fo);\n          int _is_movie=0;\n          if(_fo>=0x10000u && _fo<0x4F000000u){\n            uint32_t _p30=vm_read32(_fo+0x30u);\n            if(_p30>=0x10000u && _p30<0x4F000000u){\n              char _pt[64]; _pt[0]=0;\n              for(int _i=0;_i<63;_i++){ unsigned char _ch=(unsigned char)vm_read8(_p30+_i);\n                _pt[_i]=(char)_ch; if(!_ch) break; }\n              _pt[63]=0;\n              if(strstr(_pt,"movies")||strstr(_pt,"Movies")||strstr(_pt,".m2v")||strstr(_pt,".M2V")\n                 ||strstr(_pt,".wav")||strstr(_pt,".WAV"))\n                _is_movie=1;\n            }\n          }\n          if(_mfd && movie_io_is(_mfd) && _sz && !_is_movie){\n            /* Rebind limit/cursor on container (stream window fields).\n             * Do not write +0x08 (open io ptr). */\n            vm_write32(_c+0x10u, _sz);\n            vm_write32(_c+0x14u, 0u);\n            if(vm_read32(_c+0x0Cu)==0u) vm_write32(_c+0x0Cu, 0x00010020u);\n            uint32_t ring=0x40080000u;\n            uint32_t ring_sz=0x00100000u; /* 1 MiB ring */\n            extern unsigned char* vm_base;\n            extern uint32_t ppu_vm_size;\n            if(vm_base && (uint64_t)ring+ring_sz <= (ppu_vm_size?ppu_vm_size:0x50000000u)){\n              uint32_t pos=0, chunk=0x20000u; /* 128 KiB */\n              unsigned total=0; int nchunk=0;\n              while(pos < _sz && nchunk < 400){\n                uint32_t n=_sz-pos; if(n>chunk) n=chunk;\n                uint32_t dst=ring + (pos % ring_sz);\n                if(dst+n > ring+ring_sz) n=ring+ring_sz-dst;\n                vm_write32(_c+0x14u, pos);\n                unsigned got = movie_io_pread(_mfd, vm_base + dst, n, pos);\n                if(got != n){\n                  fprintf(stderr,"[FIOSOPEN] F2B-STREAM-PUMP short fo=0x%08X pos=%u n=%u got=%u\\n",\n                    _fo, pos, n, got);\n                  break;\n                }\n                total+=got; pos+=got; nchunk++;\n                vm_write32(_c+0x14u, pos);\n              }\n              fprintf(stderr,"[FIOSOPEN] F2B-STREAM-PUMP fo=0x%08X mfd=0x%X pumped=%u/%u chunks=%d\\n",\n                _fo, _mfd, total, _sz, nchunk);\n              fflush(stderr);\n            }\n            /* F2B-STREAM-FILL: WADLD stream ring at type_sys+0x1A8 (container\n             * is type_sys+0x64). Init leaves avail=0; without fill state-2\n             * never copies 0x20 hdr bytes (002E1480). Arm host fill state and\n             * load the first window into the ring. */\n            g_f2b_fill_fo = _fo;\n            g_f2b_fill_mfd = _mfd;\n            g_f2b_fill_sz = _sz;\n            g_f2b_fill_file_pos = 0;\n            g_f2b_fill_stream = 0;\n            if(_c >= 0x64u){\n              uint32_t _ts = _c - 0x64u;\n              uint32_t _st = vm_read32(_ts + 0x1A8u);\n              if(_st >= 0x10000u && _st < 0x4F000000u){\n                /* also publish size into type_sys+0x74 (state1 rem source) */\n                if(vm_read32(_ts + 0x74u) == 0u)\n                  vm_write32(_ts + 0x74u, _sz);\n                f2b_stream_fill(_st, 0x20u);\n              }\n            }\n          } }\n        }\n'
BLK_D5CC = '        /* F2B-KEEP-DONE: re-assert host success after field setup; 0030B058\n         * still runs (links op to media/container) but may stamp\n         * op+44=0x8001070A from dearch reject. RESTATUS in 4274 clears it\n         * before 6610. Keep DONEFORCE sticky so poll sees +90. */\n        { uint32_t _op=(uint32_t)ctx->gpr[27], _fo=(uint32_t)ctx->gpr[31];\n          if(_fo && f2b_fo_mfd_get(_fo)){\n            vm_write32(_op+0x90u, 1u);\n            vm_write32(_op+0x44u, 0u);\n            vm_write32(_op+0xCCu, 0u);\n            ps3_fios_sticky_publish(_op);\n            { static int _n=0; if(_n++<8){\n              fprintf(stderr,"[FIOSOPEN] F2B-KEEP-DONE fo=0x%08X op=0x%08X (then 0030B058)\\n",\n                _fo, _op);\n              fflush(stderr); } }\n          }\n        }\n'

M_4274 = "F2B-RESTATUS"
M_D5CC = "F2B-KEEP-DONE"


def _region(text: str, fn: str):
    """(inicio, fim) do corpo de `fn` no texto, ou None."""
    sig = "void %s(ppu_context* ctx) {\n" % fn
    i = text.find(sig)
    if i < 0:
        return None
    j = text.find("\nvoid func_", i + len(sig))
    return (i, len(text) if j < 0 else j)


def _insert_in_region(text, fn, anchor, block, before):
    """Insere `block` junto a `anchor` dentro da regiao de `fn`.

    Devolve (novo_texto, estado): estado in {"applied", "no-fn", "no-anchor",
    "dup-anchor"}.
    """
    reg = _region(text, fn)
    if reg is None:
        return text, "no-fn"
    a, b = reg
    body = text[a:b]
    n = body.count(anchor)
    if n == 0:
        return text, "no-anchor"
    if n > 1:
        return text, "dup-anchor"
    new_body = body.replace(anchor, block + anchor if before else anchor + block, 1)
    return text[:a] + new_body + text[b:], "applied"


def patch_one(path: Path) -> int:
    """0 aplicado | 1 ja' aplicado | -1 sem funcoes alvo | -2 ancora ausente."""
    text = path.read_text(encoding="utf-8", errors="replace")
    orig = text
    has_4274 = _region(text, FN_4274) is not None
    has_d5cc = _region(text, FN_D5CC) is not None
    if not (has_4274 or has_d5cc):
        return -1

    need_4274 = has_4274 and M_4274 not in text
    need_d5cc = has_d5cc and M_D5CC not in text
    if not (need_4274 or need_d5cc):
        print("  %s: ALREADY" % path.name)
        return 1

    # 1) preambulo (por partes, cada uma guardada por sentinela)
    if INCLUDE_ANCHOR not in text:
        print("  %s: ancora de include ausente" % path.name, file=sys.stderr)
        return -2
    pre = ""
    if S_HEADERS not in text:
        pre += PRE_HEADERS + PRE_STICKY
    if S_EXTERNS not in text:
        pre += PRE_EXTERNS
    if S_MAP not in text:
        pre += PRE_MAP
    if S_FILL not in text:
        pre += PRE_FILL
    if pre:
        text = text.replace(INCLUDE_ANCHOR, INCLUDE_ANCHOR + pre, 1)

    # 2) blocos nas funcoes
    done = []
    for need, fn, anchor, block, before, marker in (
        (need_4274, FN_4274, ANCHOR_4274, BLK_4274, True, M_4274),
        (need_d5cc, FN_D5CC, ANCHOR_D5CC, BLK_D5CC, False, M_D5CC),
    ):
        if not need:
            continue
        text, st = _insert_in_region(text, fn, anchor, block, before)
        if st != "applied":
            print("  %s: %s -> ancora %s (%s)" % (path.name, fn, st, marker),
                  file=sys.stderr)
            return -2
        done.append(fn)

    if text == orig:
        print("  %s: ALREADY" % path.name)
        return 1
    try:
        path.write_text(text, encoding="utf-8", newline="\n")
    except TypeError:                                   # Python < 3.10
        path.write_text(text, encoding="utf-8")
    print("  %s: APPLIED (%s)" % (path.name, ", ".join(done)))
    return 0


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], DEFAULT_LIFT) if p.is_file()]
    if not paths:
        print("ERRO: nenhum ficheiro de lift legivel", file=sys.stderr)
        return 3
    applied = already = refused = 0
    for p in paths:
        r = patch_one(p)
        if r == 0:
            applied += 1
        elif r == 1:
            already += 1
        elif r == -2:
            refused += 1
    if refused:
        print("ERRO: func_002B4274 / func_0030D5CC existem mas a linha gerada de\n"
              "  ancoragem nao. O lifter mudou de forma -- nao insiro as cegas.\n"
              "  Revalidar os blocos contra um lift de producao conhecido-bom e\n"
              "  regerar este script (ver 'Como foi gerado' no cabecalho).",
              file=sys.stderr)
        return 2
    if applied or already:
        print("[f2b-open-block] ok (%d aplicado, %d ja' aplicado)" % (applied, already))
        return 0
    print("ERRO: nem func_002B4274 nem func_0030D5CC existem no lift dado.",
          file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
