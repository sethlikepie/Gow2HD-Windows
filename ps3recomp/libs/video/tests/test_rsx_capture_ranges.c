/* test_rsx_capture_ranges: expected values below are hand-computed from the
 * rules cited in rsx_capture_ranges.h/.c (mirrors of the Metal read rules),
 * NOT by calling the helpers under test, so a bug shared between the
 * implementation and a test-side "shortcut" call would still be caught.
 * See .superpowers/sdd/2026-09-23-vulkan-v2-0b-draw-granular-capture/task-2-brief.md.
 */
#include "rsx_capture_ranges.h"
#include "rsx_commands.h"
#include "rsx_texture_formats.h"
#include "rsx_vertex_formats.h"

#include <stdio.h>
#include <string.h>

/* A 1 MiB fake guest-memory arena. Real ea values this file uses stay well
 * inside it (loc==2 / bit31 "main memory" paths go through stub_io2ea,
 * which maps an IO offset to itself -- never the 0xC0000000 local range,
 * which would need a full 4 GiB-addressable vm_base to dereference safely). */
#define ARENA_SIZE (1024u * 1024u)
static u8 g_arena[ARENA_SIZE];

static int stub_io2ea(u32 off, u32* ea)
{
    *ea = off;
    return 1;
}

/* ppu_guest_range_committed: rsxc_fp_span / rsxc_index_span call this.
 * Default (t_reject_len == 0): everything inside the arena is committed. A
 * test can name one rejected [ea, ea+len) window. */
static u32 t_reject_ea = 0xFFFFFFFFu, t_reject_len;
int ppu_guest_range_committed(u32 addr, u32 n)
{
    if (t_reject_len && addr < t_reject_ea + t_reject_len && addr + n > t_reject_ea) return 0;
    return addr + (u64)n <= ARENA_SIZE;
}
static void reject_none(void) { t_reject_len = 0u; }
static void reject_window(u32 ea, u32 len) { t_reject_ea = ea; t_reject_len = len; }

static int fails;
#define CHECK(name, cond) do { \
    if (!(cond)) { printf("FAIL %s\n", name); fails++; } \
    else printf("[PASS] %s\n", name); \
} while (0)
#define CHECK_SPAN(name, sp, want_ea, want_len) do { \
    if ((sp).ea != (u32)(want_ea) || (sp).len != (u32)(want_len)) { \
        printf("FAIL %s: got ea=0x%08X len=%u want ea=0x%08X len=%u\n", \
               name, (sp).ea, (sp).len, (u32)(want_ea), (u32)(want_len)); \
        fails++; \
    } else printf("[PASS] %s\n", name); \
} while (0)

/* ---- rsxc_fp_span -------------------------------------------------------- */

/* rsx_fp_read_word swaps the two 16-bit halves of a big-endian dword read.
 * To store a LOGICAL word W (the value the decoder sees) at bytes b[0..3],
 * solve be = swap16(W) (swap16 is its own inverse), then b = be as
 * big-endian bytes. */
static void store_fp_word(u8* b, u32 logical_w)
{
    u32 be = (logical_w << 16) | (logical_w >> 16);
    b[0] = (u8)(be >> 24); b[1] = (u8)(be >> 16); b[2] = (u8)(be >> 8); b[3] = (u8)be;
}

