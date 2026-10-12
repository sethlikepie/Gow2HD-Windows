/* spurs_kernel.h — SPURS HLE kernel: asynchronous workload execution.
 *
 * The missing execution engine of the SPURS bring-up (docs/SPURS_BRINGUP.md,
 * Marco 2.5 / Marco 3). On a real PS3, cellSpursInitialize creates an SPU
 * thread group running the SPURS *kernel*, which schedules the policy modules
 * the title adds via cellSpursAddWorkload and runs their SPU code. Our
 * cellSpurs HLE tracked workloads but never ran them: a workload's policy
 * module was registered and then ignored, so any PPU code that waits on the
 * work completing spins forever.
 *
 * This module is the HLE kernel: a single host thread (the "kernel") created
 * at cellSpursInitialize, parked on a condition variable, that pulls submitted
 * workloads off a queue and dispatches each one — OFF the PPU thread, which is
 * the semantics a real workload needs (a task is one-shot and can dispatch
 * inline, but a workload's policy module is scheduler-driven and concurrent
 * with the PPU; running it inline on the AddWorkload caller could block it).
 *
 * The actual per-workload work is injected as a callback (`spurs_dispatch_fn`)
 * so this engine is decoupled from — and independently testable of — the SPU
 * image dispatch (runtime/spu/spu_workload.c, already tested). In production
 * cellSpurs sets the callback to one that calls spu_workload_dispatch(); a test
 * sets it to a plain C function and verifies the scheduling/async/completion.
 *
 * Scope (honest): this runs each submitted workload ONCE (the Marco 3 "job
 * runs" foundation). A real policy module is invoked repeatedly per scheduling
 * quantum and interacts with title-specific structures — that is the iterative
 * Marco 5 work. This engine is the reusable, tested substrate underneath it.
 */
#ifndef PS3RECOMP_SPURS_KERNEL_H
#define PS3RECOMP_SPURS_KERNEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One unit of work handed to the kernel — a SPURS workload (policy module) or
 * a task. `pm`/`sizePm` are the guest SPU image; `data` is the workload arg
 * (the low 32 bits become the SPU job's r3 / task-arg EA). */
typedef struct {
    const void* pm;
    uint32_t    sizePm;
    uint64_t    data;
    uint32_t    wid;
    uint32_t    is_task;     /* wid names a tracked task (serialize re-runs) */
    uint32_t    taskset_ea;  /* SPURS task: guest EA of the SDK-layout taskset */
    uint32_t    task_id;     /* SPURS task: index within the taskset (0..127) */
} spurs_workload_job;

/* Per-workload dispatch callback, invoked on the kernel thread. */
typedef void (*spurs_dispatch_fn)(const spurs_workload_job* job, void* user);

/* Start the kernel thread (idempotent: a second call with the kernel already
 * running just updates the dispatch callback). `nSpus` is informational. */
void spurs_kernel_start(unsigned nSpus, spurs_dispatch_fn fn, void* user);

/* Submit a workload to the kernel; copies `*job`, enqueues it, wakes the
 * kernel, and returns immediately. Returns 1 if queued, 0 if the kernel is not
 * running or the queue is full. */
int spurs_kernel_submit(const spurs_workload_job* job);

/* Block until every submitted workload has finished dispatching (completed ==
 * submitted), or until `timeout_ms` elapses (0 = no wait, just poll). Returns 1
 * if drained, 0 on timeout. Used by join paths and by tests. */
int spurs_kernel_drain(unsigned timeout_ms);

/* Stop the kernel thread and join it. Idempotent. */
void spurs_kernel_stop(void);

/* Diagnostics. */
int      spurs_kernel_running(void);
unsigned spurs_kernel_submitted(void);
unsigned spurs_kernel_completed(void);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_SPURS_KERNEL_H */
