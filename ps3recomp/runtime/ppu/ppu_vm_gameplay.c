/*
 * Out-of-line vm_read32: thin body after gameplay, fat diagnostics before.
 * Lift TUs keep calling vm_read32 (no 87MB inline). The fat function lives
 * in ppu_loader as vm_read32_full.
 */
#define PPU_VM_HOT_NO_MACRO 1
#include "ppu_vm_hot.h"

extern uint32_t vm_read32_full(uint64_t addr);

uint32_t vm_read32(uint64_t addr)
{
    if (__builtin_expect(!ps3_vm_gameplay_ready_hot(), 0))
        return vm_read32_full(addr);
    return vm_read32_thin(addr);
}
