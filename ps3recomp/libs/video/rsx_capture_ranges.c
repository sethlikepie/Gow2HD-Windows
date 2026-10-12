/* rsx_capture_ranges: see rsx_capture_ranges.h. Each function's body is a
 * portable re-statement of the equivalent read rule in rsx_metal_backend.m
 * -- cited at the top of each function -- reusing the same shared helpers
 * the Metal backend uses (rsx_core_rules.c, rsx_fp_decompiler.c,
 * rsx_texture_formats.h, rsx_texture_deswizzle.h, rsx_vertex_formats.h,
 * rsx_index.h) so a behaviour change to those helpers stays in sync with
 * both backends automatically. No Metal-specific code lives here. */
#include "rsx_capture_ranges.h"

#include "rsx_core_rules.h"
#include "rsx_fp_decompiler.h"
#include "rsx_texture_formats.h"
#include "rsx_texture_deswizzle.h"
#include "rsx_vertex_formats.h"
#include "rsx_index.h"

#include <string.h>

/* Same declaration convention as libs/video/rsx_capture.c / cellGcmSys.c:
 * the runtime provides this (runtime/ppu/ppu_memory.h); linking it in full
 * is not needed here. */
extern int ppu_guest_range_committed(u32 addr, u32 n);

static void rsxc_span_zero(rsxc_span* out)
{
    if (out) { out->ea = 0u; out->len = 0u; }
}

/* metal_set_shader, rsx_metal_backend.m ~9807-9870:
 *   if (!state->shader_program) -> no shader, skip
 *   ea = metal_resolve_fp_ea(state)              (== rsx_core_resolve_fp_ea)
 *   if (!ppu_guest_range_committed(ea, 512)) -> skip
 *   prog_bytes = rsx_fp_program_size(vm_base + ea, 16 KiB)
 *   if (!prog_bytes) -> skip
 * The FP range must cover at least the 512-byte committed check AND the
 * decoded program (inline FP constants patched per draw, re-read at every
 * draw by metal_set_shader's sibling capture_fc path -- Task 3 decides when
 * to call this, not this file). */
int rsxc_fp_span(const rsx_state* s, const u8* vm_base, rsxc_io2ea_fn io2ea, rsxc_span* out)
{
    rsxc_span_zero(out);
    if (!s || !vm_base || !out) return 0;
    if (!s->shader_program) return 0;

    int unresolved = 0;
    u32 ea = rsx_core_resolve_fp_ea(s, (rsx_core_io2ea_fn)io2ea, &unresolved);

    if (!ppu_guest_range_committed(ea, 512u)) return 0;

    const u32 MAX_FP_BYTES = 16u * 1024u;
    u32 prog_bytes = rsx_fp_program_size(vm_base + ea, MAX_FP_BYTES);
    if (!prog_bytes) return 0;

    out->ea = ea;
    out->len = prog_bytes > 512u ? prog_bytes : 512u;
    return 1;
}

/* metal_format_from_rsx, rsx_metal_backend.m ~1432-1471: the exact set of
 * RSX texture formats the Metal backend can sample. Anything outside this
 * switch is "unsupported, keep dummy" in metal_bind_texture -- no guest
 * memory is read for it. */
static int rsxc_tex_format_supported(u32 rsx_fmt, int* out_is_bc)
{
    if (out_is_bc) *out_is_bc = 0;
    switch (rsx_fmt) {
    case RSX_TEXTURE_A8R8G8B8:
    case RSX_TEXTURE_D8R8G8B8:
    case RSX_TEXTURE_B8:
    case RSX_TEXTURE_G8B8:
    case RSX_TEXTURE_R5G6B5:
    case RSX_TEXTURE_A1R5G5B5:
    case RSX_TEXTURE_R5G5B5A1:
    case RSX_TEXTURE_A4R4G4B4:
        return 1;
    case RSX_TEXTURE_COMPRESSED_DXT1:
    case RSX_TEXTURE_COMPRESSED_DXT23:
    case RSX_TEXTURE_COMPRESSED_DXT45:
        if (out_is_bc) *out_is_bc = 1;
        return 1;
    default:
        return 0;
    }
}

