/* gow2_midasm_hooks.cpp -- corpos dos mid-asm hooks HOST do GoW2.
 *
 * Ver o cabecalho de gow2_midasm_hooks.h para o contrato do simbolo e para a
 * razao de este ficheiro existir.
 *
 * ESTADO NESTE PLANO (17-03): O PILOTO CE03C TEM CORPO REAL.
 * ----------------------------------------------------------
 * O 17-01 poe o lifter a emitir a chamada, o 17-02 provou que o simbolo
 * RESOLVE no boot_gow2 (nm -> T) com corpo no-op, e este plano poe ca' dentro
 * o bloco que ate' agora era TEXTO INJECTADO por
 * recomp_mid_v2/patch_ce03c_introseq_block.py dentro do lift gitignored.
 *
 * O principio G2 do marco (probes OFF por default = zero mudanca de
 * comportamento) continua a exigir que TODO o hook declarado no TOML tenha
 * corpo, mesmo o nao implementado: sem stub, declarar um hook da' erro de LINK
 * em vez do no-op que se pediu. Regra pratica para quem mexer nisto: uma
 * entrada [[midasm_hook]] nova no games/gow2/config/gow2_recomp.toml sem corpo
 * aqui parte o link -- e' o comportamento desejado (falha alto, nao em
 * silencio), mas o corpo deve entrar no mesmo commit que a entrada do TOML.
 *
 * PORTABILIDADE (G4 -- Windows nao regride): zero tokens Win32 fora de
 * #ifdef _WIN32 -- e nem sequer os NOMES dessa lista aparecem aqui, para um
 * grep de auditoria do G4 sobre este ficheiro sair vazio em vez de sair a
 * apontar para um comentario. A lista vive no CLAUDE.md da raiz.
 *
 * A espera do pump usa `usleep`, como o bloco original. A alternativa portatil
 * `std::this_thread::sleep_for` foi TENTADA e NAO compila neste TU: o
 * ppu_memory.h inclui <stdatomic.h> e o libc++ recusa <atomic> (que <thread> e
 * <chrono> arrastam) depois de <stdatomic.h> antes de C++23 -- erro medido, nao
 * suposto. Ficou <unistd.h>: hoje este ficheiro so' e' compilado pelo
 * build_macos.sh (o build_d3d.sh do Windows nao o toca), e se alguem o
 * acrescentar la' o erro sera' alto e imediato em vez de silencioso.
 */
#include "gow2_midasm_hooks.h"

#include "ppu_memory.h"   /* vm_read32 / vm_write32 / vm_write8 (static inline) */
#include "ps3_trace.h"    /* ps3_trace_emit -- formato G1 "[PS3T] a | b | ..." */

#include <unistd.h>       /* usleep -- ver a nota de portabilidade acima */

#include <cstdint>
#include <cstdio>
#include <cstdlib>        /* getenv -- gate da sonda E1 */
#include <ctime>          /* clock_gettime -- dump por tempo da sonda E1 */

/* GOW2_MIDASM_USED: impede que um linker com dead-strip agressivo deixe cair
 * um hook que ainda nenhum lift chama -- e' exactamente o caso de um lift de
 * producao que ainda nao foi regenerado com --config. Verificado por leitura
 * da linha de link do build_macos.sh: nao passa -dead_strip. O contrafactual
 * (linkar SEM o atributo) NAO foi corrido, por isso o que prova o aceite e' o
 * `nm` registado no SUMMARY, nao esta linha -- o atributo e' cinto e
 * suspensorios. Fora de GCC/clang (ex.: MSVC) expande para nada, para nao
 * partir o Windows. */
#if defined(__GNUC__) || defined(__clang__)
#  define GOW2_MIDASM_USED __attribute__((used))
#else
#  define GOW2_MIDASM_USED
#endif

/* ---------------------------------------------------------------------------
 * Dependencias do runtime e do lift.
 *
 * Nenhuma destas entidades tem header publico no motor (o proprio ppu_loader e
 * varios syscalls declaram-nas localmente com `extern`), por isso sao
 * declaradas aqui -- e' o padrao ja' estabelecido no repositorio, nao uma
 * invencao deste ficheiro.
 * ------------------------------------------------------------------------ */
extern "C" {

/* Giant lock do interpretador PPU. O corpo de uma funcao liftada corre com ele
 * TOMADO; o pump ca' em baixo larga-o durante a espera para nao congelar as
 * mediathreads do FIOS -- exactamente como o bloco original fazia. */
void ppu_giant_lock_acquire(void);
void ppu_giant_lock_release(void);

/* Estado do movie player HLE (libs/video + runtime): EA do hook de EOS sticky,
 * limpeza do overlay do VideoToolbox e reset do temporizador de "done". */
extern uint32_t g_movie_eos_ea;
void movie_vt_clear_overlay_done(void);
void movie_done_timebased_reset(void);

} /* extern "C" */

/* Cross-fragment trampoline pointer -- a MESMA declaracao que o ppu_lifter.py
 * escreve no preambulo de toda a TU liftada (ppu_lifter.py, bloco
 * DRAIN_TRAMPOLINE) e que casa com a definicao do runtime/ppu/ppu_loader.cpp.
 * Em Apple TEM de ser `thread_local` do C++: um `extern "C" __thread` nao pede
 * o wrapper TLS do Itanium (__ZTW...) que as TUs C++ resolvem, e o link
 * morreria em "thread-local wrapper routine for g_trampoline_fn". */
#if defined(_MSC_VER)
extern "C" __declspec(thread) void (*g_trampoline_fn)(void*);
#elif defined(__APPLE__)
extern "C" thread_local void (*g_trampoline_fn)(void*);
#else
extern "C" __thread void (*g_trampoline_fn)(void*);
#endif

