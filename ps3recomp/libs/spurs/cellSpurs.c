/*
 * ps3recomp - cellSpurs HLE implementation
 *
 * Provides the SPURS management API so games can call SPU task/workload
 * functions without crashing.  Full SPU execution requires recompiling
 * SPU programs; this layer provides the scheduling and management APIs.
 *
 * Tasks and workloads are tracked.  If a game provides PPU fallback
 * callbacks, those can be invoked through the task submission path.
 */

#include "cellSpurs.h"
int wp_stats_on(void);   /* winport release: WINPORT_STATS=1 logs the per-second statistics (win_compat.c) */
static void spurs_task_tick_start(void);
void spurs_task_kick_all(void);
#include "spu_workload.h"   /* SPU image -> lifted-entry dispatch (runtime/spu) */
#include "spurs_kernel.h"   /* SPURS HLE kernel: async workload execution (M2.5/3) */
#include "ps3_trace.h"      /* Task 4 (bringup-instrumentacao-fase0): subsistema SPURS */
#include <stdio.h>
#include <string.h>
#include <stddef.h>   /* offsetof */

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#include <stdlib.h>   /* getenv: os gates PS3_* deste modulo */
#endif

/* ---------------------------------------------------------------------------
 * Internal workload tracking
 * -----------------------------------------------------------------------*/

typedef struct {
    int         in_use;
    const void* pm;
    u32         sizePm;
    u64         data;
    u8          priority[CELL_SPURS_MAX_SPU];
    u32         minContention;
    u32         maxContention;
    u32         readyCount;
    int         fp_checked;      /* E183: fingerprint do PM ja' confrontado com o registo */
    int         fp_registered;
} SpursWorkload;

typedef struct {
    int         in_use;
    u32         id;
    int         active;
    int         completed;
    s32         exitCode;
    void*       entryPoint;   /* host ptr to the task's SPU ELF image */
    u32         elfSize;      /* image size (for re-dispatch)         */
    u64         argA;         /* task-argument guest EA               */
    u32         tasksetEa;    /* guest EA of the SDK-layout taskset   */
    u32         tasksetId;    /* index within the taskset (0..127)    */
    volatile long running;    /* a worker is executing this task now  */
    volatile long pending;    /* a kick arrived while running: re-run */
    volatile long queued;     /* an entry for this task sits in the kernel queue */
} SpursTask;

/* SDK CellSpursTaskset layout offsets (RPCS3 cellSpurs.h): the guest struct
 * is shared with the SPU task library, which reads it via the LS 0x2700
 * context window and raw DMA -- so the guest memory must hold the REAL
 * layout, not an HLE-private one. HLE bookkeeping lives in a side table. */
#define SDK_TS_RUNNING     0x00   /* 128-bit task bitmasks: bit i = (1<<31)>>(i%32) em values[i/32] */
#define SDK_TS_READY       0x10
#define SDK_TS_PENDING     0x20
#define SDK_TS_ENABLED     0x30
#define SDK_TS_SIGNALLED   0x40
#define SDK_TS_WAITING     0x50
#define SDK_TS_SPURS       0x60   /* u64 spurs EA   */
#define SDK_TS_ARGS        0x68   /* u64 args       */
#define SDK_TS_WID         0x74
#define SDK_TS_TASKINFO    0x80   /* TaskInfo[128] × 48B: args16, elf u64, ctxsave u64, lspattern16 */
#define SDK_TS_SIZE_FIELD  0x1890
#define SDK_TS_BYTES       6400

typedef struct {
    u32 ea;          /* guest EA of the taskset (0 = free slot) */
    u32 next_id;     /* next task index to hand out             */
} TasksetSide;
#define MAX_TASKSETS 16
static TasksetSide s_taskset_side[MAX_TASKSETS];

static TasksetSide* taskset_side(u32 ea, int alloc)
{
    for (int i = 0; i < MAX_TASKSETS; i++)
        if (s_taskset_side[i].ea == ea) return &s_taskset_side[i];
    if (!alloc) return NULL;
    for (int i = 0; i < MAX_TASKSETS; i++)
        if (!s_taskset_side[i].ea) {
            s_taskset_side[i].ea = ea;
            s_taskset_side[i].next_id = 0;
            return &s_taskset_side[i];
        }
    return NULL;
}

static void sdk_wr32(unsigned char* p, u32 v)
{ p[0]=(unsigned char)(v>>24); p[1]=(unsigned char)(v>>16); p[2]=(unsigned char)(v>>8); p[3]=(unsigned char)v; }
static void sdk_wr64(unsigned char* p, u64 v)
{ sdk_wr32(p, (u32)(v>>32)); sdk_wr32(p+4, (u32)v); }

/* Set task bit i in a 128-bit taskset mask (SDK bit order). */
static void sdk_set_task_bit(unsigned char* mask, u32 id)
{
    unsigned char* w = mask + 4u * (id / 32);
    u32 v = ((u32)w[0]<<24)|((u32)w[1]<<16)|((u32)w[2]<<8)|w[3];
    v |= (0x80000000u >> (id % 32));
    sdk_wr32(w, v);
}

/* Global workload table (per-SPURS instance in a real system, simplified) */
static SpursWorkload s_workloads[CELL_SPURS_MAX_WORKLOAD];
static SpursTask     s_tasks[CELL_SPURS_MAX_TASK];
static u32           s_next_task_id = 0;

/* ---------------------------------------------------------------------------
 * Event flag sync side table
 *
 * We can't embed OS handles in CellSpursEventFlag (game code controls its
 * layout), so we keep a small side table that maps event flag pointers to
 * their host mutex + condition variable.
 * -----------------------------------------------------------------------*/
#define MAX_EVENT_FLAGS 64

typedef struct {
    CellSpursEventFlag* ef;
#ifdef _WIN32
    CRITICAL_SECTION    cs;
    CONDITION_VARIABLE  cv;
#else
    pthread_mutex_t     mtx;
    pthread_cond_t      cond;
#endif
    int                 initialized;
} EventFlagSync;

static EventFlagSync s_ef_sync[MAX_EVENT_FLAGS];

/* The generic HLE adapter passes raw GUEST addresses; dereferencing one as a
 * host pointer faults (observed: write fault at 0x43008120 in
 * _cellSpursEventFlagInitialize once the v2 lift let GoW2 reach it). Translate
 * guest->host idempotently: vm_base allocations sit above 4 GB, so any value
 * below 4 GB is a guest address. */
static inline void* spurs_p2h(void* p)
{
    extern unsigned char* vm_base;
    if (p && (uintptr_t)p < 0x100000000ull)
        return vm_base + (uintptr_t)p;
    return p;
}
#define EF_P2H(ef)   ((CellSpursEventFlag*)spurs_p2h(ef))

static EventFlagSync* ef_sync_find(CellSpursEventFlag* ef)
{
    for (int i = 0; i < MAX_EVENT_FLAGS; i++) {
        if (s_ef_sync[i].initialized && s_ef_sync[i].ef == ef)
            return &s_ef_sync[i];
    }
    return NULL;
}

static EventFlagSync* ef_sync_alloc(CellSpursEventFlag* ef)
{
    for (int i = 0; i < MAX_EVENT_FLAGS; i++) {
        if (!s_ef_sync[i].initialized) {
            s_ef_sync[i].ef = ef;
            s_ef_sync[i].initialized = 1;
#ifdef _WIN32
            InitializeCriticalSection(&s_ef_sync[i].cs);
            InitializeConditionVariable(&s_ef_sync[i].cv);
#else
            pthread_mutex_init(&s_ef_sync[i].mtx, NULL);
            pthread_cond_init(&s_ef_sync[i].cond, NULL);
#endif
            return &s_ef_sync[i];
        }
    }
    return NULL;
}

static void ef_sync_free(EventFlagSync* sync)
{
    if (!sync) return;
#ifdef _WIN32
    DeleteCriticalSection(&sync->cs);
    /* CONDITION_VARIABLE has no destroy on Windows */
#else
    pthread_mutex_destroy(&sync->mtx);
    pthread_cond_destroy(&sync->cond);
#endif
    sync->ef = NULL;
    sync->initialized = 0;
}

static inline void ef_lock(EventFlagSync* s)
{
#ifdef _WIN32
    EnterCriticalSection(&s->cs);
#else
    pthread_mutex_lock(&s->mtx);
#endif
}

static inline void ef_unlock(EventFlagSync* s)
{
#ifdef _WIN32
    LeaveCriticalSection(&s->cs);
#else
    pthread_mutex_unlock(&s->mtx);
#endif
}

static inline void ef_wait(EventFlagSync* s)
{
#ifdef _WIN32
    SleepConditionVariableCS(&s->cv, &s->cs, INFINITE);
#else
    pthread_cond_wait(&s->cond, &s->mtx);
#endif
}

static inline void ef_broadcast(EventFlagSync* s)
{
#ifdef _WIN32
    WakeAllConditionVariable(&s->cv);
#else
    pthread_cond_broadcast(&s->cond);
#endif
}

/* =========================================================================
 * SPURS core
 * =====================================================================*/

/* SPURS kernel per-workload dispatch: run the policy module's SPU image if a
 * lifted build is registered for it (MISS -> inert, as before). Runs on the
 * kernel thread, off the PPU caller. */
#ifdef _WIN32
#define task_cas(p, nv, ov)  InterlockedCompareExchange((volatile LONG*)(p), (nv), (ov))
#define task_xchg(p, nv)     InterlockedExchange((volatile LONG*)(p), (nv))
#define task_barrier()       MemoryBarrier()
#else
#define task_cas(p, nv, ov)  __sync_val_compare_and_swap((p), (ov), (nv))
#define task_xchg(p, nv)     __sync_lock_test_and_set((p), (nv))
#define task_barrier()       __sync_synchronize()
#endif

