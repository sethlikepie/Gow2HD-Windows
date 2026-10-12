/* spurs_kernel.c — SPURS HLE kernel: asynchronous workload execution.
 * See spurs_kernel.h for the design and scope. */

#include "spurs_kernel.h"
#include "ps3_trace.h"   /* Task 4 (bringup-instrumentacao-fase0): subsistema SPURS */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif
#include <errno.h>
#include <sys/time.h>
#endif

/* Capacidade da ring de jobs.
 *
 * Com o coalescing por task do cellSpurs.c (no maximo UMA entrada em fila por
 * task viva), as entradas de task estao limitadas por CELL_SPURS_MAX_TASK=128 —
 * um limite provado, nao um palpite: um kick para uma task que ja esta em fila
 * dobra-se na que la esta em vez de acrescentar outra. Os restantes 384 slots
 * sobram para os workloads do cellSpursAddWorkload, que nao sao coalescidos
 * (cada AddWorkload e' um pedido distinto). Antes disto a ring enchia por
 * construcao: o kick_all enfileirava uma entrada por task a cada push de
 * LFQueue, ou seja por frame. ~20 KB de estado. */
#define SPURS_KQ_CAP 512   /* job ring-buffer capacity */

typedef struct {
    /* Sync primitives (mirror cellSpurs.c's portable pattern). */
#ifdef _WIN32
    CRITICAL_SECTION   cs;
    CONDITION_VARIABLE cv_work;   /* kernel waits here for a job / stop */
    CONDITION_VARIABLE cv_done;   /* submitters wait here for completion */
    HANDLE             thread;    /* legacy single handle (worker 0) */
    HANDLE             workers[8];
#else
    pthread_mutex_t    mtx;
    pthread_cond_t     cv_work;
    pthread_cond_t     cv_done;
    pthread_t          thread;
    pthread_t          workers[8];
#endif
    unsigned           nworkers;
    spurs_dispatch_fn  dispatch;
    void*              user;
    unsigned           nSpus;

    spurs_workload_job q[SPURS_KQ_CAP];
    unsigned           head, tail;     /* ring indices */
    unsigned           submitted;      /* total enqueued */
    unsigned           completed;      /* total dispatched */

    int                running;
    int                started;
} spurs_kernel;

static spurs_kernel g_k;

/* ---- portable sync helpers -------------------------------------------------*/
static void k_lock(void)      {
#ifdef _WIN32
    EnterCriticalSection(&g_k.cs);
#else
    pthread_mutex_lock(&g_k.mtx);
#endif
}
static void k_unlock(void)    {
#ifdef _WIN32
    LeaveCriticalSection(&g_k.cs);
#else
    pthread_mutex_unlock(&g_k.mtx);
#endif
}
static void k_wait_work(void) {
#ifdef _WIN32
    SleepConditionVariableCS(&g_k.cv_work, &g_k.cs, INFINITE);
#else
    pthread_cond_wait(&g_k.cv_work, &g_k.mtx);
#endif
}
static void k_signal_work(void) {
#ifdef _WIN32
    WakeAllConditionVariable(&g_k.cv_work);
#else
    pthread_cond_broadcast(&g_k.cv_work);
#endif
}
static void k_signal_done(void) {
#ifdef _WIN32
    WakeAllConditionVariable(&g_k.cv_done);
#else
    pthread_cond_broadcast(&g_k.cv_done);
#endif
}
/* Wait for completion with a timeout; returns 1 if signaled, 0 on timeout.
 * Caller holds the lock. */
static int k_wait_done(unsigned timeout_ms) {
#ifdef _WIN32
    return SleepConditionVariableCS(&g_k.cv_done, &g_k.cs,
                                    timeout_ms ? timeout_ms : 0) ? 1 : 0;
#else
    struct timeval now;
    struct timespec ts;
    gettimeofday(&now, NULL);
    uint64_t ns = (uint64_t)now.tv_usec * 1000ull + (uint64_t)timeout_ms * 1000000ull;
    ts.tv_sec  = now.tv_sec + (time_t)(ns / 1000000000ull);
    ts.tv_nsec = (long)(ns % 1000000000ull);
    int r = pthread_cond_timedwait(&g_k.cv_done, &g_k.mtx, &ts);
    return r == 0 ? 1 : 0;
#endif
}

