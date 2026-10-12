/*
 * ps3recomp - RSX Command Buffer Processor
 *
 * The PS3's RSX (Reality Synthesizer) is an NV47-class GPU that receives
 * rendering commands through a ring buffer. Games write NV47xx method
 * calls to this buffer via cellGcm, and the RSX processes them.
 *
 * In recompilation, we intercept the command buffer and translate
 * NV47xx methods to the host graphics API (D3D12 on Windows, Vulkan
 * on Linux/macOS).
 *
 * Architecture:
 *   Game code → cellGcmSys (state tracking) → command buffer → RSX processor
 *                                                                  ↓
 *                                                          Host GPU backend
 *
 * Reference: NV47xx method registers documented in envytools/rnndb
 * and RPCS3's RSX module (rpcs3/Emu/RSX/).
 */

#ifndef PS3RECOMP_RSX_COMMANDS_H
#define PS3RECOMP_RSX_COMMANDS_H

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"
#include <stdlib.h>   /* getenv (rsx_vp_entry) */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * NV47xx Method Register Addresses
 *
 * These are the GPU method addresses that games write to the command buffer.
 * Each method has a 32-bit address and one or more 32-bit data words.
 *
 * Naming: NV4097_SET_* for 3D methods, NV3062_SET_* for 2D methods
 * -----------------------------------------------------------------------*/

/* Surface / render target configuration (real NV4097 numbering -- RPCS3 /
 * nouveau; same table as upstream sp00nznet 247251b0). The old table was
 * shuffled: only COLOR_AOFFSET was right, so clip dims read the format
 * register, COLOR_TARGET (0x220) landed in surface_zeta_offset (measured:
 * zeta_off=0x1 / 0x13 = TARGET_0 / TARGET_MRT1) and every Metal surface fell
 * back to 1280x720. */
#define NV4097_SET_SURFACE_CLIP_HORIZONTAL     0x00000200
#define NV4097_SET_SURFACE_CLIP_VERTICAL       0x00000204
#define NV4097_SET_SURFACE_FORMAT              0x00000208
#define NV4097_SET_SURFACE_PITCH_A             0x0000020C
#define NV4097_SET_SURFACE_COLOR_AOFFSET       0x00000210
#define NV4097_SET_SURFACE_ZETA_OFFSET         0x00000214
#define NV4097_SET_SURFACE_COLOR_BOFFSET       0x00000218
#define NV4097_SET_SURFACE_PITCH_B             0x0000021C
#define NV4097_SET_SURFACE_COLOR_TARGET        0x00000220
/* IDs do RPCS3 gcm_enums.h (A=0x194, B=0x18C: nao sao sequenciais). */
#define NV4097_SET_CONTEXT_DMA_COLOR_A         0x00000194
#define NV4097_SET_CONTEXT_DMA_COLOR_B         0x0000018C
#define NV4097_SET_CONTEXT_DMA_COLOR_C         0x000001B4
#define NV4097_SET_CONTEXT_DMA_COLOR_D         0x000001B8
#define RSX_CONTEXT_DMA_MAIN                   0xFEED0001u
#define NV4097_SET_SURFACE_PITCH_Z             0x0000022C
#define NV4097_SET_SURFACE_PITCH_C             0x00000280
#define NV4097_SET_SURFACE_PITCH_D             0x00000284
#define NV4097_SET_SURFACE_COLOR_COFFSET       0x00000288
#define NV4097_SET_SURFACE_COLOR_DOFFSET       0x0000028C

/* Viewport (real NV4097 numbering; the old 0x300/0x304 belong to the
 * dither/alpha-test block, so viewport rects read garbage). */
#define NV4097_SET_VIEWPORT_HORIZONTAL         0x00000A00
#define NV4097_SET_VIEWPORT_VERTICAL           0x00000A04
#define NV4097_SET_VIEWPORT_OFFSET             0x00000A20  /* 4 floats x,y,z,w */
#define NV4097_SET_VIEWPORT_SCALE              0x00000A30  /* 4 floats x,y,z,w */
#define NV4097_SET_CLIP_MIN                    0x00000394
#define NV4097_SET_CLIP_MAX                    0x00000398