/* Os tres campos de estado da task (running/pending/queued) sao tocados pelo
 * worker do SPURS, pela thread de tick e pela thread guest que faz o kick, e
 * estavam apenas `volatile`: so' `running` usava CAS de verdade. Volatile nao
 * e' atomico -- o ThreadSanitizer apanhou 15 relatorios so' no store de
 * `pending` (spurs_task_kick vs spurs_pm_dispatch sobre o mesmo s_tasks[i]),
 * e isto acontece HOJE com PS3_CONCURRENCY=1, porque os workers do SPURS
 * correm FORA do giant lock. SEQ_CST mantem a ordem que os comentarios deste
 * ficheiro assumem (registar o kick ANTES de olhar o estado). */
#if defined(_MSC_VER)
#  define task_ld(p)     (*(p))
#  define task_st(p, v)  (*(p) = (v))
#else
#  define task_ld(p)     __atomic_load_n((p), __ATOMIC_SEQ_CST)
#  define task_st(p, v)  __atomic_store_n((p), (v), __ATOMIC_SEQ_CST)
#endif

static SpursTask* spurs_task_by_id(u32 id)
{
    for (u32 i = 0; i < CELL_SPURS_MAX_TASK; i++)
        if (s_tasks[i].in_use && s_tasks[i].id == id)
            return &s_tasks[i];
    return NULL;
}

/* Arranque do kernel a partir do host.
 *
 * Os handlers ctx de runtime/ppu/ppu_sysprx.cpp ganham o dispatch por NID
 * (ps3_hle_call consulta a tabela ctx primeiro e retorna), portanto as entradas
 * cellSpursInitialize* deste ficheiro NAO correm para o GoW2 -- e com elas nunca
 * corria o spurs_kernel_start abaixo. Expor o arranque aqui mantem a politica de
 * dispatch num sitio so e deixa o handler que realmente corre subir o kernel.
 */
void spurs_kernel_start_default(u32 nSpus);

/* readyCount semantics (PS3): title ReadyCountStore(N) means "N work units
 * ready for SPU". The policy module / SPU side consumes them as jobs complete.
 * GoW2 post-AUTO_LOAD waits in func_00454C60 until GetWorkloadInfo reports
 * readyCount==0 (func_002F2C20 / 00455BD4 invert). If we leave readyCount>0
 * forever (no consume), that loop + lv2_syscall spins and st620 never moves. */
static void spurs_ready_consume(u32 wid)
{
    if (wid >= CELL_SPURS_MAX_WORKLOAD) return;
    if (!s_workloads[wid].in_use) return;
    if (s_workloads[wid].readyCount > 0)
        s_workloads[wid].readyCount--;
}

/* A task may be kicked while its current SPU slice is running.  Keep that
 * rerun asynchronous: running more slices inline here can monopolize every
 * host worker and starve policy workloads (notably GoW2's SCREAM mixer). */
static void spurs_task_kick(SpursTask* t);

static void spurs_pm_dispatch(const spurs_workload_job* job, void* user)
{
    (void)user;
    if (!job->pm || !job->sizePm) {
        /* Empty / MISS job: still consume one ready unit so waiters unblock. */
        if (!job->is_task)
            spurs_ready_consume(job->wid);
        return;
    }

    /* A SPURS task's lifted job is not re-entrant (it owns its LS and its
     * shared control blocks); with several kernel workers, a kick storm could
     * run the same task concurrently with itself (observed: rip=0 crashes).
     * Serialize per task: overlapping kicks fold into one deferred re-run. */
    SpursTask* t = job->is_task ? spurs_task_by_id(job->wid) : NULL;
    if (t) {
        /* A entrada saiu da fila: a partir de agora um kick novo pode voltar a
         * enfileirar esta task. Tem de vir ANTES do dispatch -- se so se
         * limpasse no fim, um job longo (ou que nunca termina) mantinha a task
         * marcada como "ja em fila" e os kicks que chegassem entretanto eram
         * dobrados na entrada errada, esta, que ja esta a ser consumida. */
        task_st(&t->queued, 0);
        task_barrier();

        if (task_cas(&t->running, 1, 0) != 0) {
            /* Outro worker ja corre esta task: o kick dobra-se no ciclo dele.
             * Nao ha nada a executar aqui -- e sobretudo nao aqui INLINE. */
            task_st(&t->pending, 1);
            return;
        }
        /* One SPU dispatch is one scheduling quantum.  Do not consume a
         * pending kick in an inner loop: persistent tasks can immediately
         * re-kick themselves, which otherwise keeps all workers inside this
         * function forever and prevents queued policy modules from running. */
        task_st(&t->pending, 0);
        task_barrier();
        PS3_TRACE(PS3_TS_SPURS, "spurs_pm_dispatch", "TASK_START",
                  "task_id=%u entry=0x%08X", job->wid, (unsigned)(uintptr_t)job->pm);
        spu_workload_dispatch_task((const uint8_t*)job->pm, job->sizePm,
                                   (uint32_t)job->data, job->taskset_ea,
                                   job->task_id);
        PS3_TRACE(PS3_TS_SPURS, "spurs_pm_dispatch", "TASK_END",
                  "task_id=%u entry=0x%08X", job->wid, (unsigned)(uintptr_t)job->pm);

        /* Close the hand-off window before re-queuing.  A kick that landed
         * while running either already owns a queued entry or remains in
         * pending; spurs_task_kick folds the former and submits the latter. */
        task_st(&t->running, 0);
        task_barrier();
        if (task_xchg(&t->pending, 0))
            spurs_task_kick(t);
        return;
    }

    spu_workload_dispatch((const uint8_t*)job->pm, job->sizePm,
                          (uint32_t)job->data);
    /* Workload policy-module job finished once (Marco 3): consume one ready. */
    spurs_ready_consume(job->wid);
}

/* Kick de uma task: garante que ela volta a CORRER, sem nunca a executar na
 * thread que chama. E' o unico sitio por onde uma task entra em execucao
 * (CreateTask, kick_all da LFQueue e SendSignal passam todos por aqui).
 *
 * Contrato da consola: o SPU corre em paralelo com o PPU, portanto um kick e'
 * uma NOTIFICACAO ("ha dados novos, volta a olhar"), nao um pedido sincrono. O
 * PPU deposita, avisa e segue para o frame seguinte.
 *
 * Duas propriedades, e as duas sao precisas:
 *
 *  1. COALESCING -- no maximo UMA entrada em fila por task. N kicks para uma
 *     task que ja esta em fila (ou a correr) dobram-se num so, que e'
 *     exactamente o que um SPU real ve: ele nao precisa de N acordares, precisa
 *     de saber que ha trabalho quando fechar o ciclo. Sem isto, o kick_all
 *     enfileirava uma entrada POR TASK a CADA push de LFQueue (por frame): com
 *     os workers presos num job que nao termina nada era consumido, a ring de
 *     256 enchia, e a partir dai TODOS os submits falhavam.
 *
 *  2. SEM INLINE -- submit falhado nunca cai para executar o job aqui. Era isso
 *     que transformava "SPU ocupado" em "PPU inteiro congelado" (medido com
 *     sample: 3 frames vivos de spu_run_lifted_job_ex, os 2 workers mais a
 *     thread principal). O trabalho nao se perde porque o kick fica registado
 *     em t->pending: se a task corre agora, o ciclo do worker torna a corre-la;
 *     se nao, o kick seguinte (ha um por push/frame) volta a tentar a fila.
 *
 * O inline SO se mantem quando nao existe executor NENHUM -- kernel em baixo,
 * ou PS3_SPU_SYNC=1 para A/B. Ai deixar cair seria perder trabalho de verdade.
 */
