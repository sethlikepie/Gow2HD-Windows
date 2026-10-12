/*
 * ps3recomp - RSX backend smoke test (phases F3 and F5 of docs/MACOS_PORT_PLAN.md)
 *
 * Drives the RSX command processor with hand-issued NV4097 clear methods and
 * checks that the selected backend turns them into a presented frame of exactly
 * the requested colour. No GPU feature beyond clear + present is exercised; the
 * textured-triangle path lives in rsx_harness_main.c and belongs to the rest of
 * the Metal work.
 *
 * PS3_RSX_BACKEND picks the backend, mirroring the launcher:
 *   sdl    - SDL_Renderer (default; SDL picks Metal or GL underneath)
 *   metal  - the native Metal backend, macOS only
 *   vulkan - the Vulkan backend (MoltenVK on macOS, native elsewhere)
 *
 * The assertion is on pixels, not on "a window appeared": PS3_FRAME_DUMP=1
 * makes the backend write frame_<n>.bmp and the test reads it back.
 *
 * Build: see run_backend_smoke.sh
 */

#include "rsx_commands.h"
#include "rsx_null_backend.h"
#if defined(RSX_SMOKE_HAVE_METAL)
#include "rsx_metal_backend.h"
#endif
#if defined(RSX_SMOKE_HAVE_VULKAN)
#include "rsx_vulkan_backend.h"
#endif
#include "ps3_checkpoint.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* rsx_commands.c resolves guest pointers through this. Clear needs no guest
 * memory, but the symbol has to exist to link. */
uint8_t* vm_base = NULL;

/* Regression fix (bringup-instrumentacao-fase0, Tasks 1-2 vs.
 * run_backend_smoke.sh): rsx_commands.c's rsx_process_method() now calls
 * ps3_checkpoint_fire() (FIRST_DRAW), so this harness links
 * runtime/trace/ps3_trace.c + runtime/trace/ps3_checkpoint.c (see
 * run_backend_smoke.sh). Post-review fix (Task 4 Minor #3): the four stubs
 * ps3_checkpoint.c needs now live in a shared header -- see
 * runtime/trace/tests/trace_link_stubs.h. */
#include "trace_link_stubs.h"

#define WIDTH   320u
#define HEIGHT  240u

/* A colour with three distinct channels, so a swapped R/B or a dropped
 * channel fails loudly instead of looking plausible. */
#define CLEAR_ARGB  0x00337799u
#define EXPECT_R    0x33
#define EXPECT_G    0x77
#define EXPECT_B    0x99

static int failures = 0;

static void check(const char* what, long got, long expected)
{
    if (got == expected) {
        printf("  [OK  ] %-28s = %ld\n", what, got);
    } else {
        printf("  [FAIL] %-28s = %ld (expected %ld)\n", what, got, expected);
        failures++;
    }
}

/* Read the centre pixel of a 32-bit uncompressed BMP written by SDL_SaveBMP. */
static int read_bmp_centre_pixel(const char* path, uint8_t* r, uint8_t* g, uint8_t* b)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("  [FAIL] cannot open %s\n", path);
        return -1;
    }

    uint8_t hdr[54];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f);
        printf("  [FAIL] %s: short header\n", path);
        return -1;
    }

    if (hdr[0] != 'B' || hdr[1] != 'M') {
        fclose(f);
        printf("  [FAIL] %s: not a BMP\n", path);
        return -1;
    }

    uint32_t data_off = (uint32_t)hdr[10] | ((uint32_t)hdr[11] << 8) |
                        ((uint32_t)hdr[12] << 16) | ((uint32_t)hdr[13] << 24);
    int32_t  w    = (int32_t)((uint32_t)hdr[18] | ((uint32_t)hdr[19] << 8) |
                              ((uint32_t)hdr[20] << 16) | ((uint32_t)hdr[21] << 24));
    int32_t  h    = (int32_t)((uint32_t)hdr[22] | ((uint32_t)hdr[23] << 8) |
                              ((uint32_t)hdr[24] << 16) | ((uint32_t)hdr[25] << 24));
    uint16_t bpp  = (uint16_t)((uint32_t)hdr[28] | ((uint32_t)hdr[29] << 8));

    if (bpp != 32 && bpp != 24) {
        fclose(f);
        printf("  [FAIL] %s: unexpected bpp %u\n", path, bpp);
        return -1;
    }

    uint32_t bytes_pp = bpp / 8;
    uint32_t stride   = (((uint32_t)(w < 0 ? -w : w) * bytes_pp) + 3u) & ~3u;
    uint32_t abs_h    = (uint32_t)(h < 0 ? -h : h);
    uint32_t row      = abs_h / 2;          /* vertical centre */
    uint32_t col      = (uint32_t)(w < 0 ? -w : w) / 2;

    /* Positive height means the rows are stored bottom-up. */
    uint32_t file_row = (h > 0) ? (abs_h - 1u - row) : row;
    long offset = (long)data_off + (long)file_row * (long)stride +
                  (long)col * (long)bytes_pp;

    if (fseek(f, offset, SEEK_SET) != 0) {
        fclose(f);
        printf("  [FAIL] %s: seek failed\n", path);
        return -1;
    }

    uint8_t px[4] = {0};
    size_t got = fread(px, 1, bytes_pp, f);
    fclose(f);

    if (got != bytes_pp) {
        printf("  [FAIL] %s: short pixel read\n", path);
        return -1;
    }

    /* BMP stores BGR(A). */
    *b = px[0];
    *g = px[1];
    *r = px[2];
    return 0;
}