/* Scissor */
#define NV4097_SET_SCISSOR_HORIZONTAL          0x000008C0
#define NV4097_SET_SCISSOR_VERTICAL            0x000008C4

/* Clear */
#define NV4097_SET_COLOR_CLEAR_VALUE           0x00001D90
#define NV4097_SET_ZSTENCIL_CLEAR_VALUE        0x00001D8C
#define NV4097_CLEAR_SURFACE                   0x00001D94

/* Draw commands — NV40/RSX method map (envytools + rsx_dispatch.c):
 *   0x1808 BEGIN_END
 *   0x1814 VB_VERTEX_BATCH   first[0:23] (count-1)[24:31]
 *   0x181C IDXBUF_OFFSET
 *   0x1820 IDXBUF_FORMAT     location[0:3] type[4:11] (0=u32, 1=u16)
 *   0x1824 VB_INDEX_BATCH    first[0:23] (count-1)[24:31]  ← real indexed draw
 * NOTE: older headers mis-labelled 0x1820 as DRAW and 0x1738 as index
 * address (0x1738 is VERTEX_DATA_BASE_OFFSET). Fixed 2026-07-22. */
#define NV4097_SET_BEGIN_END                    0x00001808
#define NV4097_DRAW_ARRAYS                     0x00001814
#define NV4097_DRAW_INDEX_ARRAY                0x00001824

/* Vertex attributes */
#define NV4097_SET_VERTEX_DATA_ARRAY_FORMAT     0x00001740
#define NV4097_SET_VERTEX_DATA_ARRAY_OFFSET     0x00001680
#define NV4097_SET_VERTEX_DATA_BASE_OFFSET      0x00001738
#define NV4097_SET_VERTEX_DATA_BASE_INDEX       0x0000173C

/* Index buffer setup (feeds NV4097_DRAW_INDEX_ARRAY / VB_INDEX_BATCH). */
#define NV4097_SET_INDEX_ARRAY_ADDRESS          0x0000181C
#define NV4097_SET_INDEX_ARRAY_DMA              0x00001820
/* Alias kept for clarity when reading FIFO dumps. */
#define NV4097_IDXBUF_OFFSET                    NV4097_SET_INDEX_ARRAY_ADDRESS
#define NV4097_IDXBUF_FORMAT                    NV4097_SET_INDEX_ARRAY_DMA
#define NV4097_VB_INDEX_BATCH                   NV4097_DRAW_INDEX_ARRAY

/* Texture */
#define NV4097_SET_TEXTURE_OFFSET               0x00001A00
#define NV4097_SET_TEXTURE_FORMAT               0x00001A04
#define NV4097_SET_TEXTURE_ADDRESS              0x00001A08
#define NV4097_SET_TEXTURE_CONTROL0             0x00001A0C
#define NV4097_SET_TEXTURE_CONTROL1             0x00001A10
#define NV4097_SET_TEXTURE_FILTER               0x00001A14
#define NV4097_SET_TEXTURE_IMAGE_RECT           0x00001A18
#define NV4097_SET_TEXTURE_BORDER_COLOR         0x00001A1C

/* Shader programs */
#define NV4097_SET_SHADER_PROGRAM               0x000008E4
/* Fragment program control word; bit 0x40 = 32_BITS_EXPORTS: the colour export
 * is r0 (fp32) when set, h0 (fp16) when clear (upstream HEAD / RPCS3). */
#define NV4097_SET_SHADER_CONTROL               0x00001D60
#define NV4097_SET_VERTEX_ATTRIB_OUTPUT_MASK    0x00001FF4
#define NV4097_SET_TRANSFORM_PROGRAM_LOAD       0x00001E9C
#define NV4097_SET_TRANSFORM_PROGRAM_START      0x00001EA0
#define NV4097_SET_TRANSFORM_PROGRAM            0x00000B80
#define NV4097_SET_TRANSFORM_CONSTANT_LOAD      0x00001EFC
/* Vertex constants are written as up to 64 dwords (16 vec4s) per command,
 * starting at 0x1F00. Slot = transform_constant_load + (reg/4); lane = reg%4.
 * The hardware supports 512 vec4 constants total in the vertex register file. */
#define NV4097_SET_TRANSFORM_CONSTANT           0x00001F00
#define RSX_MAX_VERTEX_CONSTANTS                512

