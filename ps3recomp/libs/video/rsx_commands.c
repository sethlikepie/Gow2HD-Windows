/*
 * ps3recomp - RSX Command Buffer Processor
 *
 * Parses NV47xx GPU methods from the command buffer and updates RSX state.
 * Dispatches to the registered graphics backend for actual rendering.
 *
 * Command buffer format (NV47xx FIFO):
 *   Each command is a 32-bit header followed by N data words.
 *   Header format: [31:29] type | [28:18] count | [17:13] subchannel | [12:2] method | [1:0] flags
 *
 *   Type 0 (increasing): method, method+4, method+8, ... for each data word
 *   Type 2 (non-increasing): same method repeated for each data word
 *   Type 1 (jump): jump to address in data
 *   Type 3 (call/return): call/return from subroutine
 */

#include "rsx_commands.h"
#include "rsx_index.h"
#include "rsx_depth_debug.h"
#include "ps3_checkpoint.h"
#include "ps3_trace.h"   /* Task 5 (bringup-instrumentacao-fase0): subsistema RSX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Global backend
 * -----------------------------------------------------------------------*/

static rsx_backend* s_backend = NULL;

static u32 s_st_logical, s_st_packets, s_st_indices, s_st_tri_rem, s_st_aoffset;

/* Merge DRAW_INDEX_ARRAY packets only inside the same BEGIN/END.
 * Same IB + contiguous first across BEGINs is NOT enough (CONST/VS may change).
 * Default ON; PS3_RSX_COALESCE_INDEX=0 disables. */
static int rsx_coalesce_begin_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_RSX_COALESCE_INDEX");
        on = (!e || *e != '0') ? 1 : 0;
    }
    return on;
}

#define RSX_IDX_PKTS_MAX 32
static u32 s_idx_first, s_idx_count, s_idx_prim, s_idx_begin;
static int s_idx_pending;
static u32 s_idx_pkt_first[RSX_IDX_PKTS_MAX];
static u32 s_idx_pkt_count[RSX_IDX_PKTS_MAX];
static u32 s_idx_pkts;

static void rsx_dump_begin_packets(const rsx_state* st, u32 begin_id, u32 prim, u32 total)
{
    static int dumps = 0;
    if (s_idx_pkts < 2 || dumps >= 16)
        return;
    dumps++;
    fprintf(stderr, "========== BEGIN %u ==========\n", begin_id);
    fprintf(stderr, "primitive=%u\n", prim);
    if (st) {
        fprintf(stderr,
                "depthTest=%d depthWrite=%d depthFunc=0x%X zMin=%g zMax=%g\n",
                st->depth_test_enable, st->depth_mask, st->depth_func,
                st->clip_min, st->clip_max);
        fprintf(stderr,
                "blendEnable=%d srcRGB=0x%X dstRGB=0x%X rgbOp=0x%X mask=0x%X\n",
                st->blend_enable, st->blend_sfactor, st->blend_dfactor,
                st->blend_equation, st->color_mask);
    }
    u32 cum = 0;
    for (u32 i = 0; i < s_idx_pkts; i++) {
        u32 n = s_idx_pkt_count[i];
        u32 before = cum;
        cum += n;
        fprintf(stderr,
                "packet=%u first=0x%X n=%u cumBefore=%u phaseBefore=%u "
                "cumAfter=%u phaseAfter=%u\n",
                i + 1u, s_idx_pkt_first[i], n, before, before % 3u,
                cum, cum % 3u);
    }
    fprintf(stderr, "TOTAL indices=%u TOTAL%%3=%u\n", total, total % 3u);
    fprintf(stderr, "=================================\n");
    fflush(stderr);
}

/* DRAW_ARRAYS packets carry at most 256 vertices (count-1 in 8 bits), so a big
 * non-indexed draw arrives as contiguous packets inside one BEGIN/END. Drawn one
 * by one, a triangle list restarted every 256 vertices (256 % 3 = 1): from the
 * second packet on, triangles joined vertices of different particles and GoW2's
 * particles/light shafts (59864B76, DD5B993E) became screen-sized planes
 * (2026-09-16 gameplay shots). Contiguous ranges of the same BEGIN are merged,
 * as RPCS3's draw clause does. PS3_RSX_COALESCE_ARRAYS=0 disables. */
static int rsx_coalesce_arrays_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_RSX_COALESCE_ARRAYS");
        on = (!e || *e != '0') ? 1 : 0;
    }
    return on;
}
static u32 s_arr_first, s_arr_count, s_arr_prim, s_arr_begin;
static int s_arr_pending;

static void rsx_flush_array_batch(void)
{
    if (!s_arr_pending)
        return;
    s_arr_pending = 0;
    if (s_backend && s_backend->draw_arrays) {
        s_backend->draw_arrays(s_backend->userdata, s_arr_prim, s_arr_first, s_arr_count);
        ps3_checkpoint_fire(PS3_CP_FIRST_DRAW, "rsx_process_method",
                            "prim=%u first=%u count=%u", s_arr_prim, s_arr_first, s_arr_count);
        PS3_TRACE(PS3_TS_RSX, "rsx_process_method", "DRAW",
                  "prim=%u first=%u count=%u indexed=0", s_arr_prim, s_arr_first, s_arr_count);
    }
}

static void rsx_flush_index_batch(rsx_state* state)
{
    rsx_flush_array_batch();
    if (!s_idx_pending)
        return;
    s_idx_pending = 0;
    s_st_logical++;
    s_st_packets += s_idx_pkts ? s_idx_pkts : 1u;
    s_st_indices += s_idx_count;
    if (s_idx_prim == RSX_PRIMITIVE_TRIANGLES && (s_idx_count % 3u) != 0u) {
        s_st_tri_rem++;
        fprintf(stderr, "WARN TRIANGLES begin=%u count=%u remainder=%u\n",
                s_idx_begin, s_idx_count, s_idx_count % 3u);
        fflush(stderr);
    }
    rsx_dump_begin_packets(state, s_idx_begin, s_idx_prim, s_idx_count);
    if (s_backend && s_backend->draw_indexed) {
        s_backend->draw_indexed(s_backend->userdata, s_idx_prim,
                                s_idx_first, s_idx_count);
        ps3_checkpoint_fire(PS3_CP_FIRST_DRAW, "rsx_process_method",
                            "prim=%u first=%u count=%u indexed=1",
                            s_idx_prim, s_idx_first, s_idx_count);
        PS3_TRACE(PS3_TS_RSX, "rsx_process_method", "DRAW",
                  "prim=%u first=%u count=%u indexed=1",
                  s_idx_prim, s_idx_first, s_idx_count);
    }
    s_idx_pkts = 0;
    (void)state;
}

/* Vertex transform constants (bone matrices live here): writes per frame, the highest
 * slot the title asked for, and writes dropped for being past RSX_MAX_VERTEX_CONSTANTS --
 * a dropped bone matrix is a deformed mesh. */
