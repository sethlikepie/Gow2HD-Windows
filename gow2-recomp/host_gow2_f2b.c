/* host_gow2_f2b.c — subsistema host F2B (file-object -> mfd/tamanho) do GoW2.
 *
 * PORQUE ESTE FICHEIRO EXISTE
 * ----------------------------
 * Estas ~218 linhas viviam DENTRO do lift gerado (recomp_macos_v2/
 * ppu_recomp_001.cpp), que e' gitignored e regeneravel. Ou seja: codigo host
 * escrito a' mao, sem copia em repositorio nenhum, que desaparecia sem aviso
 * ao apagar ou re-gerar o lift -- exactamente o mesmo problema que
 * host_gow2_factory.cpp resolveu para a familia ps3_factory_ / ps3_type15_.
 *
 * Medido a 2026-07-26 (02-CONTEXT.md, D-2.3/D-2.4): 17 simbolos distintos --
 * f2b_fo_mfd_get, f2b_fo_mfd_put, f2b_fo_sz_get, f2b_stream_ensure,
 * f2b_stream_eof_try_complete, f2b_stream_fill, f2b_stream_pre_consume, e os
 * 10 globais g_f2b_* -- com definicoes contiguas em ppu_recomp_001.cpp:496-713
 * mas usos espalhados de :498 a :128446 (78 ocorrencias). A responsabilidade
 * do subsistema: mapa file-object guest -> (mfd, tamanho) host, e o
 * preenchimento (fill) do anel de stream do caminho FIOS do movie player
 * (WADLD stream ring em type_sys+0x1A8).
 *
 * EXTRACCAO POR SIMBOLO, NAO POR INTERVALO DE LINHAS (D-2.4): a linha 664 do
 * ficheiro original (`extern "C" uint32_t g_wadld_eof_ea = 0;`) fica
 * FISICAMENTE dentro do span 496-713, mas NAO e' nenhum dos 17 simbolos deste
 * subsistema -- e' do subsistema WAD-wait completion (outra familia). Um
 * recorte por intervalo de linhas tê-la-ia apanhado por engano; aqui fica
 * DE FORA, e a sua definicao (com inicializador) continua a viver no lift.
 * Este ficheiro so' a DECLARA (sem inicializador, ver abaixo) porque
 * f2b_stream_eof_try_complete escreve-lhe.
 *
 * NOTA DE INTEGRACAO: extraido literalmente do lift em producao (mesma
 * semantica, sem reescrita) -- byte a byte, incluindo comentarios de
 * debugging originais. As unicas alteracoes sao mecanicas (ver lista abaixo),
 * nunca de logica. Ainda NAO esta ligado ao build -- o passo seguinte
 * (plano 02-03) e' compilar este ficheiro e deixar no lift apenas as
 * declaracoes extern; isso exige remover o bloco do lift para nao haver
 * simbolo duplicado (ver o guard de deteccao de definicao vs prototipo
 * documentado no CLAUDE.md da raiz).
 *
 * Alteracoes mecanicas feitas na extraccao (nenhuma outra linha mudou):
 *   - g_wadld_eof_ea (linha 664 do original): OMITIDA -- fora dos 17 simbolos
 *     (D-2.4); so' declarada (sem inicializador) no preambulo abaixo.
 *   - vm_base (linha 531 do original, duplicada com a linha 269 do ficheiro
 *     de origem): OMITIDA aqui -- ja' declarada uma unica vez no preambulo.
 *   - f2b_fo_mfd_put/get, f2b_fo_sz_get, f2b_stream_fill,
 *     f2b_stream_pre_consume: perderam `static` -- todas tem pelo menos um
 *     ponto de chamada FORA do bloco original (ex.: f2b_fo_mfd_get chamada em
 *     ppu_recomp_001.cpp:34517/:34545/:128446; f2b_stream_fill em :34607).
 *   - f2b_stream_ensure, f2b_stream_eof_try_complete: perderam o prefixo
 *     `extern "C" ` -- ja' eram externas no C++ original (chamadas do chunk
 *     002), mas `extern "C"` nao e' sintaxe valida em C11 puro (D-2.3: este
 *     ficheiro e' .c, nao .cpp).
 *   - g_f2b_fill_fo/mfd/sz/file_pos/stream, g_f2b_natural_movie_fo: perderam
 *     `static` -- lidos/escritos DIRECTAMENTE de fora do bloco
 *     (ppu_recomp_001.cpp:34595-34599 escreve os 5 primeiros de uma vez;
 *     :128281-128284 le' g_f2b_natural_movie_fo).
 *   - g_f2b_fo_mfd_fo/fd/sz[8] e g_f2b_fo_mfd_n: MANTIVERAM `static` -- uso
 *     confinado ao proprio bloco (so' f2b_fo_mfd_put/get e f2b_fo_sz_get os
 *     tocam), por isso continuam privados a este ficheiro (tabela mfd).
 *
 * E' codigo especifico do GoW2 (mapa de file-object do movie player do
 * titulo), por isso vive no checkout do jogo e nao no runtime generico do
 * motor -- ver a meta da Fase 12 do plano de fiabilidade: "o runtime generico
 * nao contem enderecos ou FSMs especificas de um jogo".
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppu_context.h"

/* Primitivas de memoria guest e I/O do movie player, definidas no runtime do
 * motor (ppu_loader / ppu_fs). Sem "C" -- este ficheiro e' C11 puro. */