/* ---- winport v76-spurs-kick (engine_fixes_v76_spurs_kick.py) ---- */
static int wp76_wake1(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("WINPORT_SPURS_WAKE1");
        v = (e && e[0] == '1') ? 1 : 0;
        if (v) { fprintf(stderr, "[SPURS76] kernel submit wakes ONE worker\n"); fflush(stderr); }
    }
    return v;
}
static unsigned long long g_wp76_kspin_ns = ~0ull;   /* ~0 = not initialised yet (spurs_kernel_start does it) */
static unsigned g_wp76_kspin_max = 1;
static unsigned g_wp76_spinners = 0;                  /* idle workers spinning now -- guarded by g_k's lock */
static volatile long long g_wp76_ks[4];               /* submit, wakes, spin-hit, spin-miss */
static void wp76_kspin_init(void)
{
    if (g_wp76_kspin_ns != ~0ull) return;
    const char* e = getenv("WINPORT_SPURS_KSPIN_US");
    long v = (e && *e) ? atol(e) : 0;
    if (v < 0) v = 0;
    if (v > 2000) v = 2000;
    const char* m = getenv("WINPORT_SPURS_KSPIN_MAX");
    long mx = (m && *m) ? atol(m) : 1;
    if (mx < 1) mx = 1;
    if (mx > 4) mx = 4;
    g_wp76_kspin_max = (unsigned)mx;
    g_wp76_kspin_ns = (unsigned long long)v * 1000ull;
    if (v) { fprintf(stderr, "[SPURS76] idle worker spins up to %ld us before sleeping (max %ld spinner(s))\n", v, mx); fflush(stderr); }
}
int wp76_kernel_stats_on(void) { wp76_kspin_init(); return wp76_wake1() || g_wp76_kspin_ns != 0; }
void wp76_kernel_take(long long out[4])
{
    for (int i = 0; i < 4; i++) out[i] = __atomic_exchange_n(&g_wp76_ks[i], 0ll, __ATOMIC_RELAXED);
}
static void k_signal_one(void) {
#ifdef _WIN32
    WakeConditionVariable(&g_k.cv_work);
#else
    pthread_cond_signal(&g_k.cv_work);
#endif
}
/* Spin (lock NOT held) until a job is queued or the budget is used up. 1 = saw work. */
static int wp76_spin_wait(void)
{
#ifdef _WIN32
    static long long qf = 0;
    LARGE_INTEGER t0, t;
    if (!qf) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); qf = f.QuadPart; }
    const long long lim = (long long)(g_wp76_kspin_ns * (unsigned long long)qf / 1000000000ull);
    QueryPerformanceCounter(&t0);
    for (;;) {
        for (int i = 0; i < 64; i++) {
            if (__atomic_load_n(&g_k.head, __ATOMIC_ACQUIRE) != __atomic_load_n(&g_k.tail, __ATOMIC_ACQUIRE)) return 1;
            YieldProcessor();
        }
        if (!__atomic_load_n(&g_k.running, __ATOMIC_RELAXED)) return 0;
        QueryPerformanceCounter(&t);
        if (t.QuadPart - t0.QuadPart >= lim) return 0;
    }
#else
    return 0;
#endif
}
/* ---- end winport v76-spurs-kick ---- */
/* ---- kernel thread ---------------------------------------------------------*/
static void kernel_loop(void)
{
#ifdef __APPLE__
    /* Os workers do SPURS correm os jobs SPU liftados, que sao a computacao
     * mais pesada do gameplay. Sem dica de QoS o macOS pode pousa-los nos
     * E-cores. PS3_QOS=0 desliga, para A/B. */
    { const char* q = getenv("PS3_QOS");
      if (!(q && q[0] == '0'))
          pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0); }
