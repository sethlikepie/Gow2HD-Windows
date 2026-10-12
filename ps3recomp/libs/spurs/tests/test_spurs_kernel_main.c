/* test_spurs_kernel_main.c — isolated test for the SPURS HLE kernel.
 *
 * Validates libs/spurs/spurs_kernel.c (Marco 2.5/3 execution engine) WITHOUT
 * the SPU harness: the per-workload dispatch is a plain C callback. Checks:
 *   - the kernel thread starts and reports running,
 *   - submitted workloads are all dispatched (completed == submitted),
 *   - each workload runs with the correct arg (data) and id (wid),
 *   - dispatch happens on the KERNEL thread, not the submitting thread (async),
 *   - drain() blocks until completion,
 *   - stop() joins cleanly.
 *
 * The real SPU image dispatch (spu_workload.c) is tested separately
 * (test_workload_main.c); this isolates the scheduling/async/completion logic.
 */
#include "spurs_kernel.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Task 4 (bringup-instrumentacao-fase0): spurs_kernel.c now calls
 * PS3_TRACE(PS3_TS_SPURS, ...) (WORKER_IDLE/QUEUE_PUSH/QUEUE_POP), which
 * needs runtime/trace/ps3_trace.c linked (see run_tests.sh's run_extra
 * spurs_kernel line) -- and ps3_trace_emit() declares ps3_timebase_now()
 * `extern`, defined for real in runtime/syscalls/sys_timer.c, which this
 * isolated test does not want to drag in (heavy transitive deps). Same
 * stub pattern as runtime/trace/tests/test_ps3_mem_trace.c /
 * test_ps3_checkpoint.c: a fixed value is fine, nothing here asserts on
 * <tb>. */
uint64_t ps3_timebase_now(void) { return 1; }

#ifdef _WIN32
#include <windows.h>
typedef DWORD tid_t;
static tid_t cur_tid(void) { return GetCurrentThreadId(); }
static void  sleep_ms(unsigned m) { Sleep(m); }
#else
#include <pthread.h>
#include <unistd.h>
typedef unsigned long long tid_t;
static tid_t cur_tid(void) { return (tid_t)(uintptr_t)pthread_self(); }
static void  sleep_ms(unsigned m) { usleep(m * 1000u); }
#endif

#define NJOBS 50

static uint64_t g_results[NJOBS];
static int      g_ran[NJOBS];
static tid_t    g_dispatch_tid;
static volatile int g_dispatch_count;

/* Test dispatch: record arg by id, stamp the thread it ran on. A tiny sleep
 * makes the async race observable (results must NOT be ready right after
 * submit; they appear only after drain). */
static void test_dispatch(const spurs_workload_job* job, void* user)
{
    (void)user;
    sleep_ms(1);
    if (job->wid < NJOBS) {
        g_results[job->wid] = job->data;
        g_ran[job->wid] = 1;
    }
    g_dispatch_tid = cur_tid();
    /* Atomico, e nao `volatile int` com ++: o kernel corre 2+ workers e ambos
     * executam este callback, portanto o read-modify-write perdia incrementos
     * (visto: 48/50). `volatile` ordena acessos, nao os torna atomicos -- era
     * a causa da flakiness deste teste, nao um bug do kernel. */
    __atomic_fetch_add(&g_dispatch_count, 1, __ATOMIC_RELAXED);
}

int main(void)
{
    int ok = 1;
    tid_t main_tid = cur_tid();

    memset(g_results, 0, sizeof(g_results));
    memset(g_ran, 0, sizeof(g_ran));
    g_dispatch_count = 0;

    spurs_kernel_start(2, test_dispatch, NULL);
    int running = spurs_kernel_running();
    printf("  [START      ] kernel running=%d                              %s\n",
           running, running ? "OK" : "FAIL");
    ok &= running;

    /* Submit NJOBS workloads. */
    int all_queued = 1;
    for (int i = 0; i < NJOBS; i++) {
        spurs_workload_job job;
        memset(&job, 0, sizeof(job));
        job.wid  = (uint32_t)i;
        job.data = 0xC0DE0000ull + (uint64_t)i;
        job.pm   = NULL;
        job.sizePm = 0;
        if (!spurs_kernel_submit(&job)) all_queued = 0;
    }
    printf("  [SUBMIT     ] %d workloads queued (submitted=%u)             %s\n",
           NJOBS, spurs_kernel_submitted(), all_queued ? "OK" : "FAIL");
    ok &= all_queued;

    /* Block until the kernel has dispatched them all. */
    int drained = spurs_kernel_drain(5000);
    printf("  [DRAIN      ] drained=%d completed=%u/%u                     %s\n",
           drained, spurs_kernel_completed(), spurs_kernel_submitted(),
           drained ? "OK" : "FAIL");
    ok &= drained;

    /* Every job ran exactly once with the right arg. */
    int all_ran = 1, all_data = 1;
    for (int i = 0; i < NJOBS; i++) {
        if (!g_ran[i]) all_ran = 0;
        if (g_results[i] != 0xC0DE0000ull + (uint64_t)i) all_data = 0;
    }
    printf("  [RAN        ] all %d workloads dispatched                    %s\n",
           NJOBS, all_ran ? "OK" : "FAIL");
    printf("  [ARG        ] each workload saw its data value               %s\n",
           all_data ? "OK" : "FAIL");
    ok &= all_ran & all_data;

    /* Async: dispatch ran on the kernel thread, not main. */
    int async_ok = (g_dispatch_tid != main_tid) && (g_dispatch_count == NJOBS);
    printf("  [ASYNC      ] dispatch tid != main tid (count=%d)            %s\n",
           g_dispatch_count, async_ok ? "OK" : "FAIL");
    ok &= async_ok;

    spurs_kernel_stop();
    int stopped = !spurs_kernel_running();
    printf("  [STOP       ] kernel joined, running=%d                      %s\n",
           spurs_kernel_running(), stopped ? "OK" : "FAIL");
    ok &= stopped;

    if (ok)
        printf("  PASS: SPURS HLE kernel schedules, runs async, completes, joins.\n");
    else
        printf("  FAIL: SPURS HLE kernel test failed.\n");
    return ok ? 0 : 1;
}