/* ---- winport v76-spurs-kick (engine_fixes_v76_spurs_kick.py) ---- */
static int wp76_kick_run(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("WINPORT_SPURS_KICK_RUN");
        v = (e && e[0] == '1') ? 1 : 0;
        if (v) { fprintf(stderr, "[SPURS76] kick of a RUNNING task -> pending flag only (no kernel submit / wake)\n"); fflush(stderr); }
    }
    return v;
}
extern int  wp76_kernel_stats_on(void);              /* spurs_kernel.c */
extern void wp76_kernel_take(long long out[4]);      /* spurs_kernel.c: submit, wakes, spin-hit, spin-miss */
static volatile long long g_wp76_kick[5];            /* kicks, run-skip, folded, submitted, ring-full */
static void wp76_kick_note(int what)
{
    static int st = -1;
    if (st < 0) { const char* e = getenv("WINPORT_SPURS_STATS76");
                  st = (wp76_kick_run() || wp76_kernel_stats_on() || (e && e[0] == '1')) ? 1 : 0; }
    if (!st) return;
    __atomic_add_fetch(&g_wp76_kick[0], 1ll, __ATOMIC_RELAXED);
    __atomic_add_fetch(&g_wp76_kick[what], 1ll, __ATOMIC_RELAXED);
#ifdef _WIN32
    static volatile long long last = 0;
    const long long now = (long long)GetTickCount64();
    long long l = __atomic_load_n(&last, __ATOMIC_RELAXED);
    if (!l) { __atomic_compare_exchange_n(&last, &l, now, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED); return; }
    if (now - l < 1000) return;
    if (!__atomic_compare_exchange_n(&last, &l, now, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return;
    const double s = (double)(now - l) / 1000.0;
    long long k[5], kk[4];
    for (int i = 0; i < 5; i++) k[i] = __atomic_exchange_n(&g_wp76_kick[i], 0ll, __ATOMIC_RELAXED);
    wp76_kernel_take(kk);
    if (!wp_stats_on()) ; else fprintf(stderr, "[SPURS76] kicks=%.0f/s run-skip=%.0f folded=%.0f submit=%.0f ring-full=%.0f | kernel submit=%.0f wakes=%.0f spin-hit=%.0f spin-miss=%.0f\n",
            k[0] / s, k[1] / s, k[2] / s, k[3] / s, k[4] / s, kk[0] / s, kk[1] / s, kk[2] / s, kk[3] / s);
    fflush(stderr);
#endif
}
/* ---- end winport v76-spurs-kick ---- */
static void spurs_task_kick(SpursTask* t)
{
    if (!t->entryPoint || !t->elfSize) return;

    static int force_sync = -1;
    if (force_sync < 0) force_sync = getenv("PS3_SPU_SYNC") ? 1 : 0;

    /* Registar o kick ANTES de olhar para o estado: quem estiver a correr esta
     * task ve o flag e da mais uma volta. A ordem importa -- se se lesse o
     * estado primeiro, um worker que largasse a task no meio levava o kick
     * consigo. */
    task_st(&t->pending, 1);
    task_barrier();

    if (force_sync || !spurs_kernel_running()) {
        task_st(&t->pending, 0);
        task_barrier();
        /* Task 4: TASK_START/TASK_END, sync path (PS3_SPU_SYNC=1 or kernel not
         * up) -- same "one dispatch slice" caveat as the async path in
         * spurs_pm_dispatch() above. */
        PS3_TRACE(PS3_TS_SPURS, "spurs_task_kick", "TASK_START",
                  "task_id=%u entry=0x%08X", t->id, (unsigned)(uintptr_t)t->entryPoint);
        spu_workload_dispatch_task((const uint8_t*)t->entryPoint, t->elfSize,
                                   (u32)t->argA, t->tasksetEa, t->tasksetId);
        PS3_TRACE(PS3_TS_SPURS, "spurs_task_kick", "TASK_END",
                  "task_id=%u entry=0x%08X", t->id, (unsigned)(uintptr_t)t->entryPoint);
        return;
    }

    /* winport v76-spurs-kick: the task is executing right now -> the pending flag set above is the whole kick; the
     * worker that runs it re-kicks on its way out (running=0; barrier; xchg(pending)). */
    if (wp76_kick_run() && task_ld(&t->running)) { wp76_kick_note(1); return; }
    int fold = (task_cas(&t->queued, 1, 0) != 0);
    int submitted = 0;
    if (!fold) {
        spurs_workload_job job;
        job.pm     = (const void*)t->entryPoint;
        job.sizePm = t->elfSize;
        job.data   = (u32)t->argA;
        job.wid    = t->id;
        job.is_task = 1;
        /* O taskset TEM de ir no job: sem ele o spu_workload_dispatch_task
         * salta o spu_task_ls_setup e a task-lib do GoW2 (spu0 0xE2B0) le o seu
         * global de contexto em LS 0xBEC8 por semear -- mgmt fica lixo e o
         * `bi [mgmt+196]` salta para dentro de codigo. */
        job.taskset_ea = t->tasksetEa;
        job.task_id    = t->tasksetId;
        submitted = spurs_kernel_submit(&job);
        if (!submitted)
            task_st(&t->queued, 0);   /* liberta o slot: o proximo kick torna a tentar */
    }
    wp76_kick_note(submitted ? 3 : (fold ? 2 : 4));   /* winport v76-spurs-kick */

    /* PS3_TRACE_KICK=1: a repartição real dos kicks. Imprime a cada 64, logo a
     * ultima linha e' sempre anterior ao kick que interessa -- ler os totais no
     * fim, nunca concluir "nunca aconteceu" da ultima linha. */
    { static int on = -1; if (on < 0) on = getenv("PS3_TRACE_KICK") ? 1 : 0;
      if (on) { static unsigned ok = 0, folded = 0, full = 0;
          if (submitted) ok++; else if (fold) folded++; else full++;
          if (((ok + folded + full) & 0x3F) == 1)
              fprintf(stderr, "[KICK] submit_ok=%u coalescido=%u fila_cheia=%u\n",
                      ok, folded, full); } }
}

s32 cellSpursInitialize(CellSpurs* spurs, s32 nSpus, s32 spuPriority,
                        s32 ppuPriority, u8 exitIfNoWork)
{
    (void)spuPriority; (void)ppuPriority; (void)exitIfNoWork;

    if (!spurs)
        return CELL_SPURS_CORE_ERROR_NULL_POINTER;

    printf("[cellSpurs] Initialize(nSpus=%d)\n", nSpus);

    memset(spurs, 0, sizeof(CellSpurs));
    spurs->initialized = 1;
    spurs->nSpus = (nSpus > 0 && nSpus <= CELL_SPURS_MAX_SPU)
                   ? (u32)nSpus : 1;

    memset(s_workloads, 0, sizeof(s_workloads));
    /* Materialize the SPURS kernel (the SPU thread group's scheduler): a host
     * thread that runs added workloads asynchronously. See docs/SPURS_BRINGUP. */
    spurs_kernel_start(spurs->nSpus, spurs_pm_dispatch, NULL);
    return CELL_OK;
}

void spurs_kernel_start_default(u32 nSpus)
{
    spurs_kernel_start(nSpus ? nSpus : 1, spurs_pm_dispatch, NULL);
}

s32 cellSpursInitializeWithAttribute(CellSpurs* spurs,
                                     const CellSpursAttribute* attr)
{
    if (!spurs || !attr)
        return CELL_SPURS_CORE_ERROR_NULL_POINTER;

    printf("[cellSpurs] InitializeWithAttribute(nSpus=%u, prefix=\"%.15s\")\n",
           attr->nSpus, attr->prefix);

    memset(spurs, 0, sizeof(CellSpurs));
    spurs->initialized = 1;
    spurs->nSpus = attr->nSpus;
    spurs->flags = attr->flags;
    memcpy(spurs->prefix, attr->prefix, sizeof(spurs->prefix));

    memset(s_workloads, 0, sizeof(s_workloads));
    spurs_kernel_start(spurs->nSpus, spurs_pm_dispatch, NULL);
    return CELL_OK;
}

s32 cellSpursFinalize(CellSpurs* spurs)
{
    if (!spurs)
        return CELL_SPURS_CORE_ERROR_NULL_POINTER;

    if (!spurs->initialized)
        return CELL_SPURS_CORE_ERROR_STAT;

    printf("[cellSpurs] Finalize()\n");

    spurs_kernel_drain(2000);   /* let in-flight workloads finish */
    spurs_kernel_stop();
    memset(s_workloads, 0, sizeof(s_workloads));
    spurs->initialized = 0;
    return CELL_OK;
}

s32 cellSpursAttributeInitialize(CellSpursAttribute* attr, s32 nSpus,
                                 s32 spuPriority, s32 ppuPriority,
                                 u8 exitIfNoWork)
{
    (void)spuPriority; (void)ppuPriority; (void)exitIfNoWork;

    if (!attr)
        return CELL_SPURS_CORE_ERROR_NULL_POINTER;

    memset(attr, 0, sizeof(CellSpursAttribute));
    attr->nSpus = (nSpus > 0 && nSpus <= CELL_SPURS_MAX_SPU)
                  ? (u32)nSpus : 1;

    for (int i = 0; i < CELL_SPURS_MAX_SPU; i++)
        attr->spuPriority[i] = spuPriority;

    printf("[cellSpurs] AttributeInitialize(nSpus=%d)\n", nSpus);
    return CELL_OK;
}

s32 cellSpursAttributeSetNamePrefix(CellSpursAttribute* attr,
                                    const char* prefix, u32 size)
{
    if (!attr)
        return CELL_SPURS_CORE_ERROR_NULL_POINTER;

    if (prefix && size > 0) {
        u32 copyLen = size < sizeof(attr->prefix) ? size : sizeof(attr->prefix) - 1;
        memcpy(attr->prefix, prefix, copyLen);
        attr->prefix[copyLen] = '\0';
        attr->prefixSize = copyLen;
    }

    return CELL_OK;
}

s32 cellSpursAttributeSetSpuThreadGroupType(CellSpursAttribute* attr,
                                            s32 type)
{
    (void)type;
    if (!attr) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    return CELL_OK;
}

s32 cellSpursAttributeEnableSpuPrintfIfAvailable(CellSpursAttribute* attr)
{
    if (!attr) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    return CELL_OK;
}

s32 cellSpursGetNumSpuThread(const CellSpurs* spurs, u32* nThreads)
{
    if (!spurs || !nThreads)
        return CELL_SPURS_CORE_ERROR_NULL_POINTER;

    if (!spurs->initialized)
        return CELL_SPURS_CORE_ERROR_STAT;

    *nThreads = spurs->nSpus;
    return CELL_OK;
}

s32 cellSpursSetMaxContention(CellSpurs* spurs, CellSpursWorkloadId wid,
                              u32 maxContention)
{
    (void)maxContention;

    if (!spurs) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    if (wid >= CELL_SPURS_MAX_WORKLOAD) return CELL_SPURS_CORE_ERROR_INVAL;
    if (!s_workloads[wid].in_use) return CELL_SPURS_CORE_ERROR_SRCH;

    s_workloads[wid].maxContention = maxContention;
    return CELL_OK;
}

s32 cellSpursSetPriorities(CellSpurs* spurs, CellSpursWorkloadId wid,
                           const u8* priorities)
{
    if (!spurs || !priorities) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    if (wid >= CELL_SPURS_MAX_WORKLOAD) return CELL_SPURS_CORE_ERROR_INVAL;
    if (!s_workloads[wid].in_use) return CELL_SPURS_CORE_ERROR_SRCH;

    memcpy(s_workloads[wid].priority, priorities, CELL_SPURS_MAX_SPU);
    return CELL_OK;
}

s32 cellSpursAttachLv2EventQueue(CellSpurs* spurs, u32 queue, u8* port,
                                 s32 isDynamic)
{
    (void)queue; (void)isDynamic;

    if (!spurs || !port) return CELL_SPURS_CORE_ERROR_NULL_POINTER;

    *port = 0; /* give it port 0 */
    printf("[cellSpurs] AttachLv2EventQueue(queue=%u)\n", queue);
    return CELL_OK;
}

s32 cellSpursDetachLv2EventQueue(CellSpurs* spurs, u8 port)
{
    (void)port;
    if (!spurs) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    printf("[cellSpurs] DetachLv2EventQueue(port=%u)\n", port);
    return CELL_OK;
}

/* =========================================================================
 * Taskset
 * =====================================================================*/

s32 cellSpursCreateTaskset(CellSpurs* spurs, CellSpursTaskset* taskset,
                           u64 args, const u8* priority, u32 maxContention)
{
    (void)args; (void)priority; (void)maxContention;

    if (!spurs || !taskset)
        return CELL_SPURS_TASK_ERROR_NULL_POINTER;

    /* Both `spurs` and `taskset` are guest pointers (game-allocated buffers);
     * translate to host pointers before dereferencing. Raw deref of the guest EA
     * faults now that the heap hands out real addresses (e.g. 0x40001100).
     * HLE-internal fields stay host-endian (the game treats these as opaque). */
    extern unsigned char* vm_base;
    CellSpurs* sp = (CellSpurs*)(vm_base + (uintptr_t)spurs);
    CellSpursTaskset* ts = (CellSpursTaskset*)(vm_base + (uintptr_t)taskset);

    if (!sp->initialized) {
        /* GoW2: cellSpursInitialize(WithAttribute) for this instance is
         * observed to run with spurs==NULL (PS3_TRACE_SPURS_CALLER), so
         * `initialized` never gets set even though -- as proven right here --
         * the game DOES hold a valid, non-null spurs pointer by the time it
         * reaches CreateTaskset (a NULL `spurs` would have already returned
         * NULL_POINTER above). Root cause of the upstream NULL is still open
         * (trampoline dispatch defeats both host-RBP and guest-LR caller
         * tracing -- see ps3recomp-feasibility memory). Lazy-init on first
         * real use so downstream taskset/task calls aren't permanently
         * wedged by the earlier bad call. */
        memset(sp, 0, sizeof(CellSpurs));
        sp->initialized = 1;
        sp->nSpus = 1;
        /* The lazy path must ALSO start the HLE kernel: without it every
         * CreateTask falls back to synchronous inline dispatch on the PPU
         * caller, and a task that waits on PPU-fed queues (GoW2's decoder
         * control task) deadlocks the boot thread. */
        spurs_kernel_start(4, spurs_pm_dispatch, NULL);
        fprintf(stderr, "[cellSpurs] CreateTaskset: spurs@0x%08X was never "
                        "Initialize()'d -- lazy-init workaround applied\n",
                (unsigned)spurs);
    }

    /* Guest taskset in the REAL SDK layout (shared with the SPU task-lib via
     * the LS 0x2700 window + DMA). HLE bookkeeping goes to the side table. */
    (void)ts;
    memset(vm_base + (uintptr_t)taskset, 0, SDK_TS_BYTES);
    sdk_wr64(vm_base + (uintptr_t)taskset + SDK_TS_SPURS, (u64)(uintptr_t)spurs);
    sdk_wr64(vm_base + (uintptr_t)taskset + SDK_TS_ARGS,  args);
    sdk_wr32(vm_base + (uintptr_t)taskset + SDK_TS_SIZE_FIELD, SDK_TS_BYTES);
    taskset_side((u32)(uintptr_t)taskset, 1);

    printf("[cellSpurs] CreateTaskset(ea=0x%X args=0x%llX)\n",
           (unsigned)(uintptr_t)taskset, (unsigned long long)args);
    return CELL_OK;
}

s32 cellSpursCreateTasksetWithAttribute(CellSpurs* spurs,
                                        CellSpursTaskset* taskset,
                                        const CellSpursTasksetAttribute* attr)
{
    /* Forward the attribute's args/priority/maxContention (see
     * _cellSpursTasksetAttributeInitialize for the guest layout). Discarding
     * args starved GoW2's SPU job workers of their work context. */
    u64 args = 0;
    u32 maxc = 0;
    const u8* prio = NULL;
    u8 prio_buf[8];
    if (attr) {
        const unsigned char* a = (const unsigned char*)spurs_p2h((void*)attr);
        args = ((u64)a[0x08]<<56)|((u64)a[0x09]<<48)|((u64)a[0x0A]<<40)|((u64)a[0x0B]<<32)|
               ((u64)a[0x0C]<<24)|((u64)a[0x0D]<<16)|((u64)a[0x0E]<<8)|(u64)a[0x0F];
        memcpy(prio_buf, a + 0x10, 8);
        prio = prio_buf;
        maxc = ((u32)a[0x18]<<24)|((u32)a[0x19]<<16)|((u32)a[0x1A]<<8)|(u32)a[0x1B];
    }
    return cellSpursCreateTaskset(spurs, taskset, args, prio, maxc);
}

s32 cellSpursDestroyTaskset(CellSpursTaskset* taskset)
{
    if (!taskset)
        return CELL_SPURS_TASK_ERROR_NULL_POINTER;

    printf("[cellSpurs] DestroyTaskset()\n");
    taskset->initialized = 0;
    return CELL_OK;
}

s32 cellSpursShutdownTaskset(CellSpursTaskset* taskset)
{
    if (!taskset)
        return CELL_SPURS_TASK_ERROR_NULL_POINTER;

    printf("[cellSpurs] ShutdownTaskset()\n");
    taskset->shutdownRequested = 1;
    return CELL_OK;
}

s32 cellSpursJoinTaskset(CellSpursTaskset* taskset)
{
    if (!taskset)
        return CELL_SPURS_TASK_ERROR_NULL_POINTER;

    printf("[cellSpurs] JoinTaskset()\n");
    /* In a full implementation, wait for all tasks to complete */
    return CELL_OK;
}

s32 cellSpursTasksetAttributeInitialize(CellSpursTasksetAttribute* attr)
{
    if (!attr) return CELL_SPURS_TASK_ERROR_NULL_POINTER;
    memset(attr, 0, sizeof(CellSpursTasksetAttribute));
    attr->revision = 1;
    return CELL_OK;
}

s32 cellSpursTasksetAttributeSetName(CellSpursTasksetAttribute* attr,
                                      const char* name)
{
    (void)name;
    if (!attr) return CELL_SPURS_TASK_ERROR_NULL_POINTER;
    return CELL_OK;
}

/* =========================================================================
 * Task
 * =====================================================================*/

s32 cellSpursCreateTask(CellSpursTaskset* taskset, CellSpursTaskId* taskId,
                        void* elf, void* context, u32 sizeContext,
                        void* lsPattern, void* argument)
{
    (void)context; (void)sizeContext;

    if (!taskset)
        return CELL_SPURS_TASK_ERROR_NULL_POINTER;

    /* Real SDK prototype (7 params): (taskset, taskId, eaElf, eaContext,
     * sizeContext, lsPattern r8, argument r9). The observed r8 values
     * ([0,0,0,1] and [03FFFFFF,FFFFFFFF,...]) are 128-bit LS PATTERNS, not
     * arguments -- the actual CellSpursTaskArgument (4x u32, loaded by value
     * into the task's r3-r6) is the r9 pointer, which two earlier ABI
     * attempts misread (first as attr, then r8 as the argument). */
    { extern unsigned char* vm_base;
      fprintf(stderr, "[SPURSTASK] context=0x%X lsPat=0x%X arg=0x%X",
              (unsigned)(uintptr_t)context, (unsigned)(uintptr_t)lsPattern,
              (unsigned)(uintptr_t)argument);
      if (argument && (uintptr_t)argument < 0x100000000ull) {
          const unsigned char* p = vm_base + (uintptr_t)argument;
          fprintf(stderr, " *arg=[%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X]",
              p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]); }
      fprintf(stderr, "\n"); fflush(stderr); }
    void* arg = argument;   /* the job/data EA handed to the SPU task */

    /* taskset / taskId / elf are guest pointers — translate to host before
     * dereferencing (the heap now hands out real guest EAs). taskId is a guest
     * output: write it byte-swapped (BE) so the game reads the correct id. */
    extern unsigned char* vm_base;
    extern void vm_write32(unsigned long long addr, unsigned int val);
    const uint8_t* elf_host = elf ? (const uint8_t*)(vm_base + (uintptr_t)elf) : NULL;

    TasksetSide* side = taskset_side((u32)(uintptr_t)taskset, 0);
    if (!side) {
        /* Taskset created before this HLE tracked SDK layouts (or by a path
         * we don't cover): adopt it lazily. */
        side = taskset_side((u32)(uintptr_t)taskset, 1);
        if (!side) return CELL_SPURS_TASK_ERROR_STAT;
    }

    /* Find a free task slot */
    for (u32 i = 0; i < CELL_SPURS_MAX_TASK; i++) {
        if (!s_tasks[i].in_use) {
            u32 tid = side->next_id++;
            task_st(&s_tasks[i].in_use, 1);
            s_tasks[i].id = s_next_task_id++;
            task_st(&s_tasks[i].active, 1);
            s_tasks[i].completed = 0;
            s_tasks[i].exitCode = 0;
            s_tasks[i].entryPoint = (void*)elf_host;
            s_tasks[i].tasksetEa = (u32)(uintptr_t)taskset;
            s_tasks[i].tasksetId = tid;
            /* Slot reciclado (JoinTask liberta-o): um `queued` herdado da task
             * anterior dobrava TODOS os kicks desta numa entrada que ja foi
             * consumida -- a task nunca mais corria. */
            task_st(&s_tasks[i].running, 0);
            task_st(&s_tasks[i].pending, 0);
            task_st(&s_tasks[i].queued, 0);

            /* SDK guest bookkeeping: fill task_info[tid] and mark the task
             * enabled+ready so the task-lib's taskset view is coherent. */
            { unsigned char* tsb = vm_base + (uintptr_t)taskset;
              unsigned char* ti  = tsb + SDK_TS_TASKINFO + 48u * tid;
              if (argument && (uintptr_t)argument < 0x100000000ull)
                  memcpy(ti + 0x00, vm_base + (uintptr_t)argument, 16);
              else
                  memset(ti + 0x00, 0, 16);
              sdk_wr64(ti + 0x10, (u64)(uintptr_t)elf);
              sdk_wr64(ti + 0x18, (u64)(uintptr_t)context);
              if (lsPattern && (uintptr_t)lsPattern < 0x100000000ull)
                  memcpy(ti + 0x20, vm_base + (uintptr_t)lsPattern, 16);
              else
                  memset(ti + 0x20, 0, 16);
              sdk_set_task_bit(tsb + SDK_TS_ENABLED, tid);
              sdk_set_task_bit(tsb + SDK_TS_READY,   tid); }

            if (taskId) vm_write32((unsigned long long)(uintptr_t)taskId, tid);

            printf("[cellSpurs] CreateTask(id=%u, entry=%p) - task logged\n",
                   s_tasks[i].id, elf);
    spurs_task_tick_start();
            PS3_TRACE(PS3_TS_SPURS, "cellSpursCreateTask", "TASK_CREATE",
                      "task_id=%u entry=0x%08X taskset=0x%08X",
                      s_tasks[i].id, (unsigned)(uintptr_t)elf, (unsigned)(uintptr_t)taskset);

            /* Run the task's SPU program ASYNCHRONOUSLY on the persistent SPURS
             * kernel thread, not inline. GoW2's movie decoder is a SPURS task
             * that waits on PPU-side signals mid-decode; a synchronous inline
             * dispatch blocks the PPU (the very thread that must feed those
             * signals) -> the decoder stalls/corrupts and the movie never
             * completes. Submitting to the kernel queue runs the decoder off
             * the PPU thread, so PPU<->SPU signalling proceeds concurrently
             * (the real SPURS execution model).
             * O arranque passa pelo MESMO spurs_task_kick que os kicks
             * seguintes: um CreateTask com a fila cheia bloqueava o PPU tal e
             * qual como um kick, e a politica de "nunca na thread que chama"
             * tem de viver num sitio so. */
            if (elf_host) {
                size_t sz = spu_elf_image_size(elf_host, 2u * 1024 * 1024);
                s_tasks[i].elfSize = (u32)sz;
                s_tasks[i].argA    = (u64)(uintptr_t)arg;
                if (sz)
                    spurs_task_kick(&s_tasks[i]);
            }
            return CELL_OK;
        }
    }

    return CELL_SPURS_TASK_ERROR_NOMEM;
}

s32 cellSpursJoinTask(CellSpursTaskset* taskset, CellSpursTaskId taskId,
                      s32* exitCode)
{
    (void)taskset;

    printf("[cellSpurs] JoinTask(id=%u)\n", taskId);

    /* Find the task and mark as completed */
    for (u32 i = 0; i < CELL_SPURS_MAX_TASK; i++) {
        if (s_tasks[i].in_use && s_tasks[i].id == taskId) {
            s_tasks[i].completed = 1;
            task_st(&s_tasks[i].active, 0);
            if (exitCode)
                *exitCode = s_tasks[i].exitCode;
            task_st(&s_tasks[i].in_use, 0);
            return CELL_OK;
        }
    }

    return CELL_SPURS_TASK_ERROR_SRCH;
}

/* Re-dispatch one tracked task's SPU job. The standalone lifted runtime can't
 * park a task in a kernel wait, so a task that ran out of queue data exits its
 * job; "signalling" it means giving it a fresh run (it re-checks its queues
 * against the shared control blocks in main memory and resumes work). */
static void spurs_task_redispatch(SpursTask* t)
{
    spurs_task_kick(t);
}

/* Data-available kick from the LFQueue HLE (no registered waiter): run every
 * live task so the consumer sees the new entries. */
/* E156 (2026-09-02): SPU "residente". No hardware uma task SPURS activa fica no SPU a fazer poll
 * (GETLLAR no bloco de controlo) ate' haver trabalho; aqui cada despacho corre uma fatia de
 * PS3_SPU_TASK_IDLE_MS e larga o worker, e so' um push de LFQueue voltava a despachar. Medido no
 * GoW2: o mixer do SCREAM (tasks 1/2 da imagem spu0) consumia ~4 jobs/s (consola: 32/s) porque so'
 * era re-despachado pelos pushes do vdec (~1/s) -- o contador de samples do stream ficava em 0 e o
 * relogio do filme (corrigido pelo audio) parado. Este tick re-kicka as tasks activas a cada
 * PS3_SPU_TASK_TICK_MS (default 2; 0 desliga); os kicks coalescem (t->queued), logo custa no maximo
 * um despacho por task por tick. */
static unsigned spurs_task_tick_ms(void)
{
    static int ms = -1;
    if (ms < 0) { const char* e = getenv("PS3_SPU_TASK_TICK_MS"); ms = e ? atoi(e) : 2; if (ms < 0) ms = 0; }
    return (unsigned)ms;
}
#if !defined(_WIN32) || defined(__MINGW32__) /* winport */
#include <pthread.h>
#include <unistd.h>
static void* spurs_task_tick_thread(void* arg)
{
    (void)arg;
    unsigned ms = spurs_task_tick_ms();
    for (;;) {
        usleep(ms * 1000u);
        int any = 0;
        /* Lidos pela thread de tick enquanto CreateTask/JoinTask os escrevem
         * noutra thread: loads atomicos, senao e' data race (TSan). */
        for (u32 i = 0; i < CELL_SPURS_MAX_TASK; i++)
            if (task_ld(&s_tasks[i].in_use) && task_ld(&s_tasks[i].active)) { any = 1; break; }
        if (any) spurs_task_kick_all();
        /* RPCS3's SPURS kernel stays in select/dispatch. A policy module
         * such as SCREAM only runs here when the game calls ReadyCountStore,
         * which is twice at startup — before any voice exists. Re-submit
         * registered modules so they keep polling the mixer control block. */
        for (u32 i = 0; i < CELL_SPURS_MAX_WORKLOAD; i++) {
            if (!s_workloads[i].in_use || !s_workloads[i].fp_registered) continue;
            if (!s_workloads[i].pm || !s_workloads[i].sizePm) continue;
            spurs_workload_job job;
            job.pm = s_workloads[i].pm;
            job.sizePm = s_workloads[i].sizePm;
            job.data = s_workloads[i].data;
            job.wid = i;
            job.is_task = 0;
            job.taskset_ea = 0;
            job.task_id = 0;
            spurs_kernel_submit(&job);
        }
    }
    return NULL;
}
#endif
static void spurs_task_tick_start(void)
{
    static int started = 0;
    if (started || !spurs_task_tick_ms()) return;
    started = 1;
#if !defined(_WIN32) || defined(__MINGW32__) /* winport */
    pthread_t th; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, spurs_task_tick_thread, NULL) == 0)
        fprintf(stderr, "[cellSpurs] task tick thread: re-kick das tasks activas a cada %u ms\n", spurs_task_tick_ms());
    pthread_attr_destroy(&at);
