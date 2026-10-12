/*
 * ps3recomp - Timer and time syscalls (implementation)
 */

#include "sys_timer.h"
#include <stdlib.h>
#include "sys_event.h"
#include "../memory/vm.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * Globals
 * -----------------------------------------------------------------------*/
sys_timer_info g_sys_timers[SYS_TIMER_MAX];

#ifdef _WIN32
static LARGE_INTEGER s_qpc_freq;
static int           s_qpc_init = 0;

static void ensure_qpc_init(void)
{
    if (!s_qpc_init) {
        QueryPerformanceFrequency(&s_qpc_freq);
        s_qpc_init = 1;
    }
}
#endif

/* ---------------------------------------------------------------------------
 * ps3_timebase_now -- REAL wall-clock PS3 timebase (79.8 MHz), boot-relative.
 *
 * The lifter used to emit `mftb` as a per-call-site static counter advancing
 * 16667 ticks (~0.209 ms) PER EXECUTION -- guest time ran hundreds of times
 * slower than real time, freezing every duration-based state machine (the
 * GoW2 intro/loading sequencer measures logo stages through mftb deltas and
 * never advanced). All lifted mftb/mftbu sites now call this instead.
 * Overflow-safe scaling: split div/mod so qpc * 79.8e6 never exceeds u64.
 * Never returns 0 (guest code spins `mftb; beq` until nonzero).
 * -----------------------------------------------------------------------*/
/* ---- winport v51-rt-cutscene (engine_fixes_v51_realtime_cutscene.py): fast-forward clock ------------------------
 * g_wp51_ff_k > 1 while cellPad.c fast-forwards a real-time cutscene (the hold-START skip): sys_timer_usleep sleeps
 * 1/k, cellAudio consumes k guest blocks per period (muted), cellGcmSys drops its 60 Hz flip pacing.
 * g_wp51_tb_k > 1 also runs the guest timebase (every lifted mftb, sys_time_get_current_time) k times faster. The lead
 * gained is kept afterwards: guest time never goes backwards, it just stays ahead of the wall clock. */
volatile long g_wp51_ff_k = 0;
volatile long g_wp51_tb_k = 0;
volatile long long g_wp51_tb_extra = 0;   /* guest-time lead in host QPC ticks (Windows) */
#ifdef _WIN32
static volatile LONGLONG s_wp51_last = 0;
static LONGLONG wp51_tb_lead(LONGLONG real)
{
    const long k = g_wp51_tb_k;
    const LONGLONG last = s_wp51_last;
    if (k > 1) {
        if (real > last && InterlockedCompareExchange64((volatile LONG64*)&s_wp51_last, real, last) == last)
            InterlockedExchangeAdd64((volatile LONG64*)&g_wp51_tb_extra, (real - last) * (LONGLONG)(k - 1));
    } else if (real > last) {
        s_wp51_last = real;   /* only the start point of the next warp; a lost race costs nothing */
    }
    return (LONGLONG)g_wp51_tb_extra;
}
void wp51_timewarp(long k) { g_wp51_tb_k = (k > 1 && k <= 16) ? k : 0; }
#else
void wp51_timewarp(long k) { (void)k; }
#endif
/* ---- end winport v51-rt-cutscene ---- */

