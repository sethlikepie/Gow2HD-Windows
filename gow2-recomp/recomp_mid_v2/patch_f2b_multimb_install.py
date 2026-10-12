#!/usr/bin/env python3
"""Instala o bloco F2B multi-MB stream: helpers host + os 9 call sites.

Porque existe
-------------
`patch_f2b_multimb_stream.py` e' um VERIFICADOR PURO (zero escritas): confirma
os marcadores F2B-STREAM-* e as chamadas dentro dos corpos, e devolve rc=2
quando faltam. O comportamento que ele verifica NUNCA teve escritor -- era
edicao MANUAL de sessao dentro do lift gitignored. Medido a 2026-07-25 num lift
limpo com todos os patches aplicados: `score ok=0 fail=21`, rc=2.

Este ficheiro e' o escritor que faltava. O verificador fica a verificar (nao foi
tocado); depois deste instalador correr, passa legitimamente (`ok=21 fail=0`).

O que instala
-------------
A) Preambulo host, recortado VERBATIM de recomp_macos_v2/ppu_recomp_001.cpp e
   partido em 4 sub-blocos, cada um com a sua sentinela de idempotencia:
     1. externs movie_io_open/pread/is + ps3_fios_aread_hle
        sentinela: 'extern "C" unsigned movie_io_pread('
     2. mapa FO->mfd + contadores do ring (g_f2b_fill_fo/mfd/sz/file_pos/
        stream) + extern vm_base
        sentinela: 'static uint32_t g_f2b_fo_mfd_fo[8];'
     3. comentario "F2B multi-MB fix" + f2b_stream_fill()
        sentinela: 'static int f2b_stream_fill('
     4. f2b_stream_ensure() + g_wadld_eof_ea + f2b_stream_eof_try_complete()
        + f2b_stream_pre_consume()
        sentinela: 'void f2b_stream_ensure(uint32_t type_sys) {'
   As 3 primeiras sentinelas sao EXACTAMENTE as (S_EXTERNS / S_MAP / S_FILL) do
   `patch_fios_f2b_open_block_install.py`, que instala o mesmo preambulo para o
   bloco do open. Assim os dois instaladores coexistem em qualquer ordem sem
   duplicar simbolos -- medido: sem isto, os 5 `static ... g_f2b_fill_*` ficavam
   declarados duas vezes no mesmo TU (erro de compilacao).
   Vai para o chunk que contem os 4 consumidores do ring (pre_consume e' static,
   tem de viver no mesmo TU), inserido imediatamente antes da 1a `void func_`.
B) `extern "C"` de ensure/eof_try_complete nos OUTROS chunks com call sites (na
   producao e' o ppu_recomp_002.cpp), tal e qual como la' estao.
C) As 9 insercoes de call site, cada uma recortada verbatim do corpo da funcao
   correspondente no lift de producao:
     ensure(+eof): 002BA76C, 002BA9BC (so' ensure), 002BAB88, 002BA9F0, 002BA9F4
     pre_consume : 002E11EC, 002E1228, 002E1290, 002E1480

O que NAO instala (de proposito)
--------------------------------
- F2B-KEEP-DONE / F2B-RESTATUS / F2B-FO-SIZE / F2B-STREAM-PUMP / F2B-STREAM-SEED
  e o corpo do open: sao de OUTROS orfaos, com verificadores proprios
  (patch_fios_f2b_open_success.py, patch_fios_stream_pump.py,
  patch_2b3d1c_movie_io.py) e instalador proprio
  (patch_fios_f2b_open_block_install.py). E' esse bloco que arma
  g_f2b_fill_mfd/sz/file_pos; enquanto nao correr, os helpers daqui ficam
  inertes (comecam todos por `if (!g_f2b_fill_mfd) return;`), tal como na
  producao antes do open. Nota: se esse instalador nunca correr, as 3 funcoes
  static do mapa FO (f2b_fo_mfd_put/get/sz_get) ficam sem uso -- e' so' aviso
  do compilador, e o preambulo e' partilhado de proposito.
- F2B-BODY-CLAMP / F2B-STREAM-RESYNC / F2B-STREAM-ALIGN / DESYNC-STOP em
  func_002BA9BC: ~150 linhas com offsets absolutos do R_PermA, de uma sessao
  posterior, que NENHUM patch_*.py verifica (grep a zero em recomp_mid_v2/*.py)
  e que nao constam da lista de fixes do verificador deste item. Ficam orfaos e
  por instalar -- declarado aqui para nao se perder.
- Probes com escritor proprio ([WADLD-SM] em 2BA76C, [WADLD-BODY] em 2BAB88):
  sao de patch_wad_state_machine.py.

Ordem dentro do apply_all_patches.sh
------------------------------------
O nome ordena antes de `patch_f2b_multimb_stream.py` ("_i" < "_s") para que numa
UNICA passagem o instalador corra primeiro e o verificador ja' encontre tudo.
`patch_wad_state_machine.py` corre DEPOIS: a sua ancora de [WADLD-SM] termina na
linha `ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x122C);`, por isso a probe fica
ANTES do par ensure/eof em func_002BA76C, ao contrario da producao (que a tem
depois). A probe e' read-only e gated por PS3_TRACE_TYMAP: a diferenca e' de
posicao textual, nao de comportamento do guest. Nao se mexeu no outro patch.

Como foi gerado
---------------
Por script (scratchpad/gen_f2b_installer.py): recorta do lift de producao os 4
sub-blocos do preambulo (dos externs movie_io ate' ao fecho de
f2b_stream_pre_consume) e, para cada call site, as linhas F2B que a producao tem
a mais no inicio do corpo. Nada foi transcrito a mao.

Contrato de rc
--------------
- 0 : tudo instalado agora (APPLIED) e/ou ja' presente (ALREADY)
- 2 : ancora esperada ausente / funcao alvo em falta / os 4 consumidores do ring
      espalhados por chunks diferentes (pre_consume e' static) / a mesma funcao
      em mais de um chunk. NAO adivinha: o lifter mudou de forma e o bloco tem
      de ser revalidado a mao antes de reaplicado.
- 3 : nenhum ficheiro de lift legivel

Uso:  patch_f2b_multimb_install.py [LIFT_DIR_OU_FICHEIRO...]
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

ROOT_DEFAULT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# Preambulo host: (sentinela, texto). Sentinelas 1-3 partilhadas com
# patch_fios_f2b_open_block_install.py (S_EXTERNS / S_MAP / S_FILL).
PREAMBLE = (
    ('extern "C" unsigned movie_io_pread(', 'extern "C" unsigned movie_io_open(const char* path, unsigned* out_size);\nextern "C" unsigned movie_io_pread(unsigned fd, void* dst, unsigned n, unsigned pos);\nextern "C" int movie_io_is(unsigned fd);\nextern "C" unsigned ps3_fios_aread_hle(uint32_t op, uint32_t dst, uint32_t n, unsigned fd);\n'),
    ("static uint32_t g_f2b_fo_mfd_fo[8];", '/* F2B FO side-channel: real FO has +0x38=0; stashing mfd there may be read\n * as a guest pointer. Keep mfd in a host map keyed by FO EA. */\nstatic uint32_t g_f2b_fo_mfd_fo[8];\nstatic unsigned g_f2b_fo_mfd_fd[8];\nstatic uint32_t g_f2b_fo_mfd_sz[8];\nstatic int g_f2b_fo_mfd_n = 0;\n/* Natural FO EA from first successful movie file_new; re-used by F2B re-open. */\nstatic uint32_t g_f2b_natural_movie_fo = 0;\nstatic void f2b_fo_mfd_put(uint32_t fo, unsigned mfd, uint32_t sz) {\n    for (int i = 0; i < g_f2b_fo_mfd_n; i++)\n        if (g_f2b_fo_mfd_fo[i] == fo) { g_f2b_fo_mfd_fd[i] = mfd; g_f2b_fo_mfd_sz[i] = sz; return; }\n    if (g_f2b_fo_mfd_n < 8) {\n        g_f2b_fo_mfd_fo[g_f2b_fo_mfd_n] = fo;\n        g_f2b_fo_mfd_fd[g_f2b_fo_mfd_n] = mfd;\n        g_f2b_fo_mfd_sz[g_f2b_fo_mfd_n] = sz;\n        g_f2b_fo_mfd_n++;\n    }\n}\nstatic unsigned f2b_fo_mfd_get(uint32_t fo) {\n    for (int i = 0; i < g_f2b_fo_mfd_n; i++)\n        if (g_f2b_fo_mfd_fo[i] == fo) return g_f2b_fo_mfd_fd[i];\n    return 0;\n}\nstatic uint32_t f2b_fo_sz_get(uint32_t fo) {\n    for (int i = 0; i < g_f2b_fo_mfd_n; i++)\n        if (g_f2b_fo_mfd_fo[i] == fo) return g_f2b_fo_mfd_sz[i];\n    return 0;\n}\n/* WADLD stream ring (type_sys+0x1A8): base@+0 cursor@+8 cap@+C avail@+10.\n * Init leaves avail=0; natural path areads FO into the ring. F2B host fill. */\nstatic uint32_t g_f2b_fill_fo = 0;\nstatic unsigned g_f2b_fill_mfd = 0;\nstatic uint32_t g_f2b_fill_sz = 0;\nstatic uint32_t g_f2b_fill_file_pos = 0;\nstatic uint32_t g_f2b_fill_stream = 0;\nextern "C" uint8_t* vm_base;\n'),
    ("static int f2b_stream_fill(", '/* F2B multi-MB fix (2026-07-22): body steps with avail==0 used to WAIT without\n * ever refilling (BA9F0→yield). Multi-pass body (1.1MB SBP vs 256KB ring) then\n * desyncs: rem decrements by header size while stream stays empty / mid-payload\n * is read as next header → need=0x687DD790 hang. Fill loops until ring full or\n * min_need met; callers ENSURE before body when avail==0. */\nstatic int f2b_stream_fill(uint32_t stream, uint32_t min_need) {\n    if (!stream || !vm_base || !g_f2b_fill_mfd || !movie_io_is(g_f2b_fill_mfd))\n        return 0;\n    if (g_f2b_fill_stream && stream != g_f2b_fill_stream\n        && g_f2b_fill_stream != 0) {\n        /* New FO: allow rebind */\n    }\n    g_f2b_fill_stream = stream;\n    int rounds = 0;\n    for (;;) {\n        uint32_t avail = vm_read32(stream + 0x10u);\n        uint32_t cursor = vm_read32(stream + 0x8u);\n        uint32_t base = vm_read32(stream + 0x0u);\n        uint32_t cap = vm_read32(stream + 0xCu);\n        if (!base || !cap || base >= 0x4F000000u || cap > 0x01000000u) return 0;\n        /* Cap min_need to ring size — multi-pass guest consumes cap-sized chunks. */\n        uint32_t target = min_need;\n        if (target == 0) target = 1; /* force at least one byte attempt when empty */\n        if (target > cap) target = cap;\n        if (avail >= target) return 1;\n        if (g_f2b_fill_file_pos >= g_f2b_fill_sz) return avail > 0 ? 1 : 0;\n        /* Compact unread to base so space = cap - avail is contiguous.\n         * Guest ring may wrap (E1480 split-copy when cursor+need > cap). Old\n         * code zeroed avail on wrap → dropped unread bytes while file_pos had\n         * already advanced past them → stream desync after multi-MB bodies\n         * (first bad header after ~010decorchest; residual body1D4 garbage). */\n        if (avail > 0 && cursor > 0 && cursor < cap && avail <= cap) {\n            uint8_t* b = vm_base + base;\n            if (cursor + avail <= cap) {\n                memmove(b, b + cursor, avail);\n            } else {\n                uint32_t len1 = cap - cursor;\n                uint32_t len2 = avail - len1;\n                if (len2 <= cap && avail <= cap) {\n                    /* One cap-sized scratch is enough (ring holds ≤cap bytes). */\n                    uint8_t* tmp = (uint8_t*)malloc((size_t)avail);\n                    if (tmp) {\n                        memcpy(tmp, b + cursor, (size_t)len1);\n                        memcpy(tmp + len1, b, (size_t)len2);\n                        memcpy(b, tmp, (size_t)avail);\n                        free(tmp);\n                        { static int _n=0; if(_n++<32)\n                            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-WRAP-COMPACT "\n                              "len1=%u len2=%u avail=%u file_pos=%u\\n",\n                              len1, len2, avail, g_f2b_fill_file_pos); }\n                    } else {\n                        { static int _n=0; if(_n++<8)\n                            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-WRAP-COMPACT-OOM "\n                              "avail=%u → drop\\n", avail); }\n                        avail = 0;\n                    }\n                } else {\n                    { static int _n=0; if(_n++<8)\n                        fprintf(stderr,"[FIOSOPEN] F2B-STREAM-WRAP-COMPACT-BAD "\n                          "cursor=%u avail=%u cap=%u → drop\\n",\n                          cursor, avail, cap); }\n                    avail = 0;\n                }\n            }\n            cursor = 0;\n            vm_write32(stream + 0x8u, 0u);\n            vm_write32(stream + 0x10u, avail);\n            vm_write32(stream + 0x4u, avail); /* write_pos @ end of compact data */\n        } else if (avail == 0) {\n            cursor = 0;\n            vm_write32(stream + 0x8u, 0u);\n            vm_write32(stream + 0x4u, 0u);\n        }\n        uint32_t space = (cap > avail) ? (cap - avail) : 0;\n        if (!space) return avail > 0 ? 1 : 0;\n        uint32_t n = g_f2b_fill_sz - g_f2b_fill_file_pos;\n        if (n > space) n = space;\n        if (!n) return avail > 0 ? 1 : 0;\n        unsigned got = movie_io_pread(g_f2b_fill_mfd, vm_base + base + avail, n,\n                                      g_f2b_fill_file_pos);\n        if (got != n) {\n            { static int _n=0; if(_n++<12)\n                fprintf(stderr,"[FIOSOPEN] F2B-STREAM-FILL short stream=0x%08X pos=%u n=%u got=%u\\n",\n                  stream, g_f2b_fill_file_pos, n, got); }\n            if (!got) return avail > 0 ? 1 : 0;\n            n = got;\n        }\n        avail += n;\n        g_f2b_fill_file_pos += n;\n        vm_write32(stream + 0x10u, avail);\n        /* Keep write_pos (+4) consistent with cursor=0 compact form: data is\n         * [0, avail). E1254/E1290 use +4 as the producer cursor. */\n        vm_write32(stream + 0x4u, avail);\n        { static int _n=0; if(_n++<64){\n            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-FILL stream=0x%08X base=0x%08X avail=%u file_pos=%u/%u need=%u\\n",\n              stream, base, avail, g_f2b_fill_file_pos, g_f2b_fill_sz, min_need);\n            fflush(stderr); } }\n        rounds++;\n        if (rounds > 64) return avail > 0 ? 1 : 0;\n        if (avail >= target) return 1;\n        /* else loop: more file data into remaining ring space */\n    }\n}\n'),
    ("void f2b_stream_ensure(uint32_t type_sys) {", 'extern "C" void f2b_stream_ensure(uint32_t type_sys) {\n    if (!type_sys || type_sys < 0x10000u || !g_f2b_fill_mfd) return;\n    uint32_t st = vm_read32(type_sys + 0x1A8u);\n    if (st < 0x10000u || st >= 0x4F000000u) return;\n    uint32_t av = vm_read32(st + 0x10u);\n    uint32_t rem_body = vm_read32(type_sys + 0x1D4u);\n    uint32_t need = rem_body ? rem_body : 0x20u;\n    /* Always top-up if less than one header (0x20) remains readable. */\n    if (av < 0x20u || (need && av < need)) {\n        uint32_t want = need ? need : 0x1000u;\n        if (want < 0x20u) want = 0x20u;\n        f2b_stream_fill(st, want);\n        { static int _n=0; if(_n++<200){\n            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-ENSURE ts=0x%08X st=0x%08X was_av=%u rem1D4=0x%X -> av=%u file_pos=%u/%u\\n",\n              type_sys, st, av, rem_body, vm_read32(st+0x10u),\n              g_f2b_fill_file_pos, g_f2b_fill_sz);\n            fflush(stderr); } }\n    }\n}\n/* When the F2B FO is fully read (file_pos>=size) and type_sys rem is 0 but\n * state is stuck in 2/3 with a residual body1D4, force idle so\n * func_002BA76C can take the finish path (r3=0) and 00041D5C exits.\n * Without this: sample B71B8→41D5C→B367C yield forever (state=3 rem=0\n * body1D4=0x23DB43D0 after R_Perm FULL).\n *\n * Same-class audit 2026-07-22 — do NOT fire on clean FO end (body1D4==0):\n * R_LglScA (3KiB) reaches file_pos==size with state∈{2} rem==0 body==0 after\n * members expand; forcing idle there aborts before R_PermA open. The real hang\n * always had non-zero residual body1D4. Also require ring avail==0. */\nextern "C" uint32_t g_wadld_eof_ea = 0; /* SM EA at true-EOF drain — WAD-wait completion gate */\nextern "C" void f2b_stream_eof_try_complete(uint32_t type_sys) {\n    if (!type_sys || type_sys < 0x10000u || !g_f2b_fill_sz) return;\n    if (g_f2b_fill_file_pos < g_f2b_fill_sz) return;\n    uint32_t rem = vm_read32(type_sys + 0x1ACu);\n    uint32_t state = vm_read32(type_sys + 0x1CCu);\n    uint32_t body = vm_read32(type_sys + 0x1D4u);\n    if (state != 2u && state != 3u) return;\n    uint32_t st = vm_read32(type_sys + 0x1A8u);\n    uint32_t av = 0u;\n    if (st >= 0x10000u && st < 0x4F000000u)\n        av = vm_read32(st + 0x10u);\n    if (av != 0u) return;\n    /* Classic residual body; also R_Perm-sized FO stuck in state 2/3 with\n     * rem=body=0 (guest finish never fires after residual EOF-QUIET). R_Lgl\n     * is 3KiB — size gate keeps its clean path. */\n    int classic = (rem == 0u && body != 0u);\n    int large_fo = (g_f2b_fill_sz > 0x100000u);\n    if (!classic && !(large_fo && rem == 0u)) {\n        if (!(large_fo && rem != 0u)) return;\n    }\n    vm_write32(type_sys + 0x1D4u, 0u);\n    vm_write32(type_sys + 0x1ACu, 0u);\n    vm_write32(type_sys + 0x1CCu, 0u); /* idle — not 1/2/3 */\n    g_wadld_eof_ea = type_sys; /* drained: WAD-wait (func_002BA808/BA824) may now complete via timeout path */\n    { static int _n=0; if(_n++<16){\n        fprintf(stderr,"[FIOSOPEN] F2B-STREAM-EOF-DONE ts=0x%08X was_state=%u body1D4=0x%X rem=0x%X file_pos=%u/%u → state=0\\n",\n          type_sys, state, body, rem, g_f2b_fill_file_pos, g_f2b_fill_sz);\n        fflush(stderr); } }\n}\n/* Shared F2B pre-hook: refill + clamp need to avail. Used by 002E1480/1228/11EC/1290. */\nstatic void f2b_stream_pre_consume(ppu_context* ctx) {\n    uint32_t _st = (uint32_t)ctx->gpr[3];\n    uint32_t _need = (uint32_t)ctx->gpr[4];\n    if (!_need || !g_f2b_fill_mfd || _st < 0x10000u || _st >= 0x4F000000u) return;\n    uint32_t _av = vm_read32(_st + 0x10u);\n    if (_av < _need) f2b_stream_fill(_st, _need);\n    _av = vm_read32(_st + 0x10u);\n    if (_av > 0 && _need > _av) {\n        { static int _n=0; if(_n++<48)\n            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-CLAMP need=0x%X -> avail=0x%X st=0x%08X file_pos=%u op=pre\\n",\n              _need, _av, _st, g_f2b_fill_file_pos); }\n        ctx->gpr[4] = _av;\n    } else if (_av == 0 && _need > 0) {\n        { static int _n=0; if(_n++<16)\n            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-EMPTY need=0x%X st=0x%08X file_pos=%u/%u\\n",\n              _need, _st, g_f2b_fill_file_pos, g_f2b_fill_sz); }\n        ctx->gpr[4] = 0; /* do not underflow avail */\n    }\n}\n'),
)

# Includes que o preambulo precisa (memmove/memcpy/malloc/fprintf). Repetidos de
# proposito: sao idempotentes e nao dependem do que outros patches puseram.
INCLUDES = ("/* F2B stream core -- instalado por"
            " recomp_mid_v2/patch_f2b_multimb_install.py */\n"
            "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n")
INCLUDES_MARK = "/* F2B stream core -- instalado por"

EXTERNS = 'extern "C" void f2b_stream_ensure(uint32_t type_sys);\nextern "C" void f2b_stream_eof_try_complete(uint32_t type_sys);\n'

# func -> (tipo de ancora, texto da ancora, bloco a inserir)
#   "sig"  -> insere logo a seguir a' linha da assinatura
#   "line" -> insere logo a seguir a' linha dada (dentro do corpo da funcao)
SITES = {
    'func_002BA76C': ('line', '        ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x122C);\n',
        '        f2b_stream_ensure((uint32_t)ctx->gpr[31]);\n        f2b_stream_eof_try_complete((uint32_t)ctx->gpr[31]);\n'),
    'func_002BA9BC': ('sig', '',
        '        /* F2B multi-MB: state-2 header needs avail>0x1F; refill before wait. */\n        f2b_stream_ensure((uint32_t)ctx->gpr[31]);\n'),
    'func_002BAB88': ('sig', '',
        '        /* F2B multi-MB: refill before body step if ring empty (else wait forever). */\n        f2b_stream_ensure((uint32_t)ctx->gpr[31]);\n        f2b_stream_eof_try_complete((uint32_t)ctx->gpr[31]);\n'),
    'func_002BA9F0': ('sig', '',
        '        f2b_stream_ensure((uint32_t)ctx->gpr[31]);\n        f2b_stream_eof_try_complete((uint32_t)ctx->gpr[31]);\n'),
    'func_002BA9F4': ('sig', '',
        '        /* F2B audit: direct entry to wait (from BAB88) must ensure+eof too. */\n        f2b_stream_ensure((uint32_t)ctx->gpr[31]);\n        f2b_stream_eof_try_complete((uint32_t)ctx->gpr[31]);\n'),
    'func_002E11EC': ('sig', '',
        '        /* F2B: refill+clamp before ring advance. */\n        f2b_stream_pre_consume(ctx);\n'),
    'func_002E1228': ('sig', '',
        '        /* F2B: refill+clamp before cursor skip (same class as 1480). */\n        f2b_stream_pre_consume(ctx);\n'),
    'func_002E1290': ('sig', '',
        '        /* F2B: ring copy sibling of 1480. */\n        f2b_stream_pre_consume(ctx);\n'),
    'func_002E1480': ('sig', '',
        '        /* F2B-STREAM-REFILL + CLAMP (shared pre_consume). */\n        f2b_stream_pre_consume(ctx);\n'),
}

PRE_FUNCS = ('func_002E11EC', 'func_002E1228', 'func_002E1290', 'func_002E1480')
ENSURE_FUNCS = ('func_002BA76C', 'func_002BA9BC', 'func_002BAB88', 'func_002BA9F0', 'func_002BA9F4')


def fn_span(text: str, name: str):
    """(inicio, fim) do corpo de `name` em `text`, ou None."""
    m = re.search(rf"^void {name}\(ppu_context\* ctx\) {{", text, re.M)
    if not m:
        return None
    nxt = re.search(r"^void func_", text[m.end():], re.M)
    return m.start(), (m.end() + nxt.start() if nxt else len(text))


def first_func_at(text: str) -> int:
    m = re.search(r"^void func_", text, re.M)
    if not m:
        raise LookupError("chunk sem nenhuma `void func_`")
    return m.start()


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], ROOT_DEFAULT) if p.is_file()]
    if not paths:
        print("ERRO: nenhum ficheiro de lift legivel", file=sys.stderr)
        return 3
    texts = {p: p.read_text(encoding="utf-8", errors="replace") for p in paths}

    # --- localizar cada funcao alvo (tem de existir e ser unica) -------------
    where = {}
    for name in SITES:
        hits = [p for p, t in texts.items() if fn_span(t, name)]
        if len(hits) != 1:
            print(f"ERRO: {name} aparece em {len(hits)} chunk(s) (esperado 1): "
                  + ", ".join(p.name for p in hits), file=sys.stderr)
            return 2
        where[name] = hits[0]

    # --- pre_consume e' static: os 4 consumidores tem de partilhar o chunk ---
    pre_chunks = {where[n] for n in PRE_FUNCS}
    if len(pre_chunks) != 1:
        print("ERRO: os consumidores do ring estao espalhados por "
              + ", ".join(sorted(p.name for p in pre_chunks))
              + " -- f2b_stream_pre_consume e' static e so' pode viver num TU.",
              file=sys.stderr)
        return 2
    def_chunk = pre_chunks.pop()

    applied = already = 0

    # --- 1) preambulo, sub-bloco a sub-bloco --------------------------------
    t = texts[def_chunk]
    try:
        at = first_func_at(t)
    except LookupError as e:
        print(f"ERRO: {def_chunk.name}: {e}", file=sys.stderr)
        return 2
    add = "" if INCLUDES_MARK in t else INCLUDES
    n_new = 0
    for sentinel, block in PREAMBLE:
        if sentinel in t:
            continue
        # Um simbolo declarado DEPOIS do nosso ponto de insercao nao nos serve.
        add += block
        n_new += 1
    if not add:
        print(f"  {def_chunk.name}: preambulo ALREADY")
        already += 1
    else:
        texts[def_chunk] = t[:at] + add + t[at:]
        print(f"  {def_chunk.name}: preambulo APPLIED "
              f"({n_new}/4 sub-blocos, {add.count(chr(10))} linhas)")
        applied += 1

    # --- 2) declaracoes extern nos outros chunks com call sites --------------
    for chunk in sorted({where[n] for n in ENSURE_FUNCS}, key=lambda p: p.name):
        if chunk == def_chunk:
            continue
        t = texts[chunk]
        if "f2b_stream_ensure(uint32_t type_sys);" in t:
            print(f"  {chunk.name}: externs ALREADY")
            already += 1
            continue
        try:
            at = first_func_at(t)
        except LookupError as e:
            print(f"ERRO: {chunk.name}: {e}", file=sys.stderr)
            return 2
        texts[chunk] = t[:at] + EXTERNS + t[at:]
        print(f"  {chunk.name}: externs APPLIED")
        applied += 1

    # --- 3) call sites -------------------------------------------------------
    for name, (kind, anchor, blk) in SITES.items():
        chunk = where[name]
        t = texts[chunk]
        lo, hi = fn_span(t, name)
        region = t[lo:hi]
        needle = blk.strip().splitlines()[-1].strip()
        if needle in region:
            print(f"  {name} ({chunk.name}): ALREADY")
            already += 1
            continue
        if kind == "sig":
            head = region.index("\n") + 1  # fim da linha da assinatura
        else:
            n = region.count(anchor)
            if n != 1:
                print(f"ERRO: {name}: a ancora aparece {n}x no corpo "
                      f"(esperado 1) -> {anchor.strip()}", file=sys.stderr)
                return 2
            head = region.index(anchor) + len(anchor)
        texts[chunk] = t[:lo] + region[:head] + blk + region[head:] + t[hi:]
        print(f"  {name} ({chunk.name}): APPLIED")
        applied += 1

    for p, t in texts.items():
        if t != p.read_text(encoding="utf-8", errors="replace"):
            try:
                p.write_text(t, encoding="utf-8", newline="\n")
            except TypeError:
                p.write_text(t, encoding="utf-8")

    print(f"[f2b-multimb-install] ok ({applied} aplicado, {already} ja' aplicado)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
