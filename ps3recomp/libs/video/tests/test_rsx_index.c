/*
 * Drives rsx_index_from_guest — the same helper metal_draw_indexed uses.
 * Bytes are guest big-endian, as stored in RSX local/main memory.
 */
#include "../rsx_index.h"
#include <stdio.h>

int main(void)
{
    int ok = 1;
#define CHECK(cond) do { if (!(cond)) { printf("  [FAIL] %s\n", #cond); ok = 0; } } while (0)

    /* BE 0x0012 → 18, not 0x1200. */
    {
        const u8 be[2] = { 0x00, 0x12 };
        CHECK(rsx_index_from_guest(be, 1) == 18u);
        CHECK(!rsx_index_is_restart(rsx_index_from_guest(be, 1)));
    }
    /* BE 0xFFFF → restart, not vertex 65535. */
    {
        const u8 rst[2] = { 0xFF, 0xFF };
        CHECK(rsx_index_from_guest(rst, 1) == RSX_INDEX_RESTART);
        CHECK(rsx_index_is_restart(rsx_index_from_guest(rst, 1)));
    }
    /* 32-bit BE 5. */
    {
        const u8 be[4] = { 0x00, 0x00, 0x00, 0x05 };
        CHECK(rsx_index_from_guest(be, 0) == 5u);
    }
    /* 32-bit restart already 0xFFFFFFFF. */
    {
        const u8 rst[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
        CHECK(rsx_index_from_guest(rst, 0) == RSX_INDEX_RESTART);
    }
    /* Packetized DRAW_INDEX_ARRAY: 0x201+256=0x301, 256+95=351, 351%3=0. */
    {
        CHECK(rsx_index_batch_continues(5, 0x301u, 95u, 5, 0x201u, 256u));
        CHECK(!rsx_index_batch_continues(5, 0x301u, 95u, 5, 0x201u, 255u));
        CHECK(!rsx_index_batch_continues(6, 0x301u, 95u, 5, 0x201u, 256u));
        CHECK((256u % 3u) == 1u);
        CHECK((95u % 3u) == 2u);
        CHECK(((256u + 95u) % 3u) == 0u);
    }
    /* Same assembler-run rule for DRAW_ARRAYS and continuing topologies.
     * A 256-vertex triangle list is not a whole primitive group; a strip/fan
     * continues even when the packet length divides 3. */
    {
        CHECK(rsx_draw_batch_continues(5, 256u, 256u, 5, 0u, 256u));
        CHECK(rsx_draw_batch_continues(5, 256u, 95u, 5, 0u, 256u));
        CHECK(!rsx_draw_batch_continues(5, 512u, 256u, 5, 0u, 256u));
        CHECK(rsx_draw_batch_continues(6, 256u, 256u, 6, 0u, 256u)); /* strip */
        CHECK(rsx_draw_batch_continues(7, 256u, 256u, 7, 0u, 256u)); /* fan */
        CHECK(rsx_draw_batch_continues(3, 256u, 256u, 3, 0u, 256u)); /* line loop */
        CHECK(rsx_draw_batch_continues(9, 256u, 256u, 9, 0u, 256u)); /* quad strip */
        CHECK(!rsx_draw_batch_continues(7, 256u, 256u, 5, 0u, 256u));
    }

#undef CHECK
    printf("%s\n", ok ? "test_rsx_index: PASS" : "test_rsx_index: FAIL");
    return ok ? 0 : 1;
}