static void test_fp_span(void)
{
    reject_none();
    memset(g_arena, 0, ARENA_SIZE);

    /* Three 16-byte NV40 FP instructions at ea=0x4000: instr 0 and 1 are
     * all-zero (opcode/word0=0, no END bit, SRC regs type TEMP=0 so no
     * inline CONST slot is consumed); instr 2 has FP_END (bit 0 of word0)
     * set and is otherwise zero too. Hand count: 3 * 16 = 48 bytes, no
     * CONST slot ever added, so rsx_fp_program_size must return exactly 48. */
    const u32 EA = 0x4000u;
    u8* p = g_arena + EA;
    /* instr 0, 1: already zero from memset. */
    store_fp_word(p + 32, 0x00000001u); /* instr 2 word0: FP_END */
    /* words 1..3 of instr 2 stay 0 (TEMP src, no CONST). */

    rsx_state s; memset(&s, 0, sizeof s);
    s.shader_program = 0x2u;      /* loc=2: main memory via io2ea */
    s.fragment_program_addr = EA; /* stub_io2ea is identity */

    rsxc_span sp;
    int n = rsxc_fp_span(&s, g_arena, stub_io2ea, &sp);
    CHECK("fp_span_count", n == 1);
    /* len = max(512, prog_bytes=48) = 512 (the >=512 floor from the brief). */
    CHECK_SPAN("fp_span_synthetic_program", sp, EA, 512u);

    /* shader_program == 0 -> no shader bound, 0 spans. */
    rsx_state s0 = s; s0.shader_program = 0u;
    rsxc_span sp0; memset(&sp0, 0xAA, sizeof sp0);
    int n0 = rsxc_fp_span(&s0, g_arena, stub_io2ea, &sp0);
    CHECK("fp_span_no_shader", n0 == 0 && sp0.ea == 0 && sp0.len == 0);

    /* Program never reaches FP_END inside max_bytes -> rsx_fp_program_size
     * returns 0 -> 0 spans, even though the initial 512 bytes ARE committed. */
    rsx_state sne = s; sne.fragment_program_addr = 0x8000u;
    memset(g_arena + 0x8000u, 0, 16u * 1024u); /* all zero: END bit never set */
    rsxc_span spne; memset(&spne, 0xAA, sizeof spne);
    int nne = rsxc_fp_span(&sne, g_arena, stub_io2ea, &spne);
    CHECK("fp_span_never_ends", nne == 0 && spne.ea == 0 && spne.len == 0);

    /* First 512 bytes at ea not committed -> 0 spans, program bytes never
     * even looked at. */
    reject_window(EA, 512u);
    rsxc_span spu; memset(&spu, 0xAA, sizeof spu);
    int nu = rsxc_fp_span(&s, g_arena, stub_io2ea, &spu);
    CHECK("fp_span_not_committed", nu == 0 && spu.ea == 0 && spu.len == 0);
    reject_none();

    /* A program longer than 512 bytes pins len == prog_bytes (the 512 floor
     * does not kick in): 40 NV40 instructions, all-zero (TEMP src, no CONST
     * slot ever consumed) except the 40th, which has FP_END set.
     * Hand count: 40 * 16 = 640 bytes. */
    const u32 EA_LONG = 0xE000u;
    memset(g_arena + EA_LONG, 0, 40u * 16u);
    store_fp_word(g_arena + EA_LONG + 39u * 16u, 0x00000001u);
    rsx_state slong = s; slong.fragment_program_addr = EA_LONG;
    rsxc_span splong;
    int nlong = rsxc_fp_span(&slong, g_arena, stub_io2ea, &splong);
    CHECK("fp_span_count_long", nlong == 1);
    CHECK_SPAN("fp_span_long_program_640B", splong, EA_LONG, 640u);
}

/* ---- rsxc_tex_span --------------------------------------------------------
 * rsx_texture_state.format bits: [1:0] location (2 = main via io2ea), bits
 * [15:8] base format | LN(0x20) | UN(0x40). LN CLEAR = swizzled. */