#endif
}
void spurs_task_kick_all(void)
{
    for (u32 i = 0; i < CELL_SPURS_MAX_TASK; i++)
        if (s_tasks[i].in_use && s_tasks[i].active)
            spurs_task_redispatch(&s_tasks[i]);
}

s32 cellSpursSendSignal(CellSpursTaskset* taskset, CellSpursTaskId taskId)
{
    (void)taskset;

    printf("[cellSpurs] SendSignal(id=%u)\n", taskId);

    /* taskId here is the index within the taskset (the id we returned from
     * CreateTask). Set the SDK signalled bit so a task blocked in
     * WAIT_SIGNAL consumes it, then give the task a run. */
    for (u32 i = 0; i < CELL_SPURS_MAX_TASK; i++) {
        if (s_tasks[i].in_use &&
            (!taskset || s_tasks[i].tasksetEa == (u32)(uintptr_t)taskset) &&
            s_tasks[i].tasksetId == taskId) {
            extern unsigned char* vm_base;
            if (s_tasks[i].tasksetEa)
                sdk_set_task_bit(vm_base + s_tasks[i].tasksetEa + SDK_TS_SIGNALLED,
                                 s_tasks[i].tasksetId);
            spurs_task_redispatch(&s_tasks[i]);
            return CELL_OK;
        }
    }

    return CELL_SPURS_TASK_ERROR_SRCH;
}

