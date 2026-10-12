/*
 * ps3recomp - SPU channel + indirect-branch runtime glue
 *
 * Implements the externs the SPU lifter (tools/spu_lifter.py) emits:
 *   - spu_rdch / spu_rchcnt / spu_wrch : SPU channel access. MFC channels are
 *     routed to the DMA engine (spu_dma.h); mailboxes, signal notification,
 *     events and the decrementer use the spu_context channel fields.
 *   - spu_indirect_branch : resolves ctx->pc to a lifted spu_func_* via a
 *     registry that generated code populates by calling spu_recomp_register().
 *
 * The MFC engine state is kept per spu_context here (spu_context.h does not
 * embed one), in a small lazily-populated registry.
 */

/* Task 4 (bringup-instrumentacao-fase0): opt this real production TU into
 * spu_dma.h's DMA trace hooks (subsistema DMA -- GET/PUT/LIST/TAG_WAIT/DONE).
 * spu_channels.c is the ONLY real-runtime consumer of spu_dma.h -- it is not
 * pulled into any runtime/spu/tests/ binary (those provide their own local
 * spu_wrch stub that calls mfc_channel_write() directly, bypassing this
 * file), so defining the macro here cannot affect the existing SPU test
 * suite. See the big comment at the top of spu_dma.h for the full reasoning
 * (why the hooks are compile-time gated instead of unconditional). */
#define SPU_DMA_TRACE_HOOKS 1
#include "spu_dma.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>    /* atoi/getenv: gates PS3_* deste modulo */
#include <setjmp.h>
#ifdef _WIN32
#include <windows.h>   /* Sleep (taskset WAIT_SIGNAL poll) */
/* winport: mingw */ /* E190 reservation state, Windows side (see the POSIX branch) */
__thread uint32_t t_spu_rsv_ea = 0;
__thread int      t_spu_rsv_valid = 0;
__thread int      t_spu_atomic_stat = 0;
__thread uint8_t  t_spu_rsv_line[128];
static SRWLOCK s_atomic_line_lock = SRWLOCK_INIT;
void ps3_atomic_line_lock(void)   { AcquireSRWLockExclusive(&s_atomic_line_lock); }
void ps3_atomic_line_unlock(void) { ReleaseSRWLockExclusive(&s_atomic_line_lock); }
#else
#include <time.h>      /* clock_gettime (taskset timeslice) */

/* E190: estado de reserva GETLLAR/PUTLLC por thread de job SPU e o mutex partilhado com o
 * HLE do PPU (cellSpursLFQueue.c) para a comparacao+escrita da linha ser atomica. */
#include <pthread.h>
__thread uint32_t t_spu_rsv_ea = 0;
__thread int      t_spu_rsv_valid = 0;
__thread int      t_spu_atomic_stat = 0;
__thread uint8_t  t_spu_rsv_line[128];
static pthread_mutex_t s_atomic_line_mtx = PTHREAD_MUTEX_INITIALIZER;
void ps3_atomic_line_lock(void)   { pthread_mutex_lock(&s_atomic_line_mtx); }
void ps3_atomic_line_unlock(void) { pthread_mutex_unlock(&s_atomic_line_mtx); }
#endif

/* Monotonic nanoseconds. Never returns 0: spu_context.task_idle_ns uses 0 as
 * "not armed yet", so a legitimate timestamp must be distinguishable from it. */
static uint64_t spu_now_ns(void)
{
    uint64_t v;
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    v = f.QuadPart ? (uint64_t)((c.QuadPart * 1000000000ull) / (uint64_t)f.QuadPart) : 0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    v = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
    return v ? v : 1;
}

/* Tiny logging bridge for RE traces injected into lifted SPU code (keeps the
 * generated .c free of stdio plumbing). */
int spu_dbg_log(const char* fmt, unsigned a, unsigned b)
{
    fprintf(stderr, fmt, a, b);
    fflush(stderr);
    return 0;
}

/* ---- SPURS taskset syscall HLE ------------------------------------------
 * The SDK task library calls the LS address in SpursTasksetContext.syscallAddr
 * (LS 0x27C4) for EXIT/YIELD/WAIT_SIGNAL/POLL/RECV_WKL_FLAG. We point it at a
 * reserved LS address and emulate here (semantics: RPCS3
 * spursTasksetProcessSyscall). Convention: r3 preferred slot = syscall number
 * (bit 0x10 = "2" variant, ignored), r4 preferred = args; result in r3;
 * execution resumes inline (bisl-style call). */
#define SPU_TASKSET_SYSCALL_LS 0x1FF00u

static uint32_t ls_rd32(const uint8_t* p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }

/* ---- taskset timeslice: when must a task give the SPU back? --------------
 *
 * A SPURS task's idle loop is an UNBOUNDED spin by construction. GoW2's spu0
 * worker (LS 0x3118..0x3190) is the canonical shape:
 *
 *     0x3118  rc = LFQueue try-pop(queue)      ; queue EA from the task arg
 *             if (rc == 0) goto 0x3160         ; got an element -> do the work
 *     0x3130  rc = cellSpursPoll()             ; syscall 3
 *             if (rc != 0) goto 0x3190         ; kernel wants the SPU -> yield
 *     0x313C  goto 0x3118                      ; nothing to do -> spin again
 *     0x3190  cellSpursYield()                 ; syscall 1
 *
 * On hardware that spin is correct: the task owns a physical SPU until the
 * SPURS kernel preempts it at a timeslice boundary, at which point Poll starts
 * answering "yes, relinquish". Here the "SPU" is one of a handful of pooled
 * host worker threads (libs/spurs/spurs_kernel.c), so a task that never
 * relinquishes pins a worker forever -- MEASURED: both workers wedged in this
 * loop, the kernel job ring then filled, and the PPU-side fallback ran the
 * next task inline on the main thread, which parked there for good.
 *
 * Poll therefore has to answer the same question the real kernel answers, and
 * the honest local signal is "has this task moved any data recently?". The
 * pop-empty spin only ever issues GETLLAR against the queue control block; the
 * moment there IS work, the task DMAs the element in and DMAs a completion
 * back out. So: data moved => still productive, keep the SPU; no data moved
 * for a whole slice => idle, hand the SPU back to the kernel, which re-kicks
 * the task on the next LFQueue push (spurs_task_kick_all).
 *
 * PS3_SPU_TASK_IDLE_MS tunes the slice; 0 restores the old never-relinquish
 * behaviour for A/B comparison. The 1 ms default keeps a recognized workload
 * from parking the host worker for the historical 20 ms while it is idle. */
static unsigned spu_task_idle_ms(void)
{
    static int ms = -1;
    if (ms < 0) {
        const char* e = getenv("PS3_SPU_TASK_IDLE_MS");
        ms = e ? atoi(e) : 1;
        if (ms < 0) ms = 0;
    }
    return (unsigned)ms;
}

/* Does this MFC opcode actually move payload? Reservation ops (GETLLAR and the
 * PUTLL* family) and the ordering ops are how an EMPTY-queue poll looks, so
 * they must NOT count as progress -- otherwise the idle spin would keep
 * refreshing its own liveness and never relinquish. Note the bit tests in
 * spu_dma.h classify 0xD0/0xB4/0xC0/0xCC as get/put, hence the explicit list. */
static int mfc_cmd_moves_data(uint32_t cmd)
{
    switch (cmd) {
    case 0xD0: case 0xB4: case 0xB0: case 0xB8:      /* getllar / putll* */
    case MFC_BARRIER_CMD: case MFC_EIEIO_CMD: case MFC_SYNC_CMD:
        return 0;
    default:
        return mfc_is_get(cmd) || mfc_is_put(cmd);
    }
}

/* ---- winport v79-spu-side (engine_fixes_v79_spu_side.py) ---- */
volatile unsigned g_wp79_qline = 0;          /* EA of the PPU2SPU job queue header (cellSpursLFQueue.c push) */
static int g_wp79_st = -1;                   /* WINPORT_SPU_STATS79 */
static int wp79_stats(void)
{
    if (g_wp79_st < 0) { const char* e = getenv("WINPORT_SPU_STATS79"); g_wp79_st = (e && e[0] == '1') ? 1 : 0;
                         if (g_wp79_st) { fprintf(stderr, "[SPUW79] SPU-side job chain counters ON\n"); fflush(stderr); } }
    return g_wp79_st;
}
/* 0 getllar 1 putllc-ok 2 putllc-fail 3 putlluc 4 q-getllar 5 q-putllc-ok 6 q-putllc-fail 7 get-n 8 get-bytes
 * 9 put-n 10 put-bytes 11 poll 12 relinquish 13 idle-tail-ms 14 setup-ns */