uint64_t ps3_timebase_now(void)
{
#ifdef _WIN32
    LARGE_INTEGER now;
    static volatile LONGLONG s_tb_base = 0;
    ensure_qpc_init();
    QueryPerformanceCounter(&now);
    if (!s_tb_base) {
        LONGLONG expected = 0;
        InterlockedCompareExchange64((volatile LONGLONG*)&s_tb_base, now.QuadPart, expected);
    }
    {
        uint64_t d = (uint64_t)(now.QuadPart - s_tb_base);
        d += (uint64_t)wp51_tb_lead((LONGLONG)d);   /* winport v51-rt-cutscene: fast-forward time warp (0 when unused) */
        uint64_t f = (uint64_t)s_qpc_freq.QuadPart;
        uint64_t t = (d / f) * PS3_TIMEBASE_FREQ + ((d % f) * PS3_TIMEBASE_FREQ) / f;
        return t ? t : 1;
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    {
        /* CLOCK_MONOTONIC on Darwin counts from system boot (days of uptime), so
         * the raw value is enormous (sec-since-boot * FREQ ≈ 3e13). The guest
         * computes wait deadlines as now+timeout and stores/compares them in ways
         * that overflow / sign-extend with such a large base — observed as a WAD-
         * load wait whose deadline reads 0xFFFFFFFFEC9735B8 (never expires under
         * the unsigned lifted cmp) and post-AutoLoad logo/frontend timer waits
         * that hang. Rebase to the first call so the guest timebase starts near 0,
         * exactly like the Windows branch above (s_tb_base). Benign init race. */
        uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
        static uint64_t s_tb_base_ns = 0;
        if (s_tb_base_ns == 0) s_tb_base_ns = now_ns;
        return ps3_timebase_from_delta_ns(now_ns - s_tb_base_ns);
    }
#endif
}

/* ---------------------------------------------------------------------------
 * ppu_timebase_now -- o nome por que o LIFTER chama isto.
 *
 * ppu_lifter.py declara `extern "C" uint64_t ppu_timebase_now(void);` no
 * preambulo e emite mftb/mftbu como `ppu_timebase_now()` (tools/ppu_lifter.py
 * ~230 e ~2127); lift_selftest.py assume o mesmo contrato. O runtime, porem,
 * so' definia ps3_timebase_now(). Um lift LIMPO nunca linkava:
 *
 *   Undefined symbols for architecture arm64:
 *     "_ppu_timebase_now", referenced from: func_00195D48(ppu_context*) ...
 *
 * O lift de producao do GoW2 escondia a divergencia com um `extern "C" uint64_t
 * ps3_timebase_now(void);` escrito a' mao no preambulo e os mftb reescritos --
 * mais um orfao sem escritor versionado. Medido em 2026-07-26 ao construir a
 * partir de um lift regenerado.
 *
 * Fica aqui como wrapper em vez de se renomear no lifter: o nome ppu_* e' o
 * contrato do lifter upstream (sp00nznet) e renomea-lo criaria conflito em
 * cada merge. Um nome de cada lado, uma linha de cola.
 * -----------------------------------------------------------------------*/
uint64_t ppu_timebase_now(void)
{
    return ps3_timebase_now();
}

static void write_be32(uint32_t addr, uint32_t val)
{
    uint32_t* p = (uint32_t*)vm_to_host(addr);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
    val = ((val >> 24) & 0xFF) | ((val >> 8) & 0xFF00) |
          ((val <<  8) & 0xFF0000) | ((val << 24) & 0xFF000000u);
#endif
    *p = val;
}

static void write_be64(uint32_t addr, uint64_t val)
{
    uint64_t* p = (uint64_t*)vm_to_host(addr);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
    val = ((val >> 56) & 0xFFULL) |
          ((val >> 40) & 0xFF00ULL) |
          ((val >> 24) & 0xFF0000ULL) |
          ((val >>  8) & 0xFF000000ULL) |
          ((val <<  8) & 0xFF00000000ULL) |
          ((val << 24) & 0xFF0000000000ULL) |
          ((val << 40) & 0xFF000000000000ULL) |
          ((val << 56) & 0xFF00000000000000ULL);
#endif
    *p = val;
}

/* ---------------------------------------------------------------------------
 * sys_timer_usleep
 *
 * r3 = microseconds
 * -----------------------------------------------------------------------*/
/* winport v27-audio (cellAudio.c): syn_tick_timer_thread timing for the [AUD27] line, and an
 * opt-in high-resolution waitable timer (PS3_USLEEP_HRT=1). */
extern unsigned long long wp27_now_us(void);
extern void wp27_tick_note(unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long, int);
extern int wp27_hrt(void);
extern int wp36_usleep_enter(int is_tick_thread);   /* winport v36-audio (ppu_loader.cpp) */
extern void wp36_usleep_exit(unsigned long long t_rel, unsigned long long t_woke, unsigned long long t_acq);
/* ---- winport v76c-usleep-fast (engine_fixes_v76c_usleep_fast.py): WINPORT_USLEEP_FAST=1|2 ---- */
#ifdef _WIN32
extern volatile long long g_wp76_sl[4];   /* Sleep() counts: hot_backoff S0, acquire-prio S0, lfq_yield S1, lfq_yield S0 */
static int wp76_us_state = -1;            /* 0 off, 1 fast path, 2 fast path + [USLP76] stats */
static long long wp76_qf = 1;
static int wp76_us_mode(void)
{
    if (wp76_us_state < 0) {
        const char* e = getenv("WINPORT_USLEEP_FAST");
        LARGE_INTEGER f; QueryPerformanceFrequency(&f); wp76_qf = f.QuadPart ? f.QuadPart : 1;
        wp76_us_state = (e && e[0] == '2') ? 2 : (e && e[0] == '1') ? 1 : 0;
        if (wp76_us_state) { fprintf(stderr, "[USLP76] sys_timer_usleep(<1 ms) lean path for ordinary threads ON (stats=%d)\n", wp76_us_state == 2); fflush(stderr); }
    }
    return wp76_us_state;
}
static __thread uint64_t t_wp76_tid = ~0ull;
static __thread int t_wp76_plain = 0;
static __thread unsigned t_wp76_n = 0;
/* ordinary = no priority re-acquire (priority not 0..999) and not the syn_tick audio pacer (v27/v36 accounting) */
static int wp76_plain(uint64_t tid)
{
    if (tid != t_wp76_tid || (++t_wp76_n & 4095u) == 0) {
        extern int32_t sys_ppu_thread_priority_of(uint64_t);
        extern int sys_ppu_thread_name_is(uint64_t, const char*);
        const int32_t pr = sys_ppu_thread_priority_of(tid);
        t_wp76_plain = !(pr >= 0 && pr < 1000) && !sys_ppu_thread_name_is(tid, "syn_tick");
        t_wp76_tid = tid;
    }
    return t_wp76_plain;
}
static __thread long long t_wp76_ep0 = 0, t_wp76_last = 0;
static volatile long long g_wp76_us[4];   /* fast calls, closed episodes, episode qpc sum, episode qpc max */
static void wp76_us_stats(long long t_in, long long t_out)
{
    __atomic_add_fetch(&g_wp76_us[0], 1ll, __ATOMIC_RELAXED);
    if (!(t_wp76_last && t_in - t_wp76_last < wp76_qf / 5000)) {   /* gap >= 200 us: a new drain-wait episode */
        if (t_wp76_ep0) {
            const long long d = t_wp76_last - t_wp76_ep0;
            __atomic_add_fetch(&g_wp76_us[1], 1ll, __ATOMIC_RELAXED);
            __atomic_add_fetch(&g_wp76_us[2], d, __ATOMIC_RELAXED);
            long long m = __atomic_load_n(&g_wp76_us[3], __ATOMIC_RELAXED);
            while (d > m && !__atomic_compare_exchange_n(&g_wp76_us[3], &m, d, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
        }
        t_wp76_ep0 = t_in;
    }
    t_wp76_last = t_out;
    static volatile long long last = 0;
    long long l = __atomic_load_n(&last, __ATOMIC_RELAXED);
    if (!l) { __atomic_compare_exchange_n(&last, &l, t_out, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED); return; }
    if (t_out - l < wp76_qf || !__atomic_compare_exchange_n(&last, &l, t_out, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return;
    const double s = (double)(t_out - l) / (double)wp76_qf, us = 1e6 / (double)wp76_qf;
    const long long c = __atomic_exchange_n(&g_wp76_us[0], 0ll, __ATOMIC_RELAXED), n = __atomic_exchange_n(&g_wp76_us[1], 0ll, __ATOMIC_RELAXED);
    const long long sum = __atomic_exchange_n(&g_wp76_us[2], 0ll, __ATOMIC_RELAXED), mx = __atomic_exchange_n(&g_wp76_us[3], 0ll, __ATOMIC_RELAXED);
    long long sl[4]; for (int i = 0; i < 4; i++) sl[i] = __atomic_exchange_n(&g_wp76_sl[i], 0ll, __ATOMIC_RELAXED);
    fprintf(stderr, "[USLP76] fast=%.0f/s drain-waits=%.0f/s avg=%.1fus max=%.1fus total=%.1fms/s | Sleep/s: hot_backoff=%.0f acq_prio=%.0f lfq_S1=%.0f lfq_S0=%.0f\n",
            c / s, n / s, n ? (double)sum * us / (double)n : 0.0, (double)mx * us, (double)sum * us / 1000.0 / s,
            sl[0] / s, sl[1] / s, sl[2] / s, sl[3] / s);
    fflush(stderr);
}
#endif
/* ---- end winport v76c-usleep-fast ---- */
int64_t sys_timer_usleep(ppu_context* ctx)
{
    uint64_t usec = LV2_ARG_U64(ctx, 0);
    { const long k51 = g_wp51_ff_k; if (k51 > 1) usec /= (uint64_t)k51; }   /* winport v51-rt-cutscene */
#ifdef _WIN32
    if (usec < 1000 && wp76_us_mode() && wp76_plain(ctx->thread_id)) {   /* winport v76c-usleep-fast */
        extern void ppu_giant_lock_release(void);
        extern void ppu_giant_lock_acquire(void);
        extern void ppu_giant_lock_hot_backoff(void);
        LARGE_INTEGER q0, q1;
        const int st = (wp76_us_state == 2);
        if (st) QueryPerformanceCounter(&q0);
        ppu_giant_lock_release();          /* still a yield point for every other guest / HLE thread */
        if (usec > 0) SwitchToThread();
        ppu_giant_lock_hot_backoff();
        ppu_giant_lock_acquire();
        if (st) { QueryPerformanceCounter(&q1); wp76_us_stats(q0.QuadPart, q1.QuadPart); }
        return CELL_OK;
    }
#endif
    { static int any = -1;   /* winport v36-audio: only when PS3_AUDIO_DIAG36 / PS3_TICK_GIANT is set */
      if (any < 0) { const char* a = getenv("PS3_AUDIO_DIAG36"), *b = getenv("PS3_TICK_GIANT");
                     any = ((a && *a && *a != '0') || (b && *b && *b != '0')) ? 1 : 0; }
      if (any) { extern int sys_ppu_thread_name_is(uint64_t, const char*);
                 wp36_usleep_enter(sys_ppu_thread_name_is(ctx->thread_id, "syn_tick")); } }

    { extern char* getenv(const char*); static int on=-1; if(on<0)on=getenv("PS3_TRACE_USLEEP")?1:0;
      if(on){ static int n=0; if(n<60){ n++;
          fprintf(stderr,"[USLEEP] #%d usec=%llu lr=0x%llX r1=0x%llX\n", n,
                  (unsigned long long)usec, (unsigned long long)ctx->lr,
                  (unsigned long long)ctx->gpr[1]);
#ifdef _WIN32
          /* Host RBP walk from HERE: unlike guest-side tracing (which the
           * trampoline mechanism defeats -- see ps3recomp-feasibility memory),
           * a `sc` (syscall trap) lowers to a plain, non-tail call into
           * lv2_syscall() from the lifted func_XXXX, so the HOST frame chain
           * sys_timer_usleep -> lv2_syscall -> func_XXXX should be intact. */
          uintptr_t modbase = (uintptr_t)GetModuleHandleW(0);
          unsigned long long rbp = (unsigned long long)(uintptr_t)__builtin_frame_address(0);
          fprintf(stderr, "[USLEEP-RBP] modbase=0x%llX\n", (unsigned long long)modbase);
          for (int depth = 0; depth < 10 && rbp; depth++) {
              unsigned long long* frame = (unsigned long long*)rbp;
              unsigned long long saved_rbp = frame[0], ret = frame[1];
              unsigned long long nm = ret >= modbase ? ret - modbase + 0x140000000ull : 0;
              fprintf(stderr, "    #%d rbp=0x%llX ret=0x%llX (nm=0x%llX)\n", depth, rbp, ret, nm);
              if (!saved_rbp || saved_rbp <= rbp) break;
              rbp = saved_rbp;
          }
#endif
          fflush(stderr); } } }

    /* Release the PPU giant lock (ppu_loader.cpp) while genuinely sleeping,
     * or every other PPU thread stalls for the full duration too. */
    extern void ppu_giant_lock_release(void);
    extern void ppu_giant_lock_acquire(void);
    ppu_giant_lock_release();
    const unsigned long long wp27_t_rel = wp27_now_us();   /* winport v27-audio */

#ifdef _WIN32
    /* Use high-resolution sleep via waitable timer for better precision */
    if (usec >= 1000) {
        /* winport v27-audio: PS3_USLEEP_HRT=1 -> CREATE_WAITABLE_TIMER_MANUAL_RESET|HIGH_RESOLUTION
         * (Windows 10 1803+; NULL on older systems, then the old timer). */
        HANDLE timer = wp27_hrt() ? CreateWaitableTimerExW(NULL, NULL, 0x00000003u, 0x1F0003u) : NULL;
        if (!timer) timer = CreateWaitableTimerW(NULL, TRUE, NULL);
        if (timer) {
            LARGE_INTEGER due;
            due.QuadPart = -((LONGLONG)usec * 10); /* 100ns units, negative = relative */
            SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
            WaitForSingleObject(timer, INFINITE);
            CloseHandle(timer);
        } else {
            Sleep((DWORD)(usec / 1000));
        }
    } else if (usec > 0) {
        /* Very short sleep -- yield */
        SwitchToThread();
    }
#else
    if (usec > 0) {
        struct timespec ts;
        ts.tv_sec  = (time_t)(usec / 1000000);
        ts.tv_nsec = (long)((usec % 1000000) * 1000);
        nanosleep(&ts, NULL);
    }
#endif
    const unsigned long long wp27_t_woke = wp27_now_us();   /* winport v27-audio */

    /* PS3_TRACE_USLEEP_RATE=1: contagem de usleep por thread (tid guest, prio) a cada 5 s. Diagnostico. */
    { static int on = -1; if (on < 0) { const char* e = getenv("PS3_TRACE_USLEEP_RATE"); on = (e && e[0] && e[0] != '0') ? 1 : 0; }
      if (on) { extern int32_t sys_ppu_thread_priority_of(uint64_t); extern unsigned long long ps3_timebase_now(void);
        static unsigned long long cnt[64]; static unsigned long long t_last = 0; uint64_t tid = ctx->thread_id; if (tid < 64) cnt[tid]++;
        unsigned long long now = ps3_timebase_now();
        if (t_last == 0) t_last = now;
        if (now - t_last > 5ull * 79800000ull) { t_last = now; fprintf(stderr, "[USLEEP-RATE] 5s:");
            for (int i = 0; i < 64; i++) if (cnt[i]) { fprintf(stderr, " tid%d(prio%d)=%llu", i, sys_ppu_thread_priority_of((uint64_t)i), cnt[i]); cnt[i] = 0; }
            fprintf(stderr, "\n"); } } }
    /* E154 (2026-09-02): na consola uma thread de prioridade mais alta (numero menor) que a
     * principal preempta-a ao acordar. Medido no GoW2: o pacer do som (syn_tick_timer_thread,
     * prio 100, usleep ~4 ms) so' conseguia o giant lock ~2x/s atras da thread principal
     * (prio 1001) -> a fila de jobs do mixer SPU consumia 0,3 jobs/s (consola: 32/s), o contador
     * de samples do stream ficava em 0 e o relogio do filme (corrigido pelo audio) em 0.
     * Re-adquirir como prio-waiter faz a principal ceder no proximo ponto de store-preempt. */
    { extern int32_t sys_ppu_thread_priority_of(uint64_t);
      extern int sys_ppu_thread_name_is(uint64_t, const char*);
      extern void ppu_giant_lock_acquire_prio(void);
      extern volatile int g_scream_tick_wait;
      extern volatile int g_ppu_preempt_req;
      static int on = -1; if (on < 0) { const char* e = getenv("PS3_USLEEP_PRIO"); on = (e && e[0] == '0') ? 0 : 1; }
      int32_t pr = sys_ppu_thread_priority_of(ctx->thread_id);
      /* Only the synth tick. Every other priority waiter (FIOS) yielding on
       * each guest read kept the intro from leaving st620=0. */
      int scream = sys_ppu_thread_name_is(ctx->thread_id, "syn_tick");
      if (scream) {
          g_scream_tick_wait = 1;
          g_ppu_preempt_req = 1;
      }
      if (on && pr >= 0 && pr < 1000) {
          ppu_giant_lock_acquire_prio();
          if (scream) g_scream_tick_wait = 0;
          { const unsigned long long a36 = wp27_now_us();   /* winport v36-audio */
            wp36_usleep_exit(wp27_t_rel, wp27_t_woke, a36);
            wp27_tick_note(ctx->thread_id, usec, wp27_t_rel, wp27_t_woke, a36, scream); }   /* winport v27-audio */
          return CELL_OK;
      }
      if (scream) g_scream_tick_wait = 0;
    }
    { extern void ppu_giant_lock_hot_backoff(void); ppu_giant_lock_hot_backoff(); }
    ppu_giant_lock_acquire();
    { const unsigned long long a36 = wp27_now_us();   /* winport v36-audio */
      wp36_usleep_exit(wp27_t_rel, wp27_t_woke, a36);
      wp27_tick_note(ctx->thread_id, usec, wp27_t_rel, wp27_t_woke, a36, -1); }   /* winport v27-audio */
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_timer_sleep
 *
 * r3 = seconds
 * -----------------------------------------------------------------------*/
int64_t sys_timer_sleep(ppu_context* ctx)
{
    uint32_t sec = LV2_ARG_U32(ctx, 0);

#ifdef _WIN32
    Sleep(sec * 1000);
#else
    sleep(sec);
#endif

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_time_get_current_time
 *
 * r3 = pointer to receive seconds (u64*)
 * r4 = pointer to receive nanoseconds (u64*)
 * -----------------------------------------------------------------------*/
int64_t sys_time_get_current_time(ppu_context* ctx)
{
    uint32_t sec_addr  = LV2_ARG_PTR(ctx, 0);
    uint32_t nsec_addr = LV2_ARG_PTR(ctx, 1);

    uint64_t sec, nsec;

#ifdef _WIN32
    ensure_qpc_init();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    now.QuadPart += (LONGLONG)g_wp51_tb_extra;   /* winport v51-rt-cutscene: same lead as the timebase */

    /* Convert QPC to seconds + nanoseconds */
    sec  = (uint64_t)(now.QuadPart / s_qpc_freq.QuadPart);
    uint64_t remainder = (uint64_t)(now.QuadPart % s_qpc_freq.QuadPart);
    nsec = (remainder * 1000000000ULL) / (uint64_t)s_qpc_freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    sec  = (uint64_t)ts.tv_sec;
    nsec = (uint64_t)ts.tv_nsec;
#endif

    if (sec_addr != 0) {
        write_be64(sec_addr, sec);
    }
    if (nsec_addr != 0) {
        write_be64(nsec_addr, nsec);
    }

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_time_get_timezone (144)
 *
 * Two 32-bit out-params: minutes east of UTC, and whether summer time is in
 * effect. The CRT calls this while building its local-time conversion, right
 * after sys_time_get_timebase_frequency.
 *
 * Reported as UTC with no DST rather than following the host clock. The guest
 * only uses this to label times it displays; taking the host timezone would
 * make save timestamps and the trophy clock vary with where the machine is,
 * which is a worse default than being consistently UTC.
 * -----------------------------------------------------------------------*/
int64_t sys_time_get_timezone(ppu_context* ctx)
{
    uint32_t tz_addr  = LV2_ARG_PTR(ctx, 0);
    uint32_t dst_addr = LV2_ARG_PTR(ctx, 1);

    if (tz_addr != 0) {
        write_be32(tz_addr, 0);   /* UTC */
    }
    if (dst_addr != 0) {
        write_be32(dst_addr, 0);  /* no summer time */
    }

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_time_get_timebase_frequency
 *
 * Returns the PS3 timebase frequency in r3.
 * -----------------------------------------------------------------------*/
int64_t sys_time_get_timebase_frequency(ppu_context* ctx)
{
    (void)ctx;
    return (int64_t)PS3_TIMEBASE_FREQ;
}

/* ---------------------------------------------------------------------------
 * Periodic timer thread
 * -----------------------------------------------------------------------*/

/* Forward declaration */
static void timer_send_event(sys_timer_info* t);

#ifdef _WIN32
static DWORD WINAPI timer_thread_proc(LPVOID param)
{
    sys_timer_info* t = (sys_timer_info*)param;

    while (t->running) {
        DWORD ms = (DWORD)(t->period_usec / 1000);
        if (ms == 0) ms = 1;

        DWORD result = WaitForSingleObject(t->stop_event, ms);
        if (result == WAIT_OBJECT_0) {
            break; /* stop signaled */
        }

        if (t->running) {
            timer_send_event(t);
        }
    }

    return 0;
}
#else
static void* timer_thread_proc(void* param)
{
#ifdef __APPLE__
    pthread_setname_np("host:sys-timer");
#endif

    sys_timer_info* t = (sys_timer_info*)param;

    pthread_mutex_lock(&t->mtx);
    while (!t->stop_flag) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += (time_t)(t->period_usec / 1000000);
        ts.tv_nsec += (long)((t->period_usec % 1000000) * 1000);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }

        int rc = pthread_cond_timedwait(&t->cv, &t->mtx, &ts);
        if (t->stop_flag) break;

        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&t->mtx);
            timer_send_event(t);
            pthread_mutex_lock(&t->mtx);
        }
    }
    pthread_mutex_unlock(&t->mtx);

    return NULL;
}
#endif

static void timer_send_event(sys_timer_info* t)
{
    if (t->event_queue_id <= 0 || t->event_queue_id > SYS_EVENT_QUEUE_MAX)
        return;

    sys_event_queue_info* q = &g_sys_event_queues[t->event_queue_id - 1];
    if (!q->active)
        return;

    sys_event_t evt;
    evt.source = t->source;
    evt.data1  = t->data1;
    evt.data2  = 0;
    evt.data3  = 0;

    /* Push event (ignore if queue full) */
#ifdef _WIN32
    EnterCriticalSection(&q->lock);
#else
    pthread_mutex_lock(&q->lock);
#endif

    if (q->count < q->capacity) {
        q->buffer[q->tail] = evt;
        q->tail = (q->tail + 1) % q->capacity;
        q->count++;
#ifdef _WIN32
        WakeConditionVariable(&q->not_empty);
#else
        pthread_cond_signal(&q->not_empty);
#endif
    }

#ifdef _WIN32
    LeaveCriticalSection(&q->lock);
#else
    pthread_mutex_unlock(&q->lock);
#endif
}

/* ---------------------------------------------------------------------------
 * sys_timer_create
 *
 * r3 = pointer to receive timer ID (u32*)
 * -----------------------------------------------------------------------*/
int64_t sys_timer_create(ppu_context* ctx)
{
    uint32_t id_out_addr = LV2_ARG_PTR(ctx, 0);

    int slot = -1;
    for (int i = 0; i < SYS_TIMER_MAX; i++) {
        if (!g_sys_timers[i].active) { slot = i; break; }
    }
    if (slot < 0)
        return (int64_t)(int32_t)CELL_EAGAIN;

    sys_timer_info* t = &g_sys_timers[slot];
    memset(t, 0, sizeof(*t));
    t->active  = 1;
    t->running = 0;
    t->event_queue_id = 0;

#ifdef _WIN32
    t->stop_event    = NULL;
    t->thread_handle = NULL;
    t->timer_handle  = NULL;
#else
    pthread_mutex_init(&t->mtx, NULL);
    pthread_cond_init(&t->cv, NULL);
    t->stop_flag = 0;
#endif

    uint32_t timer_id = (uint32_t)(slot + 1);
    if (id_out_addr != 0) {
        write_be32(id_out_addr, timer_id);
    }

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_timer_destroy
 *
 * r3 = timer_id
 * -----------------------------------------------------------------------*/
int64_t sys_timer_destroy(ppu_context* ctx)
{
    uint32_t timer_id = LV2_ARG_U32(ctx, 0);

    if (timer_id == 0 || timer_id > SYS_TIMER_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_timer_info* t = &g_sys_timers[timer_id - 1];
    if (!t->active)
        return (int64_t)(int32_t)CELL_ESRCH;

    /* Stop if running */
    if (t->running) {
        t->running = 0;
#ifdef _WIN32
        if (t->stop_event) SetEvent(t->stop_event);
        if (t->thread_handle) {
            WaitForSingleObject(t->thread_handle, 3000);
            CloseHandle(t->thread_handle);
        }
        if (t->stop_event) CloseHandle(t->stop_event);
#else
        pthread_mutex_lock(&t->mtx);
        t->stop_flag = 1;
        pthread_cond_signal(&t->cv);
        pthread_mutex_unlock(&t->mtx);
        pthread_join(t->thread, NULL);
        pthread_mutex_destroy(&t->mtx);
        pthread_cond_destroy(&t->cv);
#endif
    }

    t->active = 0;
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_timer_connect_event_queue
 *
 * r3 = timer_id
 * r4 = queue_id
 * r5 = source
 * r6 = data1
 * -----------------------------------------------------------------------*/
int64_t sys_timer_connect_event_queue(ppu_context* ctx)
{
    uint32_t timer_id = LV2_ARG_U32(ctx, 0);
    uint32_t queue_id = LV2_ARG_U32(ctx, 1);
    uint64_t source   = LV2_ARG_U64(ctx, 2);
    uint64_t data1    = LV2_ARG_U64(ctx, 3);

    if (timer_id == 0 || timer_id > SYS_TIMER_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_timer_info* t = &g_sys_timers[timer_id - 1];
    if (!t->active)
        return (int64_t)(int32_t)CELL_ESRCH;

    if (queue_id == 0 || queue_id > SYS_EVENT_QUEUE_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    t->event_queue_id = (int32_t)queue_id;
    t->source         = source;
    t->data1          = data1;

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_timer_disconnect_event_queue
 *
 * r3 = timer_id
 * -----------------------------------------------------------------------*/
int64_t sys_timer_disconnect_event_queue(ppu_context* ctx)
{
    uint32_t timer_id = LV2_ARG_U32(ctx, 0);

    if (timer_id == 0 || timer_id > SYS_TIMER_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_timer_info* t = &g_sys_timers[timer_id - 1];
    if (!t->active)
        return (int64_t)(int32_t)CELL_ESRCH;

    t->event_queue_id = 0;
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_timer_start
 *
 * r3 = timer_id
 * r4 = base_time (absolute start time, 0 = now)
 * r5 = period_usec
 * -----------------------------------------------------------------------*/
int64_t sys_timer_start(ppu_context* ctx)
{
    uint32_t timer_id    = LV2_ARG_U32(ctx, 0);
    /* uint64_t base_time = LV2_ARG_U64(ctx, 1); -- ignored for now */
    uint64_t period      = LV2_ARG_U64(ctx, 2);

    if (timer_id == 0 || timer_id > SYS_TIMER_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_timer_info* t = &g_sys_timers[timer_id - 1];
    if (!t->active)
        return (int64_t)(int32_t)CELL_ESRCH;

    if (t->running)
        return (int64_t)(int32_t)CELL_EBUSY;

    if (period == 0)
        return (int64_t)(int32_t)CELL_EINVAL;

    t->period_usec = period;
    t->running     = 1;

#ifdef _WIN32
    t->stop_event    = CreateEventA(NULL, TRUE, FALSE, NULL);
    t->thread_handle = CreateThread(NULL, 0, timer_thread_proc, t, 0, NULL);
#else
    t->stop_flag = 0;
    pthread_create(&t->thread, NULL, timer_thread_proc, t);
#endif

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_timer_stop
 *
 * r3 = timer_id
 * -----------------------------------------------------------------------*/
int64_t sys_timer_stop(ppu_context* ctx)
{
    uint32_t timer_id = LV2_ARG_U32(ctx, 0);

    if (timer_id == 0 || timer_id > SYS_TIMER_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_timer_info* t = &g_sys_timers[timer_id - 1];
    if (!t->active)
        return (int64_t)(int32_t)CELL_ESRCH;

    if (!t->running)
        return CELL_OK;

    t->running = 0;

#ifdef _WIN32
    if (t->stop_event) SetEvent(t->stop_event);
    if (t->thread_handle) {
        WaitForSingleObject(t->thread_handle, 3000);
        CloseHandle(t->thread_handle);
        t->thread_handle = NULL;
    }
    if (t->stop_event) {
        CloseHandle(t->stop_event);
        t->stop_event = NULL;
    }
#else
    pthread_mutex_lock(&t->mtx);
    t->stop_flag = 1;
    pthread_cond_signal(&t->cv);
    pthread_mutex_unlock(&t->mtx);
    pthread_join(t->thread, NULL);
#endif

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Registration
 *
 * The former 141/142/145 collisions with the event-flag block are resolved:
 * SYS_EVENT_FLAG_* now carry their real PS3 numbers (82/83/85/86/87/118/132/
 * 139), so 141=usleep, 142=sleep and 145=get_current_time are unshadowed.
 * -----------------------------------------------------------------------*/
void sys_timer_init(lv2_syscall_table* tbl)
{
    memset(g_sys_timers, 0, sizeof(g_sys_timers));

#ifdef _WIN32
    ensure_qpc_init();
#endif

    lv2_syscall_register(tbl, SYS_TIMER_CREATE,                   sys_timer_create);
    lv2_syscall_register(tbl, SYS_TIMER_DESTROY,                  sys_timer_destroy);
    lv2_syscall_register(tbl, SYS_TIMER_START,                    sys_timer_start);
    lv2_syscall_register(tbl, SYS_TIMER_STOP,                     sys_timer_stop);
    lv2_syscall_register(tbl, SYS_TIMER_CONNECT_EVENT_QUEUE,      sys_timer_connect_event_queue);
    lv2_syscall_register(tbl, SYS_TIMER_DISCONNECT_EVENT_QUEUE,   sys_timer_disconnect_event_queue);

    /* These have conflicting numbers with event flag syscalls.
     * Register them here -- last registration wins. If events are
     * registered after timers, the event handlers will override.
     * The runtime should dispatch timer_usleep/sleep/get_current_time
     * via direct function calls instead. */
    lv2_syscall_register(tbl, SYS_TIME_GET_TIMEBASE_FREQUENCY, sys_time_get_timebase_frequency);

    /* Register these but be aware of collisions */
    lv2_syscall_register(tbl, SYS_TIMER_USLEEP,            sys_timer_usleep);
    lv2_syscall_register(tbl, SYS_TIMER_SLEEP,             sys_timer_sleep);
    lv2_syscall_register(tbl, SYS_TIME_GET_TIMEZONE,       sys_time_get_timezone);
    lv2_syscall_register(tbl, SYS_TIME_GET_CURRENT_TIME,   sys_time_get_current_time);
}