s32 cellSpursTaskAttributeInitialize(CellSpursTaskAttribute* attr)
{
    if (!attr) return CELL_SPURS_TASK_ERROR_NULL_POINTER;
    memset(attr, 0, sizeof(CellSpursTaskAttribute));
    attr->revision = 1;
    return CELL_OK;
}

/* =========================================================================
 * Workload
 * =====================================================================*/

/* E170 (2026-09-02): o GoW2 chama AddWorkload/ReadyCountStore numa instancia SPURS que
 * nunca passou por cellSpursInitialize (mesmo caso do lazy-init do CreateTaskset). Sem o
 * kernel HLE a correr, spurs_kernel_submit devolvia 0 em silencio e a workload do mixer do
 * SCREAM nunca chegava ao dispatcher (nem sequer o MISS do fingerprint aparecia). */
static int spurs_kernel_start_if_needed_for_workload(void)
{
    if (!spurs_kernel_running())
        spurs_kernel_start(4, spurs_pm_dispatch, NULL);
    return spurs_kernel_running();
}
s32 cellSpursAddWorkload(CellSpurs* spurs, CellSpursWorkloadId* wid,
                         const void* pm, u32 sizePm, u64 data,
                         const u8* priority, u32 minContention,
                         u32 maxContention)
{
    if (!spurs || !wid)
        return CELL_SPURS_CORE_ERROR_NULL_POINTER;

    /* spurs/wid/priority are guest EAs; translate before dereferencing (the
     * heap hands out real addresses now, so a raw host deref faults). */
    extern unsigned char* vm_base;
    extern void vm_write32(unsigned long long addr, unsigned int val);
    CellSpurs* sp = (CellSpurs*)(vm_base + (uintptr_t)spurs);
    const u8* prio_host = priority ? (const u8*)(vm_base + (uintptr_t)priority) : NULL;

    if (!sp->initialized)
        return CELL_SPURS_CORE_ERROR_STAT;

    /* pm may arrive as a raw guest EA (AddWorkload called straight from game
     * code, or the BE-swapped attribute path). The dispatcher reads the image
     * through the pointer, so translate guest->host here. */
    if (pm && (uintptr_t)pm < 0x100000000ull)
        pm = (const void*)(vm_base + (uintptr_t)pm);

    for (u32 i = 0; i < CELL_SPURS_MAX_WORKLOAD; i++) {
        if (!s_workloads[i].in_use) {
            s_workloads[i].in_use = 1;
            s_workloads[i].pm = pm;
            s_workloads[i].sizePm = sizePm;
            s_workloads[i].data = data;
            s_workloads[i].minContention = minContention;
            s_workloads[i].maxContention = maxContention;
            s_workloads[i].readyCount = 0;

            if (prio_host)
                memcpy(s_workloads[i].priority, prio_host, CELL_SPURS_MAX_SPU);
            else
                memset(s_workloads[i].priority, 0, CELL_SPURS_MAX_SPU);

            vm_write32((unsigned long long)(uintptr_t)wid, i);  /* BE output */
            printf("[cellSpurs] AddWorkload(wid=%u, pm=%p, size=%u)\n",
                   i, pm, sizePm);
            PS3_TRACE(PS3_TS_SPURS, "cellSpursAddWorkload", "WORKLOAD_ADD",
                      "wid=%u size=%u minc=%u maxc=%u",
                      i, sizePm, minContention, maxContention);
            /* Hand the workload to the SPURS kernel to run asynchronously (the
             * real workload semantics: scheduler-driven, off the PPU caller).
             * Unregistered SPU images MISS in dispatch -> inert, as before. */
            {
                spurs_workload_job job;
                job.pm = pm; job.sizePm = sizePm; job.data = data; job.wid = i;
                job.is_task = 0;
                if (!spurs_kernel_start_if_needed_for_workload())
                    fprintf(stderr, "[cellSpurs] AddWorkload(wid=%u): kernel HLE nao esta a correr -- job nao submetido\n", i);
                else if (!spurs_kernel_submit(&job))
                    fprintf(stderr, "[cellSpurs] AddWorkload(wid=%u): spurs_kernel_submit falhou (fila cheia?)\n", i);
            }
            return CELL_OK;
        }
    }

    return CELL_SPURS_CORE_ERROR_NOMEM;
}