static void test_tex_span(void)
{
    rsx_texture_state t; memset(&t, 0, sizeof t);

    /* A8R8G8B8 (0x85) swizzled, 100x60, main memory (loc=2). do_deswizzle:
     * padded = pow2(100)=128, pow2(60)=64 -> 128*64=8192 texels * bpp/8(4)
     * = 32768 bytes ("morton padded 128x64x4"). */
    t.format = (RSX_TEXTURE_A8R8G8B8 << 8) | 0x2u;
    t.offset = 0x1000u;
    t.image_rect = (100u << 16) | 60u;
    rsxc_span sp;
    int n = rsxc_tex_span(&t, stub_io2ea, &sp);
    CHECK("tex_span_count_swizzled", n == 1);
    CHECK_SPAN("tex_span_a8r8g8b8_swizzled_100x60", sp, 0x1000u, 32768u);

    /* Same format+size but LN set -> linear: row_pitch = 100*4 = 400,
     * rows = 60 -> 24000 bytes. */
    memset(&t, 0, sizeof t);
    t.format = ((RSX_TEXTURE_A8R8G8B8 | RSX_TEXTURE_LN) << 8) | 0x2u;
    t.offset = 0x2000u;
    t.image_rect = (100u << 16) | 60u;
    int nl = rsxc_tex_span(&t, stub_io2ea, &sp);
    CHECK("tex_span_count_linear", nl == 1);
    CHECK_SPAN("tex_span_a8r8g8b8_linear_100x60", sp, 0x2000u, 24000u);

    /* DXT1 (0x86), block-compressed: never Morton-deswizzled regardless of
     * LN. 64x64 -> (64/4)*(64/4) = 256 blocks * 8 B/block = 2048 bytes. */
    memset(&t, 0, sizeof t);
    t.format = (RSX_TEXTURE_COMPRESSED_DXT1 << 8) | 0x2u;
    t.offset = 0x3000u;
    t.image_rect = (64u << 16) | 64u;
    int nd = rsxc_tex_span(&t, stub_io2ea, &sp);
    CHECK("tex_span_count_dxt1", nd == 1);
    CHECK_SPAN("tex_span_dxt1_64x64", sp, 0x3000u, 2048u);

    /* Skip: width or height == 0. */
    memset(&t, 0, sizeof t);
    t.format = (RSX_TEXTURE_A8R8G8B8 << 8) | 0x2u;
    t.image_rect = (0u << 16) | 60u;
    rsxc_span spz; memset(&spz, 0xAA, sizeof spz);
    int nz = rsxc_tex_span(&t, stub_io2ea, &spz);
    CHECK("tex_span_skip_zero_dim", nz == 0 && spz.ea == 0 && spz.len == 0);

    /* Skip: a side > 4096. */
    memset(&t, 0, sizeof t);
    t.format = (RSX_TEXTURE_A8R8G8B8 << 8) | 0x2u;
    t.image_rect = (5000u << 16) | 10u;
    rsxc_span spb; memset(&spb, 0xAA, sizeof spb);
    int nb = rsxc_tex_span(&t, stub_io2ea, &spb);
    CHECK("tex_span_skip_oversize", nb == 0 && spb.ea == 0 && spb.len == 0);

    /* Skip: unsupported format (0xAD, not in metal_format_from_rsx). */
    memset(&t, 0, sizeof t);
    t.format = (RSX_TEXTURE_COMPRESSED_B8R8_G8R8 << 8) | 0x2u;
    t.image_rect = (64u << 16) | 64u;
    rsxc_span spf; memset(&spf, 0xAA, sizeof spf);
    int nf = rsxc_tex_span(&t, stub_io2ea, &spf);
    CHECK("tex_span_skip_unsupported_format", nf == 0 && spf.ea == 0 && spf.len == 0);
}

/* ---- rsxc_vertex_spans -----------------------------------------------------
 * Interleaved 2-attribute buffer: attr0 pos (F, size 3) at byte offset 0,
 * attr1 colour (UB, size 4) at byte offset 12, both stride 16 (explicit,
 * matches a packed 16-byte vertex struct). offset bit31 = 1 selects the
 * main-memory path (io2ea), matching "main-memory location via io2ea". */