static u32 s_st_vconst_writes, s_st_vconst_max, s_st_vconst_dropped;
u32 rsx_stats_vconst_writes(void)  { return s_st_vconst_writes; }
u32 rsx_stats_vconst_max(void)     { return s_st_vconst_max; }
/* begin_end_id at the last write of each vertex constant (diagnostic): tells a
 * palette entry uploaded for THIS draw from one left over by an earlier one. */
static u32 s_vc_last_be[RSX_MAX_VERTEX_CONSTANTS];
u32 rsx_vc_last_write_be(u32 slot) { return slot < RSX_MAX_VERTEX_CONSTANTS ? s_vc_last_be[slot] : 0u; }
/* Escritas por codificacao do ficheiro de atributos constantes:
 * [0]=4F [1]=2F [2]=2S [3]=4UB [4]=4S [5]=SCALED4S. Cinco delas eram
 * descartadas antes do E462; o contador diz se o titulo as usa mesmo. */
static u32 s_st_vdata[6];
/* PS3_RSX_VDATA_ALL=0 (A/B): repoe o descarte das cinco codificacoes que nao
 * eram tratadas antes do E462, para isolar uma regressao. Default ON porque o
 * comportamento fiel ao RSX e' tratar as seis. */
static int rsx_vdata_all(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_RSX_VDATA_ALL"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}
u32 rsx_stats_vdata_writes(u32 kind) { return kind < 6u ? s_st_vdata[kind] : 0u; }
/* Controlo POSITIVO para o censo acima: as escritas de constantes do VP, que
 * sabemos existir (o jogo transforma geometria). Sem isto, "tudo a zero" nao
 * distingue "o titulo nao usa" de "o instrumento nao esta' ligado". */
u32 rsx_stats_vconst_writes_total(void) { return s_st_vconst_writes; }
u32 rsx_stats_vconst_dropped(void) { return s_st_vconst_dropped; }

void rsx_stats_begin_frame(void)
{
    s_st_vconst_writes = 0;
    s_st_vconst_dropped = 0;
    s_st_logical = s_st_packets = s_st_indices = s_st_tri_rem = 0;
}
u32 rsx_stats_logical(void)      { return s_st_logical; }
/* PS3_RSX_CAPTURE: an index/array batch is open across the frame boundary. */
int rsx_commands_pending_batch(void) { return s_idx_pending || s_arr_pending; }
/* SET_SURFACE_COLOR_AOFFSET methods seen since the last call (diagnostic). */
u32 rsx_stats_take_aoffset(void) { const u32 n = s_st_aoffset; s_st_aoffset = 0; return n; }
u32 rsx_stats_packets(void)      { return s_st_packets; }
u32 rsx_stats_indices(void)      { return s_st_indices; }
u32 rsx_stats_tri_rem_warns(void){ return s_st_tri_rem; }

void rsx_set_backend(rsx_backend* backend)
{
    s_backend = backend;
}

rsx_backend* rsx_get_backend(void)
{
    return s_backend;
}

/* ---------------------------------------------------------------------------
 * State initialization
 * -----------------------------------------------------------------------*/

void rsx_state_init(rsx_state* state)
{
    memset(state, 0, sizeof(rsx_state));

    /* Constant vertex attribute registers reset to (0,0,0,1). */
    for (int i = 0; i < RSX_MAX_VERTEX_ATTRIBS; i++)
        state->vertex_data4f[i][3] = 1.0f;

    /* Default viewport */
    state->viewport_w = 1280;
    state->viewport_h = 720;
    state->clip_min = 0.0f;
    state->clip_max = 1.0f;

    /* Default scissor */
    state->scissor_w = 4096;
    state->scissor_h = 4096;

    /* Default depth */
    state->depth_func = 1; /* LESS */
    state->depth_mask = 1;

    /* Default cull */
    state->cull_face = 1; /* BACK */
    state->front_face = 0; /* CW */

    /* Default color mask: all channels writable (A|R|G|B) */
    state->color_mask = 0x01010101;

    /* Default stencil: sensible initial values */
    state->stencil_func = 0x0207; /* ALWAYS */
    state->stencil_ref = 0;
    state->stencil_mask = 0xFF;
    state->stencil_op_fail = 0x1E00;  /* KEEP */
    state->stencil_op_zfail = 0x1E00; /* KEEP */
    state->stencil_op_zpass = 0x1E00; /* KEEP */
    state->stencil_write_mask = 0xFF;
    state->zminmax_control = 0x1; /* RPCS3 rsx_methods.cpp reset */
    state->back_stencil_func = 0x0207;
    state->back_stencil_mask = 0xFF;
    state->back_stencil_write_mask = 0xFF;
    state->back_stencil_op_fail = 0x1E00;
    state->back_stencil_op_zfail = 0x1E00;
    state->back_stencil_op_zpass = 0x1E00;

    /* Default alpha test */
    state->alpha_func = 0x0207; /* ALWAYS */
    state->alpha_ref = 0;

    /* Guest SET_SHADER_CONTROL sets shader_control_valid. Until then AUTO. */
    state->shader_control = 0;
    state->shader_control_valid = 0;

    /* Mark everything dirty */
    state->surface_dirty = 1;
    state->viewport_dirty = 1;
    state->blend_dirty = 1;
    state->depth_dirty = 1;
    state->stencil_dirty = 1;
    state->texture_dirty = 1;
    state->vertex_dirty = 1;
    state->color_mask_dirty = 1;
    state->alpha_dirty = 1;
    state->shader_dirty = 1;
}

/* ---------------------------------------------------------------------------
 * Method processing
 * -----------------------------------------------------------------------*/

static int process_surface_method(rsx_state* state, u32 method, u32 data)
{
    switch (method) {
    case NV4097_SET_SURFACE_FORMAT:
        state->surface_format = data;
        state->surface_dirty = 1;
        /* E488: que formato de cor o jogo PEDE para o render target. O backend
         * Metal cria TODAS as surfaces como BGRA8Unorm sem olhar para aqui, e
         * se alguma destas for 0xA (F_W16Z16Y16X16, FP16) estamos a dar 8 bits
         * a um passe HDR -- o que clampa a saida do fragment em [0,1] antes do
         * blend e explicaria as particulas saturadas (alpha do vertice chega a
         * min(w/128, 2) = 2, medido). Conta uma vez por formato distinto; nao
         * muda comportamento. PS3_TRACE_SURFFMT=1. */
        {
            static int on = -1;
            if (on < 0) { const char* e = getenv("PS3_TRACE_SURFFMT"); on = (e && e[0] == '1') ? 1 : 0; }
            if (on) {
                static unsigned seen_mask;
                const unsigned cf = data & 0x1Fu;
                if (cf < 32u && !(seen_mask & (1u << cf))) {
                    seen_mask |= 1u << cf;
                    fprintf(stderr, "[SURFFMT] formato de cor 0x%X (registo 0x%08X)%s\n",
                            cf, data, (cf == 0xAu || cf == 0xBu || cf == 0xCu) ? "  <-- FLOAT/HDR" : "");
                }
            }
        }
        return 0;
    case NV4097_SET_SURFACE_CLIP_HORIZONTAL:
        state->surface_clip_x = data & 0xFFFF;
        state->surface_clip_w = (data >> 16) & 0xFFFF;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_CLIP_VERTICAL:
        state->surface_clip_y = data & 0xFFFF;
        state->surface_clip_h = (data >> 16) & 0xFFFF;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_COLOR_AOFFSET:
        state->surface_color_offset[0] = data;
        state->surface_dirty = 1;
        s_st_aoffset++;
        return 0;
    case NV4097_SET_SURFACE_COLOR_BOFFSET:
        state->surface_color_offset[1] = data;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_COLOR_COFFSET:
        state->surface_color_offset[2] = data;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_COLOR_DOFFSET:
        state->surface_color_offset[3] = data;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_ZETA_OFFSET:
        state->surface_zeta_offset = data;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_SURFACE_COLOR_TARGET:
        state->color_target = data;
        state->surface_dirty = 1;
        return 0;
    case NV4097_SET_CONTEXT_DMA_COLOR_A:
        state->surface_color_dma[0] = data; state->surface_dirty = 1; return 0;
    case NV4097_SET_CONTEXT_DMA_COLOR_B:
        state->surface_color_dma[1] = data; state->surface_dirty = 1; return 0;
    case NV4097_SET_CONTEXT_DMA_COLOR_C:
        state->surface_color_dma[2] = data; state->surface_dirty = 1; return 0;
    case NV4097_SET_CONTEXT_DMA_COLOR_D:
        state->surface_color_dma[3] = data; state->surface_dirty = 1; return 0;
    case NV4097_SET_SURFACE_PITCH_A:
        state->surface_color_pitch[0] = data;
        return 0;
    case NV4097_SET_SURFACE_PITCH_B:
        state->surface_color_pitch[1] = data;
        return 0;
    case NV4097_SET_SURFACE_PITCH_C:
        state->surface_color_pitch[2] = data;
        return 0;
    case NV4097_SET_SURFACE_PITCH_D:
        state->surface_color_pitch[3] = data;
        return 0;
    case NV4097_SET_SURFACE_PITCH_Z:
        state->surface_zeta_pitch = data;
        return 0;
    default:
        return -1;
    }
}

/* ---------------------------------------------------------------------------
 * Texture method processing
 *
 * Texture registers are laid out in 16 units, each spanning 0x20 bytes:
 *   Unit 0: 0x1A00..0x1A1C
 *   Unit 1: 0x1A20..0x1A3C
 *   ...
 *   Unit N: 0x1A00 + N*0x20 .. 0x1A1C + N*0x20
 * -----------------------------------------------------------------------*/

static int process_texture_method(rsx_state* state, u32 method, u32 data)
{
    /* Compute texture unit index and register offset within the unit */
    u32 base = method - NV4097_SET_TEXTURE_OFFSET; /* 0x1A00 */
    u32 unit = base / 0x20;
    u32 reg  = base % 0x20;

    if (unit >= RSX_MAX_TEXTURES)
        return -1;

    rsx_texture_state* tex = &state->textures[unit];

    switch (reg) {
    case 0x00: /* TEXTURE_OFFSET */
        tex->offset = data;
        break;
    case 0x04: /* TEXTURE_FORMAT */
        tex->format = data;
        break;
    case 0x08: /* TEXTURE_ADDRESS (wrap S/T/R) */
        tex->address = data;
        break;
    case 0x0C: /* TEXTURE_CONTROL0 (enable, min/max LOD, max aniso) */
        tex->control0 = data;
        break;
    case 0x10: /* TEXTURE_CONTROL1 (remap) */
        tex->control1 = data;
        break;
    case 0x14: /* TEXTURE_FILTER (bias, min/mag filter) */
        tex->filter = data;
        break;
    case 0x18: /* TEXTURE_IMAGE_RECT (width << 16 | height) */
        tex->image_rect = data;
        break;
    case 0x1C: /* TEXTURE_BORDER_COLOR */
        tex->border_color = data;
        break;
    default:
        return -1;
    }

    tex->dirty = 1;
    state->texture_dirty = 1;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Vertex attribute method processing
 *
 * FORMAT registers: 0x1740 + attrib*4  (16 attributes)
 * OFFSET registers: 0x1680 + attrib*4  (16 attributes)
 * -----------------------------------------------------------------------*/

static int process_vertex_attrib_method(rsx_state* state, u32 method, u32 data)
{
    if (method >= NV4097_SET_VERTEX_DATA_ARRAY_FORMAT &&
        method < NV4097_SET_VERTEX_DATA_ARRAY_FORMAT + RSX_MAX_VERTEX_ATTRIBS * 4) {
        u32 index = (method - NV4097_SET_VERTEX_DATA_ARRAY_FORMAT) / 4;
        rsx_vertex_attrib* attr = &state->vertex_attribs[index];

        /*
         * Format register layout:
         *   [3:0]   type   (float, half, ubyte, short, etc.)
         *   [7:4]   size   (number of components: 1-4)
         *   [15:8]  stride (bytes between consecutive elements)
         *   [16]    enable
         */
        attr->type    = data & 0xF;
        attr->size    = (data >> 4) & 0xF;
        attr->stride  = (data >> 8) & 0xFF;
        /* An array is disabled by size 0, whatever its type (upstream HEAD;
         * E457: GoW2 leaves type F / size 0 / stride 0 on its unused arrays). */
        attr->enabled = (attr->size != 0);
        attr->format  = data;
        state->vertex_dirty = 1;
        return 0;
    }
    /* NV4097_SET_VERTEX_DATA*_M: o valor CONSTANTE de cada atributo de vertice,
     * alimentado a todos os vertices enquanto o array desse atributo esta'
     * desligado. Sao SEIS codificacoes do MESMO ficheiro de registadores; so' a
     * 4F estava implementada e as outras cinco eram descartadas em silencio.
     *
     * Enderecos do docs/rsxfifo.txt do upstream (o mesmo documento da'
     * 0x1A08 = SET_TEXTURE_ADDRESS, que confere com o nosso header):
     *   0x0A80 SCALED4S (16 x 2 dwords)   0x1880 2F   (16 x 2 dwords)
     *   0x1900 2S       (16 x 1 dword)    0x1940 4UB  (16 x 1 dword)
     *   0x1980 4S       (16 x 2 dwords)   0x1C00 4F   (16 x 4 dwords)
     *
     * Porque importa (E462): o VP das particulas do GoW2 gera a UV a partir da
     * posicao -- o[7].xy = sign(c465+v0).xy * c466.xy + v[8].xy -- e v[8] e' a
     * BASE da UV no atlas. Com o array do atributo 8 desligado (medido:
     * "slots v0=0 v3=1 v8=-1"), v[8] vem deste ficheiro de registadores;
     * descartando a escrita, a base fica no default (0,0,0,1) e todas as
     * particulas amostram o mesmo canto do atlas. */
    if (method >= 0x1C00u && method < 0x1C00u + RSX_MAX_VERTEX_ATTRIBS * 16u) {
        u32 idx  = (method - 0x1C00u) >> 4;
        u32 lane = ((method - 0x1C00u) >> 2) & 3u;
        float f;
        memcpy(&f, &data, 4);
        state->vertex_data4f[idx][lane] = f;
        s_st_vdata[0]++;
        return 0;
    }
    if (method >= 0x1880u && method < 0x1880u + RSX_MAX_VERTEX_ATTRIBS * 8u) {
        if (!rsx_vdata_all()) return -1;
        /* 2F: dois floats; z=0, w=1. */
        u32 idx  = (method - 0x1880u) >> 3;
        u32 lane = ((method - 0x1880u) >> 2) & 1u;
        float f;
        memcpy(&f, &data, 4);
        state->vertex_data4f[idx][lane] = f;
        state->vertex_data4f[idx][2] = 0.0f;
        state->vertex_data4f[idx][3] = 1.0f;
        s_st_vdata[1]++;
        return 0;
    }
    if (method >= 0x1900u && method < 0x1900u + RSX_MAX_VERTEX_ATTRIBS * 4u) {
        if (!rsx_vdata_all()) return -1;
        /* 2S: dois s16 num dword, NAO normalizados; z=0, w=1. */
        u32 idx = (method - 0x1900u) >> 2;
        state->vertex_data4f[idx][0] = (float)(int16_t)(u16)(data & 0xFFFFu);
        state->vertex_data4f[idx][1] = (float)(int16_t)(u16)(data >> 16);
        state->vertex_data4f[idx][2] = 0.0f;
        state->vertex_data4f[idx][3] = 1.0f;
        s_st_vdata[2]++;
        return 0;
    }
    if (method >= 0x1940u && method < 0x1940u + RSX_MAX_VERTEX_ATTRIBS * 4u) {
        if (!rsx_vdata_all()) return -1;
        /* 4UB: quatro u8 NORMALIZADOS (/255) -- o encoding tipico de cor. */
        u32 idx = (method - 0x1940u) >> 2;
        for (u32 c = 0; c < 4u; c++)
            state->vertex_data4f[idx][c] = (float)((data >> (c * 8u)) & 0xFFu) / 255.0f;
        s_st_vdata[3]++;
        return 0;
    }
    if (method >= 0x1980u && method < 0x1980u + RSX_MAX_VERTEX_ATTRIBS * 8u) {
        if (!rsx_vdata_all()) return -1;
        /* 4S: quatro s16 em dois dwords, NAO normalizados. */
        u32 idx  = (method - 0x1980u) >> 3;
        u32 pair = ((method - 0x1980u) >> 2) & 1u;
        state->vertex_data4f[idx][pair * 2u + 0u] = (float)(int16_t)(u16)(data & 0xFFFFu);
        state->vertex_data4f[idx][pair * 2u + 1u] = (float)(int16_t)(u16)(data >> 16);
        s_st_vdata[4]++;
        return 0;
    }
    if (method >= 0x0A80u && method < 0x0A80u + RSX_MAX_VERTEX_ATTRIBS * 8u) {
        if (!rsx_vdata_all()) return -1;
        /* SCALED4S: quatro s16 NORMALIZADOS para [-1,1] por 32767. */
        u32 idx  = (method - 0x0A80u) >> 3;
        u32 pair = ((method - 0x0A80u) >> 2) & 1u;
        state->vertex_data4f[idx][pair * 2u + 0u] =
            (float)(int16_t)(u16)(data & 0xFFFFu) / 32767.0f;
        state->vertex_data4f[idx][pair * 2u + 1u] =
            (float)(int16_t)(u16)(data >> 16) / 32767.0f;
        s_st_vdata[5]++;
        return 0;
    }

    if (method >= NV4097_SET_VERTEX_DATA_ARRAY_OFFSET &&
        method < NV4097_SET_VERTEX_DATA_ARRAY_OFFSET + RSX_MAX_VERTEX_ATTRIBS * 4) {
        u32 index = (method - NV4097_SET_VERTEX_DATA_ARRAY_OFFSET) / 4;
        state->vertex_attribs[index].offset = data;
        state->vertex_dirty = 1;
        return 0;
    }

    return -1;
}

/* ---------------------------------------------------------------------------
 * Main method dispatch
 * -----------------------------------------------------------------------*/

/* ---- winport v85b-bulk-methods (engine_fixes_v85b_bulk_methods.py): WINPORT_RSX_BULK85=1 ----
 * n big-endian command words at src for methods method, method+4, ... (noinc: all = method). Same effect as n calls of
 * rsx_process_method for TRANSFORM_PROGRAM / TRANSFORM_CONSTANT runs; returns 0 (nothing done) for anything else. */
int wp85_rsx_bulk(rsx_state* state, u32 method, u32 noinc, const unsigned char* src, u32 n)
{
    static int vcw = -1, cb = -1;
    if (vcw < 0) { const char* e = getenv("PS3_TRACE_VCW"); vcw = (e && *e) ? 1 : 0; }
    if (cb < 0) { const char* e = getenv("PS3_RSX_CONST_BARRIER"); cb = (e && e[0] == '0') ? 0 : 1; }
    if (!n) return 0;
    const u32 last = noinc ? method : method + (n - 1u) * 4u;
    if (method >= NV4097_SET_TRANSFORM_PROGRAM && last < NV4097_SET_TRANSFORM_PROGRAM + 32u * 4u) {
        for (u32 k = 0; k < n; k++) {
            u32 raw; memcpy(&raw, src + (size_t)k * 4u, 4); const u32 data = __builtin_bswap32(raw);
            u32 wi = state->transform_program_write;
            if (wi < 512 * 4) {
                state->transform_program[wi] = data;
                state->transform_program_write = wi + 1;
                if (wi + 1 > state->transform_program_words)
                    state->transform_program_words = wi + 1;
                state->transform_program_dirty = 1;
            }
            state->shader_dirty = 1;
        }
        return 1;
    }
    if (method >= NV4097_SET_TRANSFORM_CONSTANT && last < NV4097_SET_TRANSFORM_CONSTANT + 64u * 4u &&
        !(method <= NV4097_SET_VERTEX_ATTRIB_OUTPUT_MASK && NV4097_SET_VERTEX_ATTRIB_OUTPUT_MASK <= last) && !vcw) {
        for (u32 k = 0; k < n; k++) {
            u32 raw; memcpy(&raw, src + (size_t)k * 4u, 4); const u32 data = __builtin_bswap32(raw);
            const u32 m = noinc ? method : method + k * 4u;
            if (cb && (s_idx_pending || s_arr_pending)) rsx_flush_index_batch(state);
            u32 reg_offset = (m - NV4097_SET_TRANSFORM_CONSTANT) / 4;
            u32 slot = state->transform_constant_load + (reg_offset >> 2);
            u32 lane = reg_offset & 3;
            s_st_vconst_writes++;
            if (slot > s_st_vconst_max) s_st_vconst_max = slot;
            if (slot >= RSX_MAX_VERTEX_CONSTANTS) s_st_vconst_dropped++;
            if (slot < RSX_MAX_VERTEX_CONSTANTS) {
                float f;
                memcpy(&f, &data, 4);
                state->vertex_constants[slot][lane] = f;
                s_vc_last_be[slot] = state->begin_end_id;
                if (!state->vertex_constants_dirty) {
                    state->vertex_constants_lo = slot;
                    state->vertex_constants_hi = slot;
                    state->vertex_constants_dirty = 1;
                } else {
                    if (slot < state->vertex_constants_lo) state->vertex_constants_lo = slot;
                    if (slot > state->vertex_constants_hi) state->vertex_constants_hi = slot;
                }
            }
        }
        return 1;
    }
    return 0;
}
/* ---- end winport v85b-bulk-methods ---- */

int rsx_process_method(rsx_state* state, u32 method, u32 data)
{
    {   /* winport v64-defer-vtx: WINPORT_RSX_FASTMETHOD=1 -- the high-volume constant / VP-ucode words skip the ~70 compares
         * below (no earlier branch matches these methods; 0x1FF4 = VERTEX_ATTRIB_OUTPUT_MASK stays on its path) */
        static int fm = -1;
        if (fm < 0) { const char* e = getenv("WINPORT_RSX_FASTMETHOD"); fm = (e && e[0] == '1') ? 1 : 0;
                      if (fm) fprintf(stderr, "[FASTM64] rsx_process_method fast path for constants/VP words\n"); }
        if (fm) {
            if (method >= 0x1F00u && method < 0x2000u && method != 0x1FF4u) goto wp64_vconst;
            if (method >= 0x0B80u && method < 0x0C00u) goto wp64_vprog;
        }
    }
    /* Surface configuration (+ os DMA de contexto dos alvos de cor, que ficam
     * fora da faixa 0x200..0x23C: 0x18C, 0x194, 0x1B4, 0x1B8) */
    if ((method >= 0x200 && method <= 0x23C) ||
        method == NV4097_SET_CONTEXT_DMA_COLOR_A || method == NV4097_SET_CONTEXT_DMA_COLOR_B ||
        method == NV4097_SET_CONTEXT_DMA_COLOR_C || method == NV4097_SET_CONTEXT_DMA_COLOR_D)
        return process_surface_method(state, method, data);

    /* Texture methods: 0x1A00..0x1A00 + 16*0x20 - 1 */
    if (method >= 0x1A00 && method < 0x1A00 + RSX_MAX_TEXTURES * 0x20)
        return process_texture_method(state, method, data);

    /* Vertex attribute FORMAT: 0x1740..0x177C */
    if (method >= 0x1740 && method < 0x1740 + RSX_MAX_VERTEX_ATTRIBS * 4)
        return process_vertex_attrib_method(state, method, data);

    /* Vertex attribute OFFSET: 0x1680..0x16BC */
    if (method >= 0x1680 && method < 0x1680 + RSX_MAX_VERTEX_ATTRIBS * 4)
        return process_vertex_attrib_method(state, method, data);

    if (method == NV4097_SET_VERTEX_DATA_BASE_OFFSET) {
        state->vertex_data_base_offset = data;
        state->vertex_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_VERTEX_DATA_BASE_INDEX) {
        state->vertex_data_base_index = data;
        state->vertex_dirty = 1;
        return 0;
    }

    /* Viewport */
    if (method == NV4097_SET_VIEWPORT_HORIZONTAL) {
        state->viewport_x = data & 0xFFFF;
        state->viewport_w = (data >> 16) & 0xFFFF;
        state->viewport_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_VIEWPORT_VERTICAL) {
        state->viewport_y = data & 0xFFFF;
        state->viewport_h = (data >> 16) & 0xFFFF;
        state->viewport_dirty = 1;
        return 0;
    }
    if (method >= NV4097_SET_VIEWPORT_OFFSET && method < NV4097_SET_VIEWPORT_OFFSET + 16u) {
        const u32 vi = (method - NV4097_SET_VIEWPORT_OFFSET) >> 2;
        if (vi < 4u) memcpy(&state->viewport_offset[vi], &data, 4);
        state->viewport_so_seen |= 1u;
        state->viewport_dirty = 1;
        return 0;
    }
    if (method >= NV4097_SET_VIEWPORT_SCALE && method < NV4097_SET_VIEWPORT_SCALE + 16u) {
        const u32 vi = (method - NV4097_SET_VIEWPORT_SCALE) >> 2;
        if (vi < 4u) memcpy(&state->viewport_scale[vi], &data, 4);
        state->viewport_so_seen |= 2u;
        state->viewport_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_CLIP_MIN) {
        memcpy(&state->clip_min, &data, 4);
        state->viewport_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_CLIP_MAX) {
        memcpy(&state->clip_max, &data, 4);
        state->viewport_dirty = 1;
        return 0;
    }

    /* Color mask */
    if (method == NV4097_SET_COLOR_MASK) {
        state->color_mask = data;
        state->color_mask_dirty = 1;
        return 0;
    }

    /* Alpha test */
    if (method == NV4097_SET_ALPHA_TEST_ENABLE) {
        state->alpha_test_enable = data ? 1 : 0;
        state->alpha_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_ALPHA_FUNC) {
        state->alpha_func = data;
        state->alpha_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_ALPHA_REF) {
        state->alpha_ref = data;
        state->alpha_dirty = 1;
        return 0;
    }

    /* Clear */
    if (method == NV4097_SET_COLOR_CLEAR_VALUE) {
        state->color_clear_value = data;
        return 0;
    }
    if (method == NV4097_SET_ZSTENCIL_CLEAR_VALUE) {
        state->zstencil_clear_value = data;
        return 0;
    }
    if (method == NV4097_CLEAR_SURFACE) {
        /* The clear hits the surface bound NOW. Render-target state is otherwise
         * flushed only before a draw, so a backend keeping per-surface targets
         * cleared the PREVIOUS one: E446 per-pass trace -- every display buffer
         * was cleared in the frame right after it was drawn, before being shown,
         * so the screen only ever showed freshly cleared buffers. */
        if (s_backend && state->surface_dirty && s_backend->set_render_target) {
            s_backend->set_render_target(s_backend->userdata, state);
            state->surface_dirty = 0;
        }
        if (s_backend && s_backend->clear) {
            float depth = (float)(state->zstencil_clear_value >> 8) / (float)0xFFFFFF;
            u8 stencil = state->zstencil_clear_value & 0xFF;
            s_backend->clear(s_backend->userdata, data,
                           state->color_clear_value, depth, stencil);
        }
        return 0;
    }

    /* Blend */
    if (method == NV4097_SET_BLEND_ENABLE) {
        state->blend_enable = data ? 1 : 0;
        state->blend_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BLEND_FUNC_SFACTOR) {
        state->blend_sfactor = data;
        state->blend_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BLEND_FUNC_DFACTOR) {
        state->blend_dfactor = data;
        state->blend_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BLEND_EQUATION) {
        state->blend_equation = data;
        state->blend_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BLEND_COLOR) {   /* constant for CONSTANT_COLOR/ALPHA */
        state->blend_color = data;
        state->blend_dirty = 1;
        return 0;
    }

    /* Depth */
    if (method == NV4097_SET_DEPTH_TEST_ENABLE) {
        state->depth_test_enable = data ? 1 : 0;
        state->depth_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_DEPTH_FUNC) {
        state->depth_func = data;
        state->depth_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_DEPTH_MASK) {
        state->depth_mask = data ? 1 : 0;
        state->depth_dirty = 1;
        return 0;
    }

    /* Stencil */
    if (method == NV4097_SET_STENCIL_TEST_ENABLE) {
        state->stencil_test_enable = data ? 1 : 0;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_FUNC) {
        state->stencil_func = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_FUNC_REF) {
        state->stencil_ref = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_FUNC_MASK) {
        state->stencil_mask = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_OP_FAIL) {
        state->stencil_op_fail = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_OP_ZFAIL) {
        state->stencil_op_zfail = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_OP_ZPASS) {
        state->stencil_op_zpass = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_ZMIN_MAX_CONTROL) {
        state->zminmax_control = data;
        state->depth_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_POLY_OFFSET_FILL_ENABLE) {
        state->poly_offset_fill_enable = data ? 1 : 0;
        state->depth_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_POLYGON_OFFSET_SCALE_FACTOR) {
        memcpy(&state->poly_offset_scale, &data, 4);
        state->depth_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_POLYGON_OFFSET_BIAS) {
        memcpy(&state->poly_offset_bias, &data, 4);
        state->depth_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_STENCIL_MASK) {
        state->stencil_write_mask = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_TWO_SIDED_STENCIL_TEST_ENABLE) {
        state->two_sided_stencil_enable = data ? 1 : 0;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BACK_STENCIL_MASK) {
        state->back_stencil_write_mask = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BACK_STENCIL_FUNC) {
        state->back_stencil_func = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BACK_STENCIL_FUNC_REF) {
        state->back_stencil_ref = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BACK_STENCIL_FUNC_MASK) {
        state->back_stencil_mask = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BACK_STENCIL_OP_FAIL) {
        state->back_stencil_op_fail = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BACK_STENCIL_OP_ZFAIL) {
        state->back_stencil_op_zfail = data;
        state->stencil_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_BACK_STENCIL_OP_ZPASS) {
        state->back_stencil_op_zpass = data;
        state->stencil_dirty = 1;
        return 0;
    }

    /* Culling */
    if (method == NV4097_SET_CULL_FACE_ENABLE) {
        state->cull_face_enable = data ? 1 : 0;
        return 0;
    }
    if (method == NV4097_SET_CULL_FACE) {
        state->cull_face = data;
        return 0;
    }
    if (method == NV4097_SET_FRONT_FACE) {
        state->front_face = data;
        return 0;
    }

    /* Shader programs */
    if (method == NV4097_SET_SHADER_PROGRAM) {
        /*
         * Fragment program address register:
         *   [1:0]  DMA context as libgcm writes it: location + 1, i.e.
         *          1 = RSX local, 2 = main (rsx_dispatch.h, RPCS3). An older
         *          note here said "0 = local, 1 = main"; backends that trusted
         *          it read raw-1 (local) programs as flat EAs.
         *   [31:2] offset (4-byte aligned)
         */
        state->shader_program = data;
        state->fragment_program_addr = data & ~0x3u;
        state->shader_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_SHADER_CONTROL) {
        state->shader_control = data;
        state->shader_control_valid = 1;
        state->shader_dirty = 1;
        {
            static int n = 0;
            if (n++ < 8)
                printf("[RSX] SET_SHADER_CONTROL=0x%08X export=%s\n", data,
                       (data & 0x40u) ? "r0" : "h0");
        }
        return 0;
    }
    if (method == NV4097_SET_TRANSFORM_PROGRAM_START) {
        state->transform_program_start = data & 511u;
        state->shader_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_TRANSFORM_PROGRAM_LOAD) {
        /* Vertex program load slot — index into vertex program instruction
         * memory (in whole 4-word instructions). Subsequent
         * NV4097_SET_TRANSFORM_PROGRAM pushes write starting at this slot. */
        state->transform_program_load = data;
        state->transform_program_write = data * 4;   /* word index */
        state->shader_dirty = 1;
        return 0;
    }
    /* NV4097_SET_TRANSFORM_PROGRAM[0..31] — VP instruction words streamed into
     * transform program instruction memory, auto-advancing from the load slot.
     * Captured so the backend can decompile the real vertex program. */
wp64_vprog:   /* winport v64-defer-vtx */
    if (method >= NV4097_SET_TRANSFORM_PROGRAM &&
        method <  NV4097_SET_TRANSFORM_PROGRAM + 32 * 4) {
        u32 wi = state->transform_program_write;
        if (wi < 512 * 4) {
            state->transform_program[wi] = data;
            state->transform_program_write = wi + 1;
            if (wi + 1 > state->transform_program_words)
                state->transform_program_words = wi + 1;
            state->transform_program_dirty = 1;
        }
        state->shader_dirty = 1;
        return 0;
    }
    if (method == NV4097_SET_VERTEX_ATTRIB_OUTPUT_MASK) {
        state->vertex_attrib_output_mask = data;
        return 0;
    }
    if (method == NV4097_SET_TRANSFORM_CONSTANT_LOAD) {
        state->transform_constant_load = data;
        return 0;
    }

    /* NV4097_SET_TRANSFORM_CONSTANT[0..63] — up to 64 dwords (16 vec4s) per
     * command. Each register slot writes to a lane of one vertex constant
     * vec4: vec_index = LOAD + (reg_offset/4), lane = reg_offset%4.
     * The data arrives as a host-endian u32; reinterpret the bits as float
     * because the game's intent is "these 32 bits are a float". The hardware
     * does NOT auto-advance LOAD between commands — games re-issue
     * SET_TRANSFORM_CONSTANT_LOAD before each block. */
wp64_vconst:   /* winport v64-defer-vtx */
    if (method >= NV4097_SET_TRANSFORM_CONSTANT &&
        method <  NV4097_SET_TRANSFORM_CONSTANT + 64 * 4) {
        /* A draw still pending in this BEGIN/END ran with the constants in force
         * when it was issued: flush it BEFORE the write (RPCS3 nv4097.cpp inserts
         * a transform_constant_update_barrier in the draw clause). GoW2 uploads
         * each skinned sub-mesh's bone palette between DRAW_INDEX_ARRAYs of one
         * BEGIN/END; the merged/deferred batch drew with the NEXT palette and the
         * Colossus' shoulder skinned to garbage (skin.w -63/910, 2026-09-21).
         * PS3_RSX_CONST_BARRIER=0 = old behaviour (A/B). */
        static int cb = -1;
        if (cb < 0) { const char* e = getenv("PS3_RSX_CONST_BARRIER"); cb = (e && e[0] == '0') ? 0 : 1; }
        if (cb && (s_idx_pending || s_arr_pending)) rsx_flush_index_batch(state);
        u32 reg_offset = (method - NV4097_SET_TRANSFORM_CONSTANT) / 4;
        u32 slot = state->transform_constant_load + (reg_offset >> 2);
        u32 lane = reg_offset & 3;
        {   /* PS3_TRACE_VCW=<lo>:<hi> (OFF): every write into constants lo..hi */
            static int lo = -2, hi = -1;
            static u32 nlog = 0;
            if (lo == -2) {
                const char* e = getenv("PS3_TRACE_VCW");
                if (!e || sscanf(e, "%d:%d", &lo, &hi) != 2) { lo = -1; hi = -1; }
            }
            if (lo >= 0 && (int)slot >= lo && (int)slot <= hi && nlog < 400000u) {
                nlog++;
                float fv; memcpy(&fv, &data, 4);
                fprintf(stderr, "[VCW] be=%u inbe=%d load=%u m=0x%04X slot=%u.%u = %g\n",
                        state->begin_end_id, state->in_begin_end, state->transform_constant_load,
                        method, slot, lane, fv);
            }
        }
        s_st_vconst_writes++;
        if (slot > s_st_vconst_max) s_st_vconst_max = slot;
        if (slot >= RSX_MAX_VERTEX_CONSTANTS) s_st_vconst_dropped++;
        if (slot < RSX_MAX_VERTEX_CONSTANTS) {
            float f;
            memcpy(&f, &data, 4);
            state->vertex_constants[slot][lane] = f;
            s_vc_last_be[slot] = state->begin_end_id;
            if (!state->vertex_constants_dirty) {
                state->vertex_constants_lo = slot;
                state->vertex_constants_hi = slot;
                state->vertex_constants_dirty = 1;
            } else {
                if (slot < state->vertex_constants_lo) state->vertex_constants_lo = slot;
                if (slot > state->vertex_constants_hi) state->vertex_constants_hi = slot;
            }
        }
        return 0;
    }

    /* Draw */
    if (method == NV4097_SET_BEGIN_END) {
        if (data != 0) {
            rsx_flush_index_batch(state);
            state->primitive_type = data;
            state->in_begin_end = 1;
            state->begin_end_id++;
            state->begin_end_packet = 0;

            /* Flush dirty state to backend before drawing */
            if (s_backend) {
                if (state->surface_dirty && s_backend->set_render_target)
                    s_backend->set_render_target(s_backend->userdata, state);
                if (state->viewport_dirty && s_backend->set_viewport)
                    s_backend->set_viewport(s_backend->userdata, state);
                if (state->blend_dirty && s_backend->set_blend)
                    s_backend->set_blend(s_backend->userdata, state);
                if ((state->depth_dirty || state->stencil_dirty) && s_backend->set_depth_stencil)
                    s_backend->set_depth_stencil(s_backend->userdata, state);
                if (state->color_mask_dirty && s_backend->set_color_mask)
                    s_backend->set_color_mask(s_backend->userdata, state);
                if (state->alpha_dirty && s_backend->set_alpha_test)
                    s_backend->set_alpha_test(s_backend->userdata, state);
                if (state->shader_dirty && s_backend->set_shader)
                    s_backend->set_shader(s_backend->userdata, state);
                if (state->vertex_dirty && s_backend->set_vertex_attribs)
                    s_backend->set_vertex_attribs(s_backend->userdata, state);

                /* Bind dirty textures */
                if (state->texture_dirty && s_backend->bind_texture) {
                    for (u32 i = 0; i < RSX_MAX_TEXTURES; i++) {
                        if (state->textures[i].dirty) {
                            s_backend->bind_texture(s_backend->userdata, i, &state->textures[i]);
                            state->textures[i].dirty = 0;
                        }
                    }
                }

                state->surface_dirty = 0;
                state->viewport_dirty = 0;
                state->blend_dirty = 0;
                state->depth_dirty = 0;
                state->stencil_dirty = 0;
                state->color_mask_dirty = 0;
                state->alpha_dirty = 0;
                state->shader_dirty = 0;
                state->vertex_dirty = 0;
                state->texture_dirty = 0;
            }
            {
                float clear_d = (float)(state->zstencil_clear_value >> 8) /
                                (float)0xFFFFFF;
                rsx_depth_dump_begin(state->begin_end_id,
                                     state->depth_test_enable,
                                     state->depth_func,
                                     state->depth_mask,
                                     state->clip_min, state->clip_max,
                                     clear_d);
            }
        } else {
            rsx_flush_index_batch(state);
            state->in_begin_end = 0;
        }
        return 0;
    }

    if (method == NV4097_DRAW_ARRAYS) {
        u32 first = data & 0xFFFFFF;
        u32 count = ((data >> 24) & 0xFF) + 1;
        if (rsx_coalesce_arrays_on() && state->in_begin_end) {
            if (s_idx_pending) rsx_flush_index_batch(state);
            if (s_arr_pending && s_arr_begin == state->begin_end_id &&
                rsx_draw_batch_continues(state->primitive_type, first, count,
                                         s_arr_prim, s_arr_first, s_arr_count)) {
                s_arr_count += count;
                return 0;
            }
            rsx_flush_array_batch();
            s_arr_first = first;
            s_arr_count = count;
            s_arr_prim = state->primitive_type;
            s_arr_begin = state->begin_end_id;
            s_arr_pending = 1;
            return 0;
        }
        rsx_flush_index_batch(state);
        if (s_backend && s_backend->draw_arrays) {
            s_backend->draw_arrays(s_backend->userdata, state->primitive_type, first, count);
            /* Task 2 (bringup-instrumentacao-fase0): FIRST_DRAW -- primeiro
             * draw submetido ao backend RSX. Generico: dispara para qualquer
             * backend registado (d3d12/metal/null/sdl/vulkan) porque o
             * dispatch e' pela vtable rsx_backend, nao por backend concreto.
             * Correcao pos-Task 2 (review): o comentario original afirmava
             * "nao dispara em headless PS3_NO_RSX=1 (s_backend fica NULL)" --
             * falso, refutado pelos runs reais (in-boot, run 5 de
             * .superpowers/sdd/task-2-report.md, "prim=6 first=0 count=4
             * (indexed)"). PS3_NO_RSX so' desliga a janela escolhida por
             * gow2-recomp/boot_macos.cpp (pick_backend() -> Backend::None);
             * quem regista s_backend aqui e' rsx_bridge_mode()
             * (libs/video/cellGcmSys.c), um mecanismo INDEPENDENTE que le
             * PS3_RSX_BACKEND por si proprio -- ver o comentario junto ao
             * FIRST_PRESENT em cellGcmSys.c para a cadeia completa (ordem de
             * sourcing do env_gow2.sh deixa PS3_RSX_BACKEND=metal definido
             * mesmo em runs "headless"). */
            ps3_checkpoint_fire(PS3_CP_FIRST_DRAW, "rsx_process_method",
                               "prim=%u first=%u count=%u", state->primitive_type, first, count);
            /* Task 5 (bringup-instrumentacao-fase0): RSX DRAW. Sem lock --
             * rsx_process_method corre na thread do jogo, dentro do parse
             * sincrono da FIFO (rsx_process_fifo em cellGcmSys.c), sob o
             * giant lock ambiente da PPU; nenhum lock proprio deste ficheiro. */
            PS3_TRACE(PS3_TS_RSX, "rsx_process_method", "DRAW",
                      "prim=%u first=%u count=%u indexed=0", state->primitive_type, first, count);
        }
        return 0;
    }

    if (method == NV4097_SET_INDEX_ARRAY_ADDRESS) {
        /* 0x181C IDXBUF_OFFSET — byte offset into index location. */
        state->index_array_addr = data;
        return 0;
    }
    if (method == NV4097_SET_INDEX_ARRAY_DMA) {
        /* 0x1820 IDXBUF_FORMAT — location[0:3] type[4:11]
         * (envytools/rsx_dispatch: type 0 = u32, 1 = u16;
         *  location 0 = RSX local, 1 = main — CELL_GCM_LOCATION_*). */
        state->index_location = data & 0xFu;
        state->index_is_16bit = (((data >> 4) & 0xFFu) == 1u) ? 1 : 0;
        return 0;
    }

    if (method == NV4097_DRAW_INDEX_ARRAY) {
        /*
         * 0x1824 VB_INDEX_BATCH (real indexed draw):
         *   [23:0]  first index
         *   [31:24] count - 1
         * Observed GoW2 legal: val=0x03000000 → first=0 count=4 (strip quad).
         * Count is 8 bits → max 256 per FIFO word. Merge only packets of the
         * same begin_end_id (not merely same IB).
         */
        u32 index_offset = data & 0xFFFFFF;
        u32 count = ((data >> 24) & 0xFF) + 1;
        rsx_flush_array_batch();
        state->begin_end_packet++;
        if (rsx_coalesce_begin_on() && state->in_begin_end) {
            u32 prim = state->primitive_type;
            u32 bid = state->begin_end_id;
            if (s_idx_pending && s_idx_begin == bid &&
                rsx_index_batch_continues(prim, index_offset, count,
                                          s_idx_prim, s_idx_first, s_idx_count)) {
                s_idx_count += count;
                if (s_idx_pkts < RSX_IDX_PKTS_MAX) {
                    s_idx_pkt_first[s_idx_pkts] = index_offset;
                    s_idx_pkt_count[s_idx_pkts] = count;
                    s_idx_pkts++;
                }
                return 0;
            }
            rsx_flush_index_batch(state);
            s_idx_first = index_offset;
            s_idx_count = count;
            s_idx_prim = prim;
            s_idx_begin = bid;
            s_idx_pending = 1;
            s_idx_pkts = 1;
            s_idx_pkt_first[0] = index_offset;
            s_idx_pkt_count[0] = count;
            return 0;
        }
        s_st_logical++;
        s_st_packets++;
        s_st_indices += count;
        if (state->primitive_type == RSX_PRIMITIVE_TRIANGLES && (count % 3u) != 0u)
            s_st_tri_rem++;
        if (s_backend && s_backend->draw_indexed) {
            s_backend->draw_indexed(s_backend->userdata, state->primitive_type,
                                    index_offset, count);
            /* Task 2: FIRST_DRAW -- mesmo checkpoint do draw nao-indexado
             * acima (once-guard partilhado); este e' o caminho indexado. */
            ps3_checkpoint_fire(PS3_CP_FIRST_DRAW, "rsx_process_method",
                               "prim=%u first=%u count=%u indexed=1",
                               state->primitive_type, index_offset, count);
            /* Task 5: RSX DRAW (caminho indexado). Sem lock -- ver a nota
             * igual junto ao DRAW nao-indexado acima. */
            PS3_TRACE(PS3_TS_RSX, "rsx_process_method", "DRAW",
                      "prim=%u first=%u count=%u indexed=1",
                      state->primitive_type, index_offset, count);
        }
        return 0;
    }

    /* Scissor */
    if (method == NV4097_SET_SCISSOR_HORIZONTAL) {
        state->scissor_x = data & 0xFFFF;
        state->scissor_w = (data >> 16) & 0xFFFF;
        return 0;
    }
    if (method == NV4097_SET_SCISSOR_VERTICAL) {
        state->scissor_y = data & 0xFFFF;
        state->scissor_h = (data >> 16) & 0xFFFF;
        return 0;
    }

    /* Unrecognized method — log in debug builds */
#ifndef NDEBUG
    static int s_unknown_count = 0;
    if (s_unknown_count < 50) {
        printf("[RSX] unknown method 0x%04X = 0x%08X\n", method, data);
        s_unknown_count++;
    }
#endif

    return -1;
}

/* ---------------------------------------------------------------------------
 * Command buffer parsing
 * -----------------------------------------------------------------------*/

int rsx_process_command_buffer(rsx_state* state, const u32* buf, u32 size)
{
    int methods_processed = 0;
    u32 pos = 0;
    u32 count = size / 4; /* size in dwords */

    while (pos < count) {
        u32 header = buf[pos++];
        u32 type = (header >> 29) & 0x7;

        if (type == 0 || type == 2) {
            /* Increasing or non-increasing method */
            u32 method = (header >> 2) & 0x7FF;
            method <<= 2; /* method addresses are dword-aligned */
            u32 num_data = (header >> 18) & 0x7FF;
            int increasing = (type == 0);

            for (u32 i = 0; i < num_data && pos < count; i++) {
                u32 data = buf[pos++];
                u32 m = increasing ? (method + i * 4) : method;
                rsx_process_method(state, m, data);
                methods_processed++;
            }
        } else if (type == 1) {
            /* Jump — change command buffer read position */
            /* In recomp context, this is handled by the caller */
            break;
        } else {
            /* Unknown type, skip */
            break;
        }
    }

    return methods_processed;
}