s32 cellSpursAddWorkloadWithAttribute(CellSpurs* spurs,
                                       CellSpursWorkloadId* wid,
                                       const CellSpursWorkloadAttribute* attr)
{
    if (!attr) return CELL_SPURS_CORE_ERROR_NULL_POINTER;

    /* attr is a guest EA -> translate before reading. The priority[] array lives
     * inside the (guest) attr struct; pass its guest EA so cellSpursAddWorkload
     * translates it uniformly. Fields are host-endian (HLE-filled by
     * cellSpursWorkloadAttributeInitialize). */
    extern unsigned char* vm_base;
    const CellSpursWorkloadAttribute* a =
        (const CellSpursWorkloadAttribute*)(vm_base + (uintptr_t)attr);
    const u8* prio_guest =
        (const u8*)((uintptr_t)attr + offsetof(CellSpursWorkloadAttribute, priority));

    /* The attribute may be initialized either by our HLE
     * cellSpursWorkloadAttributeInitialize (host-endian) OR directly by guest
     * code (big-endian). A native read of a guest-written struct yields garbage
     * (observed: pm=0x30F6EF0F..., size=0 -> workload never dispatched). Detect
     * the guest-BE case (native pm/sizePm implausible) and byte-swap. */
    u64 pm    = a->pm;
    u32 szpm  = a->sizePm;
    u64 data  = a->data;
    u32 minc  = a->minContention;
    u32 maxc  = a->maxContention;
    int host_plausible = (szpm != 0 && szpm < 0x00400000u &&
                          (pm >> 32) == 0 && (u32)pm >= 0x10000u && (u32)pm < 0x10000000u);
    if (!host_plausible) {
        pm   = (u64)__builtin_bswap32(*(const u32*)((const u8*)a + 8));   /* pm low32 BE */
        szpm = __builtin_bswap32(*(const u32*)((const u8*)a + 16));
        data = (u64)__builtin_bswap32(*(const u32*)((const u8*)a + 24));
        minc = __builtin_bswap32(*(const u32*)((const u8*)a + 40));
        maxc = __builtin_bswap32(*(const u32*)((const u8*)a + 44));
        fprintf(stderr, "[cellSpurs] AddWorkloadWithAttribute: guest-BE attr -> pm=0x%llX size=0x%X\n",
                (unsigned long long)pm, szpm);
        /* PS3_SPURS_ATTR_DUMP=1: one-shot raw layout probe to locate pm/size in
         * the guest-written attr (offset-8 = HIGH word of a BE u64, so the EA of
         * a 32-bit pm actually lands at offset 12). OFF by default. */
        if (getenv("PS3_SPURS_ATTR_DUMP")) {
            const unsigned char* b = (const unsigned char*)a;
            fprintf(stderr, "[cellSpurs] ATTR-RAW attr_ea=0x%08X:", (u32)(uintptr_t)attr);
            for (int i = 0; i < 48; i++) fprintf(stderr, " %02X", b[i]);
            fprintf(stderr, "\n[cellSpurs] ATTR-CAND be32 off8=0x%08X off12=0x%08X off16=0x%08X off20=0x%08X off24=0x%08X off28=0x%08X\n",
                    __builtin_bswap32(*(const u32*)(b + 8)),  __builtin_bswap32(*(const u32*)(b + 12)),
                    __builtin_bswap32(*(const u32*)(b + 16)), __builtin_bswap32(*(const u32*)(b + 20)),
                    __builtin_bswap32(*(const u32*)(b + 24)), __builtin_bswap32(*(const u32*)(b + 28)));
            fflush(stderr);
        }
    }

    return cellSpursAddWorkload(spurs, wid, (const void*)(uintptr_t)pm,
                               szpm, data, prio_guest, minc, maxc);
}

s32 cellSpursRemoveWorkload(CellSpurs* spurs, CellSpursWorkloadId wid)
{
    if (!spurs) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    if (wid >= CELL_SPURS_MAX_WORKLOAD) return CELL_SPURS_CORE_ERROR_INVAL;
    if (!s_workloads[wid].in_use) return CELL_SPURS_CORE_ERROR_SRCH;

    s_workloads[wid].in_use = 0;
    printf("[cellSpurs] RemoveWorkload(wid=%u)\n", wid);
    return CELL_OK;
}

/* cellSpursGetWorkloadInfo — NID 0x4E153E3E (import cluster next to
 * RemoveWorkload / WaitForWorkloadShutdown). Fills a guest BE
 * CellSpursWorkloadInfo at *info. Called by GoW2 immediately after AUTO_LOAD. */
s32 cellSpursGetWorkloadInfo(CellSpurs* spurs, CellSpursWorkloadId wid,
                             CellSpursWorkloadInfo* info)
{
    if (!spurs || !info) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    if (wid >= CELL_SPURS_MAX_WORKLOAD) return CELL_SPURS_CORE_ERROR_INVAL;

    extern unsigned char* vm_base;
    extern void vm_write32(unsigned long long addr, unsigned int val);
    extern void vm_write64(unsigned long long addr, unsigned long long val);

    u32 info_ea = (u32)(uintptr_t)info;
    /* Zero whole guest struct first (0x30 bytes). */
    memset(vm_base + info_ea, 0, 0x30);

    if (!s_workloads[wid].in_use) {
        /* Non-existent workload: still succeed with zeros (games may poll). */
        static int n;
        if (n++ < 8)
            printf("[cellSpurs] GetWorkloadInfo(wid=%u) empty\n", wid);
        return CELL_OK;
    }

    SpursWorkload* w = &s_workloads[wid];
    /* data @+0 (u64 BE) */
    vm_write64(info_ea + 0x00, w->data);
    /* priority[8] @+8 */
    memcpy(vm_base + info_ea + 0x08, w->priority, CELL_SPURS_MAX_SPU);
    /* policyModule ptr @+0x10 — guest EA if pm was guest-based, else 0 */
    {
        u32 pm_ea = 0;
        if (w->pm) {
            uintptr_t p = (uintptr_t)w->pm;
            if (p >= (uintptr_t)vm_base)
                pm_ea = (u32)(p - (uintptr_t)vm_base);
            else if (p < 0x100000000ull)
                pm_ea = (u32)p;
        }
        vm_write32(info_ea + 0x10, pm_ea);
    }
    vm_write32(info_ea + 0x14, w->sizePm); /* sizePolicyModule */
    /* nameClass / nameInstance left 0 */
    /* contention fields as bytes */
    vm_base[info_ea + 0x20] = 0; /* contention */
    vm_base[info_ea + 0x21] = (u8)(w->minContention > 255 ? 255 : w->minContention);
    vm_base[info_ea + 0x22] = (u8)(w->maxContention > 255 ? 255 : w->maxContention);
    vm_base[info_ea + 0x23] = (u8)(w->readyCount > 255 ? 255 : w->readyCount);

    {
        static int n;
        if (n++ < 24) {
            printf("[cellSpurs] GetWorkloadInfo(wid=%u ready=%u sizePm=%u pm=%p cont=%u)\n",
                   wid, w->readyCount, w->sizePm, (void*)w->pm,
                   (unsigned)vm_base[info_ea + 0x20]);
            fflush(stdout);
        }
    }
    return CELL_OK;
}