static void test_vertex_spans(void)
{
    rsx_state s; memset(&s, 0, sizeof s);
    s.vertex_attribs[0].enabled = 1;
    s.vertex_attribs[0].type = RSX_VERTEX_TYPE_F;
    s.vertex_attribs[0].size = 3;
    s.vertex_attribs[0].stride = 16;
    s.vertex_attribs[0].offset = 0x80000000u; /* bit31 set, raw off 0 */
    s.vertex_attribs[1].enabled = 1;
    s.vertex_attribs[1].type = RSX_VERTEX_TYPE_UB;
    s.vertex_attribs[1].size = 4;
    s.vertex_attribs[1].stride = 16;
    s.vertex_attribs[1].offset = 0x8000000Cu; /* bit31 set, raw off 12 */

    const u32 first = 5u, count = 10u;
    rsxc_span out[16];

    /* vertex_base_on = 0: ea0 = io2ea(raw_off) + (first+0)*stride.
     * attr0: io2ea(0)=0 -> ea0 = 0 + 5*16 = 80.
     * attr1: io2ea(12)=12 -> ea0 = 12 + 80 = 92.
     * span = count*stride + 16 = 10*16 + 16 = 176 for both (overlap is fine:
     * the interleaved layout means both windows legitimately cover shared
     * bytes). */
    int n = rsxc_vertex_spans(&s, first, count, /*vertex_base_on=*/0, stub_io2ea, out);
    CHECK("vertex_spans_count_base_off", n == 2);
    CHECK_SPAN("vertex_spans_attr0_base_off", out[0], 80u, 176u);
    CHECK_SPAN("vertex_spans_attr1_base_off", out[1], 92u, 176u);

    /* vertex_base_on = 1, vertex_data_base_offset = 0x40: raw off is first
     * rebased to (0x40 + raw) & 0x0FFFFFFF before io2ea.
     * attr0: rebased off = 0x40 + 0 = 0x40 -> io2ea = 0x40 -> ea0 = 64 + 80 = 144.
     * attr1: rebased off = 0x40 + 12 = 0x4C = 76 -> ea0 = 76 + 80 = 156. */
    s.vertex_data_base_offset = 0x40u;
    int nb = rsxc_vertex_spans(&s, first, count, /*vertex_base_on=*/1, stub_io2ea, out);
    CHECK("vertex_spans_count_base_on", nb == 2);
    CHECK_SPAN("vertex_spans_attr0_base_on", out[0], 144u, 176u);
    CHECK_SPAN("vertex_spans_attr1_base_on", out[1], 156u, 176u);

    /* Local-memory attribute: offset bit31 CLEAR -> 0xC0000000 + off (never
     * io2ea). first=2, count=3, stride=8 (explicit).
     * ea0 = 0xC0000000 + 0x1234 + (2+0)*8 = 0xC0001234 + 16 = 0xC0001244.
     * span = 3*8 + 16 = 40. rsxc_vertex_spans never dereferences vm_base, so
     * this huge address is safe to compute without a real mapping there. */
    rsx_state sl; memset(&sl, 0, sizeof sl);
    sl.vertex_attribs[0].enabled = 1;
    sl.vertex_attribs[0].type = RSX_VERTEX_TYPE_F;
    sl.vertex_attribs[0].size = 3;
    sl.vertex_attribs[0].stride = 8;
    sl.vertex_attribs[0].offset = 0x00001234u; /* bit31 clear: local */
    rsxc_span outl[16];
    int nlocal = rsxc_vertex_spans(&sl, /*first=*/2u, /*count=*/3u, 0, stub_io2ea, outl);
    CHECK("vertex_spans_local_count", nlocal == 1);
    CHECK_SPAN("vertex_spans_local_addr", outl[0], 0xC0001244u, 40u);

    /* stride == 0 -> auto stride = rsx_vertex_attrib_size(type, size):
     * F (4 B/component) * size 2 = 8. first=0, count=4, main memory
     * (io2ea identity, raw off 0) -> ea0 = 0 + 0*8 = 0, span = 4*8+16 = 48. */
    rsx_state sd; memset(&sd, 0, sizeof sd);
    sd.vertex_attribs[0].enabled = 1;
    sd.vertex_attribs[0].type = RSX_VERTEX_TYPE_F;
    sd.vertex_attribs[0].size = 2;
    sd.vertex_attribs[0].stride = 0; /* auto */
    sd.vertex_attribs[0].offset = 0x80000000u; /* bit31 set: main, raw off 0 */
    rsxc_span outd[16];
    int ndef = rsxc_vertex_spans(&sd, /*first=*/0u, /*count=*/4u, 0, stub_io2ea, outd);
    CHECK("vertex_spans_stride_default_count", ndef == 1);
    CHECK_SPAN("vertex_spans_stride_default", outd[0], 0u, 48u);

    /* Disabled / size==0 / type==NONE attributes are all skipped; only the
     * 4th (enabled, UB, size 4, stride 4) survives.
     * ea0 = io2ea(0x30) + (0+0)*4 = 0x30, span = 2*4+16 = 24. */
    rsx_state sk; memset(&sk, 0, sizeof sk);
    sk.vertex_attribs[0].enabled = 0; /* disabled */
    sk.vertex_attribs[0].type = RSX_VERTEX_TYPE_F;
    sk.vertex_attribs[0].size = 3;
    sk.vertex_attribs[0].stride = 12;
    sk.vertex_attribs[0].offset = 0x80000000u;
    sk.vertex_attribs[1].enabled = 1;
    sk.vertex_attribs[1].type = RSX_VERTEX_TYPE_F;
    sk.vertex_attribs[1].size = 0; /* size 0 */
    sk.vertex_attribs[1].stride = 12;
    sk.vertex_attribs[1].offset = 0x80000010u;
    sk.vertex_attribs[2].enabled = 1;
    sk.vertex_attribs[2].type = RSX_VERTEX_TYPE_NONE; /* type NONE */
    sk.vertex_attribs[2].size = 3;
    sk.vertex_attribs[2].stride = 12;
    sk.vertex_attribs[2].offset = 0x80000020u;
    sk.vertex_attribs[3].enabled = 1;
    sk.vertex_attribs[3].type = RSX_VERTEX_TYPE_UB;
    sk.vertex_attribs[3].size = 4;
    sk.vertex_attribs[3].stride = 4;
    sk.vertex_attribs[3].offset = 0x80000030u;
    rsxc_span outk[16];
    int nk = rsxc_vertex_spans(&sk, 0u, 2u, 0, stub_io2ea, outk);
    CHECK("vertex_spans_skip_disabled_size0_none_count", nk == 1);
    CHECK_SPAN("vertex_spans_skip_disabled_size0_none_survivor", outk[0], 0x30u, 24u);

    /* & 0x0FFFFFFF wrap with base offset on: base_offset=0x0FFFFFF0, raw
     * off=0x20 -> (0x0FFFFFF0+0x20) = 0x10000010, & 0x0FFFFFFF = 0x10 (16).
     * ea0 = io2ea(16) + (0+0)*4 = 16, span = 1*4+16 = 20. */
    rsx_state sw; memset(&sw, 0, sizeof sw);
    sw.vertex_attribs[0].enabled = 1;
    sw.vertex_attribs[0].type = RSX_VERTEX_TYPE_F;
    sw.vertex_attribs[0].size = 1;
    sw.vertex_attribs[0].stride = 4;
    sw.vertex_attribs[0].offset = 0x80000020u;
    sw.vertex_data_base_offset = 0x0FFFFFF0u;
    rsxc_span outw[16];
    int nw = rsxc_vertex_spans(&sw, /*first=*/0u, /*count=*/1u, /*vertex_base_on=*/1, stub_io2ea, outw);
    CHECK("vertex_spans_wrap_count", nw == 1);
    CHECK_SPAN("vertex_spans_wrap_base_offset", outw[0], 16u, 20u);
}