int main(void)
{
    const char* want = getenv("PS3_RSX_BACKEND");
    if (!want || !*want) {
        want = "sdl";
    }

    int use_metal  = (strcmp(want, "metal")  == 0);
    int use_vulkan = (strcmp(want, "vulkan") == 0);
#if !defined(RSX_SMOKE_HAVE_METAL)
    if (use_metal) {
        printf("[%s smoke] SKIP: built without the Metal backend\n", want);
        return 0;
    }
#endif
#if !defined(RSX_SMOKE_HAVE_VULKAN)
    if (use_vulkan) {
        printf("[%s smoke] SKIP: built without the Vulkan backend\n", want);
        return 0;
    }
#endif

    printf("[%s smoke] RSX command processor -> %s backend\n", want, want);

    /* Force the dump on: it is what makes this test assertable without a
     * human looking at a window. */
    setenv("PS3_FRAME_DUMP", "1", 1);

    char title[64];
    snprintf(title, sizeof(title), "ps3recomp %s smoke", want);

    int init_rc;
#if defined(RSX_SMOKE_HAVE_METAL)
    if (use_metal) {
        init_rc = rsx_metal_backend_init(WIDTH, HEIGHT, title);
    } else
#endif
#if defined(RSX_SMOKE_HAVE_VULKAN)
    if (use_vulkan) {
        init_rc = rsx_vulkan_backend_init(WIDTH, HEIGHT, title);
    } else
#endif
    {
        init_rc = rsx_null_backend_init(WIDTH, HEIGHT, title);
    }

    if (init_rc != 0) {
        printf("[%s smoke] FAIL: backend init failed\n", want);
        return 1;
    }

    rsx_backend* be = rsx_get_backend();
    if (!be) {
        printf("[%s smoke] FAIL: backend not registered\n", want);
        return 1;
    }

    rsx_state state;
    rsx_state_init(&state);

    /* Issue the clear through the real method path, not by poking the
     * backend, so a regression in rsx_process_method also trips this test. */
    rsx_process_method(&state, NV4097_SET_COLOR_CLEAR_VALUE, CLEAR_ARGB);
    rsx_process_method(&state, NV4097_CLEAR_SURFACE, 0xF3);

    if (be->begin_frame) be->begin_frame(be->userdata);
    if (be->present)     be->present(be->userdata, 0);
    if (be->end_frame)   be->end_frame(be->userdata);

    /* One pump so the window reaches a steady state and events drain. */
    int pump;
#if defined(RSX_SMOKE_HAVE_METAL)
    if (use_metal) {
        pump = rsx_metal_backend_pump_messages();
    } else
#endif
#if defined(RSX_SMOKE_HAVE_VULKAN)
    if (use_vulkan) {
        pump = rsx_vulkan_backend_pump_messages();
    } else
#endif
    {
        pump = rsx_null_backend_pump_messages();
    }
    check("pump_messages", pump, 0);

    uint8_t r = 0, g = 0, b = 0;
    /* Dumps are numbered by presents already completed, so the first
     * presented frame lands in frame_0.bmp. */
    if (read_bmp_centre_pixel("frame_0.bmp", &r, &g, &b) == 0) {
        check("presented red",   r, EXPECT_R);
        check("presented green", g, EXPECT_G);
        check("presented blue",  b, EXPECT_B);
    } else {
        failures++;
    }

#if defined(RSX_SMOKE_HAVE_METAL)
    if (use_metal) {
        rsx_metal_backend_shutdown();
    } else
#endif
#if defined(RSX_SMOKE_HAVE_VULKAN)
    if (use_vulkan) {
        rsx_vulkan_backend_shutdown();
    } else
#endif
    {
        rsx_null_backend_shutdown();
    }
    remove("frame_0.bmp");

    if (failures == 0) {
        printf("[%s smoke] PASS: window opened, RSX clear colour presented, clean shutdown\n",
               want);
        return 0;
    }
    printf("[%s smoke] FAIL: %d check(s) failed\n", want, failures);
    return 1;
}