/* Color mask */
#define NV4097_SET_COLOR_MASK                   0x00000324

/* Alpha test */
#define NV4097_SET_ALPHA_TEST_ENABLE            0x00000304
#define NV4097_SET_ALPHA_FUNC                   0x00000308
#define NV4097_SET_ALPHA_REF                    0x0000030C

/* Blending */
#define NV4097_SET_BLEND_ENABLE                 0x00000310
#define NV4097_SET_BLEND_FUNC_SFACTOR           0x00000314
#define NV4097_SET_BLEND_FUNC_DFACTOR           0x00000318
#define NV4097_SET_BLEND_EQUATION               0x00000320
#define NV4097_SET_BLEND_COLOR                  0x0000031C

/* Depth / stencil */
#define NV4097_SET_DEPTH_TEST_ENABLE            0x00000A74
#define NV4097_SET_DEPTH_FUNC                   0x00000A6C
#define NV4097_SET_DEPTH_MASK                   0x00000A70
#define NV4097_SET_STENCIL_TEST_ENABLE          0x00000328
#define NV4097_SET_STENCIL_FUNC                 0x00000330
#define NV4097_SET_STENCIL_FUNC_REF             0x00000334
#define NV4097_SET_STENCIL_FUNC_MASK            0x00000338
#define NV4097_SET_STENCIL_OP_FAIL              0x0000033C
#define NV4097_SET_STENCIL_OP_ZFAIL             0x00000340
#define NV4097_SET_STENCIL_OP_ZPASS             0x00000344
#define NV4097_SET_STENCIL_MASK                 0x0000032C
#define NV4097_SET_ZMIN_MAX_CONTROL             0x00001D78  /* bit0 clip, bit4 clamp */
#define NV4097_SET_POLY_OFFSET_FILL_ENABLE      0x00000A68
#define NV4097_SET_POLYGON_OFFSET_SCALE_FACTOR  0x00000A78  /* float */
#define NV4097_SET_POLYGON_OFFSET_BIAS          0x00000A7C  /* float */
#define NV4097_SET_TWO_SIDED_STENCIL_TEST_ENABLE 0x00000348
#define NV4097_SET_BACK_STENCIL_MASK            0x0000034C
#define NV4097_SET_BACK_STENCIL_FUNC            0x00000350
#define NV4097_SET_BACK_STENCIL_FUNC_REF        0x00000354
#define NV4097_SET_BACK_STENCIL_FUNC_MASK       0x00000358
#define NV4097_SET_BACK_STENCIL_OP_FAIL         0x0000035C
#define NV4097_SET_BACK_STENCIL_OP_ZFAIL        0x00000360
#define NV4097_SET_BACK_STENCIL_OP_ZPASS        0x00000364

/* Culling */
#define NV4097_SET_CULL_FACE_ENABLE             0x0000183C
#define NV4097_SET_CULL_FACE                    0x00001830
#define NV4097_SET_FRONT_FACE                   0x00001834

/* Primitive types */
#define RSX_PRIMITIVE_POINTS             1
#define RSX_PRIMITIVE_LINES              2
#define RSX_PRIMITIVE_LINE_LOOP          3
#define RSX_PRIMITIVE_LINE_STRIP         4
#define RSX_PRIMITIVE_TRIANGLES          5
#define RSX_PRIMITIVE_TRIANGLE_STRIP     6
#define RSX_PRIMITIVE_TRIANGLE_FAN       7
#define RSX_PRIMITIVE_QUADS              8
#define RSX_PRIMITIVE_QUAD_STRIP         9
#define RSX_PRIMITIVE_POLYGON            10

/* ---------------------------------------------------------------------------
 * RSX State — tracked as methods are processed
 * -----------------------------------------------------------------------*/

#define RSX_MAX_TEXTURES          16
#define RSX_MAX_VERTEX_ATTRIBS    16
#define RSX_MAX_RENDER_TARGETS     4

typedef struct rsx_texture_state {
    u32 offset;
    u32 format;
    u32 address;
    u32 control0;
    u32 control1;
    u32 filter;
    u32 image_rect;
    u32 border_color;
    int dirty;
} rsx_texture_state;

