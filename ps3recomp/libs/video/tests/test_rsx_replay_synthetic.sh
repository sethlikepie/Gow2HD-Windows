#!/usr/bin/env bash
# End-to-end tests of tools/rsx_replay (macOS/Metal), on captures built by a
# throwaway C writer that uses libs/video/rsx_capture.c:
#
# replay_v1_capture_still_loads (was replay_synthetic_clear): a VERSION 1
#   capture (written before RANGE existed) -- HEADER(v1) / EATABLE /
#   DISPLAY(0, off 0, pitch 5120, 1280x720) / FRAME(zeroed rsx_state) /
#   METHODS (colour surface A at local offset 0, clear colour 0xFF20A040,
#   CLEAR_SURFACE colour bits) / PRESENT(0) / END. Centre pixel = clear colour.
#
# replay_range_mid_frame (V2.0b task 4): a v2 capture whose single frame draws
#   one screen-covering triangle with a fragment program "MOV R0, c[inline]"
#   (ucode in RSX local memory at 0xC0100000, the inline constant right after
#   the instruction) and a 3-vertex float2 array at 0xC0101000. The frame-start
#   PAGE holds the constant = RED; a RANGE record written AFTER SET_BEGIN_END
#   (so after set_shader already read the ucode) and BEFORE DRAW_ARRAYS rewrites
#   the chunk with the constant = BLUE. The Metal backend reads FP inline
#   constants again at every draw (fc_buf), so the replayed centre pixel must be
#   BLUE. Control replay_range_control: the same capture without the RANGE
#   must give RED (proves the pixel depends on the RANGE, not on the setup).
#
# Every case replays twice and cmp's every BMP pair (determinism).
#
# RSX_REPLAY=<path> overrides the binary (default: <repo>/build-macos-wt/rsx_replay).
# Exit 0 = PASS. Never opens a visible window (rsx_replay sets PS3_WINDOW_HIDDEN=1).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RSX_REPLAY="${RSX_REPLAY:-$REPO/build-macos-wt/rsx_replay}"

