/*
 * Drives the shipped rsx_process_method FIFO assembler with a recording
 * backend. NV4097_DRAW_ARRAYS packs count-1 in 8 bits (max 256). A triangle
 * list split 256+256 (256 % 3 == 1) must become ONE draw of 512, not two
 * 256-vertex lists that join vertices of different particles.
 *
 * Also: remainder 256+95 = 351 (351 % 3 == 0); non-contiguous first must not
 * merge; a new BEGIN must not merge; indexed packets use the same rule;
 * strip/fan/loop/quad-strip continue across 256-vertex words.
 */
#include "rsx_commands.h"
#include "rsx_index.h"
#include "rsx_primitives.h"
#include "trace_link_stubs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    u32 prim;
    u32 first;
    u32 count;
    int indexed;
} rec_draw;

static rec_draw rec[32];
static int rec_n;

static void rec_reset(void) { rec_n = 0; memset(rec, 0, sizeof(rec)); }

static void rb_draw_arrays(void* ud, u32 prim, u32 first, u32 count)
{
    (void)ud;
    if (rec_n < 32) {
        rec[rec_n].prim = prim;
        rec[rec_n].first = first;
        rec[rec_n].count = count;
        rec[rec_n].indexed = 0;
        rec_n++;
    }
    printf("DRAW arrays prim=%u first=%u count=%u\n", prim, first, count);
}

static void rb_draw_indexed(void* ud, u32 prim, u32 first, u32 count)
{
    (void)ud;
    if (rec_n < 32) {
        rec[rec_n].prim = prim;
        rec[rec_n].first = first;
        rec[rec_n].count = count;
        rec[rec_n].indexed = 1;
        rec_n++;
    }
    printf("DRAW indexed prim=%u first=%u count=%u\n", prim, first, count);
}

static u32 pack_batch(u32 first, u32 count)
{
    return ((count - 1u) << 24) | (first & 0xFFFFFFu);
}

static int g_fail;

static void check(int cond, const char* msg)
{
    if (cond) {
        printf("[PASS] %s\n", msg);
    } else {
        printf("[FAIL] %s\n", msg);
        g_fail = 1;
    }
}

static void run_begin_end(rsx_state* st, u32 prim, const u32* words, u32 nwords, int indexed)
{
    rec_reset();
    rsx_process_method(st, NV4097_SET_BEGIN_END, prim);
    for (u32 i = 0; i < nwords; i++) {
        rsx_process_method(st, indexed ? NV4097_DRAW_INDEX_ARRAY : NV4097_DRAW_ARRAYS,
                           words[i]);
    }
    rsx_process_method(st, NV4097_SET_BEGIN_END, 0);
}