typedef struct rsx_vertex_attrib {
    u32 format;
    u32 offset;
    u32 stride;
    u32 size;
    u32 type;
    int enabled;
} rsx_vertex_attrib;

typedef struct rsx_state {
    /* Render targets */
    u32 surface_format;
    u32 surface_color_offset[RSX_MAX_RENDER_TARGETS];
    u32 surface_color_pitch[RSX_MAX_RENDER_TARGETS];
    u32 surface_zeta_offset;
    u32 surface_zeta_pitch;
    u32 surface_clip_x, surface_clip_y;
    u32 surface_clip_w, surface_clip_h;
    u32 color_target;
    /* SET_CONTEXT_DMA_COLOR_A..D: 0xFEED0000 = VRAM local, 0xFEED0001 = memoria
     * principal (RPCS3 get_address(offset, surface_dma)). O GoW2 desenha o alvo B
     * do menu em memoria principal. 0 = nunca definido (tratado como local). */
    u32 surface_color_dma[RSX_MAX_RENDER_TARGETS];

    /* Viewport */
    u32 viewport_x, viewport_y, viewport_w, viewport_h;
    /* Clip -> window transform (window = ndc * scale + offset, RPCS3
     * fill_scale_offset_data). Bit 0 offset seen, bit 1 scale seen. */
    float viewport_offset[4], viewport_scale[4];
    u32 viewport_so_seen;
    float clip_min, clip_max;

    /* Scissor */
    u32 scissor_x, scissor_y, scissor_w, scissor_h;

    /* Clear */
    u32 color_clear_value;
    u32 zstencil_clear_value;

    /* Blend */
    int blend_enable;
    u32 blend_sfactor, blend_dfactor;
    u32 blend_equation;
    u32 blend_color;

    /* Depth */
    int depth_test_enable;
    u32 depth_func;
    int depth_mask;

    /* Stencil */
    int stencil_test_enable;
    u32 stencil_func, stencil_ref, stencil_mask;
    u32 stencil_op_fail, stencil_op_zfail, stencil_op_zpass;
    u32 stencil_write_mask;          /* SET_STENCIL_MASK (0x32C) */
    int two_sided_stencil_enable;    /* SET_TWO_SIDED_STENCIL_TEST_ENABLE */
    u32 zminmax_control;             /* SET_ZMIN_MAX_CONTROL, reset 0x1 (clip on, clamp off) */
    int poly_offset_fill_enable;     /* SET_POLY_OFFSET_FILL_ENABLE */
    float poly_offset_scale, poly_offset_bias;
    u32 back_stencil_func, back_stencil_ref, back_stencil_mask, back_stencil_write_mask;
    u32 back_stencil_op_fail, back_stencil_op_zfail, back_stencil_op_zpass;

    /* Culling */
    int cull_face_enable;
    u32 cull_face;
    u32 front_face;

    /* Color mask — bits: [0] B, [8] G, [16] R, [24] A */
    u32 color_mask;
    int color_mask_dirty;

    /* Alpha test */
    int alpha_test_enable;
    u32 alpha_func;
    u32 alpha_ref;
    int alpha_dirty;

    /* Textures */
    rsx_texture_state textures[RSX_MAX_TEXTURES];

    /* Vertex attributes */
    rsx_vertex_attrib vertex_attribs[RSX_MAX_VERTEX_ATTRIBS];
    /* Added to every vertex array offset (mask 0x0FFFFFFF) and, for indexed
     * draws, to the index (mask 0x000FFFFF) -- RPCS3 rsx_utils.h. */
    u32 vertex_data_base_offset, vertex_data_base_index;
    /* Constant ("current") vertex attributes, NV4097_SET_VERTEX_DATA4F_M
     * (0x1C00): a DISABLED attribute array feeds every vertex this register,
     * not zero (upstream sp00nznet HEAD, same rule as glColor4f with the colour
     * array off). Reset value (0,0,0,1). */
    float vertex_data4f[RSX_MAX_VERTEX_ATTRIBS][4];
    /* NV4097_SET_SHADER_CONTROL: selects the fragment colour export register.
     * shader_control_valid is 0 until the guest writes the method — AUTO
     * heuristic applies (h0 if the program wrote h0). */
    u32 shader_control;
    u32 shader_control_valid;

    /* Index array (NV4097_SET_INDEX_ARRAY_ADDRESS/DMA), consumed by
     * NV4097_DRAW_INDEX_ARRAY. index_location: bit0 of the DMA word (0=RSX
     * local/VRAM, 1=main memory, same convention as shader_program).
     * index_is_16bit: DMA word's type field (1=16-bit indices, 0=32-bit). */
    u32 index_array_addr;
    u32 index_location;
    int index_is_16bit;

    /* Current draw state */
    u32 primitive_type;
    int in_begin_end;  /* between BEGIN_END(type) and BEGIN_END(0) */
    u32 begin_end_id;     /* increments on each BEGIN; 0 = none */
    u32 begin_end_packet; /* DRAW_INDEX_ARRAY index inside this BEGIN */

    /* Shader state */
    u32 shader_program;       /* fragment program address (offset | location in bits [0:1]) */
    u32 fragment_program_addr;
    u32 vertex_attrib_output_mask;
    u32 transform_program_load; /* vertex program load slot index */
    u32 transform_constant_load;
    int shader_dirty;

    /* Vertex transform constants — written by NV4097_SET_TRANSFORM_CONSTANT.
     * Each constant is a vec4 (4 floats). Games typically place the MVP
     * matrix in the first few slots. Used by the backend as the vertex
     * shader constant buffer when no RSX vertex program is translated. */
    float vertex_constants[RSX_MAX_VERTEX_CONSTANTS][4];

    /* Vertex (transform) program instruction memory — captured from
     * NV4097_SET_TRANSFORM_PROGRAM. 512 instructions × 4 words (host order,
     * as rsx_vp_decompile's rd_le expects on a little-endian host). The
     * backend decompiles this into a real vertex shader instead of the
     * passthrough VS. */
    u32 transform_program[512 * 4];
    u32 transform_program_write;  /* next word index to write (advances per push) */
    u32 transform_program_words;  /* highest word count seen (program length bound) */
    /* SET_TRANSFORM_PROGRAM_START: the entry instruction of the vertex program.
     * Several programs live in the 512-slot memory at once; the title switches
     * between them with START alone (RPCS3 RSXThread.cpp analyse_vertex_program
     * from transform_program_start). */
    u32 transform_program_start;
    int transform_program_dirty;
    /* Lowest and highest slot touched since last reset — used to push only
     * the dirty range to the host CBV. */
    u32 vertex_constants_lo;
    u32 vertex_constants_hi;
    int vertex_constants_dirty;

    /* Dirty flags for incremental state updates */
    int surface_dirty;
    int viewport_dirty;
    int blend_dirty;
    int depth_dirty;
    int stencil_dirty;
    int texture_dirty;
    int vertex_dirty;
} rsx_state;

