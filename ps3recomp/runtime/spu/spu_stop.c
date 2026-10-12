/*
 * SPU `stop` / `stopd` halt handler.
 *
 * The lifted stop path (tools/spu_lifter.py) sets ctx->stop_code (the 14-bit
 * STOP-and-signal code) and ctx->status = SPU_STATUS_STOPPED_BY_STOP, then
 * calls spu_stop(ctx) and returns out of the recompiled SPU function.
 *
 * At the halt point itself there is nothing more to do: control returns to the
 * caller (the spu_run loop / SPURS job dispatcher), which inspects
 * ctx->status and ctx->stop_code to decide the next action — SPURS reads the
 * stop code here to signal job/kernel completion. Keeping this as a real
 * (linkable) function — rather than a per-image inline — gives every lifted
 * SPU image and the test harness a single canonical definition to link
 * against, and a single place to hook host-side stop handling later.
 */
#include "spu_context.h"

void spu_stop(spu_context* ctx)
{
    (void)ctx;
    /* status + stop_code already set by the lifted caller; the dispatcher
     * reads them after the recompiled function returns. */
}