extern uint32_t vm_read32(uint64_t a);
extern void     vm_write32(uint64_t a, uint32_t v);
extern uint8_t* vm_base;
extern unsigned movie_io_pread(unsigned fd, void* dst, unsigned n, unsigned pos);
extern int      movie_io_is(unsigned fd);
/* g_wadld_eof_ea: DEFINIDO aqui, nao apenas declarado.
 *
 * A extraccao original deixou-o de fora dos 17 simbolos do F2B (era a linha 664
 * do bloco no lift) e pos aqui um `extern`, assumindo que a definicao ficava no
 * lift. Nao fica: no lift REGENERADO ninguem o define -- ele vinha do bloco
 * [BA808] escrito a mao, que era orfao e desapareceu. Medido em 2026-07-26, no
 * primeiro link real deste ficheiro:
 *
 *   Undefined symbols for architecture arm64:
 *     "_g_wadld_eof_ea", referenced from:
 *         _f2b_stream_eof_try_complete in host_gow2_f2b.o
 *
 * O harness sintetico que validou a extraccao nao apanhou isto porque fornecia
 * um stub do simbolo; o link real nao tem stub. E' a diferenca entre provar o
 * mecanismo e provar o facto.
 *
 * Fica aqui porque este ficheiro e' o seu unico ESCRITOR
 * (f2b_stream_eof_try_complete). Os leitores sao os blocos WAD-wait no lift
 * (func_002BA808/BA824), que o declaram `extern` no proprio sitio de uso.
 * Um `patch_*.py` que reinstale esses blocos nao precisa de o definir. */
uint32_t g_wadld_eof_ea = 0;

/* F2B FO side-channel: real FO has +0x38=0; stashing mfd there may be read
 * as a guest pointer. Keep mfd in a host map keyed by FO EA. */
static uint32_t g_f2b_fo_mfd_fo[8];
static unsigned g_f2b_fo_mfd_fd[8];
static uint32_t g_f2b_fo_mfd_sz[8];
static int g_f2b_fo_mfd_n = 0;
/* Natural FO EA from first successful movie file_new; re-used by F2B re-open. */
uint32_t g_f2b_natural_movie_fo = 0;
void f2b_fo_mfd_put(uint32_t fo, unsigned mfd, uint32_t sz) {
    for (int i = 0; i < g_f2b_fo_mfd_n; i++)
        if (g_f2b_fo_mfd_fo[i] == fo) { g_f2b_fo_mfd_fd[i] = mfd; g_f2b_fo_mfd_sz[i] = sz; return; }
    if (g_f2b_fo_mfd_n < 8) {
        g_f2b_fo_mfd_fo[g_f2b_fo_mfd_n] = fo;
        g_f2b_fo_mfd_fd[g_f2b_fo_mfd_n] = mfd;
        g_f2b_fo_mfd_sz[g_f2b_fo_mfd_n] = sz;
        g_f2b_fo_mfd_n++;
    }
}
unsigned f2b_fo_mfd_get(uint32_t fo) {
    for (int i = 0; i < g_f2b_fo_mfd_n; i++)
        if (g_f2b_fo_mfd_fo[i] == fo) return g_f2b_fo_mfd_fd[i];
    return 0;
}
/* E131 (2026-09-02): o guest fecha o file handle (dtor do fh, func_0031F540) --
 * libertar a entrada fo->mfd para o slot do movieio poder ser reutilizado. */