/* ---------------------------------------------------------------------------
 * RSX Command Processor API
 * -----------------------------------------------------------------------*/

/* Initialize RSX state to defaults */
void rsx_state_init(rsx_state* state);

/* Process a single NV47xx method call.
 * method: register address (e.g., NV4097_SET_BLEND_ENABLE)
 * data:   32-bit value to write
 * Returns 0 on success, -1 if method is unrecognized. */
int rsx_process_method(rsx_state* state, u32 method, u32 data);

/* Process a command buffer segment.
 * buf:  pointer to command buffer data (host memory)
 * size: size in bytes
 * Returns number of methods processed. */
int rsx_process_command_buffer(rsx_state* state, const u32* buf, u32 size);

/* ---------------------------------------------------------------------------
 * Backend callbacks — implemented per-platform (D3D12, Vulkan, null)
 * -----------------------------------------------------------------------*/

/* NV3089 scaled-image blit offered to a backend before the CPU copy in guest
 * memory (cellGcmSys.c nv3089_blit). Offsets are RSX offsets; *_local means
 * RSX local memory, otherwise IO-mapped main memory. */
typedef struct rsx_blit_desc {
    int src_local, dst_local;
    u32 src_offset, src_pitch, src_fmt;   /* NV3089 in offset / pitch / colour format */
    u32 in_w, in_h;                       /* NV3089 in size */
    u32 u0, v0;                           /* start, 12.4 fixed-point texels */
    u32 ds_dx, dt_dy;                     /* 20.12 fixed-point texels per output pixel */
    u32 dst_offset, dst_pitch, dst_fmt;   /* NV3062 destination surface */
    u32 out_x, out_y, out_w, out_h;       /* destination rectangle */
} rsx_blit_desc;