#endif
    k_lock();
    int wp76_spun = 0;   /* winport v76-spurs-kick */
    for (;;) {
        while (g_k.running && g_k.head == g_k.tail) {
            if (g_wp76_kspin_ns && g_wp76_kspin_ns != ~0ull && !wp76_spun && g_wp76_spinners < g_wp76_kspin_max) {
                /* winport v76-spurs-kick: short spin for the next job before sleeping (lock released) */
                wp76_spun = 1; g_wp76_spinners++;
                k_unlock();
                const int hit = wp76_spin_wait();
                k_lock();
                g_wp76_spinners--;
                __atomic_add_fetch(&g_wp76_ks[hit ? 2 : 3], 1ll, __ATOMIC_RELAXED);
                continue;   /* re-check the queue under the lock */
            }
            /* Task 4 (bringup-instrumentacao-fase0): WORKER_IDLE -- this
             * worker has nothing queued and is about to block on cv_work.
             * Emitted while g_k.mtx/cs is held: ps3_trace_emit() takes no
             * application lock of its own (only stdio's per-stream lock on
             * stderr), so this cannot deadlock against g_k's lock or any
             * other lock in the process -- see the lock-safety table in
             * .superpowers/sdd/task-4-report.md. It does add one fwrite+
             * fflush of latency to this critical section while
             * PS3_TRACE_SPURS is on (diagnostic runs only). */
            PS3_TRACE(PS3_TS_SPURS, "kernel_loop", "WORKER_IDLE", "-");
            k_wait_work();
        }
        if (!g_k.running && g_k.head == g_k.tail)
            break;

        /* Pop one job. */
        spurs_workload_job job = g_k.q[g_k.head];
        unsigned pop_pos = g_k.head;
        g_k.head = (g_k.head + 1u) % SPURS_KQ_CAP;
        wp76_spun = 0;   /* winport v76-spurs-kick */

        spurs_dispatch_fn fn = g_k.dispatch;
        void* user = g_k.user;

        /* Run the workload OUTSIDE the lock (it may take time / re-enter). */
        k_unlock();
        /* QUEUE_POP: emitted after k_unlock(), using only locals captured
         * under the lock (pop_pos/job) -- never re-enters g_k's lock. */
        PS3_TRACE(PS3_TS_SPURS, "kernel_loop", "QUEUE_POP",
                  "pos=%u wid=%u is_task=%d", pop_pos, job.wid, job.is_task);
        if (fn)
            fn(&job, user);
        k_lock();

        g_k.completed++;
        k_signal_done();
    }
    k_unlock();
}

#ifdef _WIN32
static DWORD WINAPI kernel_thread_main(LPVOID arg) { (void)arg; kernel_loop(); return 0; }
#else
static void* kernel_thread_main(void* arg)          {
#ifdef __APPLE__
    pthread_setname_np("host:spurs-worker");
#endif
 (void)arg; kernel_loop(); return NULL; }
#endif

/* Worker count = virtual SPUs. Real SPURS runs workloads on nSpus SPUs
 * concurrently; a single worker serializes every task, so one long/spinning
 * task (e.g. GoW2's decoder-control task waiting on its command queue)
 * starves the whole pipeline. */
#define SPURS_MAX_WORKERS 8
static unsigned spurs_worker_count(unsigned nSpus)
{
    const char* env = getenv("PS3_SPU_WORKERS");
    unsigned n = env ? (unsigned)atoi(env) : (nSpus ? nSpus : 4);
    if (n < 1) n = 1;
    if (n > SPURS_MAX_WORKERS) n = SPURS_MAX_WORKERS;
    return n;
}

/* ---- public API ------------------------------------------------------------*/
void spurs_kernel_start(unsigned nSpus, spurs_dispatch_fn fn, void* user)
{
    if (g_k.started) {
        /* Already running — just (re)bind the dispatch callback. */
        k_lock();
        g_k.dispatch = fn;
        g_k.user     = user;
        g_k.nSpus    = nSpus;
        k_unlock();
        return;
    }

    memset(&g_k, 0, sizeof(g_k));
    g_k.dispatch = fn;
    g_k.user     = user;
    g_k.nSpus    = nSpus;
    g_k.running  = 1;
    g_k.started  = 1;
    g_k.nworkers = spurs_worker_count(nSpus);
    wp76_kspin_init();   /* winport v76-spurs-kick */

#ifdef _WIN32
    InitializeCriticalSection(&g_k.cs);
    InitializeConditionVariable(&g_k.cv_work);
    InitializeConditionVariable(&g_k.cv_done);
    for (unsigned w = 0; w < g_k.nworkers; w++)
        g_k.workers[w] = CreateThread(NULL, 0, kernel_thread_main, NULL, 0, NULL);
    g_k.thread = g_k.workers[0];
    if (!g_k.thread) { g_k.running = 0; g_k.started = 0; }
#else
    pthread_mutex_init(&g_k.mtx, NULL);
    pthread_cond_init(&g_k.cv_work, NULL);
    pthread_cond_init(&g_k.cv_done, NULL);
    /* Stack grande nos workers, pelo mesmo motivo que o lado Windows corre cada
     * job numa thread de 256 MB (spu_workload.c): o lifter transforma `brsl` em
     * chamadas C aninhadas, e um decoder profundo (o spu1 e MPEG) rebenta uma
     * stack normal. Aqui o job corre NO worker, entao e o worker que tem de ser
     * dimensionado -- o default do pthread no macOS sao 512 KB, ~512x menos que
     * o Windows. Sem isto um job que COMPLETA no Windows e cortado limpo aqui
     * pela isolacao de falta (spu_workload.c): seguro, mas trabalho nenhum.
     * A stack e mapeada preguicosamente, portanto isto reserva espaco de
     * enderecamento, nao memoria fisica -- equivalente ao
     * STACK_SIZE_PARAM_IS_A_RESERVATION do CreateThread. */
    pthread_attr_t attr;
    pthread_attr_t* pattr = NULL;
    {
        const char* e = getenv("PS3_SPU_STACK_MB");
        size_t mb = e ? (size_t)strtoul(e, NULL, 10) : 256u;
        if (mb && pthread_attr_init(&attr) == 0) {
            if (pthread_attr_setstacksize(&attr, mb << 20) == 0)
                pattr = &attr;
            else
                pthread_attr_destroy(&attr);
        }
    }
    for (unsigned w = 0; w < g_k.nworkers; w++)
        if (pthread_create(&g_k.workers[w], pattr, kernel_thread_main, NULL) != 0) {
            g_k.nworkers = w;
            break;
        }
    if (pattr) pthread_attr_destroy(pattr);
    g_k.thread = g_k.workers[0];
    if (g_k.nworkers == 0) { g_k.running = 0; g_k.started = 0; }
#endif
    /* Unconditional: the only externally observable proof that the kernel came
     * up. The boot smoke greps for it -- without a line here, "kernel running"
     * is unfalsifiable from a log. Reports the count actually created, not the
     * count requested, so a partial pthread_create failure is visible. */
    fprintf(stderr, "[spurs kernel] started: %u worker(s), nSpus=%u\n",
            g_k.nworkers, nSpus);
}

