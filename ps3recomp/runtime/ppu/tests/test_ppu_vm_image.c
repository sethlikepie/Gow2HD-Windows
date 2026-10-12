/* Drives the shipped ELF/TOC predicate used by vm_read32's image fast path. */
#include "../ppu_vm_image.h"
#include <stdio.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    CHECK(vm_addr_is_guest_image(0x00248F3Cu) == 1); /* func_00248F3C */
    CHECK(vm_addr_is_guest_image(0x00293010u) == 1); /* func_00293010 */
    CHECK(vm_addr_is_guest_image(0x00541178u) == 1); /* TOC */
    CHECK(vm_addr_is_guest_image(0x00000000u) == 0);
    CHECK(vm_addr_is_guest_image(0x0000FFFFu) == 0);
    CHECK(vm_addr_is_guest_image(0x00800000u) == 0); /* hi exclusive */
    CHECK(vm_addr_is_guest_image(0x0A050000u) == 0); /* GCM context */
    CHECK(vm_addr_is_guest_image(0x40000000u) == 0); /* heap / HOSTRES */
    return 0;
}
