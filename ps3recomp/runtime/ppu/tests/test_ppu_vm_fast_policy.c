/* Drives the shipped load-vs-gameplay VM fast-mask policy. */
#include "../ppu_vm_fast_policy.h"
#include <stdio.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    CHECK(ps3_vm_fast_effective_mask(0, 0) == 0);
    CHECK(ps3_vm_fast_effective_mask(0, 1) == 0x7F);
    CHECK(ps3_vm_fast_effective_mask(0x78, 0) == 0x78);
    CHECK(ps3_vm_fast_effective_mask(0x78, 1) == 0x78);
    CHECK(ps3_vm_fast_effective_mask(0x07, 0) == 0x07);
    CHECK(ps3_vm_fast_effective_mask(0x7F, 1) == 0x7F);
    CHECK(PS3_VM_FAST_GAMEPLAY_DRAWS == 400u);
    CHECK(ps3_vm_gameplay_ready() == 0);
    ps3_vm_note_gameplay_draws(399);
    CHECK(ps3_vm_gameplay_ready() == 0);
    ps3_vm_note_gameplay_draws(400);
    CHECK(ps3_vm_gameplay_ready() == 1);
    ps3_vm_note_gameplay_draws(10);
    CHECK(ps3_vm_gameplay_ready() == 1);
    return 0;
}