/* metal_bind_texture, rsx_metal_backend.m ~2044-2189:
 *   width/height from image_rect; skip when either is 0
 *   skip when the format is not one metal_format_from_rsx supports
 *   skip when either side > 4096 (E432: padded Morton area wraps in u32)
 *   guest_bytes = swizzled && !BC ? rsx_morton_padded_texels(w,h)*(bpp/8)
 *                                  : rsx_texture_row_pitch(fmt,w) * rsx_texture_src_rows(fmt,h)
 *   skip when guest_bytes == 0 or > 256 MiB (0x10000000)
 * Mip 0 / 2D only, matching the backend (it never reads other mips/faces).
 * Render-to-texture (a unit pointed at a Metal-only surface) short-circuits
 * the real backend without touching guest memory at all -- this calculator
 * has no notion of host surfaces, so it still returns the guest span for
 * that case; capturing more real bytes than a live run reads is safe (see
 * global-constraints.md), it is only under-capturing that is not. */
int rsxc_tex_span(const rsx_texture_state* t, rsxc_io2ea_fn io2ea, rsxc_span* out)
{
    rsxc_span_zero(out);
    if (!t || !out) return 0;

    u32 width  = (t->image_rect >> 16) & 0xFFFFu;
    u32 height = t->image_rect & 0xFFFFu;
    if (width == 0u || height == 0u) return 0;

    u32 format = rsx_texture_get_format(t->format);
    int is_bc = 0;
    if (!rsxc_tex_format_supported(format, &is_bc)) return 0;

    if (width > 4096u || height > 4096u) return 0;

    int is_swizzled = rsx_texture_is_swizzled(t->format);
    int do_deswizzle = is_swizzled && !is_bc;
    u32 bpp = rsx_texture_bpp(format);
    unsigned long long guest_bytes64 = do_deswizzle
        ? (unsigned long long)rsx_morton_padded_texels(width, height) * (bpp / 8u)
        : (unsigned long long)rsx_texture_row_pitch(format, width) * rsx_texture_src_rows(format, height);
    if (guest_bytes64 == 0ull || guest_bytes64 > 0x10000000ull) return 0;

    u32 ea = rsx_core_resolve_tex_ea(t, (rsx_core_io2ea_fn)io2ea);
    out->ea = ea;
    out->len = (u32)guest_bytes64;
    return 1;
}

/* metal_vtx_ea, rsx_metal_backend.m ~3113-3130 (verbatim, io2ea/vertex_data
 * passed in instead of the Metal globals gcm_io_offset_to_ea/s_draw.current_rsx). */
static u32 rsxc_vtx_ea(u32 offset_word, u32 first, u32 i, u32 stride,
                       int vertex_base_on, const rsx_state* s, rsxc_io2ea_fn io2ea)
{
    u32 loc = (offset_word >> 31) & 1u;
    u32 off = offset_word & 0x7FFFFFFFu;
    if (vertex_base_on && s) {
        /* RPCS3 get_vertex_offset_from_base: (base + offset) & 0x0FFFFFFF. */
        off = (s->vertex_data_base_offset + off) & 0x0FFFFFFFu;
    }
    u32 base;
    if (loc == 0u) {
        base = 0xC0000000u + off;
    } else {
        u32 ea = 0u;
        if (io2ea && io2ea(off, &ea))
            base = ea;
        else
            base = off; /* last-resort: treat as absolute guest EA */
    }
    return base + (first + i) * stride;
}

/* The per-attribute fetch loop, rsx_metal_backend.m ~3376 (enabled gate) and
 * ~3484-3488 (ea0 / span / committed check):
 *   slot[k] set when a16[k].enabled && type != RSX_VERTEX_TYPE_NONE && size != 0
 *   stride = a->stride ? a->stride : rsx_vertex_attrib_size(a->type, a->size)
 *   if (a->enabled && stride && vm_base) {
 *       ea0 = metal_vtx_ea(a->offset, first, 0, stride)
 *       span = count*stride + 16
 *       if (span <= 0xFFFFFFFF && committed(ea0, span)) src0 = vm_base + ea0
 *   }
 * A zero stride (an out-of-range type field) or a span that would not fit
 * in 32 bits means the backend's own condition above is false and it never
 * reads guest memory for that attribute -- this calculator skips it too. */