/* ---- rsxc_index_span -------------------------------------------------------
 * 16-bit indices, big-endian in guest memory: [10, RESTART(0xFFFF), 3, 7, 2].
 * Valid (non-restart) set = {10, 3, 7, 2} -> min=2, max=10. */

static void store_u16be(u8* p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void store_u32be(u8* p, u32 v)
{
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

static void test_index_span(void)
{
    reject_none();
    const u32 EA = 0x6000u;
    u8* p = g_arena + EA;
    store_u16be(p + 0, 10u);
    store_u16be(p + 2, 0xFFFFu); /* restart */
    store_u16be(p + 4, 3u);
    store_u16be(p + 6, 7u);
    store_u16be(p + 8, 2u);

    rsx_state s; memset(&s, 0, sizeof s);
    s.index_is_16bit = 1;
    s.index_location = 1u;      /* loc != 0: main memory via io2ea */
    s.index_array_addr = EA;    /* stub_io2ea is identity */

    rsxc_span sp; u32 vfirst = 0xDEADBEEFu, nvert = 0xDEADBEEFu;
    int n = rsxc_index_span(&s, /*offset=*/0u, /*count=*/5u, g_arena,
                            /*vertex_base_on=*/0, stub_io2ea, &sp, &vfirst, &nvert);
    CHECK("index_span_count", n == 1);
    CHECK_SPAN("index_span_buffer", sp, EA, 10u); /* 5 * 2 bytes */
    CHECK("index_span_vfirst_base_off", vfirst == 2u);
    CHECK("index_span_nvert", nvert == 9u); /* max(10) - min(2) + 1 */

    /* vertex_base_on = 1, vertex_data_base_index = 0x100:
     * vfirst = (2 + 0x100) & 0xFFFFF = 0x102 = 258. */
    s.vertex_data_base_index = 0x100u;
    u32 vfirst2 = 0xDEADBEEFu, nvert2 = 0xDEADBEEFu;
    rsxc_span sp2;
    int n2 = rsxc_index_span(&s, 0u, 5u, g_arena, /*vertex_base_on=*/1, stub_io2ea,
                             &sp2, &vfirst2, &nvert2);
    CHECK("index_span_count_base_on", n2 == 1);
    CHECK("index_span_vfirst_base_on", vfirst2 == 0x102u);
    CHECK("index_span_nvert_base_on", nvert2 == 9u);

    /* count == 0 -> 0 spans, nvert 0. */
    rsxc_span sp0; u32 vf0 = 0xDEADBEEFu, nv0 = 0xDEADBEEFu;
    memset(&sp0, 0xAA, sizeof sp0);
    int n0 = rsxc_index_span(&s, 0u, 0u, g_arena, 0, stub_io2ea, &sp0, &vf0, &nv0);
    CHECK("index_span_zero_count", n0 == 0 && sp0.ea == 0 && sp0.len == 0 && vf0 == 0 && nv0 == 0);

    /* All-restart buffer: Metal still reads the whole span into ib_mapped
     * and its min_index>max_index reset (~10521) leaves min=max=0, so it
     * still draws ONE vertex (upload_vertices_from_rsx(vfirst, 1)) -- the
     * calculator must return the span (not 0 spans) and nvert == 1. */
    u8* pr = g_arena + 0x7000u;
    store_u16be(pr + 0, 0xFFFFu);
    store_u16be(pr + 2, 0xFFFFu);
    rsx_state sr = s; sr.index_array_addr = 0x7000u; sr.vertex_data_base_index = 0u;
    rsxc_span spr; u32 vfr = 0xDEADBEEFu, nvr = 0xDEADBEEFu;
    int nr = rsxc_index_span(&sr, 0u, 2u, g_arena, 0, stub_io2ea, &spr, &vfr, &nvr);
    CHECK("index_span_all_restart_count", nr == 1);
    CHECK_SPAN("index_span_all_restart_buffer", spr, 0x7000u, 4u); /* 2 * 2 bytes */
    CHECK("index_span_all_restart_vfirst", vfr == 0u);
    CHECK("index_span_all_restart_nvert", nvr == 1u);

    /* Non-zero offset: read starting at index #2 of the original 16-bit
     * buffer at EA=0x6000 ([10, restart, 3, 7, 2]) -> indices {3, 7, 2},
     * none restart. read_addr = EA + 2*2 = EA+4, len = 3*2 = 6.
     * min=2, max=7 -> nvert = 6, vfirst (base_off=0) = 2. */
    rsxc_span spo; u32 vfo = 0xDEADBEEFu, nvo = 0xDEADBEEFu;
    int no = rsxc_index_span(&s, /*offset=*/2u, /*count=*/3u, g_arena,
                             /*vertex_base_on=*/0, stub_io2ea, &spo, &vfo, &nvo);
    CHECK("index_span_nonzero_offset_count", no == 1);
    CHECK_SPAN("index_span_nonzero_offset_buffer", spo, EA + 4u, 6u);
    CHECK("index_span_nonzero_offset_vfirst", vfo == 2u);
    CHECK("index_span_nonzero_offset_nvert", nvo == 6u);

    /* 32-bit indices, big-endian: [100, RESTART(0xFFFFFFFF), 5].
     * Valid set = {100, 5} -> min=5, max=100 -> nvert = 96, vfirst = 5. */
    const u32 EA32 = 0xD000u;
    u8* p32 = g_arena + EA32;
    store_u32be(p32 + 0, 100u);
    store_u32be(p32 + 4, 0xFFFFFFFFu); /* restart */
    store_u32be(p32 + 8, 5u);
    rsx_state s32; memset(&s32, 0, sizeof s32);
    s32.index_is_16bit = 0;
    s32.index_location = 1u; /* main memory via io2ea */
    s32.index_array_addr = EA32;
    rsxc_span sp32; u32 vf32 = 0xDEADBEEFu, nv32 = 0xDEADBEEFu;
    int n32 = rsxc_index_span(&s32, 0u, 3u, g_arena, 0, stub_io2ea, &sp32, &vf32, &nv32);
    CHECK("index_span_32bit_count", n32 == 1);
    CHECK_SPAN("index_span_32bit_buffer", sp32, EA32, 12u); /* 3 * 4 bytes */
    CHECK("index_span_32bit_vfirst", vf32 == 5u);
    CHECK("index_span_32bit_nvert", nv32 == 96u);

    /* Local index buffer: index_location & 0xF == 0 -> ea = 0xC0000000 +
     * index_array_addr, computed in 32-bit arithmetic (no io2ea call).
     * Choosing index_array_addr = 0x40006000 makes that sum wrap the 32-bit
     * space back to 0x00006000 (0xC0000000 + 0x40006000 = 0x100006000, mod
     * 2^32 = 0x6000) -- exactly the already-populated 16-bit buffer at
     * EA=0x6000, so this exercises a REAL committed read through the local
     * branch instead of an unmapped 3+ GiB address. Same data/result as the
     * very first case above (min=2, max=10, nvert=9, vfirst=2). */
    rsx_state sloc = s; sloc.index_location = 0u; sloc.index_array_addr = 0x40006000u;
    sloc.vertex_data_base_index = 0u;
    rsxc_span spl; u32 vfl = 0xDEADBEEFu, nvl = 0xDEADBEEFu;
    int nlocidx = rsxc_index_span(&sloc, 0u, 5u, g_arena, 0, stub_io2ea, &spl, &vfl, &nvl);
    CHECK("index_span_local_count", nlocidx == 1);
    CHECK_SPAN("index_span_local_wrap", spl, 0x6000u, 10u);
    CHECK("index_span_local_vfirst", vfl == 2u);
    CHECK("index_span_local_nvert", nvl == 9u);
}

int main(void)
{
    test_fp_span();
    test_tex_span();
    test_vertex_spans();
    test_index_span();
    if (fails == 0) printf("\nALL TESTS PASSED\n");
    else printf("\n%d TEST(S) FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
