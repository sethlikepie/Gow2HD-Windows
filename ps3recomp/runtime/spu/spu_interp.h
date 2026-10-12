/*
 * ps3recomp - SPU interpreter
 *
 * Runs SPU code straight from local store, for programs a static lift cannot
 * express: code that writes the code it then runs (GoW2's SCREAM policy
 * module generates its register save/restore stub at LS 0x3FEC0 at run time),
 * real SPU interrupts (bie/bid windows, iret, the vector at LS 0) and
 * coroutine-style context switches.
 *
 * The per-instruction semantics are NOT a second implementation: the ALU cases
 * and the decode table are generated from tools/spu_lifter.py and
 * tools/spu_disasm.py (tools/gen_spu_interp.py -> spu_interp_gen.h) and call
 * the same spu_helpers.h functions lifted code calls. Channels go through the
 * same spu_rdch/spu_wrch/spu_rchcnt (spu_channels.c -> spu_dma.h).
 */
#ifndef SPU_INTERP_H
#define SPU_INTERP_H

#include "spu_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Why spu_interp_run returned. ctx->pc is where execution continues when the
 * run can be resumed (BUDGET, WAIT). */
enum spu_interp_result {
    SPU_INTERP_EXIT   = 0,  /* branched to the SPURS kernel exit (interp_spurs_pm) */
    SPU_INTERP_STOP   = 1,  /* stop / stopd (ctx->stop_code), or `br .` halt */
    SPU_INTERP_BUDGET = 2,  /* max_insns executed; resume at ctx->pc */
    SPU_INTERP_WAIT   = 3,  /* blocking channel read with nothing to read; resume at ctx->pc */
    SPU_INTERP_FAULT  = 4   /* unknown opcode (logged once) */
};

/* Execute from `pc` until one of the results above. At most max_insns
 * instructions run (0 = no limit). Interrupt state (ctx->interp_ie) and the
 * decrementer persist in ctx across calls, so a BUDGET/WAIT run resumes
 * exactly where it stopped. */
int spu_interp_run(spu_context* ctx, uint32_t pc, uint64_t max_insns);

/* Name of a result, for logs. */
const char* spu_interp_result_name(int r);

/* Instructions executed by the last spu_interp_run on this thread. */
uint64_t spu_interp_last_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SPU_INTERP_H */
