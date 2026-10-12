/* test_spurs_kick_async_main.c — o kick de uma task NUNCA corre na thread que
 * chama, mesmo com o kernel SATURADO.
 *
 * O contrato da consola: o SPU corre EM PARALELO com o PPU. Um "kick" (dados
 * novos numa LFQueue, um SendSignal) e' uma NOTIFICACAO, nao um pedido de
 * execucao sincrona. O PPU deposita os dados, avisa, e segue para o frame
 * seguinte -- nunca fica preso dentro do job do SPU.
 *
 * O bug que este teste tranca: spurs_task_redispatch() caia para
 * spu_workload_dispatch_task() INLINE quando o spurs_kernel_submit falhava, e
 * quem chama e' o spurs_task_kick_all() a partir do _cellSpursLFQueuePushBody,
 * dentro do render por frame da intro -- ou seja, a thread PRINCIPAL do PPU.
 * Com os workers do kernel presos num job que nao termina (o spu0 do GoW2), a
 * fila enchia, o submit falhava, e o job do guest passava a correr na main:
 * medido com sample(1), 3 frames vivos de spu_run_lifted_job_ex (os 2 workers
 * + a main). "SPU ocupado" virava "PPU inteiro congelado", que a consola nunca
 * faz.
 *
 * Montagem (reproduz a saturacao real, sem SPU nenhum):
 *   - o dispatch e' um stub C que BLOQUEIA (como o job que nao termina),
 *     portanto os 2 workers do kernel ficam presos e nada e' consumido;
 *   - criam-se NTASKS tasks e martelam-se KICKS rondas de spurs_task_kick_all()
 *     a partir da main, o suficiente para encher a ring de 256 do kernel;
 *   - o stub carimba a thread em que correu.
 *
 * Aceites:
 *   [NOINLINE] nenhum dispatch carimbou a tid da main  -> o PPU nunca executa
 *              o job do guest;
 *   [COALESCE] o total de submits fica limitado (~1 entrada em fila por task),
 *              em vez de uma entrada por task POR kick -> a fila deixa de
 *              encher, que e' o que tornava o fallback alcancavel;
 *   [NOLOSS]   depois de libertar os workers, cada task viva corre de facto
 *              (um kick coalescido nao e' um kick perdido).
 *
 * Nao linka runtime/spu/spu_workload.c de proposito: o ponto de observacao E'
 * o spu_workload_dispatch_task, portanto e o teste que o define.
 */