int main(void)
{
    setenv("PS3_RSX_COALESCE_ARRAYS", "1", 1);
    setenv("PS3_RSX_COALESCE_INDEX", "1", 1);

    rsx_backend be;
    memset(&be, 0, sizeof(be));
    be.draw_arrays = rb_draw_arrays;
    be.draw_indexed = rb_draw_indexed;
    rsx_set_backend(&be);

    rsx_state st;
    rsx_state_init(&st);

    check((256u % 3u) == 1u, "256 % 3 == 1 (FIFO word is not a whole triangle)");

    /* --- coalesce 256+256 → one draw of 512 --- */
    {
        u32 w[2] = { pack_batch(0, 256), pack_batch(256, 256) };
        run_begin_end(&st, RSX_PRIMITIVE_TRIANGLES, w, 2, 0);
        check(rec_n == 1, "256+256: one draw (not two 256-vertex lists)");
        check(rec_n == 1 && rec[0].indexed == 0 && rec[0].prim == RSX_PRIMITIVE_TRIANGLES &&
              rec[0].first == 0 && rec[0].count == 512u,
              "256+256: prim=triangles first=0 count=512");
    }

    /* --- coalesce 256+95 → 351, 351 % 3 == 0 --- */
    {
        u32 w[2] = { pack_batch(0, 256), pack_batch(256, 95) };
        run_begin_end(&st, RSX_PRIMITIVE_TRIANGLES, w, 2, 0);
        check(((256u + 95u) % 3u) == 0u, "256+95 total % 3 == 0");
        check(rec_n == 1 && rec[0].count == 351u && rec[0].first == 0,
              "256+95: one draw count=351");
    }

    /* --- three packets 256+256+95 --- */
    {
        u32 w[3] = { pack_batch(0, 256), pack_batch(256, 256), pack_batch(512, 95) };
        run_begin_end(&st, RSX_PRIMITIVE_TRIANGLES, w, 3, 0);
        check(rec_n == 1 && rec[0].count == (256u + 256u + 95u) && rec[0].first == 0,
              "256+256+95: one draw of the summed count");
    }

    /* --- non-contiguous first must NOT merge --- */
    {
        u32 w[2] = { pack_batch(0, 256), pack_batch(512, 256) };
        run_begin_end(&st, RSX_PRIMITIVE_TRIANGLES, w, 2, 0);
        check(rec_n == 2, "non-contiguous first: two draws");
        check(rec_n == 2 && rec[0].first == 0 && rec[0].count == 256u &&
              rec[1].first == 512u && rec[1].count == 256u,
              "non-contiguous first: (0,256) then (512,256)");
    }

    /* --- new BEGIN must NOT merge with the previous batch --- */
    {
        rec_reset();
        rsx_process_method(&st, NV4097_SET_BEGIN_END, RSX_PRIMITIVE_TRIANGLES);
        rsx_process_method(&st, NV4097_DRAW_ARRAYS, pack_batch(0, 256));
        rsx_process_method(&st, NV4097_SET_BEGIN_END, 0);
        rsx_process_method(&st, NV4097_SET_BEGIN_END, RSX_PRIMITIVE_TRIANGLES);
        rsx_process_method(&st, NV4097_DRAW_ARRAYS, pack_batch(256, 256));
        rsx_process_method(&st, NV4097_SET_BEGIN_END, 0);
        check(rec_n == 2, "new BEGIN: two draws");
        check(rec_n == 2 && rec[0].count == 256u && rec[1].count == 256u &&
              rec[1].first == 256u,
              "new BEGIN does not join 256+256 across END");
    }

    /* --- continuing topologies: strip / fan / loop / quad-strip --- */
    {
        u32 w[2] = { pack_batch(0, 256), pack_batch(256, 256) };
        run_begin_end(&st, RSX_PRIMITIVE_TRIANGLE_STRIP, w, 2, 0);
        check(rec_n == 1 && rec[0].prim == RSX_PRIMITIVE_TRIANGLE_STRIP &&
              rec[0].count == 512u,
              "triangle strip 256+256: one draw count=512");
        run_begin_end(&st, RSX_PRIMITIVE_TRIANGLE_FAN, w, 2, 0);
        check(rec_n == 1 && rec[0].prim == RSX_PRIMITIVE_TRIANGLE_FAN &&
              rec[0].count == 512u,
              "triangle fan 256+256: one draw count=512");
        run_begin_end(&st, RSX_PRIMITIVE_LINE_LOOP, w, 2, 0);
        check(rec_n == 1 && rec[0].prim == RSX_PRIMITIVE_LINE_LOOP &&
              rec[0].count == 512u,
              "line loop 256+256: one draw count=512");
        run_begin_end(&st, RSX_PRIMITIVE_QUAD_STRIP, w, 2, 0);
        check(rec_n == 1 && rec[0].prim == RSX_PRIMITIVE_QUAD_STRIP &&
              rec[0].count == 512u,
              "quad strip 256+256: one draw count=512");
        run_begin_end(&st, RSX_PRIMITIVE_QUADS, w, 2, 0);
        check(rec_n == 1 && rec[0].prim == RSX_PRIMITIVE_QUADS &&
              rec[0].count == 512u,
              "quads 256+256: one draw count=512");
    }

    /* --- indexed: 0x201+256 continues at 0x301+95 --- */
    {
        u32 w[2] = { pack_batch(0x201u, 256), pack_batch(0x301u, 95) };
        run_begin_end(&st, RSX_PRIMITIVE_TRIANGLES, w, 2, 1);
        check(rec_n == 1 && rec[0].indexed == 1 && rec[0].first == 0x201u &&
              rec[0].count == 351u,
              "indexed 0x201+256 then 0x301+95: one draw count=351");
    }
    {
        u32 w[2] = { pack_batch(0x201u, 256), pack_batch(0x301u, 95) };
        /* mismatched count: 255, not 256, so 0x201+255 != 0x301 */
        w[0] = pack_batch(0x201u, 255);
        run_begin_end(&st, RSX_PRIMITIVE_TRIANGLES, w, 2, 1);
        check(rec_n == 2, "indexed mismatched count does not merge");
    }
    {
        u32 w[2] = { pack_batch(0x201u, 256), pack_batch(0x301u, 95) };
        rec_reset();
        rsx_process_method(&st, NV4097_SET_BEGIN_END, RSX_PRIMITIVE_TRIANGLES);
        rsx_process_method(&st, NV4097_DRAW_INDEX_ARRAY, w[0]);
        rsx_process_method(&st, NV4097_SET_BEGIN_END, 0);
        rsx_process_method(&st, NV4097_SET_BEGIN_END, RSX_PRIMITIVE_TRIANGLES);
        rsx_process_method(&st, NV4097_DRAW_INDEX_ARRAY, w[1]);
        rsx_process_method(&st, NV4097_SET_BEGIN_END, 0);
        check(rec_n == 2, "indexed new BEGIN does not merge");
    }

    /* --- conversion helpers used by Metal/D3D12 for similar topologies --- */
    {
        u32 idx[12];
        u32 n = rsx_convert_quad_strip(0, 6, idx, 8);
        check(n == 0, "quad-strip of 6 verts does not overflow a 8-slot buffer");
        n = rsx_convert_quad_strip(0, 6, idx, 12);
        check(n == 12, "quad-strip of 6 verts -> 12 indices");
        u32 expect_qs[12] = { 0, 1, 3, 0, 3, 2,  2, 3, 5, 2, 5, 4 };
        check(n == 12 && memcmp(idx, expect_qs, sizeof(expect_qs)) == 0,
              "quad-strip triangulation (0,1,3)+(0,3,2) then (2,3,5)+(2,5,4)");
        u32 loop[5];
        n = rsx_convert_line_loop(0, 4, loop, 5);
        check(n == 5 && loop[0] == 0 && loop[3] == 3 && loop[4] == 0,
              "line loop of 4 closes with extra index back to first");
        u32 exp[12];
        check(rsx_expand_prim_indices(RSX_PRIMITIVE_QUADS, 0, 4, exp, 12) == 6,
              "expand QUADS");
        check(rsx_expand_prim_indices(RSX_PRIMITIVE_TRIANGLE_FAN, 0, 5, exp, 12) == 9,
              "expand FAN");
        check(rsx_expand_prim_indices(RSX_PRIMITIVE_TRIANGLES, 0, 6, exp, 12) == 0,
              "expand TRIANGLES is a no-op (host topology exists)");
        u32 src[4] = { 40, 41, 42, 43 };
        u32 pos[12], out[12];
        n = rsx_expand_indexed_prim(RSX_PRIMITIVE_QUADS, src, 4, pos, 12, out, 12);
        u32 expect_iq[6] = { 40, 41, 42, 40, 42, 43 };
        check(n == 6 && memcmp(out, expect_iq, sizeof(expect_iq)) == 0,
              "indexed QUADS remaps through guest indices");
    }

    printf("%s\n", g_fail ? "test_rsx_arrays: FAIL" : "test_rsx_arrays: PASS");
    return g_fail ? 1 : 0;
}
