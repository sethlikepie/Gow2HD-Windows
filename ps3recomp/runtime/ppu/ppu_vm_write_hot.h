#ifndef PPU_VM_WRITE_HOT_H
#define PPU_VM_WRITE_HOT_H

/*
 * Lift TUs call vm_write32_hot (thin, same functional steps as the
 * vm_fast(5) path) instead of the diagnostic-heavy vm_write32.
 * Reads stay on the out-of-line path (inlined reads at boot break the menu).
 */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void vm_write32_hot(uint64_t addr, uint32_t val);
#ifdef __cplusplus
}
#endif
#ifndef PPU_VM_WRITE_HOT_NO_MACRO
#define vm_write32(addr, val) vm_write32_hot((addr), (val))
#endif
#endif