int rsxc_vertex_spans(const rsx_state* s, u32 first, u32 count, int vertex_base_on,
                      rsxc_io2ea_fn io2ea, rsxc_span out[16])
{
    if (!out) return 0;
    for (int z = 0; z < 16; z++) rsxc_span_zero(&out[z]);
    if (!s) return 0;

    int n = 0;
    for (int k = 0; k < RSX_MAX_VERTEX_ATTRIBS && k < 16; k++) {
        const rsx_vertex_attrib* a = &s->vertex_attribs[k];
        if (!(a->enabled && a->type != RSX_VERTEX_TYPE_NONE && a->size != 0u)) continue;

        u32 stride = a->stride ? a->stride : rsx_vertex_attrib_size(a->type, a->size);
        if (!stride) continue;

        u64 span64 = (u64)count * (u64)stride + 16ull;
        if (span64 > 0xFFFFFFFFull) continue;

        u32 ea0 = rsxc_vtx_ea(a->offset, first, 0u, stride, vertex_base_on, s, io2ea);
        out[n].ea = ea0;
        out[n].len = (u32)span64;
        n++;
    }
    return n;
}

/* metal_draw_indexed, rsx_metal_backend.m ~10466-10481 (EA + committed check)
 * and ~10508-10568 (min/max decode, restart skipped, vfirst/nvert):
 *   if (count == 0) -> skip                         (metal_ib_region_idx guard, ~10457)
 *   elem = index_is_16bit ? 2 : 4
 *   loc = index_location & 0xF
 *   ea = loc == 0 ? 0xC0000000 + index_array_addr : io2ea(index_array_addr) fallback raw
 *   read_addr = ea + offset*elem
 *   if (!committed(read_addr, count*elem)) -> skip
 *   for each index: decode BE via rsx_index_from_guest; restart sentinels
 *     (rsx_index_is_restart) are not a vertex and are skipped from min/max
 *   min_index/max_index start at 0xFFFFFFFF/0 -- if every index was a
 *     restart, min stays > max; the backend's own min_index>max_index reset
 *     to 0 (~10521) leaves min = max = 0 and STILL calls
 *     upload_vertices_from_rsx(vfirst, 1) (~10566-10568) -- it draws one
 *     vertex, it does not skip the draw. This calculator mirrors that: once
 *     the index span itself is committed, it is always returned (the backend
 *     read those bytes into ib_mapped regardless of restart content); only
 *     min/max/vfirst/nvert fall back to the 0/0/…/1 values Metal computes
 *     for the all-restart case.
 *   vfirst = vertex_base_on ? (min + s->vertex_data_base_index) & 0xFFFFF : min
 *   nvert = max - min + 1 */
int rsxc_index_span(const rsx_state* s, u32 offset, u32 count, const u8* vm_base, int vertex_base_on,
                    rsxc_io2ea_fn io2ea, rsxc_span* out, u32* vfirst, u32* nvert)
{
    rsxc_span_zero(out);
    if (vfirst) *vfirst = 0u;
    if (nvert) *nvert = 0u;
    if (!s || !vm_base || !out) return 0;
    if (count == 0u) return 0;

    u32 elem_size = s->index_is_16bit ? 2u : 4u;
    u32 loc = s->index_location & 0xFu;
    u32 ea;
    if (loc == 0u) {
        ea = 0xC0000000u + s->index_array_addr;
    } else {
        u32 main_ea = 0u;
        if (io2ea && io2ea(s->index_array_addr, &main_ea))
            ea = main_ea;
        else
            ea = s->index_array_addr;
    }
    u32 read_addr = ea + offset * elem_size;
    u32 span_len = count * elem_size;

    if (!ppu_guest_range_committed(read_addr, span_len)) return 0;

    u32 max_index = 0u, min_index = 0xFFFFFFFFu;
    const u8* isrc = vm_base + read_addr;
    for (u32 i = 0; i < count; i++) {
        u32 idx = rsx_index_from_guest(isrc + (u64)i * elem_size, elem_size == 2u);
        if (rsx_index_is_restart(idx)) continue;
        if (idx > max_index) max_index = idx;
        if (idx < min_index) min_index = idx;
    }
    /* All restart: Metal's min_index>max_index reset (~10521) leaves both at
     * 0 and still draws one vertex -- match it exactly rather than reporting
     * no span (that would under-capture bytes the backend did read). */
    if (min_index > max_index) min_index = 0u;

    out->ea = read_addr;
    out->len = span_len;
    u32 vf = vertex_base_on ? ((min_index + s->vertex_data_base_index) & 0x000FFFFFu) : min_index;
    u32 nv = max_index - min_index + 1u;
    if (vfirst) *vfirst = vf;
    if (nvert) *nvert = nv;
    return 1;
}