/* Funcao liftada do guest: o pump do movie player. Chamar o CODIGO DO PROPRIO
 * JOGO (e nao simular o que ele faz) e' a regra do CLAUDE.md para HLE fiel, e
 * era ja' o que o bloco injectado fazia.
 *
 * LIGACAO: as funcoes liftadas sao emitidas com linkagem C++ (ppu_recomp.h
 * declara `void func_XXXXXXXX(ppu_context* ctx);` sem extern "C"), portanto o
 * simbolo leva o nome do TIPO: __Z13func_002C0508P11ppu_context -- medido com
 * `nm boot_gow2`. Como o typedef do motor e o do lift partilham o MESMO tag
 * `ppu_context`, a decoracao gerada aqui e' identica byte-a-byte a' do chunk
 * liftado. Se um dia os tags divergirem, isto falha no LINK (undefined
 * symbol), que e' a falha alta desejavel -- nunca em silencio. */
void func_002C0508(ppu_context* ctx);

/* Drena os trampolins pendentes depois de uma chamada que possa te-los armado.
 * Replica o macro DRAIN_TRAMPOLINE do preambulo do lift; e' funcao em vez de
 * macro porque aqui nao ha' `ctx` implicito nenhum a esconder. */
static inline void gow2_drain_trampoline(ppu_context* ctx)
{
    while (g_trampoline_fn) {
        void (*tf)(void*) = g_trampoline_fn;
        g_trampoline_fn = 0;
        tf((void*)ctx);
    }
}

/* Guest EA valido para leitura/escrita do movie object. Os mesmos limites do
 * bloco original (o topo de 0x4F000000 e' o do heap do jogo). */
static inline bool gow2_ea_ok(uint32_t ea)
{
    return ea >= 0x10000u && ea < 0x4F000000u;
}

extern "C" {

/* ---------------------------------------------------------------------------
 * Ce03cWaitIdle -- piloto da Fase 17. EA guest 0x000CE03C, hook ANTES da
 * primeira instrucao de func_000CE03C (after_instruction ausente = default).
 *
 * O QUE FAZ (comentario do bloco original, preservado):
 *   Intro idx->2 natural 2nd Play (guest already calls Play here).
 *   Wait-idle only: CE03C often races while st620 != 0 -> SAI CEDO.
 *   Pump until natural MovieStop (st=0); arm +0x714 at st>=10 for EOS.
 *   Clear sticky EOS hook + done timer so re-Play is not poisoned.
 *   No soft-clear/soft-park of st620 (not a pass path). No inject.
 *
 * O QUE **NAO** ESTA' AQUI, E PORQUE (nao e' esquecimento -- e' limite do
 * mecanismo):
 *   O bloco injectado nao era so' este prologo. Envolvia tambem o CORPO
 *   NATURAL da funcao num setjmp/longjmp pad (g_ce03c_play_abort) com
 *   cellVdec_stop_all_for_play_abort() + reconstrucao da freelist do media
 *   object no ramo de abort. Um `[[midasm_hook]]` corre AO LADO de uma
 *   instrucao; nao pode ENVOLVER o corpo da funcao. Migrar essa parte exige
 *   substituir a funcao inteira -- weak override, Fase 19 -- ou um patch
 *   residual minimo so' para o pad. Enquanto isso nao acontecer, um lift
 *   feito com --config tem o wait-idle mas NAO tem o pad de abort: esta'
 *   escrito no SUMMARY do 17-03 e no ledger, e NAO se afirma paridade com as
 *   143 linhas do lift de producao.
 *
 * REGISTOS: o pump chama codigo guest, que mexe nos GPR. O bloco original
 * salvava e repunha SO' r30 (o corpo natural le' `vm_read32(gpr[30])` no
 * fim); r2 (TOC) era deliberadamente deixado como o pump o devolvia. Mantem-se
 * exactamente essa escolha -- divergir dela seria mudar o comportamento do
 * fix medido a pretexto de o migrar.
 * ------------------------------------------------------------------------ */
GOW2_MIDASM_USED
void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx)
{
    /* OFF by default since 2026-09-23. Waiting here for the 1st movie to end
     * made the Santa Monica logo play twice: on RPCS3 the game's 2nd Play stops
     * the 1st before it ever opens the decoder (cellVdecClose(handle=0), one
     * cellVdecStartSeq in the whole intro). Without the wait: one StartSeq, 325
     * pictures, menu reached, WADs 11 s earlier. PS3_CE03C_WAIT_IDLE=1 = old. */
    { static int on = -1;
      if (on < 0) { const char* e = getenv("PS3_CE03C_WAIT_IDLE"); on = (e && e[0] == '1'); }
      if (!on) return; }
    const uint64_t sv_r30 = ctx->gpr[30];

    const uint32_t mv = vm_read32((uint32_t)ctx->gpr[2] - 0x1124u);
    uint32_t st = gow2_ea_ok(mv) ? vm_read32(mv + 0x620u) : 0u;

    if (st != 0u && gow2_ea_ok(mv)) {
        { static int n = 0; if (n++ < 8)
            fprintf(stderr, "[INTROSEQ] CE03C wait-idle 1st movie st620=%u\n", st); }

        for (int i = 0; i < 600; i++) {
            if (st >= 0xAu && vm_read32(mv + 0x714u) == 0u) {
                vm_write32(mv + 0x714u, 1u);
                { static int n = 0; if (n++ < 4)
                    fprintf(stderr, "[INTROSEQ] CE03C arm +0x714 st=%u\n", st); }
            }
            for (int j = 0; j < 32; j++) {
                func_002C0508(ctx);
                gow2_drain_trampoline(ctx);
                st = vm_read32(mv + 0x620u);
                if (st == 0u) break;
            }
            if (st == 0u) break;
            ppu_giant_lock_release();
            usleep(50000);
            ppu_giant_lock_acquire();
            if ((i % 40) == 0) {
                static int n = 0; if (n++ < 16)
                    fprintf(stderr, "[INTROSEQ] CE03C wait tick st620=%u i=%d\n", st, i);
            }
        }

        { static int n = 0; if (n++ < 8)
            fprintf(stderr, "[INTROSEQ] CE03C wait-idle exit st620=%u\n", st); }
    }

    /* O EOS sticky do filme #1 nao pode forcar "done" a meio do re-Play. */
    if (g_movie_eos_ea != 0u) {
        fprintf(stderr, "[INTROSEQ] CE03C clear sticky EOS hook 0x%08X\n",
                g_movie_eos_ea);
        fflush(stderr);
        g_movie_eos_ea = 0u;
    }
    if (gow2_ea_ok(mv)) {
        vm_write8(mv + 0x744u, 0);
        vm_write8(mv + 0x745u, 0);
        vm_write8(mv + 0x746u, 0);
    }
    movie_vt_clear_overlay_done();
    /* Subsume o patch_ce03c_movie_done_reset.py: o temporizador de "done"
     * baseado em tempo tem de ir a zero antes do re-Play natural. */
    movie_done_timebased_reset();

    ctx->gpr[30] = sv_r30;
}

} /* extern "C" */

