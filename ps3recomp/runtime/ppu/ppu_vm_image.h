#ifndef PPU_VM_IMAGE_H
#define PPU_VM_IMAGE_H

/*
 * Guest ELF/TOC window (code + rodata + TOC). Heap/HOSTRES (~0x40000000) and
 * the stack sit outside. A thin vm_read32 of this window is NOT the same as
 * PS3_VM_FAST_MASK bit1 (all addresses): all-read-fast at boot stalled load.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS3_VM_IMAGE_LO 0x00010000u
#define PS3_VM_IMAGE_HI 0x00800000u

static inline int vm_addr_is_guest_image(uint32_t a)
{
    return a >= PS3_VM_IMAGE_LO && a < PS3_VM_IMAGE_HI;
}

#ifdef __cplusplus
}
#endif

#endif