#include "cellSpurs.h"
#include "spurs_kernel.h"
#include "spu_workload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Task 4 (bringup-instrumentacao-fase0): cellSpurs.c/spurs_kernel.c now call
 * PS3_TRACE(PS3_TS_SPURS, ...), which needs runtime/trace/ps3_trace.c linked
 * (see run_tests.sh's run_extra spurs_kick_async line) -- and ps3_trace.c
 * declares ps3_timebase_now() `extern`, defined for real in
 * runtime/syscalls/sys_timer.c, which this isolated test does not want to
 * drag in. Same stub pattern as test_spurs_kernel_main.c. */
uint64_t ps3_timebase_now(void) { return 1; }

/* cellSpurs.c also contains the real ReadyCount diagnostics. This scheduler
 * harness replaces the SPU dispatcher, so supply the two registry hooks it
 * references without linking the production workload runner. */
__thread uint64_t g_last_hle_r1;
uint64_t spu_workload_fingerprint(const void* data, size_t n)
{
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
spu_lifted_entry_fn spu_workload_find(uint64_t fingerprint)
{
    (void)fingerprint;
    return NULL;
}

#ifdef _WIN32
#include <windows.h>
typedef DWORD tid_t;
static tid_t cur_tid(void) { return GetCurrentThreadId(); }
static void sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
typedef unsigned long long tid_t;
static tid_t cur_tid(void) { return (tid_t)(uintptr_t)pthread_self(); }
static void sleep_ms(unsigned ms) { usleep(ms * 1000u); }
#endif

/* kick_all nao tem header proprio (o cellSpursLFQueue.c tambem o declara
 * assim); e' a porta por onde o push da LFQueue acorda os consumidores. */
extern void spurs_task_kick_all(void);

#define NTASKS       4
#define NTASKS_LATE  2      /* criadas ja com a fila cheia (fase 2)         */
#define NTASKS_ALL   (NTASKS + NTASKS_LATE)
#define KICKS      400      /* NTASKS*KICKS = 1600 >> ring do kernel        */

#define ARENA_BYTES 0x20000u
#define TASKSET_EA  0x1000u
#define ELF_EA      0x8000u
#define ARG_EA      0x0200u
#define TASKID_EA   0x0300u

/* ---- memoria do guest (o cellSpurs desreferencia vm_base + EA) -------------*/
static uint8_t g_arena[ARENA_BYTES];
uint8_t* vm_base = g_arena;
uint32_t ppu_vm_size = ARENA_BYTES;

void vm_write32(unsigned long long addr, unsigned int val)
{
    if (addr + 4 > sizeof g_arena) return;
    uint8_t* p = &g_arena[addr];
    p[0] = (uint8_t)(val >> 24); p[1] = (uint8_t)(val >> 16);
    p[2] = (uint8_t)(val >> 8);  p[3] = (uint8_t)val;
}

/* cellSpursGetWorkloadInfo devolve o campo data (u64 BE) via vm_write64; o real
 * vem do ppu_loader.cpp que este harness nao linka. */
void vm_write64(unsigned long long addr, unsigned long long val)
{
    if (addr + 8 > sizeof g_arena) return;
    uint8_t* p = &g_arena[addr];
    p[0] = (uint8_t)(val >> 56); p[1] = (uint8_t)(val >> 48);
    p[2] = (uint8_t)(val >> 40); p[3] = (uint8_t)(val >> 32);
    p[4] = (uint8_t)(val >> 24); p[5] = (uint8_t)(val >> 16);
    p[6] = (uint8_t)(val >> 8);  p[7] = (uint8_t)val;
}

/* O giant lock do PPU vive no ppu_loader.cpp, que este harness nao linka. */
void ppu_giant_lock_release(void) { }
void ppu_giant_lock_acquire_prio(void) { }

/* ---- estado partilhado do stub de dispatch --------------------------------*/
static tid_t            g_main_tid;
static volatile int     g_inline_on_main;   /* o BUG: job do guest na main    */
static volatile int     g_release;          /* solta os workers presos        */
static volatile int     g_stalled;          /* workers presos neste momento   */
static volatile int     g_fairness_mode;    /* task re-kicka a si mesma        */
static volatile int     g_policy_runs;      /* workload nao-task foi servido   */
static int              g_runs[NTASKS_ALL]; /* por task_id, quantas vezes correu */

#ifdef _WIN32
static CRITICAL_SECTION    g_cs;
static CONDITION_VARIABLE  g_cv;
static void st_lock(void)   { EnterCriticalSection(&g_cs); }
static void st_unlock(void) { LeaveCriticalSection(&g_cs); }
static void st_bcast(void)  { WakeAllConditionVariable(&g_cv); }
static int  st_wait(unsigned ms) { return SleepConditionVariableCS(&g_cv, &g_cs, ms) ? 1 : 0; }
static void st_init(void)   { InitializeCriticalSection(&g_cs); InitializeConditionVariable(&g_cv); }
static uint64_t now_ms(void){ return (uint64_t)GetTickCount64(); }
#else
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv  = PTHREAD_COND_INITIALIZER;
static void st_lock(void)   { pthread_mutex_lock(&g_mtx); }
static void st_unlock(void) { pthread_mutex_unlock(&g_mtx); }
static void st_bcast(void)  { pthread_cond_broadcast(&g_cv); }
static int  st_wait(unsigned ms) {
    struct timeval tv; gettimeofday(&tv, NULL);
    uint64_t ns = (uint64_t)tv.tv_usec * 1000ull + (uint64_t)ms * 1000000ull;
    struct timespec ts;
    ts.tv_sec  = tv.tv_sec + (time_t)(ns / 1000000000ull);
    ts.tv_nsec = (long)(ns % 1000000000ull);
    return pthread_cond_timedwait(&g_cv, &g_mtx, &ts) == 0;
}
static void st_init(void) { }
static uint64_t now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ull + (uint64_t)tv.tv_usec / 1000ull;
}
#endif

/* ---- stubs do runtime SPU (ponto de observacao) ---------------------------*/
size_t spu_elf_image_size(const uint8_t* image, size_t max_avail)
{
    (void)image; (void)max_avail;
    return 4096;   /* imagem "valida" o suficiente para o cellSpurs submeter */
}

/* Modela o job que nao termina: nos workers do kernel bloqueia ate ser solto;
 * na thread que submeteu apenas CARIMBA o crime e volta -- se bloqueasse tambem
 * la, a corrida vermelha ficaria pendurada em vez de falhar depressa. */
static void dispatch_stub(uint32_t task_id)
{
    if (cur_tid() == g_main_tid) {
        g_inline_on_main = 1;
        return;
    }
    /* Atomico: varios workers passam por aqui (ver a nota equivalente no
     * test_spurs_kernel_main.c -- `volatile` ordena, nao torna atomico). */
    if (task_id < NTASKS_ALL)
        __atomic_fetch_add(&g_runs[task_id], 1, __ATOMIC_RELAXED);

    /* Reproduz o estado do GoW2: cada slice de task acorda de novo as tasks
     * vivas. Se o dispatcher consumir pending num loop interno, os dois
     * workers nunca devolvem a vez a um policy workload ja enfileirado. */
    if (__atomic_load_n(&g_fairness_mode, __ATOMIC_RELAXED)) {
        spurs_task_kick_all();
        return;
    }

    st_lock();
    g_stalled++;
    st_bcast();
    if (!g_release) {
        uint64_t deadline = now_ms() + 20000;   /* teto: nunca pendurar a suite */
        while (!g_release && now_ms() < deadline)
            st_wait(50);
    }
    g_stalled--;
    st_unlock();
}

int spu_workload_dispatch_task(const uint8_t* image, uint32_t image_size,
                               uint32_t arg, uint32_t taskset_ea, uint32_t task_id)
{
    (void)image; (void)image_size; (void)arg; (void)taskset_ea;
    dispatch_stub(task_id);
    return 0;
}

int spu_workload_dispatch(const uint8_t* image, uint32_t image_size, uint32_t arg)
{
    (void)image; (void)image_size; (void)arg;
    __atomic_fetch_add(&g_policy_runs, 1, __ATOMIC_RELAXED);
    return 0;
}

/* ---- teste ----------------------------------------------------------------*/
int main(void)
{
    int ok = 1;
    st_init();
    g_main_tid = cur_tid();
    memset(g_arena, 0, sizeof g_arena);

    /* 2 workers, deterministico: o teste precisa de saber quantos jobs ficam
     * presos, e o default deriva do nSpus/env. */
    setenv("PS3_SPU_WORKERS", "2", 1);
    unsetenv("PS3_SPU_SYNC");

    CellSpurs spurs;
    s32 rc = cellSpursInitialize(&spurs, 2, 0, 0, 0);
    int init_ok = (rc == CELL_OK) && spurs_kernel_running();
    printf("  [INIT       ] kernel running=%d                               %s\n",
           spurs_kernel_running(), init_ok ? "OK" : "FAIL");
    ok &= init_ok;

    /* Tasks na EA do taskset (o CreateTask adopta o taskset preguicosamente). */
    int created = 0;
    for (int i = 0; i < NTASKS; i++) {
        rc = cellSpursCreateTask((CellSpursTaskset*)(uintptr_t)TASKSET_EA,
                                 (CellSpursTaskId*)(uintptr_t)TASKID_EA,
                                 (void*)(uintptr_t)ELF_EA, NULL, 0,
                                 NULL, (void*)(uintptr_t)ARG_EA);
        if (rc == CELL_OK) created++;
    }
    printf("  [CREATE     ] %d/%d tasks criadas                              %s\n",
           created, NTASKS, created == NTASKS ? "OK" : "FAIL");
    ok &= (created == NTASKS);

    /* Espera os 2 workers ficarem presos dentro do dispatch: e' o estado do
     * boot real (job do spu0 que nao termina) e o que faz a fila encher. */
    st_lock();
    { uint64_t deadline = now_ms() + 5000;
      while (g_stalled < 2 && now_ms() < deadline) st_wait(20); }
    int stalled = g_stalled;
    st_unlock();
    printf("  [STALL      ] %d worker(s) presos no dispatch                  %s\n",
           stalled, stalled >= 2 ? "OK" : "FAIL");
    ok &= (stalled >= 2);

    /* O martelo: o push por frame da LFQueue, KICKS vezes. */
    unsigned before = spurs_kernel_submitted();
    uint64_t t0 = now_ms();
    for (int i = 0; i < KICKS; i++)
        spurs_task_kick_all();
    uint64_t elapsed = now_ms() - t0;
    unsigned added = spurs_kernel_submitted() - before;

    int noinline_ok = !g_inline_on_main;
    printf("  [NOINLINE   ] nenhum job do guest correu na thread que kicka   %s\n",
           noinline_ok ? "OK" : "FAIL");
    ok &= noinline_ok;

    /* Coalescing: um kick para uma task ja em fila/a correr dobra-se na que ja
     * la esta. Sem isto sao NTASKS submits por kick (1600) ate a ring encher.
     * Teto generoso: 4x NTASKS cobre o re-enfileirar das que estao a correr. */
    int coalesce_ok = (added <= (unsigned)(4 * NTASKS));
    printf("  [COALESCE   ] %u submits para %d kicks de %d tasks (teto %d)   %s\n",
           added, KICKS, NTASKS, 4 * NTASKS, coalesce_ok ? "OK" : "FAIL");
    ok &= coalesce_ok;

    int fast_ok = (elapsed < 2000);
    printf("  [PROMPT     ] %llu ms para %d kicks                            %s\n",
           (unsigned long long)elapsed, KICKS, fast_ok ? "OK" : "FAIL");
    ok &= fast_ok;

    /* Fase 2 -- submit REALMENTE recusado.
     *
     * Com o coalescing a funcionar, os kicks de task ja nao enchem a ring: as
     * tasks da fase 1 ficam todas com entrada em fila e DOBRAM-SE, portanto o
     * [NOINLINE] acima nunca chega a ver um submit falhado. Sozinho, deixaria
     * passar um fallback inline reintroduzido (verificado por mutacao). Para o
     * exercitar mesmo: satura-se a ring por fora com jobs de workload (que NAO
     * sao coalescidos -- cada AddWorkload e' um pedido distinto) e criam-se
     * tasks NOVAS, que por definicao ainda nao tem nada em fila. Agora cada
     * kick delas tenta um submit e leva com a recusa -- a condicao exacta do
     * boot real. A resposta certa continua a ser "volta para tras e tenta no
     * kick seguinte", nunca "corre aqui". */
    unsigned filler = 0;
    { spurs_workload_job f;
      memset(&f, 0, sizeof f);
      f.pm = g_arena; f.sizePm = 64; f.is_task = 0; f.wid = 0xFFFFFFFFu;
      while (filler < 8192 && spurs_kernel_submit(&f)) filler++; }
    int full_ok = (filler > 0);   /* o submit seguinte foi mesmo recusado */
    printf("  [SATURA     ] fila cheia com %u jobs de workload               %s\n",
           filler, full_ok ? "OK" : "FAIL");
    ok &= full_ok;

    g_inline_on_main = 0;
    unsigned before2 = spurs_kernel_submitted();
    int created_late = 0;
    for (int i = 0; i < NTASKS_LATE; i++) {
        rc = cellSpursCreateTask((CellSpursTaskset*)(uintptr_t)TASKSET_EA,
                                 (CellSpursTaskId*)(uintptr_t)TASKID_EA,
                                 (void*)(uintptr_t)ELF_EA, NULL, 0,
                                 NULL, (void*)(uintptr_t)ARG_EA);
        if (rc == CELL_OK) created_late++;
    }
    t0 = now_ms();
    for (int i = 0; i < KICKS; i++)
        spurs_task_kick_all();
    uint64_t elapsed2 = now_ms() - t0;
    int rejected = (spurs_kernel_submitted() == before2);   /* nada entrou */
    int fullq_ok = !g_inline_on_main && rejected && (elapsed2 < 2000) &&
                   (created_late == NTASKS_LATE);
    printf("  [FULLQ      ] %d tasks novas + %d kicks com a fila cheia: "
           "0 inline, %u aceites, %llu ms  %s\n",
           created_late, KICKS, spurs_kernel_submitted() - before2,
           (unsigned long long)elapsed2, fullq_ok ? "OK" : "FAIL");
    ok &= fullq_ok;

    /* Fase 3 -- um kick coalescido nao e' um kick PERDIDO.
     *
     * Solta os workers, deixa tudo assentar (todas as tasks ficam paradas) e da
     * UM kick. Cada task viva tem de correr OUTRA VEZ. Contam-se execucoes, nao
     * "ja correu alguma vez": um coalescing que ficasse colado (a achar que ha
     * sempre algo em fila) passava no "alguma vez" -- as tasks tinham corrido no
     * CreateTask -- e engolia em silencio todos os kicks seguintes. E' o modo de
     * falha simetrico do bug original: uma task que nunca corre e' tao errada
     * como uma que bloqueia o PPU. (Verificado por mutacao.) */
    st_lock(); g_release = 1; st_bcast(); st_unlock();
    spurs_kernel_drain(5000);

    int runs_before[NTASKS_ALL];
    for (int i = 0; i < NTASKS_ALL; i++)
        runs_before[i] = __atomic_load_n(&g_runs[i], __ATOMIC_RELAXED);

    spurs_task_kick_all();
    spurs_kernel_drain(5000);

    int all_rekicked = 1;
    for (int i = 0; i < NTASKS_ALL; i++)
        if (__atomic_load_n(&g_runs[i], __ATOMIC_RELAXED) <= runs_before[i])
            all_rekicked = 0;
    printf("  [NOLOSS     ] kick apos quiescencia re-executou as %d tasks    %s\n",
           NTASKS_ALL, all_rekicked ? "OK" : "FAIL");
    ok &= all_rekicked;

    /* Fase 4 -- fairness entre tasks persistentes e policy workloads.
     * Dois workers correm tasks que se auto-kickam; em seguida um workload de
     * politica entra na mesma fila. Ele deve ganhar um quantum sem depender de
     * a task parar espontaneamente. */
    g_policy_runs = 0;
    __atomic_store_n(&g_fairness_mode, 1, __ATOMIC_RELAXED);
    spurs_task_kick_all();
    sleep_ms(50);
    { spurs_workload_job policy;
      memset(&policy, 0, sizeof policy);
      policy.pm = g_arena; policy.sizePm = 64; policy.wid = 0xFE;
      policy.is_task = 0;
      ok &= spurs_kernel_submit(&policy); }
    sleep_ms(250);
    int fair_ok = __atomic_load_n(&g_policy_runs, __ATOMIC_RELAXED) > 0;
    printf("  [FAIRNESS   ] policy workload corre entre tasks persistentes    %s\n",
           fair_ok ? "OK" : "FAIL");
    ok &= fair_ok;
    __atomic_store_n(&g_fairness_mode, 0, __ATOMIC_RELAXED);
    spurs_kernel_drain(5000);

    cellSpursFinalize(&spurs);
    int fin_ok = !spurs_kernel_running();
    printf("  [FINALIZE   ] kernel joined, running=%d                        %s\n",
           spurs_kernel_running(), fin_ok ? "OK" : "FAIL");
    ok &= fin_ok;

    if (ok)
        printf("  PASS: kick de task e' assincrono e coalescido -- nunca corre no PPU.\n");
    else
        printf("  FAIL: kick de task bloqueou/executou na thread do PPU.\n");
    return ok ? 0 : 1;
}