/* ===========================================================================
 * SONDA E1 -- o walk de tipos da parede 4 (func_0041F700).
 *
 * NAO E' UM FIX. E' OBSERVACAO. Regra 5 do CLAUDE.md e anti-padrao 7 do
 * playbook: uma sonda que altere o fluxo para "passar" e' o mesmo pecado que um
 * CRC bypass. Estes dois hooks:
 *   - NAO escrevem um unico byte na memoria guest;
 *   - NAO tocam em ctx->gpr / cr / ctr / lr;
 *   - NAO chamam codigo guest;
 *   - com PS3_TYPEWALK_TRACE por definir, leem um int cacheado e devolvem.
 *
 * O QUE MEDE (o discriminador da sessao de 2026-08-03, ver
 * docs/re_sessions/2026-08-03-E1-typewalk-trace.md, onde as duas previsoes
 * estao escritas ANTES desta sonda existir):
 *
 *   push 0x0041F700 recorre em tab[low16(filho)] e incrementa o cursor +0xC8
 *   do gestor em que foi chamado; pop 0x0041F924 recorre em tab[subtag(filho)]
 *   e decrementa o dele. Os dois lados escolhem o MESMO gestor?
 *     P1: tab[low16] == tab[subtag] para todos -> o par e' coerente
 *     P2: algum no' da' gestores diferentes    -> um sobe sem descer
 *
 * VOLUME. O rasto tem 27 539 083 iteracoes; uma linha por visita sao ~3 GB de
 * stderr. Tres camadas, e a razao de cada uma:
 *   1) as primeiras TW_HEAD_RAW visitas, uma linha cada -- os nos concretos do
 *      inicio do walk, para poderem ser lidos a olho;
 *   2) UMA linha por CHAVE NOVA (low16, subtag) -- e' isto que responde a'
 *      pergunta. A distribuicao de low16 e' um CONJUNTO de pares distintos, nao
 *      uma sequencia: reportar cada par uma vez e' COMPLETO, nao amostrado;
 *   3) um agregado periodico (contagens + cursores). E' periodico e nao final
 *      DE PROPOSITO: o boot e' morto por kill -TERM (G6), que nao corre
 *      atexit -- um relatorio so' no fim nunca chegaria a sair.
 *
 * CONCORRENCIA: o corpo de uma funcao liftada corre com o giant lock do PPU
 * TOMADO (o mesmo pressuposto do Ce03cWaitIdle acima, que so' o larga de
 * proposito durante o sleep). Por isso os contadores abaixo sao simples. Se
 * esse pressuposto mudar, o pior caso e' uma contagem imprecisa numa sonda de
 * diagnostico -- nunca uma escrita no guest, que nao existe aqui.
 * ======================================================================== */

/* A tabela de tipos medida in-boot (ROADMAP) e lida do ELF em
 * PTR_DAT_0053ef1c. Serve de sonda de CONTROLO C1: o guest carrega-a para r28,
 * e se o que a sonda ve' nao for este valor, a sonda esta' a ler o sitio errado
 * e NADA do que ela diz conta. */
static const uint32_t TW_TAB_EXPECT = 0x00868D48u;

/* w0 medido na parede 1 e reproduzido estaticamente pelo desserializador
 * 0x00254C40 ((3<<16)|1 | 0x40000000). Sonda de CONTROLO C2. */
static const uint32_t TW_W0_KNOWN = 0x40030001u;

enum {
    TW_SLOTS    = 512,   /* potencia de 2; sondagem linear */
    TW_KEYS_MAX = 192,   /* carga < 0,4 -- a sondagem termina sempre */
    TW_HEAD_RAW = 48,    /* linhas cruas da cabeca do walk */
    TW_ROWS_MAX = 24     /* linhas por dump agregado */
};

struct tw_entry {
    uint32_t used;
    uint32_t key;            /* (low16 << 12) | subtag */
    uint32_t low16, subtag;
    unsigned long long count, prune;
    uint32_t w0_first, h_first, own_idx_first;
    uint32_t slot16_first;
    uint32_t hflags_or, hflags_and;
    uint32_t mgr_lo, mgr_st;         /* primeiros observados */
    uint32_t mgr_lo_chg, mgr_st_chg; /* mudaram depois? */
    int32_t  cur_lo_min, cur_lo_max;
    int32_t  cur_st_min, cur_st_max;
    uint32_t flags_mask;             /* bit por nibble de flags visto */
    uint32_t oor;                    /* 1 = idx >= 0x21 ; 2 = idx >= 0x100 */
    /* O no' de lista (r31) e o pai (r30-0x80). A 1a corrida da sonda mostrou
     * 192 377 824 visitas a UM filho com h=0x40 -- para saber se o laco de
     * irmaos esta' a girar sobre o MESMO no' (auto-ligado) e' preciso o
     * endereco do no', nao so' o do filho. */
    uint32_t node_first, node_last, pai_first, pai_w0_first;
    unsigned long long node_repeat;  /* no' igual ao da visita anterior */
};

