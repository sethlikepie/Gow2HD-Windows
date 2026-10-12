/*
 * ps3recomp - RSX Vertex Format Mapping
 *
 * Maps RSX vertex attribute types to host API formats.
 * RSX uses NV-style vertex attribute encoding from the NV4097 register.
 */

#ifndef PS3RECOMP_RSX_VERTEX_FORMATS_H
#define PS3RECOMP_RSX_VERTEX_FORMATS_H

#include "rsx_commands.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RSX vertex attribute type values (from NV4097_SET_VERTEX_DATA_ARRAY_FORMAT) */
#define RSX_VERTEX_TYPE_NONE           0  /* disabled */
#define RSX_VERTEX_TYPE_S1             1  /* signed normalized 16-bit */
#define RSX_VERTEX_TYPE_F              2  /* float32 */
#define RSX_VERTEX_TYPE_SF             3  /* half-float (float16) */
#define RSX_VERTEX_TYPE_UB             4  /* unsigned byte */
#define RSX_VERTEX_TYPE_S32K           5  /* signed 16-bit */
#define RSX_VERTEX_TYPE_CMP            6  /* compressed (11-11-10 packed) */
#define RSX_VERTEX_TYPE_UB256          7  /* unsigned byte (0-255, NOT normalized) */

/* Get the byte size of one component for an RSX vertex type */
static inline u32 rsx_vertex_type_size(u32 type)
{
    switch (type) {
    case RSX_VERTEX_TYPE_S1:     return 2;
    case RSX_VERTEX_TYPE_F:      return 4;
    case RSX_VERTEX_TYPE_SF:     return 2;
    case RSX_VERTEX_TYPE_UB:     return 1;
    case RSX_VERTEX_TYPE_S32K:   return 2;
    case RSX_VERTEX_TYPE_CMP:    return 4; /* packed */
    case RSX_VERTEX_TYPE_UB256:  return 1;
    default:                     return 0;
    }
}

/* Get the total byte size of a vertex attribute (type * components) */
static inline u32 rsx_vertex_attrib_size(u32 type, u32 num_components)
{
    if (type == RSX_VERTEX_TYPE_CMP) return 4; /* always 4 bytes packed */
    return rsx_vertex_type_size(type) * num_components;
}

/* Decode ONE component of a vertex attribute from raw big-endian guest
 * bytes (rsx_vertex_type_size(type) bytes available at p). Does not handle
 * RSX_VERTEX_TYPE_CMP -- that type packs an entire attribute (all
 * components) into a single dword; use rsx_vertex_decode_cmp32() for it.
 *
 * Layouts (NV40-era RSX vertex array formats; SNORM vs SINT distinction
 * matches this header's own RSX_VTX_TYPE_SNORM16 / RSX_VTX_TYPE_SINT16
 * naming and the DXGI_FORMAT_R16_SNORM mapping already used for S1 above):
 *   F      - 4-byte BE IEEE-754 float, used as-is.
 *   SF     - 2-byte BE IEEE-754 half float, unpacked to f32.
 *   UB     - 1 raw byte, normalized (/255) -- "unsigned normalized byte".
 *   UB256  - 1 raw byte, NOT normalized (0..255 as-is).
 *   S1     - 2-byte BE signed int16, normalized (/32767, clamped >= -1).
 *   S32K   - 2-byte BE signed int16, NOT normalized (raw integer value).
 */
static inline float rsx_vertex_decode_component(u32 type, const u8* p)
{
    switch (type) {
    case RSX_VERTEX_TYPE_F: {
        u32 bits = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        float f; memcpy(&f, &bits, 4); return f;
    }
    case RSX_VERTEX_TYPE_SF: {
        u32 h = ((u32)p[0] << 8) | p[1];
        u32 sign = (h >> 15) & 1u, exp = (h >> 10) & 0x1Fu, mant = h & 0x3FFu;
        u32 bits;
        if (exp == 0) {
            if (mant == 0) {
                bits = sign << 31; /* +/-0 */
            } else {
                /* Subnormal half -> normalize into a normal f32. */
                int e = 0;
                while (!(mant & 0x400u)) { mant <<= 1; e++; }
                mant &= 0x3FFu;
                bits = (sign << 31) | ((u32)(127 - 15 - e) << 23) | (mant << 13);
            }
        } else if (exp == 0x1F) {
            bits = (sign << 31) | 0x7F800000u | (mant << 13); /* inf/nan */
        } else {
            bits = (sign << 31) | ((exp - 15u + 127u) << 23) | (mant << 13);
        }
        float f; memcpy(&f, &bits, 4); return f;
    }
    case RSX_VERTEX_TYPE_UB:
        return p[0] / 255.0f;
    case RSX_VERTEX_TYPE_UB256:
        return (float)p[0];
    case RSX_VERTEX_TYPE_S1: {
        s16 v = (s16)(u16)(((u32)p[0] << 8) | p[1]);
        float f = v / 32767.0f;
        return f < -1.0f ? -1.0f : f;
    }
    case RSX_VERTEX_TYPE_S32K: {
        s16 v = (s16)(u16)(((u32)p[0] << 8) | p[1]);
        return (float)v;
    }
    default:
        return 0.0f;
    }
}

