/* Drives the shipped CTR-cache hit/remember helpers (ppu_icall_hot.h). */
#define PPU_ICALL_HOT_NO_WRAP 1
#include "../ppu_icall_hot.h"
#include <stdio.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

static void dummy_a(void) {}
static void dummy_b(void) {}

int main(void)
{
    struct ps3_icall_hot_state st = {0, 0};
    CHECK(ps3_icall_hot_hit(&st, 0x002F0D78u) == 0);
    ps3_icall_hot_remember(&st, 0x002F0D78u, (void (*)(void*))dummy_a);
    CHECK(ps3_icall_hot_hit(&st, 0x002F0D78u) == 1);
    CHECK(ps3_icall_hot_hit(&st, 0x00156D78u) == 0);
    ps3_icall_hot_remember(&st, 0x00156D78u, (void (*)(void*))dummy_b);
    CHECK(ps3_icall_hot_hit(&st, 0x00156D78u) == 1);
    CHECK(ps3_icall_hot_hit(&st, 0x002F0D78u) == 0);
    ps3_icall_hot_remember(&st, 0x0A0C0DE0u, 0);
    CHECK(ps3_icall_hot_hit(&st, 0x0A0C0DE0u) == 0); /* lookup miss stays slow */
    return 0;
}