void f2b_fo_mfd_del(uint32_t fo) {
    for (int i = 0; i < g_f2b_fo_mfd_n; i++)
        if (g_f2b_fo_mfd_fo[i] == fo) {
            for (int j = i + 1; j < g_f2b_fo_mfd_n; j++) {
                g_f2b_fo_mfd_fo[j-1] = g_f2b_fo_mfd_fo[j]; g_f2b_fo_mfd_fd[j-1] = g_f2b_fo_mfd_fd[j]; g_f2b_fo_mfd_sz[j-1] = g_f2b_fo_mfd_sz[j];
            }
            g_f2b_fo_mfd_n--; return;
        }
}
uint32_t f2b_fo_sz_get(uint32_t fo) {
    for (int i = 0; i < g_f2b_fo_mfd_n; i++)
        if (g_f2b_fo_mfd_fo[i] == fo) return g_f2b_fo_mfd_sz[i];
    return 0;
}
/* WADLD stream ring (type_sys+0x1A8): base@+0 cursor@+8 cap@+C avail@+10.
 * Init leaves avail=0; natural path areads FO into the ring. F2B host fill. */
uint32_t g_f2b_fill_fo = 0;
unsigned g_f2b_fill_mfd = 0;
uint32_t g_f2b_fill_sz = 0;
uint32_t g_f2b_fill_file_pos = 0;
uint32_t g_f2b_fill_stream = 0;
/* F2B multi-MB fix (2026-07-22): body steps with avail==0 used to WAIT without
 * ever refilling (BA9F0→yield). Multi-pass body (1.1MB SBP vs 256KB ring) then
 * desyncs: rem decrements by header size while stream stays empty / mid-payload
 * is read as next header → need=0x687DD790 hang. Fill loops until ring full or
 * min_need met; callers ENSURE before body when avail==0. */