s32 cellSpursWorkloadAttributeInitialize(CellSpursWorkloadAttribute* attr,
                                         u32 revision, u32 sdkVersion,
                                         const void* pm, u32 sizePm,
                                         u64 data, const u8* priority,
                                         u32 minContention,
                                         u32 maxContention)
{
    if (!attr) return CELL_SPURS_CORE_ERROR_NULL_POINTER;

    /* attr/priority are guest EAs -> translate before dereferencing. */
    extern unsigned char* vm_base;
    CellSpursWorkloadAttribute* a =
        (CellSpursWorkloadAttribute*)(vm_base + (uintptr_t)attr);
    const u8* prio_host = priority ? (const u8*)(vm_base + (uintptr_t)priority) : NULL;

    memset(a, 0, sizeof(CellSpursWorkloadAttribute));
    a->revision = revision;
    a->sdkVersion = sdkVersion;
    a->pm = (u64)(uintptr_t)pm;
    a->sizePm = sizePm;
    a->data = data;
    a->minContention = minContention;
    a->maxContention = maxContention;

    if (prio_host)
        memcpy(a->priority, prio_host, CELL_SPURS_MAX_SPU);

    return CELL_OK;
}

/* E168 (2026-09-02): o GoW2 importa a variante com underscore (NID 0xEFEB2679,
 * SDK >= 2.x: `_cellSpursWorkloadAttributeInitialize`); o gerador de NIDs deriva o NID
 * do NOME da funcao, logo so' existia 0xFD4204D7 e o import ficava por resolver ->
 * o atributo da workload do mixer do SCREAM ficava com lixo da stack ->
 * AddWorkload(pm=0,size=0) -> ReadyCountStore "cleared (no PM)" -> o mixer SPU nunca
 * corria -> posicao das vozes 0 -> relogio do filme parado. O 9o argumento
 * (maxContention) vem da area de parametros da stack guest (r1+0x70). */
extern __thread uint64_t g_last_hle_r1;
s32 _cellSpursWorkloadAttributeInitialize(CellSpursWorkloadAttribute* attr,
                                          u32 revision, u32 sdkVersion,
                                          const void* pm, u32 sizePm,
                                          u64 data, const u8* priority,
                                          u32 minContention)
{
    u32 maxContention = 1;
    if (g_last_hle_r1 >= 0x10000ull && g_last_hle_r1 < 0x100000000ull) {
        extern unsigned char* vm_base;
        u32 v = __builtin_bswap32(*(const u32*)(vm_base + (u32)g_last_hle_r1 + 0x74));
        if (v >= 1 && v <= 8) maxContention = v;
    }
    { static int n = 0; if (n++ < 4)
        fprintf(stderr, "[cellSpurs] _WorkloadAttributeInitialize attr=0x%08X pm=0x%08X size=0x%X data=0x%llX min=%u max=%u\n",
                (u32)(uintptr_t)attr, (u32)(uintptr_t)pm, sizePm, (unsigned long long)data, minContention, maxContention); }
    /* r6 chega com a metade alta suja (medido: 0x3004FD980); o PM e' um EA de 32 bits. */
    pm = (const void*)(uintptr_t)(u32)(uintptr_t)pm;
    return cellSpursWorkloadAttributeInitialize(attr, revision, sdkVersion, pm, sizePm, data,
                                                priority, minContention, maxContention);
}
s32 cellSpursWorkloadAttributeSetName(CellSpursWorkloadAttribute* attr,
                                      const char* nameClass, const char* nameInstance)
{
    (void)nameClass; (void)nameInstance;
    if (!attr) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    return CELL_OK;   /* nomes sao so' para o profiler; nada a guardar */
}
s32 cellSpursReadyCountStore(CellSpurs* spurs, CellSpursWorkloadId wid,
                             u32 value)
{
    if (!spurs) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    if (wid >= CELL_SPURS_MAX_WORKLOAD) return CELL_SPURS_CORE_ERROR_INVAL;
    if (!s_workloads[wid].in_use) return CELL_SPURS_CORE_ERROR_SRCH;

    s_workloads[wid].readyCount = value;
    /* E170: cada ReadyCountStore(N>0) com PM e' um pedido de execucao da workload -- no
     * hardware o kernel SPURS despacha-a enquanto readyCount>0; aqui submete-se um job
     * (o dispatcher consome uma unidade de ready no fim, spurs_ready_consume). */
    if (value && s_workloads[wid].pm && s_workloads[wid].sizePm) {
        /* Sem imagem liftada registada nao ha' nada para correr: submeter na mesma so'
         * enchia a fila do kernel e o log com um MISS por tick (medido e182: 110 759
         * linhas em 4 min). O MISS unico do AddWorkload ja' identifica o fingerprint. */
        extern uint64_t spu_workload_fingerprint(const void*, size_t);
        extern spu_lifted_entry_fn spu_workload_find(uint64_t);
        if (!s_workloads[wid].fp_checked) {
            s_workloads[wid].fp_checked = 1;
            s_workloads[wid].fp_registered =
                spu_workload_find(spu_workload_fingerprint(s_workloads[wid].pm, s_workloads[wid].sizePm)) != NULL;
            if (!s_workloads[wid].fp_registered) {
                uint64_t _fp = spu_workload_fingerprint(s_workloads[wid].pm, s_workloads[wid].sizePm);
                fprintf(stderr, "[cellSpurs] ReadyCountStore(wid=%u): PM sem imagem SPU liftada registada -- ready fica sem consumidor (fp=0x%016llX pm=%p size=%u)\n", wid, (unsigned long long)_fp, s_workloads[wid].pm, s_workloads[wid].sizePm);
                /* E329: dump the unlifted PM binary so it can be lifted (gated). */
                if (getenv("PS3_DUMP_MISS_PM") && s_workloads[wid].pm && s_workloads[wid].sizePm) {
                    char _fn[128]; snprintf(_fn,sizeof _fn,"miss_pm_wid%u_%016llX.bin", wid,(unsigned long long)_fp);
                    FILE* _f=fopen(_fn,"wb"); if(_f){ fwrite(s_workloads[wid].pm,1,s_workloads[wid].sizePm,_f); fclose(_f);
                        fprintf(stderr,"[cellSpurs] E329 dumped PM -> %s (%u bytes)\n",_fn,s_workloads[wid].sizePm); } }
            }
        }
        if (!s_workloads[wid].fp_registered) { s_workloads[wid].readyCount = 0; return CELL_OK; }
        spurs_workload_job job;
        job.pm = s_workloads[wid].pm; job.sizePm = s_workloads[wid].sizePm;
        job.data = s_workloads[wid].data; job.wid = wid; job.is_task = 0;
        if (spurs_kernel_start_if_needed_for_workload()) {
            static int n = 0;
            if (!spurs_kernel_submit(&job) && n++ < 4)
                fprintf(stderr, "[cellSpurs] ReadyCountStore(wid=%u): submit falhou\n", wid);
        }
    }
    /* No policy-module image → nothing can consume ready on SPU. Leave
     * readyCount stuck and GoW2's post-AUTO_LOAD wait (00454C60) livelocks on
     * GetWorkloadInfo. Clear so waiters see idle. */
    if (value && (!s_workloads[wid].pm || !s_workloads[wid].sizePm)) {
        s_workloads[wid].readyCount = 0;
        {
            static int n;
            if (n++ < 8)
                printf("[cellSpurs] ReadyCountStore(wid=%u val=%u) cleared (no PM)\n",
                       wid, value);
        }
    }
    return CELL_OK;
}

s32 cellSpursReadyCountSwap(CellSpurs* spurs, CellSpursWorkloadId wid,
                            u32* old, u32 value)
{
    if (!spurs || !old) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    if (wid >= CELL_SPURS_MAX_WORKLOAD) return CELL_SPURS_CORE_ERROR_INVAL;
    if (!s_workloads[wid].in_use) return CELL_SPURS_CORE_ERROR_SRCH;

    *old = s_workloads[wid].readyCount;
    s_workloads[wid].readyCount = value;
    return CELL_OK;
}

s32 cellSpursReadyCountCompareAndSwap(CellSpurs* spurs,
                                       CellSpursWorkloadId wid,
                                       u32* old, u32 compare, u32 value)
{
    if (!spurs || !old) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    if (wid >= CELL_SPURS_MAX_WORKLOAD) return CELL_SPURS_CORE_ERROR_INVAL;
    if (!s_workloads[wid].in_use) return CELL_SPURS_CORE_ERROR_SRCH;

    *old = s_workloads[wid].readyCount;
    if (s_workloads[wid].readyCount == compare)
        s_workloads[wid].readyCount = value;

    return CELL_OK;
}

s32 cellSpursWakeUp(CellSpurs* spurs)
{
    if (!spurs) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    /* In a full implementation, wake the worker threads */
    return CELL_OK;
}

/* =========================================================================
 * Event flags
 * =====================================================================*/

s32 cellSpursEventFlagInitialize(CellSpursTaskset* taskset,
                                 CellSpursEventFlag* eventFlag,
                                 u32 clearMode, u32 direction)
{
    (void)taskset;
    eventFlag = EF_P2H(eventFlag);

    if (!eventFlag)
        return CELL_SPURS_TASK_ERROR_NULL_POINTER;

    /* If this event flag was previously initialized, free old sync slot */
    EventFlagSync* old = ef_sync_find(eventFlag);
    if (old) ef_sync_free(old);

    memset(eventFlag, 0, sizeof(CellSpursEventFlag));
    eventFlag->initialized = 1;
    eventFlag->clearMode = (u16)clearMode;
    eventFlag->direction = (u16)direction;
    eventFlag->bits = 0;

    EventFlagSync* sync = ef_sync_alloc(eventFlag);
    if (!sync) {
        printf("[cellSpurs] EventFlagInitialize: no free sync slots!\n");
        return CELL_SPURS_TASK_ERROR_NOMEM;
    }

    printf("[cellSpurs] EventFlagInitialize(clearMode=%u, direction=%u)\n",
           clearMode, direction);
    return CELL_OK;
}

