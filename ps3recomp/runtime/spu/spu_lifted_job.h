/* spu_lifted_job.h — run a lifted SPU job with the SPURS task ABI.
 *
 * The bridge between the lv2 SPU-thread-group layer (runtime/syscalls/lv2_register.c,
 * which runs SPU threads as registered PPU-fallbacks) and the lifted-execution layer
 * (a lifted spu_func from spu_lifter). A SPURS task / SPU thread receives its argument
 * (the job/task descriptor effective address) in r3 and runs against its 256 KB local
 * store; this helper sets that up, runs the lifted entry, and bridges the local store.
 *
 * Wiring into lv2: register `spu_lifted_fallback` for an SPU image's entry point with
 * `user` = the lifted entry fn; lv2's spu_fallback_thread_proc then calls it with
 * (tid, args_ea, args_size, user), and we run the lifted job on that thread's LS.
 */
#ifndef SPU_LIFTED_JOB_H
#define SPU_LIFTED_JOB_H

#include "spu_context.h"
#include <string.h>
#include <setjmp.h>

typedef void (*spu_lifted_entry_fn)(spu_context*);

/* O argumento da task e lido com um load de 16 bytes em vm_base + args_ea; sem
 * limite, um args_ea invalido le fora do espaco do guest. A guarda anterior era
 * `args_ea < 0x100000000u`, tautologica num uint32_t (o compilador avisa com
 * -Wtautological-constant-out-of-range-compare), portanto o load estava sem
 * verificacao nenhuma. Segue a convencao de spu_dma.h: ppu_vm_size == 0 (o que
 * as harnesses de teste usam) desliga a verificacao. */
static inline int spu_args_ea_in_range(uint32_t args_ea)
{
    extern uint32_t ppu_vm_size;
    if (ppu_vm_size == 0) return 1;
    return (uint64_t)args_ea + 16u <= (uint64_t)ppu_vm_size;
}

/* Run a lifted SPU job. `local_store` (256 KB, may be NULL) is the SPU thread's
 * local store; it is brought into the context, the job's arg EA is placed in r3
 * (the SPURS task ABI), the lifted entry runs, and the LS is written back.
 * The caller links the channel ABI (spu_wrch/spu_rdch/...) that the lifted code
 * uses to reach DMA / mailboxes / events. Returns the job's exit code (0).
 *
 * `r4_hi64`/`r4_lo64`: SPURS task entry ABI (spursTasksetStartTask) also sets
 * r4 = { spurs EA (upper u64), taskset->args (lower u64) }; pass 0/0 for a
 * plain (non-taskset) job. */
