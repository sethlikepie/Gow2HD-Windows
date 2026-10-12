/*
 * ps3recomp - RSX Primitive Type Mapping
 *
 * Maps RSX GPU primitive types to host API topologies.
 * Some RSX types (triangle fans, quads, line loops) don't have direct
 * D3D12/Vulkan equivalents and need index buffer conversion.
 */

#ifndef PS3RECOMP_RSX_PRIMITIVES_H
#define PS3RECOMP_RSX_PRIMITIVES_H

#include "rsx_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/* D3D12 topology values (from d3dcommon.h) */
#define D3D_TOPOLOGY_UNDEFINED          0
#define D3D_TOPOLOGY_POINTLIST          1
#define D3D_TOPOLOGY_LINELIST           2
#define D3D_TOPOLOGY_LINESTRIP          3
#define D3D_TOPOLOGY_TRIANGLELIST       4
#define D3D_TOPOLOGY_TRIANGLESTRIP      5

/* Map RSX primitive type to D3D12 topology.
 * Returns 0 (undefined) for types that need index conversion. */
static inline u32 rsx_to_d3d12_topology(u32 rsx_prim)
{
    switch (rsx_prim) {
    case RSX_PRIMITIVE_POINTS:         return D3D_TOPOLOGY_POINTLIST;
    case RSX_PRIMITIVE_LINES:          return D3D_TOPOLOGY_LINELIST;
    case RSX_PRIMITIVE_LINE_STRIP:     return D3D_TOPOLOGY_LINESTRIP;
    case RSX_PRIMITIVE_TRIANGLES:      return D3D_TOPOLOGY_TRIANGLELIST;
    case RSX_PRIMITIVE_TRIANGLE_STRIP: return D3D_TOPOLOGY_TRIANGLESTRIP;
    /* These need index buffer conversion: */
    case RSX_PRIMITIVE_LINE_LOOP:      return D3D_TOPOLOGY_LINESTRIP;  /* + extra edge */
    case RSX_PRIMITIVE_TRIANGLE_FAN:   return D3D_TOPOLOGY_TRIANGLELIST; /* expand */
    case RSX_PRIMITIVE_QUADS:          return D3D_TOPOLOGY_TRIANGLELIST; /* 2 tris/quad */
    case RSX_PRIMITIVE_QUAD_STRIP:     return D3D_TOPOLOGY_TRIANGLELIST; /* expand */
    default:                           return D3D_TOPOLOGY_UNDEFINED;
    }
}

/* Check if a primitive type needs index conversion.
 * Returns 1 if indices need to be generated. */
static inline int rsx_prim_needs_conversion(u32 rsx_prim)
{
    return (rsx_prim == RSX_PRIMITIVE_LINE_LOOP ||
            rsx_prim == RSX_PRIMITIVE_TRIANGLE_FAN ||
            rsx_prim == RSX_PRIMITIVE_QUADS ||
            rsx_prim == RSX_PRIMITIVE_QUAD_STRIP);
}

/* Convert a triangle fan (N vertices) to triangle list indices.
 * Writes (N-2)*3 indices to out_indices.
 * Returns the number of indices written. */
static inline u32 rsx_convert_triangle_fan(u32 first, u32 count,
                                            u32* out_indices, u32 max_indices)
{
    if (count < 3) return 0;
    u32 num_tris = count - 2;
    u32 num_indices = num_tris * 3;
    if (num_indices > max_indices) return 0;

    for (u32 i = 0; i < num_tris; i++) {
        out_indices[i * 3]     = first;         /* center vertex */
        out_indices[i * 3 + 1] = first + i + 1;
        out_indices[i * 3 + 2] = first + i + 2;
    }
    return num_indices;
}

/* Convert quads (N vertices, N/4 quads) to triangle list indices.
 * Writes (N/4)*6 indices.
 * Returns the number of indices written. */
static inline u32 rsx_convert_quads(u32 first, u32 count,
                                     u32* out_indices, u32 max_indices)
{
    u32 num_quads = count / 4;
    u32 num_indices = num_quads * 6;
    if (num_indices > max_indices) return 0;

    for (u32 i = 0; i < num_quads; i++) {
        u32 base = first + i * 4;
        out_indices[i * 6]     = base;
        out_indices[i * 6 + 1] = base + 1;
        out_indices[i * 6 + 2] = base + 2;
        out_indices[i * 6 + 3] = base;
        out_indices[i * 6 + 4] = base + 2;
        out_indices[i * 6 + 5] = base + 3;
    }
    return num_indices;
}

/* Convert line loop (N vertices) to line strip + closing edge.
 * Returns the original count (line strip renders N-1 segments,
 * caller needs to add one extra segment from last to first). */
static inline u32 rsx_convert_line_loop(u32 first, u32 count,
                                         u32* out_indices, u32 max_indices)
{
    if (count < 2 || count + 1 > max_indices) return 0;
    for (u32 i = 0; i < count; i++)
        out_indices[i] = first + i;
    out_indices[count] = first; /* close the loop */
    return count + 1;
}

/* Convert a quad strip (N vertices) to triangle list indices.
 * Quad i uses vertices (2i, 2i+1, 2i+3, 2i+2). Writes ((N-2)/2)*6 indices. */
static inline u32 rsx_convert_quad_strip(u32 first, u32 count,
                                         u32* out_indices, u32 max_indices)
{
    if (count < 4) return 0;
    u32 num_quads = (count - 2) / 2;
    u32 num_indices = num_quads * 6;
    if (num_indices > max_indices) return 0;
    for (u32 i = 0; i < num_quads; i++) {
        u32 n = first + i * 2;
        out_indices[i * 6]     = n;
        out_indices[i * 6 + 1] = n + 1;
        out_indices[i * 6 + 2] = n + 3;
        out_indices[i * 6 + 3] = n;
        out_indices[i * 6 + 4] = n + 3;
        out_indices[i * 6 + 5] = n + 2;
    }
    return num_indices;
}

/* Expand a primitive that has no 1:1 host topology into triangle-list or
 * closed line-strip indices. first is the local vertex origin of the uploaded
 * slice (0 after a [first, first+count) upload). Returns 0 if prim does not
 * need conversion or the output would overflow. */
static inline u32 rsx_expand_prim_indices(u32 rsx_prim, u32 first, u32 count,
                                          u32* out_indices, u32 max_indices)
{
    switch (rsx_prim) {
    case RSX_PRIMITIVE_TRIANGLE_FAN:
        return rsx_convert_triangle_fan(first, count, out_indices, max_indices);
    case RSX_PRIMITIVE_QUADS:
        return rsx_convert_quads(first, count, out_indices, max_indices);
    case RSX_PRIMITIVE_QUAD_STRIP:
        return rsx_convert_quad_strip(first, count, out_indices, max_indices);
    case RSX_PRIMITIVE_LINE_LOOP:
        return rsx_convert_line_loop(first, count, out_indices, max_indices);
    default:
        return 0;
    }
}

/* Indexed sibling: triangulate/close SEQUENCE POSITIONS, then remap through
 * the guest index values (position j names src[j], not vertex j). */
static inline u32 rsx_expand_indexed_prim(u32 rsx_prim, const u32* src, u32 count,
                                          u32* pos_scratch, u32 pos_max,
                                          u32* out_indices, u32 max_indices)
{
    u32 n = rsx_expand_prim_indices(rsx_prim, 0, count, pos_scratch, pos_max);
    if (!n || n > max_indices) return 0;
    for (u32 j = 0; j < n; j++)
        out_indices[j] = src[pos_scratch[j]];
    return n;
}

#ifdef __cplusplus
}
#endif
#endif