#define WP79_N 15
static volatile long long g_wp79[WP79_N];
static __thread long long t_wp79[WP79_N];
static __thread unsigned t_wp79_k;
static void wp79_flush(void)
{
    for (int i = 0; i < WP79_N; i++) if (t_wp79[i]) { __atomic_add_fetch(&g_wp79[i], t_wp79[i], __ATOMIC_RELAXED); t_wp79[i] = 0; }
#ifdef _WIN32
    static volatile long long last = 0;
    const long long now = (long long)GetTickCount64();
    long long l = __atomic_load_n(&last, __ATOMIC_RELAXED);
    if (!l) { __atomic_compare_exchange_n(&last, &l, now, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED); return; }
    if (now - l < 1000 || !__atomic_compare_exchange_n(&last, &l, now, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return;
    const double s = (double)(now - l) / 1000.0;
    long long v[WP79_N]; for (int i = 0; i < WP79_N; i++) v[i] = __atomic_exchange_n(&g_wp79[i], 0ll, __ATOMIC_RELAXED);
    fprintf(stderr, "[SPUW79] getllar=%.0f/s putllc ok=%.0f fail=%.0f putlluc=%.0f | job-queue line 0x%08X: getllar=%.0f putllc ok=%.0f fail=%.0f"
                    " | dma get=%.0f/s %.0fKB/s put=%.0f/s %.0fKB/s | poll=%.0f/s relinquish=%.0f/s idle-tail=%.0fms/s | setup=%.1fms/s\n",
            v[0] / s, v[1] / s, v[2] / s, v[3] / s, (unsigned)g_wp79_qline, v[4] / s, v[5] / s, v[6] / s,
            v[7] / s, v[8] / 1024.0 / s, v[9] / s, v[10] / 1024.0 / s, v[11] / s, v[12] / s, v[13] / s, v[14] / 1e6 / s);
    fflush(stderr);
#endif
}
static inline void wp79_add(int i, long long d) { t_wp79[i] += d; if ((++t_wp79_k & 1023u) == 0) wp79_flush(); }
void wp79_note_setup(long long ns) { if (wp79_stats()) wp79_add(14, ns); }   /* spu_workload.c */
/* MFC_Cmd accounting, around mfc_channel_write (cmd already decoded by the caller). */
static void wp79_note_cmd(spu_context* ctx, uint32_t cmd)
{
    extern __thread int t_spu_atomic_stat;
    const uint32_t line = ctx->mfc_eal & ~127u;
    const int onq = g_wp79_qline && line == (g_wp79_qline & ~127u);
    switch (cmd) {
    case 0xD0: wp79_add(0, 1); if (onq) wp79_add(4, 1); return;
    case 0xB4: { const int f = t_spu_atomic_stat ? 1 : 0; wp79_add(1 + f, 1); if (onq) wp79_add(5 + f, 1); return; }
    case 0xB0: case 0xB8: wp79_add(3, 1); return;
    default:
        if (mfc_cmd_moves_data(cmd)) {
            if (mfc_is_get(cmd)) { wp79_add(7, 1); wp79_add(8, ctx->mfc_size); }
            else                 { wp79_add(9, 1); wp79_add(10, ctx->mfc_size); }
        }
        return;
    }
}
static int g_wp79_mc = -1;                   /* WINPORT_MFC_CACHE79 */
static __thread spu_context* t_wp79_mctx = 0;
static __thread mfc_engine*  t_wp79_meng = 0;
/* ---- end winport v79-spu-side ---- */
/* ---- winport v83-spuprof (engine_fixes_v83_spuprof.py): WINPORT_SPU_PROF83=1 ---- */
int g_wp83_on = -1;
static inline unsigned long long wp83_tsc(void) { return __builtin_ia32_rdtsc(); }
/* segment kinds */
enum { W83_NONE = 0, W83_PEND = 1, W83_JOB = 2, W83_START = 3, W83_K = 4 };   /* PEND closes as POP or EMPTY */
enum { W83_POP = 0, W83_JOBK = 1, W83_EMPTY = 2, W83_STARTK = 3, W83_NK = 4 };
/* per-kind fields: 0 n, 1 tsc, 2 atomic tsc, 3 dma tsc, 4 lookup tsc, 5 syscall tsc, 6 n atomic, 7 n dma, 8 dma bytes,
 * 9 n lookup, 10 n q-line atomic */
#define W83_F 11
#define W83_NT 64
#define W83_NI 8
typedef struct { unsigned type; long long n, tsc, dma, at, lk, ndma; } w83_type;
static SRWLOCK s_w83_lock = SRWLOCK_INIT;
static long long g_w83k[W83_NK][W83_F];
static w83_type g_w83t[W83_NT];
static long long g_w83i[W83_NI][5];   /* n, init+copyin, run, copyout, (unused) */
static __thread long long t_w83k[W83_NK][W83_F];
static __thread w83_type t_w83t[W83_NT];
static __thread long long t_w83i[W83_NI][5];
static __thread int t_w83_kind, t_w83_img;
static __thread unsigned t_w83_type, t_w83_nflush;
static __thread unsigned long long t_w83_t0, t_w83_sub[4], t_w83_ph[4];
static __thread long long t_w83_cnt[5];      /* n atomic, n dma, dma bytes, n lookup, n qline atomic (current segment) */
static unsigned long long s_w83_tsc0; static LARGE_INTEGER s_w83_qpc0;
static int wp83_resolve(void)
{
    const char* e = getenv("WINPORT_SPU_PROF83");
    int v = (e && e[0] == '1') ? 1 : 0;
    if (v) { QueryPerformanceCounter(&s_w83_qpc0); s_w83_tsc0 = wp83_tsc();
             fprintf(stderr, "[SPUP83] SPU job cost profiler ON (rdtsc)\n"); fflush(stderr); }
    g_wp83_on = v;
    return v;
}
static void wp83_flush(int force)
{
    AcquireSRWLockExclusive(&s_w83_lock);
    for (int k = 0; k < W83_NK; k++) for (int f = 0; f < W83_F; f++) { g_w83k[k][f] += t_w83k[k][f]; t_w83k[k][f] = 0; }
    for (int i = 0; i < W83_NI; i++) for (int f = 0; f < 5; f++) { g_w83i[i][f] += t_w83i[i][f]; t_w83i[i][f] = 0; }
    for (int j = 0; j < W83_NT; j++) {
        w83_type* a = &t_w83t[j];
        if (!a->n) continue;
        int slot = -1;
        for (int q = 0; q < W83_NT; q++) { if (g_w83t[q].n && g_w83t[q].type == a->type) { slot = q; break; } if (slot < 0 && !g_w83t[q].n) slot = q; }
        if (slot >= 0) { w83_type* g = &g_w83t[slot]; if (!g->n) g->type = a->type;
            g->n += a->n; g->tsc += a->tsc; g->dma += a->dma; g->at += a->at; g->lk += a->lk; g->ndma += a->ndma; }
        memset(a, 0, sizeof *a);
    }
    static long long last = 0;
    const long long now = (long long)GetTickCount64();
    if (!last) last = now;
    if (now - last >= 1000) {
        const double s = (double)(now - last) / 1000.0; last = now;
        LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f);
        const double secs = (double)(q.QuadPart - s_w83_qpc0.QuadPart) / (double)f.QuadPart;
        const double tpu = secs > 0 ? (double)(wp83_tsc() - s_w83_tsc0) / secs / 1e6 : 3000.0;   /* tsc ticks per us */
        const double ms = 1.0 / (tpu * 1000.0 * s);   /* tsc -> ms per second */
        char b[1024]; int o = 0;
        o += snprintf(b + o, sizeof b - o, "[SPUP83] img n/s init+in/run/out ms/s:");
        for (int i = 0; i < W83_NI; i++) if (g_w83i[i][0])
            o += snprintf(b + o, sizeof b - o, " i%d %.0f %.1f/%.1f/%.1f", i, g_w83i[i][0] / s, g_w83i[i][1] * ms, g_w83i[i][2] * ms, g_w83i[i][3] * ms);
        fprintf(stderr, "%s\n", b);
        static const char* kn[W83_NK] = { "POP", "JOB", "EMPTY", "START" };
        for (int k = 0; k < W83_NK; k++) { long long* v = g_w83k[k]; if (!v[0]) continue;
            const double c = (double)(v[1] - v[2] - v[3] - v[4] - v[5]);
            fprintf(stderr, "[SPUP83] %-5s n=%.0f/s tot=%.1fms/s avg=%.2fus | atomic %.1fms (%.1f/seg, qline %.1f) dma %.1fms (%.1f/seg %.0fB/seg)"
                            " lookup %.1fms (%.1f/seg) syscall %.1fms | compute %.1fms avg %.2fus\n",
                    kn[k], v[0] / s, v[1] * ms, v[1] / tpu / v[0], v[2] * ms, (double)v[6] / v[0], (double)v[10] / v[0],
                    v[3] * ms, (double)v[7] / v[0], (double)v[8] / v[0], v[4] * ms, (double)v[9] / v[0], v[5] * ms, c * ms, c / tpu / v[0]); }
        /* top 8 job types by time */
        int used[W83_NT] = {0}; o = 0;
        o += snprintf(b + o, sizeof b - o, "[SPUP83] top jobs (type n/s avg_us dma_us atomic_us lookup_us ndma):");
        for (int r = 0; r < 8; r++) { int bi = -1;
            for (int j = 0; j < W83_NT; j++) if (g_w83t[j].n && !used[j] && (bi < 0 || g_w83t[j].tsc > g_w83t[bi].tsc)) bi = j;
            if (bi < 0) break; used[bi] = 1; w83_type* t = &g_w83t[bi];
            o += snprintf(b + o, sizeof b - o, " | 0x%05X %.0f %.2f %.2f %.2f %.2f %.1f", t->type, t->n / s, t->tsc / tpu / t->n,
                          t->dma / tpu / t->n, t->at / tpu / t->n, t->lk / tpu / t->n, (double)t->ndma / t->n); }
        fprintf(stderr, "%s\n", b); fflush(stderr);
        memset(g_w83k, 0, sizeof g_w83k); memset(g_w83t, 0, sizeof g_w83t); memset(g_w83i, 0, sizeof g_w83i);
    }
    (void)force;
    ReleaseSRWLockExclusive(&s_w83_lock);
}
static void wp83_close(unsigned long long now, int kind)
{
    long long* v = t_w83k[kind];
    const unsigned long long d = now - t_w83_t0;
    v[0]++; v[1] += (long long)d; v[2] += t_w83_sub[0]; v[3] += t_w83_sub[1]; v[4] += t_w83_sub[2]; v[5] += t_w83_sub[3];
    v[6] += t_w83_cnt[0]; v[7] += t_w83_cnt[1]; v[8] += t_w83_cnt[2]; v[9] += t_w83_cnt[3]; v[10] += t_w83_cnt[4];
    if (kind == W83_JOBK) {
        int slot = -1;
        for (int q = 0; q < W83_NT; q++) { if (t_w83t[q].n && t_w83t[q].type == t_w83_type) { slot = q; break; } if (slot < 0 && !t_w83t[q].n) slot = q; }
        if (slot >= 0) { w83_type* a = &t_w83t[slot]; a->type = t_w83_type; a->n++; a->tsc += (long long)d;
            a->dma += t_w83_sub[1]; a->at += t_w83_sub[0]; a->lk += t_w83_sub[2]; a->ndma += t_w83_cnt[1]; }
    }
    t_w83_sub[0] = t_w83_sub[1] = t_w83_sub[2] = t_w83_sub[3] = 0;
    memset(t_w83_cnt, 0, sizeof t_w83_cnt);
    if ((++t_w83_nflush & 4095u) == 0) wp83_flush(0);
}
static void wp83_open(unsigned long long now, int kind) { t_w83_kind = kind; t_w83_t0 = now; }
/* the spu_lifted_job.h phases: 0 entry, 1 after LS copy-in, 2 after the run, 3 after copy-out */
void wp83_job_phase(int image_id, int ph)
{
    if (g_wp83_on < 0) wp83_resolve();
    if (g_wp83_on <= 0) return;
    const unsigned long long now = wp83_tsc();
    t_w83_ph[ph] = now;
    if (ph == 1) { t_w83_img = image_id; t_w83_sub[0] = t_w83_sub[1] = t_w83_sub[2] = t_w83_sub[3] = 0; memset(t_w83_cnt, 0, sizeof t_w83_cnt);
                   if (image_id == 1) wp83_open(now, W83_START); else t_w83_kind = W83_NONE; }
    if (ph == 2) { t_w83_kind = W83_NONE; }
    if (ph == 3) { long long* v = t_w83i[image_id & (W83_NI - 1)];
        v[0]++; v[1] += (long long)(t_w83_ph[1] - t_w83_ph[0]); v[2] += (long long)(t_w83_ph[2] - t_w83_ph[1]); v[3] += (long long)(now - t_w83_ph[2]);
        t_w83_img = 0; wp83_flush(0); }
}
/* MFC command / list ack timing (spu_wrch_slow) */
static void wp83_mfc(spu_context* ctx, uint32_t channel, uint32_t v, unsigned long long d)
{
    if (channel != MFC_Cmd && channel != MFC_WrListStallAck) return;
    const uint32_t cmd = v & 0xFFu;
    if (channel == MFC_Cmd && (cmd == 0xD0 || cmd == 0xB4 || cmd == 0xB0 || cmd == 0xB8)) {
        t_w83_sub[0] += d; t_w83_cnt[0]++;
        if (g_wp79_qline && (ctx->mfc_eal & ~127u) == (g_wp79_qline & ~127u)) t_w83_cnt[4]++;
    } else { t_w83_sub[1] += d; t_w83_cnt[1]++; if (channel == MFC_Cmd && mfc_cmd_moves_data(cmd)) t_w83_cnt[2] += ctx->mfc_size; }
}
/* indirect branch: lookup cost, the Poll event and the job-dispatch event (image 1 only) */
static void wp83_ib(spu_context* ctx, uint32_t target, unsigned long long t0, int is_poll)
{
    const unsigned long long now = wp83_tsc();
    if (!is_poll) { t_w83_sub[2] += now - t0; t_w83_cnt[3]++; }
    if (ctx->image_id != 1 || t_w83_kind == W83_NONE) return;
    if (is_poll) {
        if (t_w83_kind == W83_JOB) wp83_close(t0, W83_JOBK);
        else if (t_w83_kind == W83_PEND) wp83_close(t0, W83_EMPTY);
        else if (t_w83_kind == W83_START) wp83_close(t0, W83_STARTK);
        wp83_open(t0, W83_PEND);
    } else if ((ctx->gpr[0]._u32[0] & SPU_LS_MASK) == 0x3180u) {
        if (t_w83_kind == W83_PEND) wp83_close(now, W83_POP);
        else if (t_w83_kind == W83_JOB) wp83_close(now, W83_JOBK);
        else if (t_w83_kind == W83_START) wp83_close(now, W83_STARTK);
        wp83_open(now, W83_JOB); t_w83_type = target;
    }
}
/* ---- end winport v83-spuprof ---- */
/* Idle time of this job, in ms.
 *
 * task_idle_ns == 0 means "the clock is not running": either the job has just
 * started (spu_context_init zeroed it) or it moved data since the last poll
 * (spu_wrch disarms it, which costs one store and no clock read on the DMA
 * hot path). Either way the answer is "not idle", and the clock restarts from
 * here -- so idleness is always measured within one job, never across two. */
static unsigned spu_task_idle_elapsed_ms(spu_context* ctx)
{
    /* E469: quem chama isto e' o caso 3 (POLL, "o kernel quer o SPU de volta?"),
     * que o codigo SPU corre num laco apertado. Ler o relogio em TODA sondagem
     * punha clock_gettime a 9,7% do tempo PROPRIO de uma thread de SPU
     * (mach_absolute_time no perfil do gameplay, com a thread 97% ocupada).
     *
     * A decisao que isto alimenta -- ceder o SPU depois de N MILISSEGUNDOS
     * parado -- nao precisa de melhor que milissegundos, portanto amostrar o
     * relogio a cada 256 sondagens da' a mesma resposta por 1/256 do custo.
     *
     * O ARMAR continua exacto: com o relogio desarmado (task_idle_ns == 0, o
     * que spu_wrch faz a cada movimento de dados) lemos na hora, senao a
     * primeira sondagem depois de mover dados nao registava o inicio e a
     * ociosidade era medida a partir do sitio errado. */
    if (!ctx->task_idle_ns) {
        ctx->task_idle_ns = spu_now_ns();
        ctx->idle_poll_n = 0;
        ctx->idle_last_ms = 0;
        return 0;
    }
    if ((++ctx->idle_poll_n & 0xFFu) != 0u)
        return ctx->idle_last_ms;
    const uint64_t now = spu_now_ns();
    ctx->idle_last_ms = (now <= ctx->task_idle_ns)
                            ? 0u
                            : (unsigned)((now - ctx->task_idle_ns) / 1000000ull);
    return ctx->idle_last_ms;
}

static int spu_task_should_relinquish(spu_context* ctx, unsigned* idle_ms_out)
{
    unsigned budget = spu_task_idle_ms();
    unsigned idle   = spu_task_idle_elapsed_ms(ctx);
    if (idle_ms_out) *idle_ms_out = idle;
    return budget && idle >= budget;
}

/* ---- winport v76b-spu-runner: WINPORT_SPU_POLL_PAUSE=N (engine_fixes_v76b_spu_runner.py) ---- */
static void wp76_poll_pause(void)
{
    static int n = -1;
    if (n < 0) {
        const char* e = getenv("WINPORT_SPU_POLL_PAUSE");
        int v = (e && *e) ? atoi(e) : 0;
        if (v < 0) v = 0;
        if (v > 256) v = 256;
        n = v;
        if (n) { fprintf(stderr, "[SPUPOLL76] idle SPU task Poll: %d x pause after 16 empty polls\n", n); fflush(stderr); }
    }
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    for (int i = 0; i < n; i++) __builtin_ia32_pause();
#endif
}
void spu_taskset_syscall_hle(spu_context* ctx)
{
    extern unsigned char* vm_base;
    uint32_t num  = ctx->gpr[3]._u32[0] & 0x0F;
    uint32_t taskset_ea = ls_rd32(&ctx->ls[0x27B8 + 4]);   /* u64 low word */
    uint32_t task_id    = ls_rd32(&ctx->ls[0x27D4]);

    { static int n = 0;
      if (n < 40) { fprintf(stderr,
            "[SPUSYS] syscall r3=0x%08X r4=0x%08X r5=0x%08X ts=0x%X id=%u\n",
            ctx->gpr[3]._u32[0], ctx->gpr[4]._u32[0], ctx->gpr[5]._u32[0],
            taskset_ea, task_id); n++; } }

    switch (num) {
    case 0:   /* EXIT (also probed by the task-lib early -- see log) */
        /* EXPERIMENT: GoW2's edgezlib task-lib calls [mgmt+196] with r3=0 as
         * its FIRST act, which cannot be a real exit. Return 0 and continue
         * to observe the next call; revisit with evidence. */
        { static int probe = -1;
          if (probe < 0) { extern char* getenv(const char*); probe = getenv("PS3_SPUSYS_EXIT") ? 1 : 0; }
          if (probe) {
              ctx->status = SPU_STATUS_STOPPED;
              if (ctx->exit_jmp) longjmp(*(jmp_buf*)ctx->exit_jmp, 1);
          } }
        ctx->gpr[3]._u32[0] = 0;
        return;
    case 2: { /* WAIT_SIGNAL: consume the signalled bit; wait bounded if unset */
        if (!taskset_ea || task_id >= 128) { ctx->gpr[3]._u32[0] = 0; return; }
        unsigned char* w = vm_base + taskset_ea + 0x40 /* signalled */ + 4u*(task_id/32);
        uint32_t bit = 0x80000000u >> (task_id % 32);
        for (int ms = 0; ms < 2000; ms++) {
            uint32_t v = ls_rd32(w);
            if (v & bit) {
                v &= ~bit;
                w[0]=(uint8_t)(v>>24); w[1]=(uint8_t)(v>>16); w[2]=(uint8_t)(v>>8); w[3]=(uint8_t)v;
                ctx->gpr[3]._u32[0] = 0;
                return;
            }
#ifdef _WIN32
            Sleep(1);
#endif
        }
        /* No signal arrived: give the SPU back (a later SendSignal re-kicks
         * the task from entry). */
        ctx->status = SPU_STATUS_STOPPED;
        if (ctx->exit_jmp) longjmp(*(jmp_buf*)ctx->exit_jmp, 1);
        return;
    }
    case 3: {  /* POLL: "does the kernel want this SPU back?" */
        unsigned idle = 0;
        int relinquish = spu_task_should_relinquish(ctx, &idle);
        if (wp79_stats()) { wp79_add(11, 1); if (relinquish) { wp79_add(12, 1); wp79_add(13, idle); wp79_flush(); } }   /* winport v79-spu-side */
        if (!relinquish && ctx->idle_poll_n >= 16u) wp76_poll_pause();   /* winport v76b-spu-runner */
        if (relinquish) {
            static int n = 0;
            if (n < 8) { n++; fprintf(stderr,
                "[SPUSLICE] poll -> relinquish (idle %ums, ts=0x%X id=%u)\n",
                idle, taskset_ea, task_id); fflush(stderr); }
        }
        ctx->gpr[3]._u32[0] = relinquish ? 1u : 0u;
        return;
    }
    case 1: {  /* YIELD: hand the SPU back to the SPURS kernel */
        /* The task asked to be descheduled. We have no task context save /
         * restore, so relinquishing means ENDING the job -- which is exactly
         * the contract spu_task_ls_setup (spu_workload.c) already documents:
         * a yielded task is re-kicked from its entry point by the next
         * LFQueue push. Ending it here is what frees the worker thread.
         *
         * Gated on the same idle test as POLL rather than unconditional: a
         * task that yields cooperatively WHILE still draining its queue
         * (data moving every iteration) should keep the SPU, exactly as the
         * kernel would let it. With PS3_SPU_TASK_IDLE_MS=0 this degrades to
         * the previous no-op, so the old behaviour stays reachable. */
        unsigned idle = 0;
        if (spu_task_should_relinquish(ctx, &idle)) {
            static int n = 0;
            if (n < 8) { n++; fprintf(stderr,
                "[SPUSLICE] yield -> job ends (idle %ums, ts=0x%X id=%u)\n",
                idle, taskset_ea, task_id); fflush(stderr); }
            ctx->status = SPU_STATUS_STOPPED;
            if (ctx->exit_jmp) longjmp(*(jmp_buf*)ctx->exit_jmp, 1);
        }
        ctx->gpr[3]._u32[0] = 0;
        return;
    }
    case 4:   /* RECV_WKL_FLAG */
    default:
        ctx->gpr[3]._u32[0] = 0;
        return;
    }
}

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Per-context MFC engine registry
 * ===========================================================================*/
#define SPU_MAX_CONTEXTS 8

typedef struct {
    spu_context* ctx;
    mfc_engine   mfc;
} spu_mfc_slot;

static spu_mfc_slot s_mfc_slots[SPU_MAX_CONTEXTS];

/* winport v28-scream: the registry below is keyed by context POINTER and was never released. On
 * Windows each lifted job runs on a fresh thread with its context on that thread's stack, so the 8
 * slots fill up and every later context -- the SCREAM interpreter context included -- shares ONE
 * fallback engine with the jobs on the other workers (tag / list-stall state clobbered -> SCREAM
 * polls a tag that never completes -> no sound for the whole launch). PS3_MFC_SLOT_FIX=1: SCREAM
 * gets its own engine, slots are claimed with a CAS, spu_mfc_release() frees a job's slot. */
static int wp28_slot_fix(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("PS3_MFC_SLOT_FIX");
        v = (e && e[0] == '1') ? 1 : 0;
        if (v) { fprintf(stderr, "[MFC28] slot fix ON: SCREAM has its own MFC engine, job contexts release their slot\n"); fflush(stderr); }
    }
    return v;
}
static mfc_engine s_wp28_scream_mfc;
static volatile int s_wp28_scream_mfc_init;
static mfc_engine* mfc_for_impl(spu_context* ctx);
static mfc_engine* mfc_for(spu_context* ctx)   /* winport v79-spu-side: WINPORT_MFC_CACHE79 */
{
    if (g_wp79_mc < 0) { const char* e = getenv("WINPORT_MFC_CACHE79"); g_wp79_mc = (e && e[0] == '1') ? 1 : 0;
                         if (g_wp79_mc) { fprintf(stderr, "[SPUW79] per-thread mfc_for cache ON\n"); fflush(stderr); } }
    if (!g_wp79_mc) return mfc_for_impl(ctx);
    if (ctx && ctx == t_wp79_mctx) return t_wp79_meng;
    mfc_engine* m = mfc_for_impl(ctx);
    if (ctx && !ctx->interp_spurs_pm) { t_wp79_mctx = ctx; t_wp79_meng = m; }
    return m;
}
static mfc_engine* mfc_for_impl(spu_context* ctx)
{
    if (ctx && ctx->interp_spurs_pm && wp28_slot_fix()) {
        if (!s_wp28_scream_mfc_init) { mfc_engine_init(&s_wp28_scream_mfc); s_wp28_scream_mfc_init = 1; }
        return &s_wp28_scream_mfc;
    }
    spu_mfc_slot* free_slot = NULL;
    for (int i = 0; i < SPU_MAX_CONTEXTS; i++) {
        if (s_mfc_slots[i].ctx == ctx)
            return &s_mfc_slots[i].mfc;
        if (!free_slot && s_mfc_slots[i].ctx == NULL)
            free_slot = &s_mfc_slots[i];
    }
    if (wp28_slot_fix()) {
        for (int i = 0; i < SPU_MAX_CONTEXTS; i++) {
            spu_context* expected = NULL;
            if (__atomic_compare_exchange_n(&s_mfc_slots[i].ctx, &expected, ctx, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                mfc_engine_init(&s_mfc_slots[i].mfc);
                return &s_mfc_slots[i].mfc;
            }
        }
    } else if (free_slot) {
        free_slot->ctx = ctx;
        mfc_engine_init(&free_slot->mfc);
        if (ctx && ctx->interp_spurs_pm) {
            fprintf(stderr, "[MFC28] SCREAM ctx=%p has a private MFC slot (#%d of %d)\n", (void*)ctx,
                    (int)(free_slot - s_mfc_slots), SPU_MAX_CONTEXTS);
            fflush(stderr);
        }
        return &free_slot->mfc;
    }
    /* Out of slots: fall back to a shared engine (correct for single-SPU). */
    static mfc_engine fallback;
    static int fallback_init = 0;
    if (!fallback_init) { mfc_engine_init(&fallback); fallback_init = 1; }
    { static int told = 0;
      if (!told) { told = 1;
          fprintf(stderr, "[MFC28] MFC registry full (%d slots): ctx=%p image=%d and every later SPU context share ONE fallback engine\n",
                  SPU_MAX_CONTEXTS, (void*)ctx, ctx ? ctx->image_id : -1);
          fflush(stderr); } }
    if (ctx && ctx->interp_spurs_pm) {
        static int told2 = 0;
        if (!told2) { told2 = 1;
            fprintf(stderr, "[MFC28] SCREAM ctx=%p is on the SHARED fallback MFC engine -> expect no sound this launch (PS3_MFC_SLOT_FIX=1 fixes)\n", (void*)ctx);
            fflush(stderr); }
    }
    return &fallback;
}
/* winport v28-scream: a finished job's context gives its slot back (PS3_MFC_SLOT_FIX=1 only). */
void spu_mfc_release(spu_context* ctx)
{
    if (ctx && ctx == t_wp79_mctx) t_wp79_mctx = 0;   /* winport v79-spu-side */
    if (!ctx || !wp28_slot_fix()) return;
    for (int i = 0; i < SPU_MAX_CONTEXTS; i++)
        if (s_mfc_slots[i].ctx == ctx) { __atomic_store_n(&s_mfc_slots[i].ctx, (spu_context*)NULL, __ATOMIC_RELEASE); return; }
}

static int channel_is_mfc(uint32_t ch)
{
    switch (ch) {
    case MFC_WrMSSyncReq: case MFC_RdTagMask:  case MFC_LSA:
    case MFC_EAH:         case MFC_EAL:         case MFC_Size:
    case MFC_TagID:       case MFC_Cmd:         case MFC_WrTagMask:
    case MFC_WrTagUpdate: case MFC_RdTagStat:   case MFC_RdListStallStat:
    case MFC_WrListStallAck: case MFC_RdAtomicStat:
        return 1;
    default:
        return 0;
    }
}

/* ===========================================================================
 * Channel write
 * ===========================================================================*/
/* -1 = por resolver, 0 = desligado, 1 = ligado. Resolvido dentro da funcao
 * fria para que spu_wrch nao referencie getenv (o que a tornaria dependente de
 * uma chamada e engordaria o quadro outra vez). */
static int g_wrch_trace = -1;
static __attribute__((noinline)) void spu_wrch_trace_slow(uint32_t channel, uint32_t v)
{
    if (g_wrch_trace < 0) {
        const char* e = getenv("PS3_TRACE_WRCH");
        g_wrch_trace = (e && *e && *e != '0') ? ((e[0] == 'c') ? 2 : 1) : 0;
        if (!g_wrch_trace) return;
    }
    static unsigned long long n = 0;
    n++;
    /* PS3_TRACE_WRCH=census: censo por CANAL. spu_wrch e' ~14% do tempo proprio
     * das threads de SPU e tem 464 instrucoes de despacho; para lhe dar uma
     * entrada rapida e' preciso saber QUAL canal domina, e nao supor -- supor
     * ja' custou uma hipotese refutada nesta funcao (0cf97180). */
    if (g_wrch_trace == 2) {
        static unsigned long long per_ch[128];
        per_ch[channel & 127u]++;
        if ((n % 2000000ull) == 0ull) {
            unsigned ord[128];
            for (unsigned i = 0; i < 128u; i++) ord[i] = i;
            for (unsigned i = 0; i < 128u; i++)
                for (unsigned j = i + 1u; j < 128u; j++)
                    if (per_ch[ord[j]] > per_ch[ord[i]]) { unsigned t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
            fprintf(stderr, "[WRCHCENSUS] total=%llu", n);
            for (unsigned i = 0; i < 6u; i++)
                if (per_ch[ord[i]])
                    fprintf(stderr, "  ch%u=%.1f%%", ord[i],
                            100.0 * (double)per_ch[ord[i]] / (double)n);
            fprintf(stderr, "\n");
            fflush(stderr);
        }
        return;
    }
    if (n <= 40 || (n % 100000) == 0) {
        fprintf(stderr, "[WRCH] #%llu ch=%u v=0x%08X\n", n, channel, v);
        fflush(stderr);
    }
}

/* Corpo FRIO de spu_wrch: o despacho completo de canal (464 instrucoes,
 * 23 chamadas). noinline para que a capa quente abaixo nao herde o seu
 * prologo -- com o corpo la' dentro, os cinco canais de parametro do MFC
 * pagavam "sub sp, #0xa0" e 12 registadores salvos para fazer UM store. */
static __attribute__((noinline)) void spu_wrch_slow(spu_context* ctx,
                                                    uint32_t channel, uint32_t v)
{
    if (channel_is_mfc(channel)) {
        /* Liveness for the taskset timeslice: a payload-moving DMA means this
         * task is doing work, not idling on an empty queue. Stopping the clock
         * (rather than restamping it) keeps this to a single store -- the next
         * poll restarts it. See spu_task_should_relinquish above. */
        if (channel == MFC_Cmd && mfc_cmd_moves_data(v))
            ctx->task_idle_ns = 0;
        if (__builtin_expect(g_wp83_on > 0, 0)) {   /* winport v83-spuprof */
            const unsigned long long w83 = wp83_tsc();
            mfc_channel_write(mfc_for(ctx), ctx, channel, v);
            wp83_mfc(ctx, channel, v, wp83_tsc() - w83);
        } else
        mfc_channel_write(mfc_for(ctx), ctx, channel, v);
        if (channel == MFC_Cmd && wp79_stats()) wp79_note_cmd(ctx, v & 0xFFu);   /* winport v79-spu-side */
        return;
    }

    switch (channel) {
    case SPU_WrOutMbox:      spu_channel_write(&ctx->ch_out_mbox, v);       break;
    case SPU_WrOutIntrMbox:  spu_channel_write(&ctx->ch_out_intr_mbox, v);  break;
    case SPU_WrDec:          ctx->decrementer = v;                          break;
    case SPU_WrEventMask:    ctx->event_mask = v;                           break;
    case SPU_WrEventAck:     ctx->event_status &= ~v;                       break;
    case SPU_WrSRR0:         ctx->srr0 = v;                                 break;
    default:
        /* Unknown / unhandled channel write -- ignore (matches a no-op SPU). */
        break;
    }
}
void spu_wrch(spu_context* ctx, uint32_t channel, u128 value)
{
    const uint32_t v = value._u32[0];  /* channel writes use the preferred slot */

    if (__builtin_expect(g_wrch_trace != 0, 0))
        spu_wrch_trace_slow(channel, v);

    /* E472: 83% das escritas de canal sao os cinco parametros de um comando DMA
     * -- MEDIDO com PS3_TRACE_WRCH=census, 2 984 000 000 escritas em 130 s
     * repartidas 1/6 por ch16..ch21 (a assinatura de LSA/EAH/EAL/Size/TagID/Cmd,
     * ~500 milhoes de DMAs). Cada uma e' UM store, e pagava o despacho todo.
     * MFC_Cmd fica de fora: e' ele que submete e que desarma task_idle_ns. */
    switch (channel) {
    case MFC_LSA:   ctx->mfc_lsa  = v;        return;
    case MFC_EAH:   ctx->mfc_eah  = v;        return;
    case MFC_EAL:   ctx->mfc_eal  = v;        return;
    case MFC_Size:  ctx->mfc_size = v;        return;
    case MFC_TagID: ctx->mfc_tag  = v & 0x1F; return;
    default: break;
    }
    spu_wrch_slow(ctx, channel, v);
}


/* ===========================================================================
 * Channel read (returns value in the preferred word slot)
 * ===========================================================================*/
u128 spu_rdch(spu_context* ctx, uint32_t channel)
{
    uint32_t v = 0;

    if (channel_is_mfc(channel)) {
        v = mfc_channel_read(mfc_for(ctx), ctx, channel);
        return spu_make_preferred_u32(v);
    }

    switch (channel) {
    case SPU_RdInMbox:      v = spu_channel_read(&ctx->ch_in_mbox);     break;
    case SPU_RdSigNotify1:  v = spu_channel_read(&ctx->ch_sig_notify[0]); break;
    case SPU_RdSigNotify2:  v = spu_channel_read(&ctx->ch_sig_notify[1]); break;
    case SPU_RdDec:         v = ctx->decrementer;                       break;
    case SPU_RdEventMask:   v = ctx->event_mask;                        break;
    case SPU_RdEventStat:   v = ctx->event_status;                      break;
    case SPU_RdMachStat:    v = (ctx->status == SPU_STATUS_RUNNING) ? 1 : 0; break;
    case SPU_RdSRR0:        v = ctx->srr0;                              break;
    default:
        v = 0;
        break;
    }
    return spu_make_preferred_u32(v);
}

/* Completed-tag mask of this context's MFC (spu_interp.c: a blocking
 * MFC_RdTagStat is satisfied only when the tags it waits for completed -- a
 * DMA list parked at a stall-and-notify element keeps its tag pending). */
uint32_t spu_mfc_tag_completed(spu_context* ctx)
{
    return mfc_for(ctx)->tag_completed;
}

/* ===========================================================================
 * Channel count (rchcnt) -- how many entries can be read/written right now
 * ===========================================================================*/
uint32_t spu_rchcnt(spu_context* ctx, uint32_t channel)
{
    switch (channel) {
    case SPU_RdInMbox:       return ctx->ch_in_mbox.count;                 /* readable */
    case SPU_WrOutMbox:      return SPU_MBOX_DEPTH - ctx->ch_out_mbox.count; /* free slots */
    case SPU_WrOutIntrMbox:  return SPU_INTR_MBOX_DEPTH - ctx->ch_out_intr_mbox.count;
    case SPU_RdSigNotify1:   return ctx->ch_sig_notify[0].count;
    case SPU_RdSigNotify2:   return ctx->ch_sig_notify[1].count;
    case MFC_Cmd:            return MFC_QUEUE_DEPTH - mfc_for(ctx)->queue_count;
    case MFC_RdTagStat:      return 1;  /* synchronous: status always ready */
    case MFC_RdListStallStat: return mfc_for(ctx)->stall_stat != 0;
    default:                 return 1;  /* default: channel ready */
    }
}

/* ===========================================================================
 * Indirect-branch dispatch + function registry
 * ===========================================================================*/
int spu_irq_trace_on(void);
typedef void (*spu_fn)(spu_context*);

typedef struct {
    uint32_t addr;
    spu_fn   fn;
    int      image_id;   /* which recompiled image this function belongs to */
} spu_reg_entry;

#define SPU_FN_REGISTRY_MAX 65536
static spu_reg_entry s_registry[SPU_FN_REGISTRY_MAX];
static uint32_t s_registry_count = 0;

/* Inter-function control-flow trampoline (see spu_context.h / SPU_DRAIN). */
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*) = 0;

/* Image currently being registered. SPURS images (kernel/policy/job) overlap in
 * LS, so each registers under a distinct id via spu_begin_image() before calling
 * its (prefixed) spu_recomp_register(). Single-image callers leave it 0. */
static int s_reg_image = 0;
void spu_begin_image(int image_id) { s_reg_image = image_id; }

void spu_register_function(uint32_t addr, spu_fn fn)
{
    if (s_registry_count < SPU_FN_REGISTRY_MAX) {
        s_registry[s_registry_count].addr = addr;
        s_registry[s_registry_count].fn = fn;
        s_registry[s_registry_count].image_id = s_reg_image;
        s_registry_count++;
    }
}

/* Abortos de job por alvo indirecto sem fragmento liftado (E464). */
unsigned long long g_spu_abort_unknown;

static spu_fn spu_lookup_scan(uint32_t addr, int image_id)   /* winport v83c-spu-lookup: was spu_lookup */
{
    /* Linear scan is fine for the small per-image tables. Match the context's
     * active image; image_id 0 (context or entry) matches any, for back-compat
     * with single-image contexts. */
    for (uint32_t i = 0; i < s_registry_count; i++)
        if (s_registry[i].addr == addr &&
            (image_id == 0 || s_registry[i].image_id == 0 ||
             s_registry[i].image_id == image_id))
            return s_registry[i].fn;
    return NULL;
}

/* ---- winport v83c-spu-lookup (engine_fixes_v83c_spu_lookup.py): WINPORT_SPU_LOOKUP83=1 ---- */
static int g_wp83c_on = -1;
typedef struct { uint32_t addr; int image_id; uint32_t gen; spu_fn fn; } wp83c_slot;
static __thread wp83c_slot t_wp83c[512];
static spu_fn spu_lookup(uint32_t addr, int image_id)
{
    if (__builtin_expect(g_wp83c_on < 0, 0)) {
        const char* e = getenv("WINPORT_SPU_LOOKUP83");
        g_wp83c_on = (e && e[0] == '1') ? 1 : 0;
        if (g_wp83c_on) { fprintf(stderr, "[SPUL83] per-thread SPU function lookup cache ON\n"); fflush(stderr); }
    }
    if (!g_wp83c_on) return spu_lookup_scan(addr, image_id);
    wp83c_slot* c = &t_wp83c[((addr >> 2) ^ ((uint32_t)image_id * 0x9E5u)) & 511u];
    const uint32_t gen = s_registry_count;
    if (c->fn && c->addr == addr && c->image_id == image_id && c->gen == gen) return c->fn;
    spu_fn fn = spu_lookup_scan(addr, image_id);
    if (fn) { c->addr = addr; c->image_id = image_id; c->gen = gen; c->fn = fn; }
    return fn;
}
/* ---- end winport v83c-spu-lookup ---- */

/* ---- unresolvable-indirect-branch probe (gated: PS3_SPU_BRPROBE) ----------
 * OFF by default, no-op on the baseline. When an indirect branch fails to
 * resolve, this reports enough to find WHERE the bad pointer came from:
 *
 *  - lr=  : the lifter emits `ctx->gpr[0] = spu_splat_u32(<site>+4)` right
 *           before every bisl, so the link register names the failing CALL
 *           SITE (site = lr-4) without instrumenting the generated code.
 *  - in=  : the registered entry the target falls INSIDE. If the target is
 *           mid-function the value is not a code pointer at all.
 *  - ls@  : offsets in local store holding the same 32-bit word, i.e. the
 *           slot the pointer was most likely loaded from. */
static int spu_brprobe_on(void)
{
    static int on = -1;
    if (on < 0) { extern char* getenv(const char*); on = getenv("PS3_SPU_BRPROBE") ? 1 : 0; }
    return on;
}

static void spu_brprobe_dump(spu_context* ctx, uint32_t target)
{
    uint32_t raw = ctx->pc;
    /* Registered entry containing the target (largest addr <= target). */
    uint32_t encl = 0; int have_encl = 0;
    for (uint32_t i = 0; i < s_registry_count; i++) {
        uint32_t a = s_registry[i].addr;
        if (a <= target && (!have_encl || a > encl) &&
            (ctx->image_id == 0 || s_registry[i].image_id == 0 ||
             s_registry[i].image_id == ctx->image_id)) { encl = a; have_encl = 1; }
    }
    fprintf(stderr, "[SPUBRP] pc=0x%08X target=0x%05X lr=0x%08X (site=0x%05X) "
            "sp=0x%08X image=%d in=0x%05X+0x%X regs=%u\n",
            raw, target, ctx->gpr[0]._u32[0],
            (ctx->gpr[0]._u32[0] - 4) & SPU_LS_MASK, ctx->gpr[1]._u32[0],
            ctx->image_id, encl, have_encl ? target - encl : 0, s_registry_count);
    /* Where does this exact word live in LS? Scan EVERY byte offset, not just
     * 4-aligned ones: the SPU word-load idiom is `lqd`+`rotqby`, which extracts
     * the 4 bytes at the *unaligned* address. If the base pointer is garbage the
     * result is a byte-spliced window that exists in LS only at an unaligned
     * offset -- an aligned-only scan reports "computed" and sends the hunt the
     * wrong way. An unaligned hit means the pointer WAS loaded, from ls@hit. */
    int hits = 0;
    for (uint32_t o = 0; o + 4 <= SPU_LS_SIZE && hits < 8; o++) {
        uint32_t w = ((uint32_t)ctx->ls[o] << 24) | ((uint32_t)ctx->ls[o+1] << 16) |
                     ((uint32_t)ctx->ls[o+2] << 8) | ctx->ls[o+3];
        if (w != raw) continue;
        fprintf(stderr, "[SPUBRP]   ls@0x%05X (%s)\n", o,
                (o & 3) ? "UNALIGNED -> misaligned load, base pointer is bad"
                        : "4-aligned");
        hits++;
    }
    if (!hits)
        fprintf(stderr, "[SPUBRP]   value at no byte offset in LS "
                        "(genuinely computed, not loaded)\n");
    /* r3 and the LS words around it: the GoW2 spu0 site (0xD9DC) calls through
     * *(r3+12) of a {begin,end,current,callback} command-buffer object, so the
     * window shows what that object really holds (E422/E423). */
    { const uint32_t r3 = ctx->gpr[3]._u32[0] & SPU_LS_MASK & ~3u;
      const uint32_t lo = (r3 >= 0x20u) ? r3 - 0x20u : 0u;
      fprintf(stderr, "[SPUBRP]   r3=0x%05X r4=0x%08X LS[0x%05X..+0x60]:", r3, ctx->gpr[4]._u32[0], lo);
      for (uint32_t o = lo; o < lo + 0x60u && o + 4 <= SPU_LS_SIZE; o += 4)
          fprintf(stderr, "%s%08X", ((o - lo) % 16u) ? " " : " | ",
                  ((uint32_t)ctx->ls[o] << 24) | ((uint32_t)ctx->ls[o+1] << 16) |
                  ((uint32_t)ctx->ls[o+2] << 8) | ctx->ls[o+3]);
      fprintf(stderr, "\n"); }
    /* Saved r80..r83 of the current frame (SPU-GCC convention: back chain at
     * *(sp), r80+k stored at back_chain - 0x10*(k+1)), and 0x40 bytes at each one
     * that looks like an LS address: at the GoW2 site the caller's descriptor
     * pointer (spu0 0x3828's r82) sits there. */
    { const uint32_t sp = ctx->gpr[1]._u32[0] & SPU_LS_MASK & ~15u;
      const uint32_t bc = (((uint32_t)ctx->ls[sp] << 24) | ((uint32_t)ctx->ls[sp+1] << 16) |
                           ((uint32_t)ctx->ls[sp+2] << 8) | ctx->ls[sp+3]) & SPU_LS_MASK & ~15u;
      for (uint32_t k = 0; k < 4 && bc >= 0x10u * (k + 1); k++) {
          const uint32_t s = bc - 0x10u * (k + 1);
          const uint32_t v = ((uint32_t)ctx->ls[s] << 24) | ((uint32_t)ctx->ls[s+1] << 16) |
                             ((uint32_t)ctx->ls[s+2] << 8) | ctx->ls[s+3];
          fprintf(stderr, "[SPUBRP]   saved r%u=0x%08X (bc=0x%05X)", 80u + k, v, bc);
          if (v >= 0x100u && v + 0x40u <= SPU_LS_SIZE) {
              fprintf(stderr, " LS[0x%05X..+0x40]:", v);
              for (uint32_t o = v & ~3u; o < (v & ~3u) + 0x40u; o += 4)
                  fprintf(stderr, "%s%08X", ((o - (v & ~3u)) % 16u) ? " " : " | ",
                          ((uint32_t)ctx->ls[o] << 24) | ((uint32_t)ctx->ls[o+1] << 16) |
                          ((uint32_t)ctx->ls[o+2] << 8) | ctx->ls[o+3]);
          }
          fprintf(stderr, "\n");
      } }
    fflush(stderr);
}

/* SPU halt-if-<cond> (heqi/hgti/hlgti). These are compiler-inserted asserts --
 * SPU-GCC emits them after DMA alignment/size checks -- so they never fire in
 * correct execution. On hardware a taken halt stops the SPU.
 *
 * Default is REPORT-AND-CONTINUE: before the lifter decoded these opcodes the
 * instruction was dropped entirely, so continuing keeps the baseline bit-for-bit
 * while still surfacing the divergence. PS3_SPU_HALT=1 makes it authoritative
 * (stops the job as hardware does), which turns it into a real tripwire: the
 * first taken halt is the earliest point our state provably diverged. */
void spu_halt_if(spu_context* ctx, int taken, uint32_t pc)
{
    if (!taken) return;
    static int n = 0;
    if (n < 16) {
        n++;
        fprintf(stderr, "[SPUHALT] assert fired at LS 0x%05X (image %d)\n",
                pc & SPU_LS_MASK, ctx->image_id);
        fflush(stderr);
    }
    { static int hard = -1;
      if (hard < 0) { extern char* getenv(const char*); hard = getenv("PS3_SPU_HALT") ? 1 : 0; }
      if (hard) {
          ctx->status = SPU_STATUS_STOPPED_BY_HALT;
          if (ctx->exit_jmp) longjmp(*(jmp_buf*)ctx->exit_jmp, 2);
      } }
}

void spu_indirect_branch(spu_context* ctx)
{
    /* Real SPU hardware ignores the low 2 bits of an indirect branch target
     * (bi/bisl encode interrupt-enable flags there) and wraps to the 256 KB
     * local store; GoW2's spu0 branches to 0x05C03 meaning LS 0x05C00, and
     * the register value can carry garbage above bit 17. Mask both ends
     * before the lookup. */
    uint32_t target = ctx->pc & SPU_LS_MASK & ~3u;
    if (target == 0) {
        /* Task main returned through the context's default LR (0): the job is
         * done. Unwind to spu_run_lifted_job instead of running past the end. */
        ctx->status = SPU_STATUS_STOPPED;
        if (ctx->exit_jmp) longjmp(*(jmp_buf*)ctx->exit_jmp, 1);
        return;
    }
    const unsigned long long w83_t0 = (g_wp83_on > 0) ? wp83_tsc() : 0ull;   /* winport v83-spuprof */
    if (target == SPU_TASKSET_SYSCALL_LS) {
        if (w83_t0) {
            if ((ctx->gpr[3]._u32[0] & 0x0F) == 3) wp83_ib(ctx, target, w83_t0, 1);
            spu_taskset_syscall_hle(ctx);
            t_w83_sub[3] += wp83_tsc() - w83_t0;
            return;
        }
        /* SPURS taskset syscall (the LS context's syscallAddr points here):
         * emulate and return like a call -- the lifted bisl caller continues
         * inline after its SPU_DRAIN. */
        spu_taskset_syscall_hle(ctx);
        return;
    }
    if (ctx->image_id == 7 && spu_irq_trace_on()) { static unsigned k = 0; if (k++ < 20000)
        fprintf(stderr, "[SPUIRQ] bi img7 -> 0x%05X depth=%d top=0x%05X\n", target, ctx->call_depth,
                ctx->call_depth > 0 && ctx->call_depth <= 64 ? ctx->call_ret[ctx->call_depth - 1] : 0); }
    if (ctx->call_depth > 0 && ctx->call_depth <= 64 &&
        target == (ctx->call_ret[ctx->call_depth - 1] & SPU_LS_MASK & ~3u)) {
        /* `bi $rX` to the innermost pending brsl return: return to the C caller. */
        return;
    }
    if (ctx->in_irq && target == (ctx->srr0 & SPU_LS_MASK & ~3u)) {
        /* iret of the handler run by spu_irq_window: back to the C caller. */
        return;
    }
    spu_fn fn = spu_lookup(target, ctx->image_id);
    if (w83_t0) wp83_ib(ctx, target, w83_t0, 0);   /* winport v83-spuprof */
    if (!fn && ctx->image_id == 7) {
        /* SCREAM's policy module (LS 0xA00..0x3700) and the SPURS kernel 2
         * entry points it uses (kernel context at LS 0x100). */
        if (target == 0x838u) {
            /* exitToKernelAddr: the module is done for this pass. */
            ctx->status = SPU_STATUS_STOPPED;
            if (ctx->exit_jmp) longjmp(*(jmp_buf*)ctx->exit_jmp, 1);
            return;
        }
        if (target == 0x290u) {
            /* selectWorkloadAddr: one module per kernel here, so the kernel
             * keeps selecting the current workload (id in r3's upper word). */
            const uint8_t* w = &ctx->ls[0x1DC];
            ctx->gpr[3]._u32[0] = (uint32_t)w[0] << 24 | (uint32_t)w[1] << 16 | (uint32_t)w[2] << 8 | w[3];
            ctx->gpr[3]._u32[1] = 0;
            return;
        }
        if (target < 0x3700u) {
            /* `bi` through a link register other than r0 is a return to a
             * brsl the lifter already inlined in the C caller. Anything else
             * landing here is a missing entry: say so once per address. */
            static uint32_t seen[64]; static int nseen;
            int known = 0;
            for (int i = 0; i < nseen; i++) if (seen[i] == target) { known = 1; break; }
            if (!known && nseen < 64) {
                seen[nseen++] = target;
                fprintf(stderr, "[SPU] image 7: bi to unregistered 0x%05X treated as return\n", target);
            }
            return;
        }
    }
    if (fn) {
        /* Dispatch via the trampoline instead of calling fn() here: a direct
         * call would grow the host stack on every indirect branch (and a
         * branch-loop would overflow it). The nearest call site / job runner
         * drains it. See SPU_DRAIN in spu_context.h. */
        ctx->pc = target;
        g_spu_trampoline_fn = fn;
        return;
    }
    /* E464: este ramo ABORTA o job (longjmp mais abaixo) e no GoW2 dispara
     * milhoes de vezes -- 5 482 683 ocorrencias nos logs desta arvore, TODAS no
     * mesmo alvo 0x0391C da imagem 0. O fprintf era incondicional, portanto alem
     * de perder o trabalho do job pagavamos uma escrita formatada em stderr por
     * aborto. Passa a contador, com as primeiras 32 linhas mantidas para nao
     * perder o diagnostico do primeiro alvo novo. */
    g_spu_abort_unknown++;
    {
        static int shown = 0;
        if (shown < 32) {
            shown++;
            fprintf(stderr, "[SPU] indirect branch to unknown LS address 0x%05X (image %d)%s\n",
                    target, ctx->image_id,
                    shown == 32 ? " (mais ocorrencias so' no contador)" : "");
        }
    }
    if (spu_brprobe_on()) {
        static int n = 0;
        if (n < 8) { n++; spu_brprobe_dump(ctx, target); }
    }
    ctx->status = SPU_STATUS_STOPPED_BY_HALT;
    /* Unresolvable target: abort the job cleanly rather than letting the
     * lifted caller keep executing with an undefined control flow. */
    if (ctx->exit_jmp) longjmp(*(jmp_buf*)ctx->exit_jmp, 2);
}

/* ===========================================================================
 * Execution trace (for §3 validation: diff vs RPCS3 SPU interpreter)
 *
 * When the lifter is invoked with --trace, every emitted instruction is
 * surrounded by spu_trace_pc(ctx, PC) before execution and spu_trace_rt(
 * ctx, RT) after, for instructions whose destination is the rt slot. The
 * output is one line per event:
 *
 *     <PC-5hex>                          - PC about to execute
 *       r<rt> <hi-64hex> <lo-64hex>      - register written, post-state
 *
 * Direct to stderr by default; call spu_trace_init(path) once at startup
 * to redirect to a file. The format is intentionally minimal and stable
 * so a small converter can line it up against an RPCS3.log SPU trace.
 * ===========================================================================*/
static FILE* s_trace_fp = NULL;

void spu_trace_init(const char* path)
{
    if (!path || !*path) { s_trace_fp = stderr; return; }
    s_trace_fp = fopen(path, "w");
    if (!s_trace_fp) s_trace_fp = stderr;
}

void spu_trace_pc(spu_context* ctx, uint32_t pc)
{
    (void)ctx;
    if (!s_trace_fp) s_trace_fp = stderr;
    fprintf(s_trace_fp, "%05X\n", pc & SPU_LS_MASK);
}

void spu_trace_rt(spu_context* ctx, uint32_t rt)
{
    if (!s_trace_fp) s_trace_fp = stderr;
    u128 v = ctx->gpr[rt & 0x7F];
    fprintf(s_trace_fp, "  r%-3u %016llX %016llX\n",
            (unsigned)(rt & 0x7F),
            (unsigned long long)v._u64[0],
            (unsigned long long)v._u64[1]);
}

#ifdef __cplusplus
}
#endif


