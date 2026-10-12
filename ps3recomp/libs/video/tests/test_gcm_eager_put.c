/* Drives ps3_gcm_eager_put, the helper used by the RSX wake thread. */
#include "gcm_eager_put.h"
#include <stdio.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    uint32_t io = 0x47E00000u;
    uint32_t begin = io;
    uint32_t end = io + 0x200000u;
    uint32_t np = 0xFFFFFFFFu;

    CHECK(ps3_gcm_eager_put(begin + 0x1000u, io, 0, begin, end, &np) == 1);
    CHECK(np == 0x1000u);

    np = 0xDEAD;
    CHECK(ps3_gcm_eager_put(begin + 0x1000u, io, 0x1000u, begin, end, &np) == 0);
    CHECK(np == 0xDEADu);

    np = 0;
    CHECK(ps3_gcm_eager_put(begin + 0x800u, io, 0x1000u, begin, end, &np) == 0);

    CHECK(ps3_gcm_eager_put(end, io, 0, begin, end, &np) == 1);
    CHECK(np == 0x200000u);

    CHECK(ps3_gcm_eager_put(begin, io, 0, begin, end, &np) == 0);
    CHECK(ps3_gcm_eager_put(begin + 0x1000u, 0, 0, begin, end, &np) == 0);
    CHECK(ps3_gcm_eager_put(begin + 0x1000u, io, 0, begin, end, 0) == 0);
    CHECK(ps3_gcm_eager_put(begin - 4u, io, 0, begin, end, &np) == 0);
    CHECK(ps3_gcm_eager_put(end + 4u, io, 0, begin, end, &np) == 0);
    CHECK(ps3_gcm_eager_put(begin + 0x1000u, io, 0, end, begin, &np) == 0);
    CHECK(ps3_gcm_eager_put(begin + 0x1000u, io, 0x300000u, begin, end, &np) == 0);

    /* GoW2 numbers: 2 MB cmd, current 1 MB ahead of put 0. */
    CHECK(ps3_gcm_eager_put(0x47E00000u + 0x100000u, 0x47E00000u, 0,
                            0x47E00000u, 0x47E00000u + 0x200000u, &np) == 1);
    CHECK(np == 0x100000u);

    printf("PASS\n");
    return 0;
}
