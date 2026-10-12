/* The pad map/deadzone/identity-flag triple always comes from ONE
 * publication: publications interleaved deterministically (test hook) inside
 * a read never produce a mixed snapshot. */
#include <stdio.h>
#include <string.h>

static void (*s_hook)(int stage);
static int s_hook_stage = -1;
static void run_hook(int stage)
{
    if (s_hook != NULL && stage == s_hook_stage) {
        void (*h)(int) = s_hook;
        s_hook = NULL;                       /* one shot, no recursion */
        h(stage);
    }
}
#define CELLPAD_MAPPING_TEST_HOOK(stage) run_hook(stage)
#include "cellpad_mapping.h"

#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); return 0; } \
} while (0)

static cellpad_mapping s_m;
static uint16_t s_identity[CELLPAD_MAPPING_BUTTONS], s_swapped[CELLPAD_MAPPING_BUTTONS];

static void reset(const uint16_t* map, uint8_t dz)
{
    memset(&s_m, 0, sizeof s_m);
    atomic_flag_clear(&s_m.writer);
    cellpad_mapping_publish(&s_m, map, dz);
}

static void publish_identity_40(int stage) { (void)stage; cellpad_mapping_publish(&s_m, s_identity, 40); }
static void publish_swapped_40(int stage) { (void)stage; cellpad_mapping_publish(&s_m, s_swapped, 40); }
static void publish_deadzone_only_55(int stage) { (void)stage; cellpad_mapping_publish(&s_m, NULL, 55); }

/* Identity -> non-identity republished between the flag and the deadzone:
 * the reader must not report "identity" with the new deadzone. */
static int test_republish_away_from_identity(void)
{
    uint16_t map[CELLPAD_MAPPING_BUTTONS];
    uint8_t dz = 0;
    for (int stage = 1; stage <= 2; ++stage) {
        reset(s_identity, 20);
        s_hook = publish_swapped_40; s_hook_stage = stage;
        const int id = cellpad_mapping_snapshot(&s_m, map, &dz, 7);
        CHECK(s_hook == NULL);                                        /* the interleave happened */
        CHECK(!id && dz == 40 && memcmp(map, s_swapped, sizeof map) == 0);  /* all of P2 */
    }
    return 1;
}

/* Non-identity -> identity (the case the fast path skips the map for). */
static int test_republish_to_identity(void)
{
    uint16_t map[CELLPAD_MAPPING_BUTTONS];
    uint8_t dz = 0;
    for (int stage = 1; stage <= 2; ++stage) {
        reset(s_swapped, 20);
        s_hook = publish_identity_40; s_hook_stage = stage;
        const int id = cellpad_mapping_snapshot(&s_m, map, &dz, 7);
        CHECK(s_hook == NULL);
        CHECK(id && dz == 40);                                        /* all of P2 */
    }
    return 1;
}

/* A deadzone-only publication keeps the map and is seen whole. */
static int test_deadzone_only_publication(void)
{
    uint16_t map[CELLPAD_MAPPING_BUTTONS];
    uint8_t dz = 0;
    reset(s_swapped, 20);
    s_hook = publish_deadzone_only_55; s_hook_stage = 1;
    CHECK(!cellpad_mapping_snapshot(&s_m, map, &dz, 7));
    CHECK(dz == 55 && memcmp(map, s_swapped, sizeof map) == 0);
    return 1;
}

/* A read landing inside a publication (odd sequence) never returns its
 * half-written fields: it falls back to identity + the fallback deadzone,
 * and the next read sees the finished publication. */
static int s_mid_id = -1;
static uint8_t s_mid_dz;
static void read_mid_publication(int stage)
{
    uint16_t map[CELLPAD_MAPPING_BUTTONS];
    (void)stage;
    s_mid_id = cellpad_mapping_snapshot(&s_m, map, &s_mid_dz, 7);
}
static int test_read_inside_publication(void)
{
    uint16_t map[CELLPAD_MAPPING_BUTTONS];
    uint8_t dz = 0;
    reset(s_identity, 20);
    s_hook = read_mid_publication; s_hook_stage = 10;
    cellpad_mapping_publish(&s_m, s_swapped, 40);
    CHECK(s_hook == NULL);
    CHECK(s_mid_id == 1 && s_mid_dz == 7);
    CHECK(!cellpad_mapping_snapshot(&s_m, map, &dz, 7));
    CHECK(dz == 40 && memcmp(map, s_swapped, sizeof map) == 0);
    return 1;
}

int main(void)
{
    int ok = 1;
    for (unsigned i = 0; i < CELLPAD_MAPPING_BUTTONS; ++i)
        s_identity[i] = s_swapped[i] = (uint16_t)(1u << i);
    s_swapped[0] = (uint16_t)(1u << 3);
    s_swapped[3] = (uint16_t)(1u << 0);
    ok &= test_republish_away_from_identity();
    ok &= test_republish_to_identity();
    ok &= test_deadzone_only_publication();
    ok &= test_read_inside_publication();
    puts(ok ? "test_pad_mapping_seqlock: all passed" : "test_pad_mapping_seqlock: FAILED");
    return ok ? 0 : 1;
}
