/*
 * Argument layout for a raw SPURS policy module (no taskset).
 *
 * The workload `data` EA is the upper 64 bits of r4. Word 0 stays clear
 * and word 1 holds the pointer, so shlqbyi by 4 bytes moves it into the
 * preferred slot. That is what the SCREAM policy module does with r4
 * before it DMAs the mixer. A taskset job uses a different r4 and must
 * not call this.
 */
#ifndef SPU_PM_ARG_H
#define SPU_PM_ARG_H

#include <stdint.h>

static inline void spu_pm_data_r4(uint32_t data_ea, uint32_t out[4])
{
    uint64_t hi = data_ea;
    out[0] = (uint32_t)(hi >> 32);
    out[1] = (uint32_t)hi;
    out[2] = 0;
    out[3] = 0;
}

#endif
