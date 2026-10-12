/* Drives the shipped FIFO fast-load / tight-payload helpers. */
#include "gcm_fifo_fast.h"
#include <stdio.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    unsigned char buf[16];
    buf[0] = 0x12; buf[1] = 0x34; buf[2] = 0x56; buf[3] = 0x78;
    buf[4] = 0xAA; buf[5] = 0xBB; buf[6] = 0xCC; buf[7] = 0xDD;
    CHECK(gcm_fifo_load32_be(buf, 0) == 0x12345678u);
    CHECK(gcm_fifo_load32_be(buf, 4) == 0xAABBCCDDu);

    CHECK(gcm_fifo_method_is_sync(0x0050u) == 1); /* SET_REFERENCE */
    CHECK(gcm_fifo_method_is_sync(0x0014u) == 1);
    CHECK(gcm_fifo_method_is_sync(0x1824u) == 0); /* DRAW_INDEX_ARRAY */
    CHECK(gcm_fifo_method_is_sync(0x1814u) == 0);

    CHECK(gcm_fifo_use_tight_payload(0x40000000u, 16u, 0x1824u) == 1);
    CHECK(gcm_fifo_use_tight_payload(0u, 16u, 0x1824u) == 0);       /* increasing */
    CHECK(gcm_fifo_use_tight_payload(0x40000000u, 1u, 0x1824u) == 0); /* one word */
    CHECK(gcm_fifo_use_tight_payload(0x40000000u, 16u, 0x0050u) == 0); /* sync */

    /* SIGBUS 0x7e463664: JUMP target used as IO offset past the window. */
    {
        const uint32_t iosz = 0x3000000u; /* 48 MB GCM IO */
        CHECK(gcm_fifo_off_in_window(0u, iosz) == 1);
        CHECK(gcm_fifo_off_in_window(iosz - 4u, iosz) == 1);
        CHECK(gcm_fifo_off_in_window(iosz, iosz) == 0);
        CHECK(gcm_fifo_off_in_window(0x3E463664u, iosz) == 0); /* crash offset */
        CHECK(gcm_fifo_off_in_window(0u, 0u) == 0);
    }
    return 0;
}
