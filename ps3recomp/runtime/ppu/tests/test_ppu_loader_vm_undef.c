/* Drive the ppu_loader.cpp pattern: lift headers may #define vm_read32 to
 * vm_read32_hot (patch_vm_hot_all). After #undef, the fat symbol keeps its
 * own name and the thin inline remains callable under the _hot name. */
#include <stdint.h>

#define vm_read32(a) vm_read32_hot(a)
#define vm_read64(a) vm_read64_hot(a)
#define vm_write32(a, v) vm_write32_hot((a), (v))

static inline uint32_t vm_read32_hot(uint64_t a)
{
    (void)a;
    return 0xDEADu;
}

#ifdef vm_read32
#undef vm_read32
#endif
#ifdef vm_read64
#undef vm_read64
#endif
#ifdef vm_write32
#undef vm_write32
#endif
#ifdef vm_write64
#undef vm_write64
#endif

static uint32_t vm_read32(uint64_t a)
{
    (void)a;
    return 0xBEEFu;
}

int main(void)
{
    if (vm_read32(0) != 0xBEEFu)
        return 1;
    if (vm_read32_hot(0) != 0xDEADu)
        return 2;
    return 0;
}