static tw_entry           g_tw[TW_SLOTS];
static int                g_tw_keys       = 0;
static unsigned long long g_tw_visits     = 0;
static unsigned long long g_tw_enters     = 0;
static unsigned long long g_tw_same       = 0;   /* mgr_lo == mgr_st */
static unsigned long long g_tw_diff       = 0;   /* mgr_lo != mgr_st  <== P2 */
static unsigned long long g_tw_idx_same   = 0;   /* low16 == subtag */
static unsigned long long g_tw_mgr_null   = 0;
static unsigned long long g_tw_w0_known   = 0;   /* C2 */
static unsigned long long g_tw_keys_over  = 0;   /* chaves acima de TW_KEYS_MAX */
static uint32_t           g_tw_tab_first  = 0;   /* C1 */
static unsigned long long g_tw_tab_bad    = 0;   /* C1: r28 != o primeiro visto */
static int32_t            g_tw_this_cur_max = -128;
static int32_t            g_tw_this_cur_min =  127;
static uint32_t           g_tw_node_prev  = 0;   /* r31 da visita anterior */
static unsigned long long g_tw_node_rep   = 0;   /* r31 repetido (auto-laco) */
static unsigned long long g_tw_pops       = 0;   /* 0x0041F924 */
static unsigned long long g_tw_pops_empty = 0;   /* cursor < 0 -> devolve 0 */
static int                g_tw_head       = 0;

static int gow2_typewalk_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_TYPEWALK_TRACE");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}

/* Le' tab[idx]. NAO inventa um valor para um indice que o binario nunca
 * prometeu: 0x00254250 verifica `low16 < 0x21` e o push nao verifica nada, por
 * isso idx >= 0x21 e' lido na mesma mas MARCADO, e idx >= 0x100 nem e' lido. */
static inline uint32_t tw_tab_read(uint32_t tab, uint32_t idx, uint32_t* oor)
{
    if (idx >= 0x100u) { if (oor) *oor |= 2u; return 0u; }
    if (idx >= 0x21u)  { if (oor) *oor |= 1u; }
    const uint32_t ea = tab + idx * 4u;
    return gow2_ea_ok(ea) ? vm_read32(ea) : 0u;
}

/* O cursor +0xC8 e' um char COM SINAL e nasce a -1 (0x0025271C poe 0xFF).
 * -1 e' "vazio", nao e' erro. -128 e' a sentinela desta sonda para "gestor
 * nulo ou EA ilegivel" -- e o gestor sai impresso ao lado, para nao haver
 * ambiguidade. */
static inline int32_t tw_cursor(uint32_t mgr)
{
    if (!gow2_ea_ok(mgr)) return -128;
    return (int32_t)(int8_t)vm_read8(mgr + 0xC8u);
}

static inline void tw_min_max(int32_t v, int32_t* mn, int32_t* mx)
{
    if (v < *mn) *mn = v;
    if (v > *mx) *mx = v;
}

static void tw_emit_key(const tw_entry* e, const char* ev)
{
    ps3_trace_emit(PS3_TS_CHECKPOINTS, "func_0041F700", ev,
        "low16=0x%04X subtag=0x%03X same_idx=%d w0=0x%08X h=0x%08X "
        "mgr_lo=0x%08X mgr_st=0x%08X same_mgr=%d oor=%u "
        "cur_lo=%d..%d cur_st=%d..%d hflags_or=0x%04X hflags_and=0x%04X "
        "slot16=%u own_idx=0x%08X flags_seen=0x%04X n=%llu poda=%llu "
        "no=0x%08X..0x%08X no_rep=%llu pai=0x%08X paiw0=0x%08X pai_st=0x%03X",
        e->low16, e->subtag, (e->low16 == e->subtag) ? 1 : 0,
        e->w0_first, e->h_first, e->mgr_lo, e->mgr_st,
        (e->mgr_lo == e->mgr_st) ? 1 : 0, e->oor,
        (int)e->cur_lo_min, (int)e->cur_lo_max,
        (int)e->cur_st_min, (int)e->cur_st_max,
        e->hflags_or, e->hflags_and, e->slot16_first, e->own_idx_first,
        e->flags_mask, e->count, e->prune,
        e->node_first, e->node_last, e->node_repeat, e->pai_first,
        e->pai_w0_first, (e->pai_w0_first >> 16) & 0xFFFu);
}