s32 cellSpursEventFlagAttachLv2EventQueue(CellSpursEventFlag* eventFlag)
{
    eventFlag = EF_P2H(eventFlag);
    if (!eventFlag) return CELL_SPURS_TASK_ERROR_NULL_POINTER;
    printf("[cellSpurs] EventFlagAttachLv2EventQueue()\n");
    return CELL_OK;
}

s32 cellSpursEventFlagSet(CellSpursEventFlag* eventFlag, u16 bits)
{
    eventFlag = EF_P2H(eventFlag);
    if (!eventFlag)
        return CELL_SPURS_TASK_ERROR_NULL_POINTER;

    if (!eventFlag->initialized)
        return CELL_SPURS_TASK_ERROR_STAT;

    EventFlagSync* sync = ef_sync_find(eventFlag);
    if (!sync)
        return CELL_SPURS_TASK_ERROR_STAT;

    ef_lock(sync);
    eventFlag->bits |= bits;
    ef_broadcast(sync);
    ef_unlock(sync);

    return CELL_OK;
}

s32 cellSpursEventFlagWait(CellSpursEventFlag* eventFlag, u16* bits,
                           u32 mode)
{
    eventFlag = EF_P2H(eventFlag);
    bits = (u16*)spurs_p2h(bits);
    if (!eventFlag || !bits)
        return CELL_SPURS_TASK_ERROR_NULL_POINTER;

    if (!eventFlag->initialized)
        return CELL_SPURS_TASK_ERROR_STAT;

    EventFlagSync* sync = ef_sync_find(eventFlag);
    if (!sync)
        return CELL_SPURS_TASK_ERROR_STAT;

    u16 pattern = *bits;

    /* This can block indefinitely (the setter may be an SPU task or another
     * PPU thread). Blocking while holding the PPU giant lock freezes every
     * other PPU thread -- the setter included -- turning a wait into a
     * whole-process deadlock (watchdog: holder parked in ef_wait, all other
     * threads queued on the giant lock, zero flips). Mirror the
     * sys_lwcond_wait discipline: drop the giant lock across the wait,
     * reacquire with priority before returning to lifted code. */
    { extern void ppu_giant_lock_release(void); ppu_giant_lock_release(); }

    ef_lock(sync);

    /* Block until the requested bit pattern is satisfied */
    for (;;) {
        u16 current = eventFlag->bits;

        if (mode == CELL_SPURS_EVENT_FLAG_AND) {
            if ((current & pattern) == pattern)
                break;
        } else {
            /* OR mode: any requested bit set */
            if ((current & pattern) != 0)
                break;
        }

        ef_wait(sync);
    }

    *bits = eventFlag->bits;

    /* Auto-clear if configured */
    if (eventFlag->clearMode == CELL_SPURS_EVENT_FLAG_CLEAR_AUTO)
        eventFlag->bits &= ~pattern;

    ef_unlock(sync);

    { extern void ppu_giant_lock_acquire_prio(void); ppu_giant_lock_acquire_prio(); }

    return CELL_OK;
}

s32 cellSpursEventFlagTryWait(CellSpursEventFlag* eventFlag, u16* bits,
                              u32 mode)
{
    eventFlag = EF_P2H(eventFlag);
    bits = (u16*)spurs_p2h(bits);
    if (!eventFlag || !bits)
        return CELL_SPURS_TASK_ERROR_NULL_POINTER;

    if (!eventFlag->initialized)
        return CELL_SPURS_TASK_ERROR_STAT;

    EventFlagSync* sync = ef_sync_find(eventFlag);
    if (!sync)
        return CELL_SPURS_TASK_ERROR_STAT;

    u16 pattern = *bits;

    ef_lock(sync);

    u16 current = eventFlag->bits;

    if (mode == CELL_SPURS_EVENT_FLAG_AND) {
        if ((current & pattern) != pattern) {
            ef_unlock(sync);
            return CELL_SPURS_TASK_ERROR_BUSY;
        }
    } else {
        if ((current & pattern) == 0) {
            ef_unlock(sync);
            return CELL_SPURS_TASK_ERROR_BUSY;
        }
    }

    *bits = current;

    if (eventFlag->clearMode == CELL_SPURS_EVENT_FLAG_CLEAR_AUTO)
        eventFlag->bits &= ~pattern;

    ef_unlock(sync);
    return CELL_OK;
}

s32 cellSpursEventFlagClear(CellSpursEventFlag* eventFlag, u16 bits)
{
    eventFlag = EF_P2H(eventFlag);
    if (!eventFlag)
        return CELL_SPURS_TASK_ERROR_NULL_POINTER;

    if (!eventFlag->initialized)
        return CELL_SPURS_TASK_ERROR_STAT;

    EventFlagSync* sync = ef_sync_find(eventFlag);
    if (!sync)
        return CELL_SPURS_TASK_ERROR_STAT;

    ef_lock(sync);
    eventFlag->bits &= ~bits;
    ef_unlock(sync);

    return CELL_OK;
}

s32 cellSpursEventFlagGetDirection(CellSpursEventFlag* eventFlag,
                                   u32* direction)
{
    eventFlag = EF_P2H(eventFlag);
    direction = (u32*)spurs_p2h(direction);
    if (!eventFlag || !direction)
        return CELL_SPURS_TASK_ERROR_NULL_POINTER;

    *direction = eventFlag->direction;
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Additional functions needed by Tokyo Jungle (from RPCS3 audit)
 * -----------------------------------------------------------------------*/

/* _cellSpursEventFlagInitialize — internal init with more parameters */
s32 _cellSpursEventFlagInitialize(void* spurs, void* taskset,
                                    CellSpursEventFlag* eventFlag,
                                    u32 clearMode, u32 direction)
{
    (void)spurs; (void)taskset;
    printf("[cellSpurs] _EventFlagInitialize(clearMode=%u, dir=%u)\n",
           clearMode, direction);
    if (!eventFlag) return CELL_SPURS_TASK_ERROR_NULL_POINTER;
    return cellSpursEventFlagInitialize((CellSpursTaskset*)taskset, eventFlag, clearMode, direction);
}

/* _cellSpursSendSignal — internal signal delivery */
s32 _cellSpursSendSignal(void* taskset, u32 taskId)
{
    printf("[cellSpurs] _SendSignal(taskId=%u)\n", taskId);
    return cellSpursSendSignal((CellSpursTaskset*)taskset, taskId);
}

/* _cellSpursTasksetAttributeInitialize — underscore import (later SDKs route
 * the public initializer through this). The attribute MUST retain `args`:
 * CreateTasksetWithAttribute forwards it into the taskset (SDK_TS_ARGS),
 * spursTasksetStartTask hands it to every task in r4's low u64, and GoW2's
 * job workers derive their per-taskset work context from it (dropping it
 * made the workers consume 224B jobs and publish zero completions).
 * Guest layout (RPCS3 CellSpursTasksetAttribute): +0x00 revision, +0x04
 * sdkVersion, +0x08 args u64, +0x10 priority[8], +0x18 maxContention,
 * +0x1C name ptr, +0x20 taskset_size, +0x24 enable_clear_ls. All BE. */
s32 _cellSpursTasksetAttributeInitialize(void* attr, u32 revision,
                                         u32 sdkVersion, u64 args,
                                         const u8* priority, u32 maxContention)
{
    if (!attr) return CELL_SPURS_TASK_ERROR_NULL_POINTER;
    extern unsigned char* vm_base;
    unsigned char* a = (unsigned char*)spurs_p2h(attr);
    memset(a, 0, 0x40);
    sdk_wr32(a + 0x00, revision);
    sdk_wr32(a + 0x04, sdkVersion);
    sdk_wr64(a + 0x08, args);
    if (priority && (uintptr_t)priority < 0x100000000ull)
        memcpy(a + 0x10, vm_base + (uintptr_t)priority, 8);
    sdk_wr32(a + 0x18, maxContention);
    fprintf(stderr, "[cellSpurs] TasksetAttributeInitialize args=0x%llX maxC=%u\n",
            (unsigned long long)args, maxContention);
    return CELL_OK;
}

s32 cellSpursTasksetAttributeSetTasksetSize(void* attr, u32 size)
{
    (void)size;
    if (!attr) return CELL_SPURS_TASK_ERROR_NULL_POINTER;
    return CELL_OK;
}

/* cellSpursGetInfo — report a minimal, coherent view of the HLE instance. */
s32 cellSpursGetInfo(CellSpurs* spurs, void* info)
{
    if (!spurs || !info) return CELL_SPURS_CORE_ERROR_NULL_POINTER;
    extern void vm_write32(unsigned long long addr, unsigned int val);
    unsigned char* inf = (unsigned char*)spurs_p2h(info);
    memset(inf, 0, 0x80);
    vm_write32((unsigned long long)(uintptr_t)info, 1);        /* nSpus (BE) */
    return CELL_OK;
}

s32 cellSpursRequestIdleSpu(CellSpurs* spurs)
{
    (void)spurs;
    return CELL_OK;
}

s32 cellSpursSetExceptionEventHandler(CellSpurs* spurs, void* handler, void* arg)
{
    (void)spurs; (void)handler; (void)arg;
    return CELL_OK;
}

/* cellSpursRunJobChain — start a job chain execution */
s32 cellSpursRunJobChain(void* spurs, void* jobChain)
{
    (void)spurs; (void)jobChain;
    printf("[cellSpurs] RunJobChain() — stub (no SPU execution)\n");
    /* Job chains run on SPUs. Without SPU execution, we stub this.
     * Games that depend on job chain completion will need the jobs
     * to be HLE'd or run on host threads. */
    return CELL_OK;
}

/* cellSpursKickJobChain — kick a running job chain */
s32 cellSpursKickJobChain(void* spurs, void* jobChain)
{
    (void)spurs; (void)jobChain;
    return CELL_OK;
}