fail() { echo "FAIL: $*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || { echo "SKIP: rsx_replay is macOS/Metal-only"; exit 0; }
[ -x "$RSX_REPLAY" ] || fail "rsx_replay not built at $RSX_REPLAY (cmake --build <dir> --target rsx_replay)"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/rsx_replay_synth.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# --- 1. capture writer ------------------------------------------------------
cat > "$WORK/writer.c" <<'EOF'
#include "rsx_capture.h"
#include "rsx_commands.h"
#include <stdio.h>
#include <string.h>
/* rsx_capture.c's recorder half references these providers; the writer never
 * records, so stubs suffice. */
uint8_t* vm_base;
const struct rsx_state* gcm_capture_rsx_state(void) { return 0; }
void gcm_capture_io_info(u32* a, u32* s, const u16** t) { *a = 0; *s = 0; *t = 0; }
void gcm_capture_local_info(u32* a, u32* s) { *a = 0; *s = 0; }
void gcm_capture_display_table(void (*e)(u32, u32, u32, u32, u32)) { (void)e; }
/* review finding 1: rc_snapshot_range now checks this before touching
 * vm_base; the writer never records (only calls rsxc_write_* directly), so
 * this is never actually invoked -- it just needs to link. */
int ppu_guest_range_committed(uint32_t a, uint32_t n) { (void)a; (void)n; return 1; }
int rsx_commands_pending_batch(void) { return 0; }
u32 rsx_stats_logical(void) { return 0; }
/* V2.0b task 3: the recorder's backend interposer references the backend
 * registry and the IO map; never exercised by the writer. */
void rsx_set_backend(rsx_backend* b) { (void)b; }
rsx_backend* rsx_get_backend(void) { return 0; }
int gcm_io_offset_to_ea(u32 o, u32* ea) { *ea = o; return 1; }

static void fp_word(u8* p, u32 h)   /* FP words: BE with half-word swap */
{
    u32 be = (h << 16) | (h >> 16);
    p[0] = (u8)(be >> 24); p[1] = (u8)(be >> 16); p[2] = (u8)(be >> 8); p[3] = (u8)be;
}
static void be_f32(u8* p, float f)
{
    u32 v; memcpy(&v, &f, 4);
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}
/* MOV R0, c[inline] (END) + the 16-byte inline constant (r, g, b, a). */
static void fp_mov_r0_const(u8* p, float r, float g, float b, float a)
{
    fp_word(p + 0, (0x01u << 24) | (0xFu << 9) | 1u);                     /* MOV, mask xyzw, END */
    fp_word(p + 4, 2u | (0u << 9) | (1u << 11) | (2u << 13) | (3u << 15)); /* CONST, .xyzw */
    fp_word(p + 8, 0);
    fp_word(p + 12, 0);
    float k[4] = { r, g, b, a };
    for (int i = 0; i < 4; i++) { u32 w; memcpy(&w, &k[i], 4); fp_word(p + 16 + i * 4, w); }
}

static void surface_setup(rsxc_writer* w)
{
    rsxc_write_method(w, 0, NV4097_SET_CONTEXT_DMA_COLOR_A, 0xFEED0000u);
    rsxc_write_method(w, 0, NV4097_SET_SURFACE_FORMAT, 0x148u);   /* A8R8G8B8, Z24S8, pitch */
    rsxc_write_method(w, 0, NV4097_SET_SURFACE_CLIP_HORIZONTAL, 1280u << 16);
    rsxc_write_method(w, 0, NV4097_SET_SURFACE_CLIP_VERTICAL, 720u << 16);
    rsxc_write_method(w, 0, NV4097_SET_SURFACE_PITCH_A, 5120u);
    rsxc_write_method(w, 0, NV4097_SET_SURFACE_COLOR_AOFFSET, 0u);
    rsxc_write_method(w, 0, NV4097_SET_SURFACE_COLOR_TARGET, 1u);
    rsxc_write_method(w, 0, NV4097_SET_COLOR_CLEAR_VALUE, 0xFF20A040u);
    rsxc_write_method(w, 0, NV4097_CLEAR_SURFACE, 0xF0u);
}

#define FP_EA  0xC0100000u   /* local offset 0x100000 */
#define VTX_EA 0xC0101000u   /* local offset 0x101000 */

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    const char* mode = argv[2];
    const int v1 = !strcmp(mode, "clear_v1");
    const int range = !strcmp(mode, "range");
    if (!v1 && !range && strcmp(mode, "control")) return 2;
    rsxc_header h = { RSXC_MAGIC, v1 ? 1u : RSXC_VERSION, (u32)sizeof(rsx_state),
                      0x30000000u, 0x01000000u, 0xC0000000u, 0x0F900000u, RSXC_PAGE };
    rsxc_writer* w = rsxc_open_write(argv[1], &h);
    if (!w) return 1;
    static u16 io_to_ea[65536];
    memset(io_to_ea, 0xFF, sizeof io_to_ea);
    static rsx_state st;                          /* zeroed */
    rsxc_write_eatable(w, io_to_ea);
    rsxc_write_display(w, 0, 0, 5120, 1280, 720);
    rsxc_write_frame(w, 0, 0, &st, (u32)sizeof st);
    if (v1) {
        surface_setup(w);
        rsxc_write_present(w, 0);
        return rsxc_close_write(w, 1) ? 1 : 0;
    }

    /* Frame-start PAGE (64 KiB at FP_EA): FP with the constant = RED, then the
     * vertex array: one triangle covering the whole clip square. */
    static u8 page[RSXC_PAGE];
    fp_mov_r0_const(page, 1.0f, 0.0f, 0.0f, 1.0f);
    const float tri[3][2] = { { -1.0f, -1.0f }, { 3.0f, -1.0f }, { -1.0f, 3.0f } };
    for (int v = 0; v < 3; v++)
        for (int c = 0; c < 2; c++) be_f32(page + (VTX_EA - FP_EA) + v * 8 + c * 4, tri[v][c]);
    rsxc_write_page(w, FP_EA, page);

    surface_setup(w);
    rsxc_write_method(w, 0, NV4097_SET_VIEWPORT_HORIZONTAL, 1280u << 16);
    rsxc_write_method(w, 0, NV4097_SET_VIEWPORT_VERTICAL, 720u << 16);
    rsxc_write_method(w, 0, NV4097_SET_SCISSOR_HORIZONTAL, 1280u << 16);
    rsxc_write_method(w, 0, NV4097_SET_SCISSOR_VERTICAL, 720u << 16);
    rsxc_write_method(w, 0, NV4097_SET_COLOR_MASK, 0x01010101u);
    /* attribute 0: float x2, stride 8, local memory */
    rsxc_write_method(w, 0, NV4097_SET_VERTEX_DATA_ARRAY_FORMAT, 2u | (2u << 4) | (8u << 8));
    rsxc_write_method(w, 0, NV4097_SET_VERTEX_DATA_ARRAY_OFFSET, VTX_EA - 0xC0000000u);
    rsxc_write_method(w, 0, NV4097_SET_SHADER_PROGRAM, (FP_EA - 0xC0000000u) | 1u);  /* local + 1 */
    rsxc_write_method(w, 0, NV4097_SET_BEGIN_END, RSX_PRIMITIVE_TRIANGLES);        /* set_shader reads RED */
    if (range) {
        /* What the recorder emits during the draw callback: the 4 KiB chunk
         * holding the FP, now with the constant = BLUE. */
        static u8 chunk[RSXC_CHUNK];
        memcpy(chunk, page, RSXC_CHUNK);
        fp_mov_r0_const(chunk, 0.0f, 0.0f, 1.0f, 1.0f);
        if (rsxc_write_range(w, FP_EA, chunk, RSXC_CHUNK) != 0) return 1;
    }
    rsxc_write_method(w, 0, NV4097_DRAW_ARRAYS, (2u << 24) | 0u);                  /* 3 vertices */
    rsxc_write_method(w, 0, NV4097_SET_BEGIN_END, 0u);
    rsxc_write_present(w, 0);
    return rsxc_close_write(w, 1) ? 1 : 0;
}
EOF
clang -std=c17 -O0 -I "$REPO/include" -I "$REPO/libs/video" \
    "$WORK/writer.c" "$REPO/libs/video/rsx_capture.c" \
    "$REPO/libs/video/rsx_capture_ranges.c" "$REPO/libs/video/rsx_core_rules.c" \
    "$REPO/libs/video/rsx_core_decode.c" "$REPO/libs/video/rsx_fp_decompiler.c" \
    "$REPO/libs/video/rsx_glsl_dialect.c" -o "$WORK/writer" \
    || fail "capture writer did not compile"

