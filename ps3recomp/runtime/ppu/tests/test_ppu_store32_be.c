/* Drives the shipped BE store used by vm_write32_hot. */
#include "../ppu_store32_be.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    uint8_t b[8];
    memset(b, 0, sizeof b);
    ps3_store32_be(b, 0, 0x12345678u);
    CHECK(b[0] == 0x12 && b[1] == 0x34 && b[2] == 0x56 && b[3] == 0x78);
    ps3_store32_be(b, 4, 0xAABBCCDDu);
    CHECK(b[4] == 0xAA && b[5] == 0xBB && b[6] == 0xCC && b[7] == 0xDD);
    uint8_t q[8];
    memset(q, 0, sizeof q);
    ps3_store64_be(q, 0, 0x0123456789ABCDEFULL);
    CHECK(q[0]==0x01 && q[1]==0x23 && q[2]==0x45 && q[3]==0x67);
    CHECK(q[4]==0x89 && q[5]==0xAB && q[6]==0xCD && q[7]==0xEF);
    return 0;
}