/* ===========================================================================
 * SPU interrupts (lifted `bie` / `bid`)
 *
 * The lifter emits spu_irq_window() where the code branches with interrupts
 * enabled (the E flag of bi/biz/...). If an enabled event is pending, the SPU
 * takes the interrupt there: it jumps to LS 0, whose instruction the program
 * put there (GoW2's SCREAM policy module writes `bra 0xA2C` and enables the
 * DMA-list stall-and-notify event, 0x2). The handler preserves the
 * interrupted context: SCREAM's does it with register save/restore code it
 * writes at run time at 0x3FEC0, which a static lift cannot run, so the whole
 * register file is saved and restored here around the handler, and its
 * `iret` returns to this C frame. A poll loop that keeps opening windows with
 * nothing pending would spin the host worker forever; after 256 empty windows
 * the job ends, and the program re-enters on its next dispatch.
 * ===========================================================================*/
int spu_irq_trace_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_TRACE_SPU_IRQ"); on = (e && *e && *e != '0') ? 1 : 0; }
    return on;
}
void spu_irq_window(spu_context* ctx)
{
    if (spu_irq_trace_on()) { static unsigned k = 0; if (k++ < 400)
        fprintf(stderr, "[SPUIRQ] window img=%d ev=0x%X mask=0x%X in_irq=%d spins=%u\n",
                ctx->image_id, ctx->event_status, ctx->event_mask, ctx->in_irq, ctx->irq_spins); }
    if (ctx->in_irq) return;
    if ((ctx->event_status & ctx->event_mask) == 0) {
        if (++ctx->irq_spins >= 256u) {
            ctx->irq_spins = 0;
            ctx->status = SPU_STATUS_STOPPED;
            if (ctx->exit_jmp) longjmp(*(jmp_buf*)ctx->exit_jmp, 1);
        }
        return;
    }
    ctx->irq_spins = 0;
    const uint8_t* v = &ctx->ls[0];
    const uint32_t insn = (uint32_t)v[0] << 24 | (uint32_t)v[1] << 16 | (uint32_t)v[2] << 8 | v[3];
    uint32_t handler;
    if ((insn >> 23) == 0x060u || (insn >> 23) == 0x064u)        /* bra / br from LS 0 */
        handler = (uint32_t)((int32_t)(int16_t)((insn >> 7) & 0xFFFF) << 2) & SPU_LS_MASK;
    else
        return;                                                  /* no vector installed */
    spu_fn fn = spu_lookup(handler, ctx->image_id);
    if (!fn) {
        static int shown = 0;
        if (shown++ < 4)
            fprintf(stderr, "[SPU] interrupt vector -> 0x%05X not lifted (image %d)\n",
                    handler, ctx->image_id);
        return;
    }
    u128 saved[128];
    memcpy(saved, ctx->gpr, sizeof saved);
    const uint32_t saved_srr0 = ctx->srr0;
    ctx->srr0 = 0x3FFFC;            /* return point: the iret comes back here */
    ctx->in_irq = 1;
    fn(ctx);
    SPU_DRAIN(ctx);
    ctx->in_irq = 0;
    ctx->srr0 = saved_srr0;
    memcpy(ctx->gpr, saved, sizeof saved);
}