# --- 2. replay helpers -------------------------------------------------------
# replay <case>: writes the capture, replays it twice (a/b), checks determinism.
replay() {
    local c="$1"
    "$WORK/writer" "$WORK/$c.rsxc" "$2" || fail "$c: capture writer failed"
    for run in a b; do
        "$RSX_REPLAY" "$WORK/$c.rsxc" "$WORK/${c}_$run" --census > "$WORK/${c}_$run.log" 2>&1 \
            || { tail -40 "$WORK/${c}_$run.log" >&2; fail "$c: rsx_replay run $run exited non-zero"; }
    done
    [ -f "$WORK/${c}_a_display.bmp" ] || { ls "$WORK" >&2; fail "$c: no ${c}_a_display.bmp"; }
    local n=0 fa fb
    for fa in "$WORK/${c}_a_"*.bmp; do
        fb="$WORK/${c}_b_${fa##*/${c}_a_}"
        [ -f "$fb" ] || fail "$c: run b is missing ${fb##*/}"
        cmp "$fa" "$fb" || fail "$c: ${fa##*/} differs between runs"
        n=$((n + 1))
    done
    [ "$(ls "$WORK/${c}_b_"*.bmp | wc -l)" -eq "$n" ] || fail "$c: run b wrote a different set of BMPs"
    echo "$c determinism: $n BMP pair(s) identical"
    grep '^\[rsx_replay\] frames=' "$WORK/${c}_a.log" || true
}

# centre <bmp> <rr> <gg> <bb>: the display centre pixel is exactly that RGB.
centre() {
    python3 - "$@" <<'PYEOF'
import struct, sys
d = open(sys.argv[1], "rb").read()
want = tuple(int(x, 16) for x in sys.argv[2:5])
assert d[:2] == b"BM", "not a BMP"
off = struct.unpack_from("<I", d, 10)[0]
w, h = struct.unpack_from("<ii", d, 18)
bpp = struct.unpack_from("<H", d, 28)[0]
assert bpp == 32, "bpp=%d" % bpp
stride = (w * 4 + 3) & ~3
x, y = w // 2, abs(h) // 2
b, g, r, a = d[off + y * stride + x * 4: off + y * stride + x * 4 + 4]
print("display %dx%d centre(%d,%d) BGRA=%02X %02X %02X %02X -> ARGB 0x%02X%02X%02X%02X (want RGB %02X%02X%02X)"
      % (w, h, x, y, b, g, r, a, a, r, g, b, *want))
sys.exit(0 if (r, g, b) == want else 1)
PYEOF
}

# --- 3. replay_v1_capture_still_loads ---------------------------------------
replay v1 clear_v1
grep '^frame=' "$WORK/v1_a.log" || fail "v1: no census line"
grep -q '^frame=0 methods=9 draws=0 clears=1 color_targets={0x1} tex_formats={} blend_on=0 stencil_on=0 2d_words=0$' \
    "$WORK/v1_a.log" || fail "v1: census line differs from the synthetic stream"
centre "$WORK/v1_a_display.bmp" 20 A0 40 || fail "v1: centre pixel is not the clear colour"
echo "PASS: replay_v1_capture_still_loads"

# --- 4. replay_range_control (no RANGE: frame-start PAGE value = RED) -------
replay control control
centre "$WORK/control_a_display.bmp" FF 00 00 || fail "control: centre pixel is not the PAGE constant (red)"
echo "PASS: replay_range_control"

# --- 5. replay_range_mid_frame (RANGE before the draw: BLUE) ----------------
replay range range
grep -q '^\[rsx_replay\] frames=1 presents=1 pages=1 ranges=1 (4096 B) ' "$WORK/range_a.log" \
    || fail "range: the replay did not apply exactly one 4 KiB RANGE"
grep -q '^frame=0 methods=20 draws=1 clears=1 color_targets={0x1} tex_formats={} blend_on=0 stencil_on=0 2d_words=0$' "$WORK/range_a.log" || { grep '^frame=' "$WORK/range_a.log" >&2; fail "range: census line differs"; }
centre "$WORK/range_a_display.bmp" 00 00 FF || fail "range: centre pixel is not the RANGE constant (blue)"
echo "PASS: replay_range_mid_frame"
