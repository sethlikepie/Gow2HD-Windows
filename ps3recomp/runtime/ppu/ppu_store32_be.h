#ifndef PPU_STORE32_BE_H
#define PPU_STORE32_BE_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host-endian value -> 4 big-endian bytes at base+addr. Used by vm_write32_hot. */
static inline void ps3_store32_be(uint8_t* base, uint32_t addr, uint32_t host_val)
{
    uint32_t raw = __builtin_bswap32(host_val);
    memcpy(base + addr, &raw, 4);
}

/* Host-endian value -> 8 big-endian bytes at base+addr. Used by vm_write64_hot. */
static inline void ps3_store64_be(uint8_t* base, uint32_t addr, uint64_t host_val)
{
    uint64_t raw = __builtin_bswap64(host_val);
    memcpy(base + addr, &raw, 8);
}

#ifdef __cplusplus
}
#endif

#endif