/* Decode a packed CMP (compressed) vertex attribute: ONE big-endian dword
 * holding 3 signed bitfields, D3D9 DEC3N-compatible layout (the standard
 * "compressed normal" packing shared by the NV40-era GPU family RSX
 * derives from): bits [0:10] = X (11-bit signed), [11:21] = Y (11-bit
 * signed), [22:31] = Z (10-bit signed); each normalized to [-1,1] by its
 * max positive value (1023 for 11-bit fields, 511 for the 10-bit field).
 * Always writes out[0..2]; there is no room for a 4th component in 32
 * bits, so callers keep their own w=1.0 default. */
static inline void rsx_vertex_decode_cmp32(const u8* p, float out[3])
{
    u32 w = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
    u32 ux = w & 0x7FFu;
    u32 uy = (w >> 11) & 0x7FFu;
    u32 uz = (w >> 22) & 0x3FFu;
    s32 sx = (ux & 0x400u) ? (s32)(ux | 0xFFFFF800u) : (s32)ux;
    s32 sy = (uy & 0x400u) ? (s32)(uy | 0xFFFFF800u) : (s32)uy;
    s32 sz = (uz & 0x200u) ? (s32)(uz | 0xFFFFFC00u) : (s32)uz;
    out[0] = sx / 1023.0f;
    out[1] = sy / 1023.0f;
    out[2] = sz / 511.0f;
}

/* DXGI format values for D3D12 input layout */
#define DXGI_FORMAT_R32_FLOAT           41
#define DXGI_FORMAT_R32G32_FLOAT        16
#define DXGI_FORMAT_R32G32B32_FLOAT     6
#define DXGI_FORMAT_R32G32B32A32_FLOAT  2
#define DXGI_FORMAT_R16_FLOAT           54
#define DXGI_FORMAT_R16G16_FLOAT        34
#define DXGI_FORMAT_R16G16B16A16_FLOAT  10
#define DXGI_FORMAT_R8_UNORM            61
#define DXGI_FORMAT_R8G8_UNORM          49
#define DXGI_FORMAT_R8G8B8A8_UNORM      28
#define DXGI_FORMAT_R8_UINT             62
#define DXGI_FORMAT_R8G8_UINT           50
#define DXGI_FORMAT_R8G8B8A8_UINT       30
#define DXGI_FORMAT_R16_SNORM           56
#define DXGI_FORMAT_R16G16_SNORM        36
#define DXGI_FORMAT_R16G16B16A16_SNORM  13

/* Map RSX vertex type + component count to DXGI format.
 * Returns 0 (DXGI_FORMAT_UNKNOWN) for unsupported combinations. */