int f2b_stream_fill(uint32_t stream, uint32_t min_need) {
    if (!stream || !vm_base || !g_f2b_fill_mfd || !movie_io_is(g_f2b_fill_mfd))
        return 0;
    if (g_f2b_fill_stream && stream != g_f2b_fill_stream
        && g_f2b_fill_stream != 0) {
        /* New FO: allow rebind */
    }
    g_f2b_fill_stream = stream;
    int rounds = 0;
    for (;;) {
        uint32_t avail = vm_read32(stream + 0x10u);
        uint32_t cursor = vm_read32(stream + 0x8u);
        uint32_t base = vm_read32(stream + 0x0u);
        uint32_t cap = vm_read32(stream + 0xCu);
        if (!base || !cap || base >= 0x4F000000u || cap > 0x01000000u) return 0;
        /* Cap min_need to ring size — multi-pass guest consumes cap-sized chunks. */
        uint32_t target = min_need;
        if (target == 0) target = 1; /* force at least one byte attempt when empty */
        if (target > cap) target = cap;
        if (avail >= target) return 1;
        if (g_f2b_fill_file_pos >= g_f2b_fill_sz) return avail > 0 ? 1 : 0;
        /* Compact unread to base so space = cap - avail is contiguous.
         * Guest ring may wrap (E1480 split-copy when cursor+need > cap). Old
         * code zeroed avail on wrap → dropped unread bytes while file_pos had
         * already advanced past them → stream desync after multi-MB bodies
         * (first bad header after ~010decorchest; residual body1D4 garbage). */
        if (avail > 0 && cursor > 0 && cursor < cap && avail <= cap) {
            uint8_t* b = vm_base + base;
            if (cursor + avail <= cap) {
                memmove(b, b + cursor, avail);
                { extern void ps3_watch_store_bulk(uint32_t, uint32_t, const char*);
                  ps3_watch_store_bulk(base, avail, "f2b compact memmove"); }
            } else {
                uint32_t len1 = cap - cursor;
                uint32_t len2 = avail - len1;
                if (len2 <= cap && avail <= cap) {
                    /* One cap-sized scratch is enough (ring holds ≤cap bytes). */
                    uint8_t* tmp = (uint8_t*)malloc((size_t)avail);
                    if (tmp) {
                        memcpy(tmp, b + cursor, (size_t)len1);
                        memcpy(tmp + len1, b, (size_t)len2);
                        memcpy(b, tmp, (size_t)avail);
                        { extern void ps3_watch_store_bulk(uint32_t, uint32_t, const char*);
                          ps3_watch_store_bulk(base, avail, "f2b wrap-compact memcpy"); }
                        free(tmp);
                        { static int _n=0; if(_n++<32)
                            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-WRAP-COMPACT "
                              "len1=%u len2=%u avail=%u file_pos=%u\n",
                              len1, len2, avail, g_f2b_fill_file_pos); }
                    } else {
                        { static int _n=0; if(_n++<8)
                            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-WRAP-COMPACT-OOM "
                              "avail=%u → drop\n", avail); }
                        avail = 0;
                    }
                } else {
                    { static int _n=0; if(_n++<8)
                        fprintf(stderr,"[FIOSOPEN] F2B-STREAM-WRAP-COMPACT-BAD "
                          "cursor=%u avail=%u cap=%u → drop\n",
                          cursor, avail, cap); }
                    avail = 0;
                }
            }
            cursor = 0;
            vm_write32(stream + 0x8u, 0u);
            vm_write32(stream + 0x10u, avail);
            vm_write32(stream + 0x4u, avail); /* write_pos @ end of compact data */
        } else if (avail == 0) {
            cursor = 0;
            vm_write32(stream + 0x8u, 0u);
            vm_write32(stream + 0x4u, 0u);
        }
        uint32_t space = (cap > avail) ? (cap - avail) : 0;
        if (!space) return avail > 0 ? 1 : 0;
        uint32_t n = g_f2b_fill_sz - g_f2b_fill_file_pos;
        if (n > space) n = space;
        if (!n) return avail > 0 ? 1 : 0;
        unsigned got = movie_io_pread(g_f2b_fill_mfd, vm_base + base + avail, n,
                                      g_f2b_fill_file_pos);
        /* PS3_WATCH_STORE: o pread escreve DIRECTAMENTE em vm_base e nunca
         * passa por vm_write* -- e' invisivel aos dois watches de store.
         * Gated, OFF por default. */
        { extern void ps3_watch_store_bulk(uint32_t, uint32_t, const char*);
          ps3_watch_store_bulk(base + avail, got, "f2b_stream_fill pread"); }
        if (got != n) {
            { static int _n=0; if(_n++<12)
                fprintf(stderr,"[FIOSOPEN] F2B-STREAM-FILL short stream=0x%08X pos=%u n=%u got=%u\n",
                  stream, g_f2b_fill_file_pos, n, got); }
            if (!got) return avail > 0 ? 1 : 0;
            n = got;
        }
        avail += n;
        g_f2b_fill_file_pos += n;
        vm_write32(stream + 0x10u, avail);
        /* Keep write_pos (+4) consistent with cursor=0 compact form: data is
         * [0, avail). E1254/E1290 use +4 as the producer cursor. */
        vm_write32(stream + 0x4u, avail);
        { static int _n=0; if(_n++<64){
            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-FILL stream=0x%08X base=0x%08X avail=%u file_pos=%u/%u need=%u\n",
              stream, base, avail, g_f2b_fill_file_pos, g_f2b_fill_sz, min_need);
            fflush(stderr); } }
        rounds++;
        if (rounds > 64) return avail > 0 ? 1 : 0;
        if (avail >= target) return 1;
        /* else loop: more file data into remaining ring space */
    }
}
void f2b_stream_ensure(uint32_t type_sys) {
    if (!type_sys || type_sys < 0x10000u || !g_f2b_fill_mfd) return;
    uint32_t st = vm_read32(type_sys + 0x1A8u);
    if (st < 0x10000u || st >= 0x4F000000u) return;
    uint32_t av = vm_read32(st + 0x10u);
    uint32_t rem_body = vm_read32(type_sys + 0x1D4u);
    uint32_t need = rem_body ? rem_body : 0x20u;
    /* Always top-up if less than one header (0x20) remains readable. */
    if (av < 0x20u || (need && av < need)) {
        uint32_t want = need ? need : 0x1000u;
        if (want < 0x20u) want = 0x20u;
        f2b_stream_fill(st, want);
        { static int _n=0; if(_n++<200){
            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-ENSURE ts=0x%08X st=0x%08X was_av=%u rem1D4=0x%X -> av=%u file_pos=%u/%u\n",
              type_sys, st, av, rem_body, vm_read32(st+0x10u),
              g_f2b_fill_file_pos, g_f2b_fill_sz);
            fflush(stderr); } }
    }
}
/* When the F2B FO is fully read (file_pos>=size) and type_sys rem is 0 but
 * state is stuck in 2/3 with a residual body1D4, force idle so
 * func_002BA76C can take the finish path (r3=0) and 00041D5C exits.
 * Without this: sample B71B8→41D5C→B367C yield forever (state=3 rem=0
 * body1D4=0x23DB43D0 after R_Perm FULL).
 *
 * Same-class audit 2026-07-22 — do NOT fire on clean FO end (body1D4==0):
 * R_LglScA (3KiB) reaches file_pos==size with state∈{2} rem==0 body==0 after
 * members expand; forcing idle there aborts before R_PermA open. The real hang
 * always had non-zero residual body1D4. Also require ring avail==0. */