/* ---- winport v27-audio: the "no sound at all this launch" state ------------------------------
 * Seen in 7 of 26 Windows logs (run_v22_160316, v22c_162617, v22d_163831, v24_170451, v25_smoke,
 * v26_smoke, v26gate60_smoke2): image 7 parked for good on MFC_RdTagStat (ch 24) with ie=0, ev=0,
 * event mask 0x2 (list stall-and-notify) and one tag never completing (e.g. tagmask=0x100,
 * tagdone=0xFFFFFEFF). Print the MFC stall state once next to the STUCK report so the cause can be
 * read (a list parked at a stall-and-notify element whose notify was already consumed, or a plain
 * tag left pending), and with PS3_SCREAM_UNSTICK=N (dispatches, e.g. 500 ~= 1 s) release it: resume
 * the stalled list / mark the tag complete. Default 0 = report only. Called once per dispatch. */
void wp27_stuck_check(spu_context* ctx, int rc)
{
    static unsigned waits; static uint32_t wpc; static int told, unstick = -1, fixes;
    if (unstick < 0) { const char* e = getenv("PS3_SCREAM_UNSTICK"); unstick = (e && *e) ? atoi(e) : 0; if (unstick < 0) unstick = 0; }
    if (rc != 3 /* SPU_INTERP_WAIT */) { waits = 0; told = 0; return; }
    const uint32_t pc = ctx->pc & 0x3FFFCu;
    const uint32_t ch = ((((uint32_t)ctx->ls[pc + 2] << 8) | ctx->ls[pc + 3]) >> 7) & 0x7Fu;
    if (pc != wpc || ch != MFC_RdTagStat) { wpc = pc; waits = 0; told = 0; if (ch != MFC_RdTagStat) return; }
    waits++;
    mfc_engine* m = mfc_for(ctx);
    uint32_t act = 0;
    for (uint32_t t = 0; t < 32; t++) if (m->stall[t].active) act |= 1u << t;
    if (waits == 2000 && !told) {
        told = 1;
        fprintf(stderr, "[SPUINTERP27] stuck detail: image %d pc=0x%05X tagmask=0x%X tag_completed=0x%X stall_stat=0x%X "
                "stall_active=0x%X ev=0x%X mask=0x%X ie=%d (PS3_SCREAM_UNSTICK=%d)\n", ctx->image_id, pc,
                ctx->mfc_tag_mask, m->tag_completed, m->stall_stat, act, ctx->event_status, ctx->event_mask,
                ctx->interp_ie, unstick);
        fflush(stderr);
    }
    if (unstick > 0 && waits >= (unsigned)unstick) {
        const uint32_t pend = ctx->mfc_tag_mask & ~m->tag_completed;
        for (uint32_t t = 0; t < 32; t++) {
            if (!(pend & (1u << t))) continue;
            if (m->stall[t].active) mfc_list_stall_ack(m, ctx, t);
            m->tag_completed |= 1u << t;
        }
        m->stall_stat &= ~pend;
        if (++fixes <= 16) {
            fprintf(stderr, "[SPUINTERP27] SCREAM unstick #%d: released tags 0x%X after %u parked dispatches "
                    "(stall_active was 0x%X)\n", fixes, pend, waits, act);
            fflush(stderr);
        }
        waits = 0; told = 0;
    }
}

/* ---- winport v28-scream: how SCREAM loads its mixer job / overlays --------------------------
 * Called from spu_dma.h for every GET of >= 1 KB into LS >= 0x4000 by image 7. Prints the first 48
 * and then every 4096th, with an FNV-1a hash of the bytes, so the code images can be told from
 * sample data and repeated loads of the same job from different overlays. */
void wp28_job_get(unsigned ea, unsigned lsa, unsigned size, const void* src)
{
    static unsigned n = 0;
    const unsigned k = __atomic_fetch_add(&n, 1u, __ATOMIC_RELAXED);
    if (k >= 48 && (k % 4096u) != 0) return;
    unsigned long long h = 1469598103934665603ull;
    const unsigned char* p = (const unsigned char*)src;
    for (unsigned i = 0; i < size; i++) { h ^= p[i]; h *= 1099511628211ull; }
    fprintf(stderr, "[SCREAMLD] #%u GET ea=0x%08X lsa=0x%05X size=%u fnv=0x%016llX\n", k, ea, lsa, size, h);
    fflush(stderr);
}