static void tw_dump(void)
{
    ps3_trace_emit(PS3_TS_CHECKPOINTS, "func_0041F700", "TYPEWALK_SUM",
        "enters=%llu visits=%llu chaves=%d chaves_over=%llu "
        "same_mgr=%llu diff_mgr=%llu same_idx=%llu mgr_null=%llu "
        "tab=0x%08X tab_ok=%d tab_bad=%llu w0_%08X=%llu "
        "this_cur=%d..%d pops=%llu pops_vazios=%llu no_rep=%llu",
        g_tw_enters, g_tw_visits, g_tw_keys, g_tw_keys_over,
        g_tw_same, g_tw_diff, g_tw_idx_same, g_tw_mgr_null,
        g_tw_tab_first, (g_tw_tab_first == TW_TAB_EXPECT) ? 1 : 0,
        g_tw_tab_bad, TW_W0_KNOWN, g_tw_w0_known,
        (int)g_tw_this_cur_min, (int)g_tw_this_cur_max,
        g_tw_pops, g_tw_pops_empty, g_tw_node_rep);

    /* Top TW_ROWS_MAX por contagem, por seleccao directa com lista de ja'
     * emitidos (empates incluidos -- uma seleccao por "corte" perderia chaves
     * com contagem igual). 192 chaves x 24 passagens e' irrisorio ao lado de
     * 4,2 M visitas entre dumps. */
    int done[TW_ROWS_MAX];
    int ndone = 0;
    for (int r = 0; r < TW_ROWS_MAX; r++) {
        int best = -1;
        for (int i = 0; i < TW_SLOTS; i++) {
            if (!g_tw[i].used) continue;
            int skip = 0;
            for (int d = 0; d < ndone; d++) if (done[d] == i) { skip = 1; break; }
            if (skip) continue;
            if (best < 0 || g_tw[i].count > g_tw[best].count) best = i;
        }
        if (best < 0) break;
        done[ndone++] = best;
        tw_emit_key(&g_tw[best], "TYPEWALK_ROW");
    }
}

static inline int tw_milestone(unsigned long long n)
{
    return (n == 1ULL || n == 100ULL || n == 1000ULL || n == 10000ULL ||
            n == 100000ULL || n == 1000000ULL || n == 10000000ULL ||
            (n & 0x3FFFFFULL) == 0ULL);   /* depois: a cada 2^22 ~ 4,2 M */
}

/* Dump por TEMPO, alem dos marcos por contagem. Sem isto ha' cegueira entre
 * marcos: a 1a corrida desta sonda parou de reportar em visits=1000 e o marco
 * seguinte era 100 000 -- o numero final ficou por saber. O boot e' morto por
 * kill -TERM (G6), que nao corre atexit, por isso o ultimo dump TEM de estar a
 * poucos segundos do fim. Verificado a cada 4096 visitas para o relogio nao
 * entrar no caminho quente. */
static inline int tw_time_due(void)
{
#ifndef _WIN32
    static long last_s = -1;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    if (last_s < 0) { last_s = (long)ts.tv_sec; return 0; }
    if ((long)ts.tv_sec - last_s >= 5) { last_s = (long)ts.tv_sec; return 1; }
#endif
    return 0;
}