static inline int32_t spu_run_lifted_job_ex_image(spu_lifted_entry_fn entry,
                                                  uint8_t* local_store,
                                                  uint32_t args_ea,
                                                  uint64_t r4_hi64, uint64_t r4_lo64,
                                                  int image_id)
{
    if (!entry) return -1;
    extern void wp83_job_phase(int, int); wp83_job_phase(image_id, 0);   /* winport v83-spuprof */
    spu_context ctx;
    spu_context_init(&ctx, 0);
    ctx.image_id = image_id;
    if (local_store) memcpy(ctx.ls, local_store, SPU_LS_SIZE);  /* job's LS in */
    /* SPURS task ABI: the task argument is a CellSpursTaskArgument -- ONE
     * 128-bit value loaded whole into vector register r3 (SPU registers are
     * 128-bit; RPCS3's taskset dispatch does spu.gpr[3] = args u128). The 4
     * BE words land in r3's four elements: GoW2's worker tasks read their
     * LFQueue EA from r3 word 1. (Two earlier attempts -- arg EA as a pointer
     * in r3, then the words spread across r3..r6 -- left the base registers
     * zero and sent the task into base-0 DMA loops.) */
    if (local_store && (r4_hi64 | r4_lo64)) {
        /* SPURS taskset task: the argument is the CreateTask-time SNAPSHOT in
         * TaskInfo (copied to tempAreaTaskInfo.args at LS 0x2780 by
         * spu_task_ls_setup) -- RPCS3's spursTasksetStartTask(taskInfo->args).
         * Reading the original guest EA here is wrong: dispatch is async and
         * the game's stack that held the argument is long overwritten by the
         * time a re-kick runs the task (observed: arg words read back as 0). */
        const unsigned char* a = local_store + 0x2780;
        for (int i = 0; i < 4; i++)
            ctx.gpr[3]._u32[i] =
                ((unsigned)a[i*4] << 24) | ((unsigned)a[i*4+1] << 16) |
                ((unsigned)a[i*4+2] << 8) | (unsigned)a[i*4+3];
    } else if (args_ea && spu_args_ea_in_range(args_ea)) {
        extern unsigned char* vm_base;
        const unsigned char* a = vm_base + args_ea;
        for (int i = 0; i < 4; i++)
            ctx.gpr[3]._u32[i] =
                ((unsigned)a[i*4] << 24) | ((unsigned)a[i*4+1] << 16) |
                ((unsigned)a[i*4+2] << 8) | (unsigned)a[i*4+3];
    } else {
        ctx.gpr[3]._u32[0] = args_ea;
    }
    /* r4 = { spurs EA, taskset args } for SPURS taskset tasks. */
    ctx.gpr[4]._u32[0] = (uint32_t)(r4_hi64 >> 32);
    ctx.gpr[4]._u32[1] = (uint32_t)r4_hi64;
    ctx.gpr[4]._u32[2] = (uint32_t)(r4_lo64 >> 32);
    ctx.gpr[4]._u32[3] = (uint32_t)r4_lo64;
    jmp_buf done;
    ctx.exit_jmp = &done;
    wp83_job_phase(image_id, 1);   /* winport v83-spuprof */
    extern SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
    g_spu_trampoline_fn = 0;
    if (setjmp(done) == 0) {
        entry(&ctx);                                            /* run the lifted job  */
        SPU_DRAIN(&ctx);          /* drain any tail-dispatch the entry left pending */
    }
    g_spu_trampoline_fn = 0;
    ctx.exit_jmp = 0;
    wp83_job_phase(image_id, 2);   /* winport v83-spuprof */
    { extern SPU_THREAD_LOCAL int t_wp79_ls_discard;   /* winport v79-spu-side: WINPORT_SPU_LS_NOCOPYOUT */
      if (local_store && !t_wp79_ls_discard) memcpy(local_store, ctx.ls, SPU_LS_SIZE); }  /* LS back out */
    wp83_job_phase(image_id, 3);   /* winport v83-spuprof */
    { extern void spu_mfc_release(spu_context*); spu_mfc_release(&ctx); }   /* winport v28-scream: stack context gives its MFC slot back (PS3_MFC_SLOT_FIX=1) */
    return 0;
}

/* Compatibility entry point for PPU fallbacks that do not identify an image.
 * Workload dispatch must use the `_image` form so an overlapping LS address
 * cannot resolve to a function from a different SPU binary. */
static inline int32_t spu_run_lifted_job_ex(spu_lifted_entry_fn entry,
                                            uint8_t* local_store,
                                            uint32_t args_ea,
                                            uint64_t r4_hi64, uint64_t r4_lo64)
{
    return spu_run_lifted_job_ex_image(entry, local_store, args_ea,
                                       r4_hi64, r4_lo64, 0);
}

static inline int32_t spu_run_lifted_job(spu_lifted_entry_fn entry,
                                         uint8_t* local_store,
                                         uint32_t args_ea)
{
    return spu_run_lifted_job_ex(entry, local_store, args_ea, 0, 0);
}

/* lv2 PPU-fallback wrapper: signature matches spu_ppu_fallback_fn so it can be
 * registered via spu_register_ppu_fallback(entry_point, spu_lifted_fallback, fn).
 * Defined where spu_thread_get_local_store() is available (the lv2 TU); declared
 * here for callers. (uint32_t tid, uint32_t args_ea, uint32_t args_size, void* user) */
int32_t spu_lifted_fallback(uint32_t tid, uint32_t args_ea,
                            uint32_t args_size, void* user);

#endif /* SPU_LIFTED_JOB_H */
