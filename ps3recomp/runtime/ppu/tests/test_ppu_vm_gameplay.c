/* Drives runtime/ppu/ppu_vm_gameplay.c: fat before gameplay, thin after. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint8_t* vm_base;
volatile int g_ppu_preempt_req;
volatile int g_ps3_vm_gameplay;
uint32_t g_committed_lo[8];
uint32_t g_committed_hi[8];
int g_committed_count;
void ppu_preempt_check(void) {}

static int g_full_n;
uint32_t vm_read32_full(uint64_t a)
{
    (void)a;
    g_full_n++;
    return 0x11111111u;
}

#include "../ppu_vm_gameplay.c"

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    uint8_t arena[16];
    memset(arena, 0, sizeof arena);
    vm_base = arena;
    g_ppu_preempt_req = 0;
    g_committed_lo[0] = 0;
    g_committed_hi[0] = 16;
    g_committed_count = 1;

    g_ps3_vm_gameplay = 0;
    g_full_n = 0;
    CHECK(vm_read32(0) == 0x11111111u);
    CHECK(g_full_n == 1);

    g_ps3_vm_gameplay = 1;
    arena[0] = 0x12; arena[1] = 0x34; arena[2] = 0x56; arena[3] = 0x78;
    g_full_n = 0;
    CHECK(vm_read32(0) == 0x12345678u);
    CHECK(g_full_n == 0);
    CHECK(vm_read32(16) == 0); /* uncommitted */
    CHECK(g_full_n == 0);
    printf("PASS\n");
    return 0;
}