extern "C" {

/* ---------------------------------------------------------------------------
 * E3 -- a sonda de CONTROLO. EA guest 0x0041F700, antes da primeira instrucao.
 * Nesse ponto r3 = `this` (o gestor em que o push foi chamado, cujo cursor
 * +0xC8 esta' prestes a ser incrementado) e r4 = param_2 (q do no', ou 0).
 * ------------------------------------------------------------------------ */
GOW2_MIDASM_USED
void gow2_midasm_TypewalkPushEnter(ppu_context* ctx)
{
    if (!gow2_typewalk_on()) return;

    const uint32_t self = (uint32_t)ctx->gpr[3];
    const uint32_t arg  = (uint32_t)ctx->gpr[4];

    g_tw_enters++;

    /* Cursor ANTES do incremento -- e' a profundidade da pilha do push. O
     * limite estrutural esta' em 32: 0x48 + 32*4 == 0xC8, ou seja um push a
     * essa profundidade escreve POR CIMA do proprio byte do cursor. */
    const int32_t cur = tw_cursor(self);
    if (cur != -128) tw_min_max(cur, &g_tw_this_cur_min, &g_tw_this_cur_max);

    if (g_tw_enters == 1ULL) {
        ps3_trace_emit(PS3_TS_CHECKPOINTS, "func_0041F700", "TYPEWALK_ALIVE",
            "hook=enter ea=0x0041F700 this=0x%08X arg=0x%08X cur=%d "
            "argw0=0x%08X",
            self, arg, (int)cur,
            (arg >= 4u && gow2_ea_ok(arg)) ? vm_read32(arg) : 0u);
    }
    /* Os primeiros 200 enters, um por linha: 176 e' o total medido, logo isto
     * e' COMPLETO e nao amostrado. `lr` e' o LR guest (o lift copia-o para r0
     * no prologo, portanto e' o endereco de retorno do CHAMADOR) -- e' o que
     * NOMEIA quem invoca o push, que e' onde o fix vai. */
    if (g_tw_enters <= 200ULL) {
        const uint32_t h = (arg >= 4u) ? (arg - 4u) : 0u;
        ps3_trace_emit(PS3_TS_CHECKPOINTS, "func_0041F700", "TYPEWALK_CALLER",
            "n=%llu lr=0x%08X this=0x%08X arg=0x%08X h=0x%08X w0=0x%08X "
            "cur=%d sent_head=0x%08X",
            g_tw_enters, (uint32_t)ctx->lr, self, arg, h,
            gow2_ea_ok(h) ? vm_read32(h + 4u) : 0u, (int)cur,
            gow2_ea_ok(h) ? vm_read32(h + 0x80u) : 0u);
    }

    if (tw_milestone(g_tw_enters)) {
        ps3_trace_emit(PS3_TS_CHECKPOINTS, "func_0041F700", "TYPEWALK_ENTER",
            "enters=%llu visits=%llu this=0x%08X cur=%d cur_max=%d",
            g_tw_enters, g_tw_visits, self, (int)cur, (int)g_tw_this_cur_max);
    }
}

/* ---------------------------------------------------------------------------
 * A MEDICAO. EA guest 0x0041F78C (`rlwinm r9,r0,2,14,29`), dentro do anel e
 * ANTES da guarda de poda -- por isso a amostra cobre TODOS os filhos
 * visitados, nao so' os despachados.
 * ------------------------------------------------------------------------ */
GOW2_MIDASM_USED
void gow2_midasm_TypewalkPushChild(ppu_context* ctx)
{
    if (!gow2_typewalk_on()) return;

    const uint32_t w0     = (uint32_t)ctx->gpr[0];   /* ja' lido pelo guest */
    const uint32_t low16  = w0 & 0xFFFFu;
    const uint32_t subtag = (w0 >> 16) & 0xFFFu;
    const uint32_t flags  = (w0 >> 28) & 0xFu;
    const uint32_t h      = (uint32_t)ctx->gpr[10];
    const uint32_t tab    = (uint32_t)ctx->gpr[28];
    const uint32_t pai_st = (uint32_t)ctx->gpr[29] & 0xFFFu;
    const int      poda   = (subtag == pai_st) ? 1 : 0;
    const uint32_t node   = (uint32_t)ctx->gpr[31];

    g_tw_visits++;
    if (node == g_tw_node_prev) g_tw_node_rep++;
    g_tw_node_prev = node;

    /* C1 -- a base da tabela e' a que o guest carrega, nao uma constante
     * nossa. Se divergir do valor medido in-boot, a sonda esta' a ler outro
     * sitio e nada do que ela diz conta. */
    if (g_tw_tab_first == 0u) g_tw_tab_first = tab;
    else if (tab != g_tw_tab_first) g_tw_tab_bad++;

    /* C2 -- reproduzir um numero ja' medido antes de confiar no trace. */
    if (w0 == TW_W0_KNOWN) g_tw_w0_known++;
    if (low16 == subtag) g_tw_idx_same++;

    uint32_t key = (low16 << 12) | subtag;
    uint32_t i = (key * 2654435761u) & (uint32_t)(TW_SLOTS - 1);
    while (g_tw[i].used && g_tw[i].key != key) i = (i + 1u) & (uint32_t)(TW_SLOTS - 1);

    tw_entry* e = &g_tw[i];

    if (!e->used) {
        if (g_tw_keys >= TW_KEYS_MAX) {
            /* Tabela cheia. NAO se sobrescreve nada e NAO se finge cobertura:
             * conta-se a parte, e o dump diz quantas ficaram de fora. */
            g_tw_keys_over++;
            e = 0;
        } else {
            uint32_t oor = 0u;
            const uint32_t mgr_lo = tw_tab_read(tab, low16,  &oor);
            const uint32_t mgr_st = tw_tab_read(tab, subtag, &oor);
            const int32_t  cur_lo = tw_cursor(mgr_lo);
            const int32_t  cur_st = tw_cursor(mgr_st);
            const uint32_t hf     = gow2_ea_ok(h) ? vm_read16(h + 8u) : 0xFFFFu;

            e->used = 1u; e->key = key; e->low16 = low16; e->subtag = subtag;
            e->count = 0ULL; e->prune = 0ULL;
            e->w0_first = w0; e->h_first = h;
            e->own_idx_first = gow2_ea_ok(h) ? vm_read32(h + 0x20u) : 0u;
            e->slot16_first  = gow2_ea_ok(h) ? vm_read16(h + 0x0Au) : 0xFFFFu;
            e->hflags_or = hf; e->hflags_and = hf;
            e->mgr_lo = mgr_lo; e->mgr_st = mgr_st;
            e->mgr_lo_chg = 0u; e->mgr_st_chg = 0u;
            e->cur_lo_min = e->cur_lo_max = cur_lo;
            e->cur_st_min = e->cur_st_max = cur_st;
            e->flags_mask = 0u;
            e->oor = oor;
            e->node_first = e->node_last = node;
            e->node_repeat = 0ULL;
            e->pai_first = (uint32_t)ctx->gpr[30] - 0x80u;
            e->pai_w0_first = gow2_ea_ok(e->pai_first)
                            ? vm_read32(e->pai_first + 4u) : 0u;
            /* C2: o w0 do PAI e' onde 0x40030001 (o valor medido na parede 1)
             * pode aparecer -- o do filho nunca o teve nas corridas de base. */
            if (e->pai_w0_first == TW_W0_KNOWN) g_tw_w0_known++;
            g_tw_keys++;

            tw_emit_key(e, "TYPEWALK_KEY");
        }
    }

    if (e) {
        e->count++;
        if (poda) e->prune++;
        e->flags_mask |= (1u << flags);
        if (e->node_last == node) e->node_repeat++;
        e->node_last = node;

        /* Caminho quente: 2 leituras de byte. Os ponteiros dos gestores ficam
         * em cache e sao re-lidos a cada 64 K visitas -- a tabela vive em .bss
         * e pode ser reescrita durante a corrida; assumir que nao muda seria
         * exactamente o tipo de suposicao que esta sonda existe para evitar. */
        tw_min_max(tw_cursor(e->mgr_lo), &e->cur_lo_min, &e->cur_lo_max);
        tw_min_max(tw_cursor(e->mgr_st), &e->cur_st_min, &e->cur_st_max);

        if ((g_tw_visits & 0xFFFFULL) == 0ULL) {
            uint32_t oor = e->oor;
            const uint32_t nlo = tw_tab_read(tab, low16,  &oor);
            const uint32_t nst = tw_tab_read(tab, subtag, &oor);
            if (nlo != e->mgr_lo) e->mgr_lo_chg++;
            if (nst != e->mgr_st) e->mgr_st_chg++;
            e->oor = oor;
            if (gow2_ea_ok(h)) {
                const uint32_t hf = vm_read16(h + 8u);
                e->hflags_or |= hf; e->hflags_and &= hf;
            }
        }

        if (e->mgr_lo == e->mgr_st) g_tw_same++; else g_tw_diff++;
        if (e->mgr_lo == 0u || e->mgr_st == 0u) g_tw_mgr_null++;
    }

    if (g_tw_head < TW_HEAD_RAW) {
        g_tw_head++;
        const uint32_t pai_h = (uint32_t)ctx->gpr[30] - 0x80u;
        uint32_t oor = 0u;
        const uint32_t mgr_lo = tw_tab_read(tab, low16,  &oor);
        const uint32_t mgr_st = tw_tab_read(tab, subtag, &oor);
        ps3_trace_emit(PS3_TS_CHECKPOINTS, "func_0041F700", "TYPEWALK_CHILD",
            "n=%d h=0x%08X w0=0x%08X low16=0x%04X subtag=0x%03X flags=0x%X "
            "pai=0x%08X paiw0=0x%08X pai_subtag=0x%03X poda=%d "
            "mgr_lo=0x%08X mgr_st=0x%08X same_mgr=%d cur_lo=%d cur_st=%d "
            "this=0x%08X hflags=0x%04X slot16=%u own_idx=0x%08X tab=0x%08X no=0x%08X",
            g_tw_head, h, w0, low16, subtag, flags,
            pai_h, gow2_ea_ok(pai_h) ? vm_read32(pai_h + 4u) : 0u, pai_st, poda,
            mgr_lo, mgr_st, (mgr_lo == mgr_st) ? 1 : 0,
            (int)tw_cursor(mgr_lo), (int)tw_cursor(mgr_st),
            (uint32_t)ctx->gpr[3],
            gow2_ea_ok(h) ? vm_read16(h + 8u) : 0xFFFFu,
            gow2_ea_ok(h) ? vm_read16(h + 0x0Au) : 0xFFFFu,
            gow2_ea_ok(h) ? vm_read32(h + 0x20u) : 0u,
            tab, node);
    }

    if (tw_milestone(g_tw_visits)) tw_dump();
    else if ((g_tw_visits & 0xFFFULL) == 0ULL && tw_time_due()) tw_dump();
}

/* ---------------------------------------------------------------------------
 * O POP -- EA guest 0x0041F924 (`vt+0x44`), antes da primeira instrucao.
 * r3 = param_1 = o gestor em que o pop foi chamado; o cursor +0xC8 dele ainda
 * NAO foi decrementado.
 *
 * Porque esta' aqui: a nota de 2026-08-02 mediu **27 539 083 passagens pelo
 * walk contra 2 088 pops**. Contar os dois lados na MESMA corrida e' a unica
 * forma de saber se essa desproporcao e' desta corrida ou daquela -- e o ramo
 * `cursor < 0` do pop devolve 0, o que faz o corpo ler o EA guest 4.
 * ------------------------------------------------------------------------ */
GOW2_MIDASM_USED
void gow2_midasm_TypewalkPop(ppu_context* ctx)
{
    if (!gow2_typewalk_on()) return;

    const uint32_t self = (uint32_t)ctx->gpr[3];
    const int32_t  cur  = tw_cursor(self);

    g_tw_pops++;
    if (cur < 0 && cur != -128) g_tw_pops_empty++;

    if (g_tw_pops == 1ULL || tw_milestone(g_tw_pops)) {
        ps3_trace_emit(PS3_TS_CHECKPOINTS, "func_0041F924", "TYPEWALK_POP",
            "pops=%llu vazios=%llu this=0x%08X cur=%d enters=%llu visits=%llu",
            g_tw_pops, g_tw_pops_empty, self, (int)cur,
            g_tw_enters, g_tw_visits);
    }
}

} /* extern "C" */