int spurs_kernel_submit(const spurs_workload_job* job)
{
    if (!job || !g_k.started) return 0;
    int queued = 0;
    unsigned push_pos = 0;
    k_lock();
    if (g_k.running) {
        unsigned next = (g_k.tail + 1u) % SPURS_KQ_CAP;
        if (next != g_k.head) {            /* not full */
            push_pos = g_k.tail;
            g_k.q[g_k.tail] = *job;
            g_k.tail = next;
            g_k.submitted++;
            queued = 1;
            {   /* winport v76-spurs-kick: spinning idle workers cover the first jobs; else wake one (WAKE1) or all (today) */
                const unsigned qlen = (g_k.tail + SPURS_KQ_CAP - g_k.head) % SPURS_KQ_CAP;
                __atomic_add_fetch(&g_wp76_ks[0], 1ll, __ATOMIC_RELAXED);
                if (qlen > g_wp76_spinners) {
                    __atomic_add_fetch(&g_wp76_ks[1], 1ll, __ATOMIC_RELAXED);
                    if (wp76_wake1()) k_signal_one(); else k_signal_work();
                }
            }
        }
    }
    k_unlock();
    /* QUEUE_PUSH: emitted after k_unlock(), locals only (job/push_pos were
     * captured under the lock above) -- never re-enters g_k's lock. */
    if (queued) {
        PS3_TRACE(PS3_TS_SPURS, "spurs_kernel_submit", "QUEUE_PUSH",
                  "pos=%u wid=%u is_task=%d", push_pos, job->wid, job->is_task);
    }
    return queued;
}

int spurs_kernel_drain(unsigned timeout_ms)
{
    if (!g_k.started) return 1;
    int drained;
    k_lock();
    while (g_k.completed < g_k.submitted) {
        if (!k_wait_done(timeout_ms) && timeout_ms) break; /* timed out */
    }
    drained = (g_k.completed >= g_k.submitted);
    k_unlock();
    return drained;
}

void spurs_kernel_stop(void)
{
    if (!g_k.started) return;
    k_lock();
    g_k.running = 0;
    k_signal_work();
    k_unlock();

#ifdef _WIN32
    for (unsigned w = 0; w < g_k.nworkers; w++) {
        if (!g_k.workers[w]) continue;
        WaitForSingleObject(g_k.workers[w], INFINITE);
        CloseHandle(g_k.workers[w]);
        g_k.workers[w] = NULL;
    }
    g_k.thread = NULL;
    DeleteCriticalSection(&g_k.cs);
#else
    for (unsigned w = 0; w < g_k.nworkers; w++)
        pthread_join(g_k.workers[w], NULL);
    pthread_mutex_destroy(&g_k.mtx);
    pthread_cond_destroy(&g_k.cv_work);
    pthread_cond_destroy(&g_k.cv_done);
#endif
    g_k.started = 0;
}

int      spurs_kernel_running(void)   { return g_k.started && g_k.running; }
unsigned spurs_kernel_submitted(void) { return g_k.submitted; }
unsigned spurs_kernel_completed(void) { return g_k.completed; }