void f2b_stream_eof_try_complete(uint32_t type_sys) {
    if (!type_sys || type_sys < 0x10000u || !g_f2b_fill_sz) return;
    if (g_f2b_fill_file_pos < g_f2b_fill_sz) return;
    uint32_t rem = vm_read32(type_sys + 0x1ACu);
    uint32_t state = vm_read32(type_sys + 0x1CCu);
    uint32_t body = vm_read32(type_sys + 0x1D4u);
    if (state != 2u && state != 3u) return;
    uint32_t st = vm_read32(type_sys + 0x1A8u);
    uint32_t av = 0u;
    if (st >= 0x10000u && st < 0x4F000000u)
        av = vm_read32(st + 0x10u);
    if (av != 0u) return;
    /* Classic residual body; also R_Perm-sized FO stuck in state 2/3 with
     * rem=body=0 (guest finish never fires after residual EOF-QUIET). R_Lgl
     * is 3KiB — size gate keeps its clean path. */
    int classic = (rem == 0u && body != 0u);
    int large_fo = (g_f2b_fill_sz > 0x100000u);
    if (!classic && !(large_fo && rem == 0u)) {
        if (!(large_fo && rem != 0u)) return;
    }
    vm_write32(type_sys + 0x1D4u, 0u);
    vm_write32(type_sys + 0x1ACu, 0u);
    vm_write32(type_sys + 0x1CCu, 0u); /* idle — not 1/2/3 */
    g_wadld_eof_ea = type_sys; /* drained: WAD-wait (func_002BA808/BA824) may now complete via timeout path */
    { static int _n=0; if(_n++<16){
        fprintf(stderr,"[FIOSOPEN] F2B-STREAM-EOF-DONE ts=0x%08X was_state=%u body1D4=0x%X rem=0x%X file_pos=%u/%u → state=0\n",
          type_sys, state, body, rem, g_f2b_fill_file_pos, g_f2b_fill_sz);
        fflush(stderr); } }
}
/* Shared F2B pre-hook: refill + clamp need to avail. Used by 002E1480/1228/11EC/1290. */
void f2b_stream_pre_consume(ppu_context* ctx) {
    uint32_t _st = (uint32_t)ctx->gpr[3];
    uint32_t _need = (uint32_t)ctx->gpr[4];
    if (!_need || !g_f2b_fill_mfd || _st < 0x10000u || _st >= 0x4F000000u) return;
    uint32_t _av = vm_read32(_st + 0x10u);
    if (_av < _need) f2b_stream_fill(_st, _need);
    _av = vm_read32(_st + 0x10u);
    if (_av > 0 && _need > _av) {
        { static int _n=0; if(_n++<48)
            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-CLAMP need=0x%X -> avail=0x%X st=0x%08X file_pos=%u op=pre\n",
              _need, _av, _st, g_f2b_fill_file_pos); }
        ctx->gpr[4] = _av;
    } else if (_av == 0 && _need > 0) {
        { static int _n=0; if(_n++<16)
            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-EMPTY need=0x%X st=0x%08X file_pos=%u/%u\n",
              _need, _st, g_f2b_fill_file_pos, g_f2b_fill_sz); }
        ctx->gpr[4] = 0; /* do not underflow avail */
    }
}
