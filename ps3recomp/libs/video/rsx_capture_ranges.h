/* rsx_capture_ranges: portable calculators of the guest-memory ranges each
 * RSX backend callback actually reads (fragment program ucode, bound
 * texture, vertex attribute arrays, index buffer). The recorder
 * (rsx_capture.c, Task 3) calls these right before forwarding set_shader /
 * bind_texture / draw_arrays / draw_indexed to the backend and emits the
 * bytes as RANGE records instead of whole-page snapshots.
 *
 * Every rule here mirrors the equivalent read in the production Metal
 * backend (rsx_metal_backend.m) exactly -- same EA resolution, same skip
 * conditions, same size formula -- so the ranges returned are always a
 * SUPERSET of what the backend reads (never less: under-capturing would
 * make replay diverge). See
 * .superpowers/sdd/2026-09-23-vulkan-v2-0b-draw-granular-capture/task-2-brief.md.
 *
 * Portable C17: no ObjC, no platform-specific calls (globbed into the
 * runtime on every platform, same as rsx_capture.c / cellGcmSys.c).
 */
#ifndef PS3RECOMP_RSX_CAPTURE_RANGES_H
#define PS3RECOMP_RSX_CAPTURE_RANGES_H

#include "ps3emu/ps3types.h"
#include "rsx_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { u32 ea, len; } rsxc_span;

/* Same convention as rsx_core_io2ea_fn (rsx_core_rules.h): translate an RSX
 * IO offset (main-memory DMA context) to a guest EA. Returns non-zero and
 * writes *ea on success. */
typedef int (*rsxc_io2ea_fn)(u32 io_off, u32* ea);

/* Fragment program ucode bound by SET_SHADER_PROGRAM (metal_set_shader,
 * rsx_metal_backend.m ~9807-9870). EA via rsx_core_resolve_fp_ea; len =
 * max(512, rsx_fp_program_size(vm_base+ea, 16 KiB)), scanned on the SAME
 * bytes the backend scans. Writes *out and returns 1 on success; returns 0
 * (out zeroed) when shader_program == 0, the first 512 bytes at ea are not
 * committed, or the program never hits its END bit within 16 KiB. */
int rsxc_fp_span(const rsx_state* s, const u8* vm_base, rsxc_io2ea_fn io2ea, rsxc_span* out);

/* Bound texture unit (metal_bind_texture, rsx_metal_backend.m ~2044-2189).
 * EA via rsx_core_resolve_tex_ea; len = swizzled && !BC ?
 * rsx_morton_padded_texels(w,h) * (bpp/8) : rsx_texture_row_pitch(fmt,w) *
 * rsx_texture_src_rows(fmt,h). Mip 0 / 2D only (the backend ignores
 * mips/cube/3D). Returns 0 (out zeroed) for w/h == 0, either side > 4096, an
 * unsupported format, or a computed size of 0 or > 256 MiB -- the same skip
 * rules metal_bind_texture applies before it touches guest memory. Does not
 * itself check ppu_guest_range_committed (it never dereferences vm_base);
 * the recorder does that per emitted chunk. */
int rsxc_tex_span(const rsx_texture_state* t, rsxc_io2ea_fn io2ea, rsxc_span* out);

/* Vertex attribute arrays for draw_arrays/draw_indexed(first, count)
 * (metal_vtx_ea + the per-attribute fetch loop, rsx_metal_backend.m
 * ~3113-3130 / ~3484-3488). An attribute is included when enabled &&
 * type != RSX_VERTEX_TYPE_NONE && size != 0 && its stride is non-zero
 * (stride = a->stride ? a->stride : rsx_vertex_attrib_size(type, size); a
 * zero stride means the backend never reads guest memory for it either).
 * EA0 per metal_vtx_ea: loc = bit 31 of a->offset (0 local -> 0xC0000000 +
 * off, 1 main -> io2ea(off) with a fallback to the raw off as EA); when
 * vertex_base_on, off is first rebased to
 * (s->vertex_data_base_offset + off) & 0x0FFFFFFF. Span =
 * [ea0, ea0 + count*stride + 16). Writes up to 16 spans into out[] (in
 * vertex_attribs[] order) and returns the count written; an attribute whose
 * span would not fit in 32 bits is skipped (the backend does not read it
 * either in that case). */
int rsxc_vertex_spans(const rsx_state* s, u32 first, u32 count, int vertex_base_on,
                      rsxc_io2ea_fn io2ea, rsxc_span out[16]);

/* Index buffer for draw_indexed(offset, count) (metal_draw_indexed,
 * rsx_metal_backend.m ~10466-10481 / ~10566-10568). EA: elem = 2 or 4 bytes
 * per state->index_is_16bit; loc = index_location & 0xF (0 local ->
 * 0xC0000000 + index_array_addr, else io2ea(index_array_addr) with a
 * fallback to the raw address). *out = [ea + offset*elem, + count*elem).
 * Also decodes min/max over that span from vm_base (restart indices
 * skipped, rsx_index_from_guest / rsx_index_is_restart) to produce
 * *vfirst / *nvert exactly like the backend: vfirst = vertex_base_on ?
 * (min + s->vertex_data_base_index) & 0xFFFFF : min; *nvert = max - min + 1.
 * When every decoded index is a restart sentinel, min/max fall back to 0/0
 * (Metal's own min_index>max_index reset, ~10521) and *nvert comes out 1 --
 * the backend still reads the whole index span into ib_mapped and still
 * draws one vertex in that case, it does not skip the draw. Returns 0 (out,
 * *vfirst and *nvert all zeroed, no read of vm_base beyond the committed
 * check) only when count == 0 or the index span itself is not committed --
 * the only two cases where the backend reads nothing at all. */
int rsxc_index_span(const rsx_state* s, u32 offset, u32 count, const u8* vm_base, int vertex_base_on,
                    rsxc_io2ea_fn io2ea, rsxc_span* out, u32* vfirst, u32* nvert);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_RSX_CAPTURE_RANGES_H */