/* ===========================================================================
 * SONDA E3 -- a contagem que decide D-11.2 (Fase 11: fix A no desserializador
 * 0x00254C40 vs. fix B no walk exterior 0x0041FF70).
 *
 * NAO E' UM FIX. E' OBSERVACAO -- a mesma disciplina da SONDA E1 acima:
 *   - NAO escreve um unico byte na memoria guest;
 *   - NAO toca em ctx->gpr / cr / ctr / lr;
 *   - NAO chama codigo guest;
 *   - com PS3_OUTER_SUBTAG_CENSUS por definir, le' um int cacheado e devolve.
 *
 * O QUE MEDE (o discriminador da sessao de 2026-08-04, ver
 * docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md, onde as duas
 * previsoes estao escritas ANTES deste hook existir):
 *
 *   Em func_0041FF70 (o walk exterior), no EA 0x0042003C -- `lwz r27,
 *   0x88(r28)` -- o wrapper de registo em ctx->gpr[28] ("piVar8" no C do
 *   Ghidra) JA' passou os dois filtros de flags (flags&0x10 em 0x42000C,
 *   flags&3 em 0x420018) e o gate "tem filho" (0x420030/0x420038). Quantos
 *   desses wrappers, nesta corrida, tem subtag != 1?
 *     == 1 (so' o registo WAD conhecido) -> candidato B (D-11.1/D-11.2)
 *      > 1                                -> a pergunta reabre para A
 *
 * VOLUME: a E1 mediu apenas 2 objectos a alcancar este ponto por corrida (o
 * walk exterior enumera muito menos do que o walk interior que ele chama).
 * Por isso um pequeno conjunto (cap OSC_KEYS_MAX pares distintos) chega --
 * nao e' precisa a tabela de 512 slots com sondagem linear da E1 -- e o
 * resumo pode sair em CADA chamada em vez de periodico por tempo: o custo e'
 * irrisorio e evita a janela de cegueira entre marcos que a E1 teve de
 * resolver com tw_time_due(). O boot e' morto por kill -TERM (G6), que nao
 * corre atexit -- por isso o ultimo resumo tem de estar a' distancia de UMA
 * chamada do fim, nao de um marco por tempo.
 *
 * CONCORRENCIA: mesma nota da SONDA E1 -- o corpo de uma funcao liftada corre
 * com o giant lock do PPU TOMADO, por isso os contadores abaixo sao simples.
 * ======================================================================== */

