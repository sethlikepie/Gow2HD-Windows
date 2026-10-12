#ifndef PPU_P10_CTR_H
#define PPU_P10_CTR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Gated by PS3_TRACE_P10_CTR (OFF default). Records ctx->ctr at the 4
 * func_00293010 bctrl sites. Probe: do not recompute the target. */
void ps3_p10_ctr_note(unsigned site, uint32_t ctr);

unsigned long long ps3_p10_ctr_count(unsigned site, uint32_t ctr);
unsigned ps3_p10_ctr_nunique(unsigned site);
void ps3_p10_ctr_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif
