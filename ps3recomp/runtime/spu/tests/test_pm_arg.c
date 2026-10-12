/*
 * The SCREAM policy module reads its workload data EA from r4, then
 * shifts that qword left by 4 bytes. This drives the same helper the
 * dispatcher uses, then the real shlqbyi.
 *
 * Build: cc -std=c11 -I../../.. -I.. test_pm_arg.c -o test_pm_arg
 */
#include "../spu_pm_arg.h"
#include "../spu_helpers.h"

#include <stdio.h>

static int g_fail;

static void check(int cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        g_fail++;
    }
}

int main(void)
{
    const uint32_t data_ea = 0x886280u;
    uint32_t w[4];
    spu_pm_data_r4(data_ea, w);
    check(w[0] == 0, "word 0 clear");
    check(w[1] == data_ea, "word 1 is the data EA");
    check(w[2] == 0 && w[3] == 0, "lower u64 clear");

    u128 r4;
    r4._u32[0] = w[0];
    r4._u32[1] = w[1];
    r4._u32[2] = w[2];
    r4._u32[3] = w[3];
    u128 shifted = spu_shlqbyi(r4, 4);
    check(shifted._u32[0] == data_ea, "4-byte shift puts the EA in the preferred word");
    check(shifted._u32[1] == 0, "preferred-slot neighbour cleared");

    if (g_fail) {
        fprintf(stderr, "%d failed\n", g_fail);
        return 1;
    }
    printf("ok\n");
    return 0;
}