typedef struct rsx_backend {
    void* userdata;

    /* Lifecycle */
    int  (*init)(void* userdata, u32 width, u32 height);
    void (*shutdown)(void* userdata);

    /* Frame */
    void (*begin_frame)(void* userdata);
    void (*end_frame)(void* userdata);
    void (*present)(void* userdata, u32 buffer_id);

    /* State changes (called when dirty flags are set) */
    void (*set_render_target)(void* userdata, const rsx_state* state);
    void (*set_viewport)(void* userdata, const rsx_state* state);
    void (*set_blend)(void* userdata, const rsx_state* state);
    void (*set_depth_stencil)(void* userdata, const rsx_state* state);
    void (*set_color_mask)(void* userdata, const rsx_state* state);
    void (*set_alpha_test)(void* userdata, const rsx_state* state);
    void (*set_shader)(void* userdata, const rsx_state* state);
    void (*set_vertex_attribs)(void* userdata, const rsx_state* state);

    /* Clear */
    void (*clear)(void* userdata, u32 flags, u32 color, float depth, u8 stencil);

    /* Draw */
    void (*draw_arrays)(void* userdata, u32 primitive, u32 first, u32 count);
    void (*draw_indexed)(void* userdata, u32 primitive, u32 index_offset, u32 count);

    /* Texture */
    void (*bind_texture)(void* userdata, u32 unit, const rsx_texture_state* tex);

    /* 2D (optional): perform an NV3089 blit on the GPU. Return 1 if done, 0 to
     * let cellGcmSys fall back to the CPU copy in guest memory. */
    int (*blit_scaled)(void* userdata, const rsx_blit_desc* d);
} rsx_backend;

/* Per-frame packetization counters (reset by rsx_stats_begin_frame). */
void rsx_stats_begin_frame(void);
u32  rsx_stats_logical(void);
int  rsx_commands_pending_batch(void);   /* PS3_RSX_CAPTURE: s_idx_pending || s_arr_pending */
u32  rsx_stats_take_aoffset(void);   /* diagnostic: SET_SURFACE_COLOR_AOFFSET since last call */
u32  rsx_stats_packets(void);
u32  rsx_stats_vconst_writes(void);   /* transform-constant writes this frame */
u32  rsx_stats_vconst_max(void);      /* highest constant slot ever asked for */
u32  rsx_vc_last_write_be(u32 slot);  /* begin_end_id of the last write to that constant */
u32  rsx_stats_vconst_dropped(void);  /* writes past RSX_MAX_VERTEX_CONSTANTS */
/* Escritas ao ficheiro de atributos constantes por codificacao:
 * 0=4F 1=2F 2=2S 3=4UB 4=4S 5=SCALED4S (E462). */
u32  rsx_stats_vdata_writes(u32 kind);
u32  rsx_stats_vconst_writes_total(void);  /* controlo positivo do censo */
u32  rsx_stats_indices(void);
u32  rsx_stats_tri_rem_warns(void);

/* Register a backend. Call before any RSX processing. */
void rsx_set_backend(rsx_backend* backend);

/* Get the current backend (or NULL if none set). */
rsx_backend* rsx_get_backend(void);

/* The vertex program the draw runs: words from the START instruction up to the
 * highest word written. START 0 = the whole memory, as before.
 * PS3_RSX_VP_START=0 ignores START (A/B). */
static inline const u32* rsx_vp_entry(const rsx_state* s, u32* nwords)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_RSX_VP_START"); on = (e && e[0] == '0') ? 0 : 1; }
    const u32 w0 = on ? s->transform_program_start * 4u : 0u;
    const u32 vpw = s->transform_program_words;
    *nwords = (w0 < vpw) ? vpw - w0 : 0u;
    return s->transform_program + (w0 < 512u * 4u ? w0 : 0u);
}

#ifdef __cplusplus
}
#endif
#endif /* PS3RECOMP_RSX_COMMANDS_H */