static inline u32 rsx_to_dxgi_format(u32 type, u32 num_components)
{
    switch (type) {
    case RSX_VERTEX_TYPE_F:
        switch (num_components) {
        case 1: return DXGI_FORMAT_R32_FLOAT;
        case 2: return DXGI_FORMAT_R32G32_FLOAT;
        case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
        break;
    case RSX_VERTEX_TYPE_SF:
        switch (num_components) {
        case 1: return DXGI_FORMAT_R16_FLOAT;
        case 2: return DXGI_FORMAT_R16G16_FLOAT;
        case 4: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        }
        break;
    case RSX_VERTEX_TYPE_UB: /* normalized unsigned byte */
        switch (num_components) {
        case 1: return DXGI_FORMAT_R8_UNORM;
        case 2: return DXGI_FORMAT_R8G8_UNORM;
        case 4: return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
        break;
    case RSX_VERTEX_TYPE_UB256: /* non-normalized unsigned byte */
        switch (num_components) {
        case 1: return DXGI_FORMAT_R8_UINT;
        case 2: return DXGI_FORMAT_R8G8_UINT;
        case 4: return DXGI_FORMAT_R8G8B8A8_UINT;
        }
        break;
    case RSX_VERTEX_TYPE_S1: /* signed normalized 16-bit */
        switch (num_components) {
        case 1: return DXGI_FORMAT_R16_SNORM;
        case 2: return DXGI_FORMAT_R16G16_SNORM;
        case 4: return DXGI_FORMAT_R16G16B16A16_SNORM;
        }
        break;
    }
    return 0; /* DXGI_FORMAT_UNKNOWN */
}

/* Host-side vertex format tags for Metal (M6). Not MTLVertexFormat enums —
 * those are ObjC-only; map in the Metal backend. 0 = unsupported. */
#define RSX_MTL_VF_UNKNOWN       0
#define RSX_MTL_VF_FLOAT         1
#define RSX_MTL_VF_FLOAT2        2
#define RSX_MTL_VF_FLOAT3        3
#define RSX_MTL_VF_FLOAT4        4
#define RSX_MTL_VF_HALF2         5
#define RSX_MTL_VF_HALF4         6
#define RSX_MTL_VF_UCHAR4_NORM   7
#define RSX_MTL_VF_UCHAR4        8
#define RSX_MTL_VF_SHORT2_NORM   9
#define RSX_MTL_VF_SHORT4_NORM  10

/* MVP map used by Metal upload path. Returns RSX_MTL_VF_*. */
static inline u32 rsx_to_mtl_vertex_format_tag(u32 type, u32 num_components)
{
    switch (type) {
    case RSX_VERTEX_TYPE_F:
        if (num_components == 1) return RSX_MTL_VF_FLOAT;
        if (num_components == 2) return RSX_MTL_VF_FLOAT2;
        if (num_components == 3) return RSX_MTL_VF_FLOAT3;
        if (num_components >= 4) return RSX_MTL_VF_FLOAT4;
        break;
    case RSX_VERTEX_TYPE_SF:
        if (num_components == 2) return RSX_MTL_VF_HALF2;
        if (num_components >= 4) return RSX_MTL_VF_HALF4;
        if (num_components == 1 || num_components == 3)
            return RSX_MTL_VF_HALF2; /* best-effort; upload expands */
        break;
    case RSX_VERTEX_TYPE_UB:
        if (num_components >= 3) return RSX_MTL_VF_UCHAR4_NORM;
        break;
    case RSX_VERTEX_TYPE_UB256:
        if (num_components >= 3) return RSX_MTL_VF_UCHAR4;
        break;
    case RSX_VERTEX_TYPE_S1:
        if (num_components == 2) return RSX_MTL_VF_SHORT2_NORM;
        if (num_components >= 3) return RSX_MTL_VF_SHORT4_NORM;
        break;
    default:
        break;
    }
    return RSX_MTL_VF_UNKNOWN;
}

/* True if the attribute can be consumed by the host BasicVertex converter. */
static inline int rsx_vertex_attrib_is_position_ok(const rsx_vertex_attrib* a)
{
    if (!a || !a->enabled) return 0;
    u32 tag = rsx_to_mtl_vertex_format_tag(a->type, a->size);
    return tag == RSX_MTL_VF_FLOAT2 || tag == RSX_MTL_VF_FLOAT3 ||
           tag == RSX_MTL_VF_FLOAT4 || tag == RSX_MTL_VF_HALF2 ||
           tag == RSX_MTL_VF_HALF4 || tag == RSX_MTL_VF_SHORT2_NORM ||
           tag == RSX_MTL_VF_SHORT4_NORM;
}

static inline int rsx_vertex_attrib_is_color_ok(const rsx_vertex_attrib* a)
{
    if (!a || !a->enabled) return 0;
    u32 tag = rsx_to_mtl_vertex_format_tag(a->type, a->size);
    return tag == RSX_MTL_VF_UCHAR4_NORM || tag == RSX_MTL_VF_UCHAR4 ||
           tag == RSX_MTL_VF_FLOAT3 || tag == RSX_MTL_VF_FLOAT4 ||
           tag == RSX_MTL_VF_HALF4;
}

#ifdef __cplusplus
}
#endif
#endif