enum { OSC_KEYS_MAX = 16 };   /* cap de pares (low16,subtag) distintos */

struct osc_key { uint32_t low16, subtag; };

static osc_key             g_osc_keys[OSC_KEYS_MAX];
static int                 g_osc_nkeys       = 0;
static unsigned long long  g_osc_calls       = 0;   /* total de chamadas ao hook */
static unsigned long long  g_osc_match_total = 0;   /* D-11.2: subtag != 1 */
static unsigned long long  g_osc_w0_known    = 0;   /* C-known: w0 == TW_W0_KNOWN */
static uint32_t            g_osc_tab_first   = 0;   /* C1 */
static unsigned long long  g_osc_tab_bad     = 0;   /* C1: gpr[24] mudou entre chamadas */

static int gow2_outer_subtag_census_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_OUTER_SUBTAG_CENSUS");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}

/* Resumo por CHAMADA (nao periodico -- ver a nota de VOLUME acima). Reusa as
 * constantes de controlo TW_TAB_EXPECT/TW_W0_KNOWN ja' declaradas para a
 * SONDA E1: e' o MESMO tab base (0x00868D48, mesmo TOC -0x225C(r2), so' que
 * lido para r24 aqui em vez de r28 no push interior) e o MESMO w0 patologico
 * (0x40030001) medido pela E1 -- nao se re-deriva o que ja' esta' medido. */
static void osc_dump(void)
{
    ps3_trace_emit(PS3_TS_CHECKPOINTS, "func_0041FF70", "OUTER_SUBTAG_CENSUS_SUM",
        "calls=%llu match=%llu keys=%d w0_%08X=%llu tab=0x%08X tab_ok=%d tab_bad=%llu",
        g_osc_calls, g_osc_match_total, g_osc_nkeys, TW_W0_KNOWN, g_osc_w0_known,
        g_osc_tab_first, (g_osc_tab_first == TW_TAB_EXPECT) ? 1 : 0, g_osc_tab_bad);
}

extern "C" {

/* ---------------------------------------------------------------------------
 * EA guest 0x0042003C, ANTES da instrucao `lwz r27, 0x88(r28)`. Nesse ponto
 * ctx->gpr[28] = piVar8 (o wrapper de registo, ja' filtrado por flags+gate
 * "tem filho") e ctx->gpr[24] = tab (a base da tabela de tipos, carregada em
 * 0x0041FF7C e viva ate' aqui -- C1).
 * ------------------------------------------------------------------------ */
GOW2_MIDASM_USED
void gow2_midasm_OuterWalkSubtagCensus(ppu_context* ctx)
{
    if (!gow2_outer_subtag_census_on()) return;

    g_osc_calls++;

    /* C1 -- a base da tabela e' a que o guest carrega, nao uma constante
     * nossa. Se divergir do primeiro valor visto, a sonda esta' a ler outro
     * sitio e nada do que ela diz conta. */
    const uint32_t tab = (uint32_t)ctx->gpr[24];
    if (g_osc_tab_first == 0u) g_osc_tab_first = tab;
    else if (tab != g_osc_tab_first) g_osc_tab_bad++;

    const uint32_t piVar8 = (uint32_t)ctx->gpr[28];

    /* C-alive -- sem esta linha, um silencio nao distingue "nao passou aqui"
     * de "a sonda nao foi compilada". */
    if (g_osc_calls == 1ULL) {
        ps3_trace_emit(PS3_TS_CHECKPOINTS, "func_0041FF70", "OUTER_SUBTAG_ALIVE",
            "hook=before_0042003C tab=0x%08X piVar8=0x%08X", tab, piVar8);
    }

    /* Guarda de leitura: piVar8+0x88 e' exactamente o que a instrucao
     * seguinte (0x0042003C) vai carregar em r27 -- o hook LE' o mesmo valor,
     * sem escrever em ctx->gpr[27] nem alterar o que o guest faz a seguir. */
    if (gow2_ea_ok(piVar8 + 0x88u)) {
        const uint32_t h_child = vm_read32(piVar8 + 0x88u);

        if (gow2_ea_ok(h_child + 4u)) {
            const uint32_t w0     = vm_read32(h_child + 4u);
            const uint32_t low16  = w0 & 0xFFFFu;
            const uint32_t subtag = (w0 >> 16) & 0xFFFu;

            /* C-known -- reproduzir um numero ja' medido pela E1 antes de
             * confiar no que esta sonda diz de novo. */
            if (w0 == TW_W0_KNOWN) g_osc_w0_known++;

            /* D-11.2 -- o discriminador em si: piVar8 ja' passou os dois
             * filtros de flags e o gate "tem filho" (garantido pela ancora
             * em 0x0042003C); resta perguntar se subtag != 1. */
            if (subtag != 1u) {
                g_osc_match_total++;

                int found = -1;
                for (int i = 0; i < g_osc_nkeys; i++) {
                    if (g_osc_keys[i].low16 == low16 && g_osc_keys[i].subtag == subtag) {
                        found = i;
                        break;
                    }
                }
                if (found < 0 && g_osc_nkeys < OSC_KEYS_MAX) {
                    g_osc_keys[g_osc_nkeys].low16  = low16;
                    g_osc_keys[g_osc_nkeys].subtag = subtag;
                    g_osc_nkeys++;
                    ps3_trace_emit(PS3_TS_CHECKPOINTS, "func_0041FF70", "OUTER_SUBTAG_MATCH",
                        "w0=0x%08X h_child=0x%08X low16=0x%04X subtag=0x%03X "
                        "piVar8=0x%08X n=%llu tab=0x%08X",
                        w0, h_child, low16, subtag, piVar8, g_osc_match_total, tab);
                }
            }
        }
    }

    osc_dump();
}

} /* extern "C" */
