/*
 * ps3recomp - Track B capture-replay harness (macOS / Metal, arm64)
 *
 * Mac sibling of replay_main.c (Windows/D3D12): same .rxs container format,
 * same guest-memory arenas, same NV4097 dispatch via ../rsx_dispatch.c/.h
 * (reused as-is, unmodified), same stage-1/2 fallback math. Only the GPU
 * backend is swapped: D3D12 -> a headless Metal offscreen renderer. See
 * replay_main.c's top-of-file comment for the full stage 1-4 plan; THIS
 * file implements only:
 *
 *   stage 1: CLEAR_BUFFERS with the capture's clear color.
 *   stage 2: vertex-color geometry -- ATTR0 (position) and ATTR3 (color)
 *            fetched from the capture's guest-memory blocks at draw time
 *            (big-endian -> host float), quads/strips/fans expanded to
 *            triangle lists on the CPU. No vertex program execution: when
 *            no transform constants (c0..c3) were uploaded, positions are
 *            assumed to already be in RSX window/viewport pixel space and
 *            are mapped to clip space with the INVERSE of the viewport
 *            scale/translate -- see sink_end() below, which mirrors
 *            replay_main.c's sink_end() "have_mvp" fallback bit for bit
 *            (same 2/w, -2/h, -1, 1 matrix, same column-vector layout).
 *
 * Stage 3 (texturing) and stage 4 (NV40 shader translation) are explicitly
 * out of scope for this file -- see docs referenced from the report this
 * milestone produced for what they'd need.
 *
 * Rendering targets are offscreen MTLTextures (MTLStorageModeShared, no
 * window/CAMetalLayer): each flip reads the relevant surface back with
 * -getBytes:... and writes a PNG next to the other frames. The PNG encoder
 * is a small self-contained writer (stored/uncompressed zlib deflate
 * blocks -- valid per RFC1950/1951, no zlib or ImageIO dependency).
 *
 * Build:
 *   clang -fobjc-arc -std=c17 -I ../../../include \
 *       replay_mac.m ../rsx_dispatch.c \
 *       -framework Metal -framework Foundation -framework CoreGraphics \
 *       -o replay_mac
 *
 * Usage:
 *   replay_mac <stream.rxs> [--out <dir>]
 */

#ifndef __APPLE__
#error replay_mac is macOS/Metal-only (see replay_main.c for the Windows/D3D12 harness)
#endif

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "../rsx_dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------------------
 * .rxs stream container (ported from replay_main.c ~L73-170; format/layout
 * unchanged -- see tools/rrc_export.py / replay_main.c's rxs_load() comment)
 * -----------------------------------------------------------------------*/

typedef struct {
    u32 location, offset, size, data_off;
} rxs_block;

typedef struct {
    u32 w, h, pitch, offset;
} rxs_display_buf;

typedef struct {
    u32  n_blocks, n_records, reg_words, vp_words, disp_w, disp_h;
    u32  disp_count;
    rxs_display_buf disp[8];
    u32* regs;
    u32* vp;
    rxs_block* blocks;
    u8*  data;      /* concatenated block payloads */
    u32* records;   /* n_records * 2 words         */
} rxs_stream;

static void* xmalloc_ge1(size_t n)
{
    void* p = malloc(n ? n : 1);
    if (p && n == 0)
        memset(p, 0, 1);
    return p;
}

static int rxs_load(const char* path, rxs_stream* s)
{
    FILE* f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); return -1; }

    u32 hdr[8];
    if (fread(hdr, 4, 8, f) != 8 || memcmp(hdr, "RXS1", 4) != 0 || hdr[1] != 2) {
        printf("%s: not an RXS1 v2 stream (re-run gen_synthetic_rxs.py / tools/rrc_export.py)\n", path);
        fclose(f);
        return -1;
    }
    s->n_blocks  = hdr[2];
    s->n_records = hdr[3];
    s->reg_words = hdr[4];
    s->vp_words  = hdr[5];
    s->disp_w    = hdr[6];
    s->disp_h    = hdr[7];
    if (fread(&s->disp_count, 4, 1, f) != 1 ||
        fread(s->disp, sizeof(rxs_display_buf), 8, f) != 8) {
        printf("%s: truncated display table\n", path);
        fclose(f);
        return -1;
    }

    s->regs   = xmalloc_ge1((size_t)s->reg_words * 4);
    s->vp     = xmalloc_ge1((size_t)s->vp_words * 4);
    s->blocks = xmalloc_ge1((size_t)s->n_blocks * sizeof(rxs_block));
    if (fread(s->regs, 4, s->reg_words, f) != s->reg_words ||
        fread(s->vp, 4, s->vp_words, f) != s->vp_words ||
        fread(s->blocks, sizeof(rxs_block), s->n_blocks, f) != s->n_blocks) {
        printf("%s: truncated header sections\n", path);
        fclose(f);
        return -1;
    }
    size_t data_size = 0;
    for (u32 i = 0; i < s->n_blocks; i++) {
        size_t end = (size_t)s->blocks[i].data_off + s->blocks[i].size;
        if (end > data_size) data_size = end;
    }
    s->data    = xmalloc_ge1(data_size);
    s->records = xmalloc_ge1((size_t)s->n_records * 8);
    if (fread(s->data, 1, data_size, f) != data_size ||
        fread(s->records, 8, s->n_records, f) != s->n_records) {
        printf("%s: truncated data/records\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Guest memory arenas (local VRAM + main IO), populated by apply-block
 * records. Ported verbatim from replay_main.c (VirtualAlloc -> calloc).
 * -----------------------------------------------------------------------*/

#define ARENA_SIZE (256u * 1024 * 1024)

static u8* g_arena[2]; /* [RSX_LOCATION_LOCAL], [RSX_LOCATION_MAIN] */

static const u8* guest_ptr(u32 location, u32 offset)
{
    if (location > 1 || offset >= ARENA_SIZE)
        return NULL;
    return g_arena[location] + offset;
}

static void apply_block(const rxs_stream* s, u32 index)
{
    if (index >= s->n_blocks)
        return;
    const rxs_block* b = &s->blocks[index];
    if (b->location > 1 || (u64)b->offset + b->size > ARENA_SIZE)
        return;
    memcpy(g_arena[b->location] + b->offset, s->data + b->data_off, b->size);
}

/* RSX primitive ids (gcm public constants; same values as replay_main.c) */
#define PRIM_TRIANGLES      5
#define PRIM_TRIANGLE_STRIP 6
#define PRIM_TRIANGLE_FAN   7
#define PRIM_QUADS          8

/* Fixed-function vertex for the stage-1/2 fallback pipeline: position +
 * vertex color only (ATTR0 + ATTR3) -- texcoord/the other 13 attributes are
 * stage 3/4 territory and out of scope here. */
typedef struct { float x, y, z; float r, g, b, a; } rmac_vertex;

/* ---------------------------------------------------------------------------
 * Minimal self-contained PNG writer (new: no zlib/ImageIO dependency).
 * IDAT is a valid zlib stream built from *stored* (uncompressed) deflate
 * blocks, so any PNG decoder reads it -- only the write side skips a real
 * compressor since a headless test PNG has no size constraint.
 * -----------------------------------------------------------------------*/

static u32 g_crc_table[256];
static int g_crc_ready;

static void crc32_init(void)
{
    for (u32 n = 0; n < 256; n++) {
        u32 c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_table[n] = c;
    }
    g_crc_ready = 1;
}

static u32 crc32_update(u32 crc, const u8* buf, size_t len)
{
    if (!g_crc_ready) crc32_init();
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = g_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static u32 adler32_calc(const u8* data, size_t len)
{
    u32 a = 1, b = 0;
    const u32 MOD = 65521;
    size_t i = 0;
    while (i < len) {
        size_t chunk = (len - i < 5552) ? (len - i) : 5552;
        for (size_t j = 0; j < chunk; j++, i++) {
            a += data[i];
            b += a;
        }
        a %= MOD;
        b %= MOD;
    }
    return (b << 16) | a;
}

static void put_u32be(u8* p, u32 v)
{
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

static void write_chunk(FILE* f, const char* type, const u8* data, u32 len)
{
    u8 lenbuf[4];
    put_u32be(lenbuf, len);
    fwrite(lenbuf, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len)
        fwrite(data, 1, len, f);
    u32 crc = crc32_update(0, (const u8*)type, 4);
    if (len)
        crc = crc32_update(crc, data, len);
    u8 crcbuf[4];
    put_u32be(crcbuf, crc);
    fwrite(crcbuf, 1, 4, f);
}

/* Writes w*h RGBA8 pixels (tightly packed, row-major, top-down) as PNG. */
static int write_png(const char* path, const u8* rgba, u32 w, u32 h)
{
    FILE* f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return -1; }

    static const u8 sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);

    u8 ihdr[13];
    put_u32be(ihdr + 0, w);
    put_u32be(ihdr + 4, h);
    ihdr[8]  = 8;  /* bit depth        */
    ihdr[9]  = 6;  /* color type: RGBA */
    ihdr[10] = 0;  /* compression      */
    ihdr[11] = 0;  /* filter           */
    ihdr[12] = 0;  /* interlace        */
    write_chunk(f, "IHDR", ihdr, sizeof(ihdr));

    /* raw scanlines: 1 filter-type byte (0 = None) + w*4 pixel bytes, per row */
    const size_t row_bytes = (size_t)w * 4 + 1;
    const size_t raw_size = row_bytes * h;
    u8* raw = malloc(raw_size ? raw_size : 1);
    for (u32 y = 0; y < h; y++) {
        u8* row = raw + (size_t)y * row_bytes;
        row[0] = 0;
        memcpy(row + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
    }

    /* zlib stream: 2-byte header + stored deflate blocks (<=65535B each) +
     * big-endian Adler32 of the raw (pre-"compression") bytes */
    const size_t max_deflate = raw_size + (raw_size / 65535 + 1) * 5 + 6;
    u8* zbuf = malloc(max_deflate);
    size_t zp = 0;
    zbuf[zp++] = 0x78; zbuf[zp++] = 0x01;  /* CMF/FLG: deflate, 32K window, fastest */
    size_t off = 0;
    do {
        size_t block = raw_size - off;
        if (block > 65535) block = 65535;
        const u8 bfinal = (off + block >= raw_size) ? 1 : 0;
        zbuf[zp++] = bfinal;   /* stored block: BFINAL | BTYPE(00), byte-aligned */
        const u16 len = (u16)block;
        const u16 nlen = (u16)~len;
        zbuf[zp++] = (u8)(len & 0xFF);  zbuf[zp++] = (u8)(len >> 8);
        zbuf[zp++] = (u8)(nlen & 0xFF); zbuf[zp++] = (u8)(nlen >> 8);
        memcpy(zbuf + zp, raw + off, block);
        zp += block;
        off += block;
    } while (off < raw_size);
    const u32 adler = adler32_calc(raw, raw_size);
    put_u32be(zbuf + zp, adler); zp += 4;

    write_chunk(f, "IDAT", zbuf, (u32)zp);
    write_chunk(f, "IEND", NULL, 0);

    free(raw);
    free(zbuf);
    fclose(f);
    printf("[gpu] wrote %s\n", path);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Metal: headless offscreen renderer (replaces replay_main.c's D3D12 gpu_t)
 * -----------------------------------------------------------------------*/

static const char* k_msl_source =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VSIn {\n"
    "    float3 position [[attribute(0)]];\n"
    "    float4 color    [[attribute(1)]];\n"
    "};\n"
    "struct VSOut {\n"
    "    float4 position [[position]];\n"
    "    float4 color;\n"
    "};\n"
    "struct Xform {\n"
    "    float4 c0, c1, c2, c3;\n"
    "};\n"
    "vertex VSOut replay_vs(VSIn in [[stage_in]], constant Xform& xf [[buffer(1)]]) {\n"
    "    VSOut out;\n"
    "    out.position = xf.c0 * in.position.x + xf.c1 * in.position.y\n"
    "                  + xf.c2 * in.position.z + xf.c3 * 1.0;\n"
    "    out.color = in.color;\n"
    "    return out;\n"
    "}\n"
    "fragment float4 replay_fs(VSOut in [[stage_in]]) {\n"
    "    return in.color;\n"
    "}\n";

static id<MTLDevice>              g_device;
static id<MTLCommandQueue>        g_queue;
static id<MTLLibrary>             g_library;
static id<MTLRenderPipelineState> g_pso;

static u32 g_width, g_height;

/* The capture may render through more than one target before a flip; model
 * each distinct (location, color0 offset) as its own offscreen texture,
 * same identity rule as replay_main.c's surface cache (stage-3 render-to-
 * texture chains and their multi-surface dump are out of scope here, so
 * this cache only needs to be big enough for a handful of targets). */
#define MAX_SURFACES 8
static id<MTLTexture> g_surf_tex[MAX_SURFACES];
static u32            g_surf_loc[MAX_SURFACES];
static u32            g_surf_off[MAX_SURFACES];
static u32             g_n_surfaces;

static int gpu_init(void)
{
    g_device = MTLCreateSystemDefaultDevice();
    if (!g_device) {
        printf("[gpu] no Metal device available\n");
        return -1;
    }
    printf("[gpu] device: %s\n", g_device.name.UTF8String);

    g_queue = [g_device newCommandQueue];
    if (!g_queue) {
        printf("[gpu] command queue create failed\n");
        return -1;
    }

    NSError* err = nil;
    NSString* src = [NSString stringWithUTF8String:k_msl_source];
    g_library = [g_device newLibraryWithSource:src options:nil error:&err];
    if (!g_library) {
        printf("[gpu] MSL compile failed: %s\n", err ? err.localizedDescription.UTF8String : "?");
        return -1;
    }

    id<MTLFunction> vfn = [g_library newFunctionWithName:@"replay_vs"];
    id<MTLFunction> ffn = [g_library newFunctionWithName:@"replay_fs"];
    if (!vfn || !ffn) {
        printf("[gpu] MSL entry points missing\n");
        return -1;
    }

    MTLVertexDescriptor* vdesc = [MTLVertexDescriptor vertexDescriptor];
    vdesc.attributes[0].format = MTLVertexFormatFloat3;   /* ATTR0 position */
    vdesc.attributes[0].offset = 0;
    vdesc.attributes[0].bufferIndex = 0;
    vdesc.attributes[1].format = MTLVertexFormatFloat4;   /* ATTR3 color    */
    vdesc.attributes[1].offset = 12;
    vdesc.attributes[1].bufferIndex = 0;
    vdesc.layouts[0].stride = sizeof(rmac_vertex);
    vdesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* pdesc = [MTLRenderPipelineDescriptor new];
    pdesc.vertexFunction = vfn;
    pdesc.fragmentFunction = ffn;
    pdesc.vertexDescriptor = vdesc;
    pdesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

    g_pso = [g_device newRenderPipelineStateWithDescriptor:pdesc error:&err];
    if (!g_pso) {
        printf("[gpu] PSO create failed: %s\n", err ? err.localizedDescription.UTF8String : "?");
        return -1;
    }
    return 0;
}

/* Find-or-create the offscreen texture for (location, offset), sized to the
 * capture's display dimensions. Shared storage: Apple Silicon unified
 * memory lets the CPU read GPU-written texels back with no blit/sync step
 * once the command buffer has completed (see CLAUDE.md's Mac port guide). */
static u32 surface_get(u32 location, u32 offset)
{
    for (u32 i = 0; i < g_n_surfaces; i++)
        if (g_surf_loc[i] == location && g_surf_off[i] == offset)
            return i;
    if (g_n_surfaces >= MAX_SURFACES) {
        printf("[gpu] surface cache full; reusing surface 0\n");
        return 0;
    }

    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                             width:g_width
                                                            height:g_height
                                                         mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [g_device newTextureWithDescriptor:td];
    if (!tex) {
        printf("[gpu] surface create failed\n");
        return 0;
    }

    const u32 idx = g_n_surfaces;
    g_surf_tex[idx] = tex;
    g_surf_loc[idx] = location;
    g_surf_off[idx] = offset;
    g_n_surfaces++;
    return idx;
}

/* Surface currently selected as color target 0 by the register state. */
static u32 current_surface(const rsx_dispatch* rsx)
{
    rsx_dsp_surface sf;
    rsx_dsp_get_surface(rsx, &sf);
    return surface_get(sf.color_location[0], sf.color_offset[0]);
}

static void metal_clear(u32 surf_idx, float r, float g, float b, float a)
{
    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture = g_surf_tex[surf_idx];
    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor = MTLClearColorMake(r, g, b, a);

    id<MTLCommandBuffer> cmd = [g_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
    [enc endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
}

/* Uploads `tri` (n_tri*3 vertices, already CPU-expanded to a triangle list)
 * to a shared MTLBuffer and draws it into surf_idx with loadAction=Load so
 * prior clears/draws on the same target are preserved. `cols` is the 4x4
 * (column-major, 16 float) fallback transform -- see sink_end(). */
static void metal_draw_triangles(u32 surf_idx, const rmac_vertex* tri, u32 n_tri, const float cols[16])
{
    if (!n_tri)
        return;

    id<MTLBuffer> vb = [g_device newBufferWithBytes:tri
                                              length:(NSUInteger)n_tri * sizeof(rmac_vertex)
                                             options:MTLResourceStorageModeShared];
    if (!vb) {
        printf("[gpu] vertex buffer alloc failed (%u tris)\n", n_tri);
        return;
    }

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture = g_surf_tex[surf_idx];
    rpd.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> cmd = [g_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
    [enc setRenderPipelineState:g_pso];
    [enc setCullMode:MTLCullModeNone];
    [enc setTriangleFillMode:MTLTriangleFillModeFill];
    [enc setViewport:(MTLViewport){0.0, 0.0, (double)g_width, (double)g_height, 0.0, 1.0}];
    [enc setVertexBuffer:vb offset:0 atIndex:0];
    [enc setVertexBytes:cols length:16 * sizeof(float) atIndex:1];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:n_tri];
    [enc endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
}

static void metal_readback_png(u32 surf_idx, const char* path)
{
    id<MTLTexture> tex = g_surf_tex[surf_idx];
    const u32 w = (u32)tex.width;
    const u32 h = (u32)tex.height;
    const size_t pitch = (size_t)w * 4;
    u8* px = malloc(pitch * h);
    if (!px) {
        printf("[gpu] readback alloc failed\n");
        return;
    }
    MTLRegion region = MTLRegionMake2D(0, 0, w, h);
    [tex getBytes:px bytesPerRow:pitch fromRegion:region mipmapLevel:0];
    write_png(path, px, w, h);
    free(px);
}

/* ---------------------------------------------------------------------------
 * Vertex fetch (ported from replay_main.c's be_f32/be_f16/fetch_attr):
 * decode ATTR0 (position) and ATTR3 (color) straight out of guest memory,
 * per the layout rsx_dsp_get_vertex_attr() decoded from the capture.
 * -----------------------------------------------------------------------*/

static float be_f32(const u8* p)
{
    u32 v = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static float be_f16(const u8* p)
{
    const u16 h = (u16)((p[0] << 8) | p[1]);
    const u32 sign = (u32)(h >> 15) << 31;
    u32 exp = (h >> 10) & 0x1F;
    u32 man = h & 0x3FF;
    u32 out;
    if (exp == 0) {
        out = sign; /* denormals flushed */
    } else if (exp == 31) {
        out = sign | 0x7F800000 | (man << 13);
    } else {
        out = sign | ((exp + 112) << 23) | (man << 13);
    }
    float f;
    memcpy(&f, &out, 4);
    return f;
}

static int fetch_attr(const rsx_dispatch* rsx, u32 index, u32 base_offset, u32 vert, float out[4])
{
    rsx_dsp_vertex_attr a;
    rsx_dsp_get_vertex_attr(rsx, index, &a);
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    if (!a.type || !a.size)
        return 0;

    const u32 addr = base_offset + a.offset + vert * a.stride;
    const u8* p = guest_ptr(a.location, addr);
    if (!p)
        return 0;

    for (u32 c = 0; c < a.size && c < 4; c++) {
        switch (a.type) {
        case RSX_VTX_TYPE_FLOAT:  out[c] = be_f32(p + c * 4); break;
        case RSX_VTX_TYPE_HALF:   out[c] = be_f16(p + c * 2); break;
        case RSX_VTX_TYPE_UNORM8: out[c] = p[c] / 255.0f; break;
        case RSX_VTX_TYPE_UINT8:  out[c] = (float)p[c]; break;
        case RSX_VTX_TYPE_SNORM16: {
            const s16 v = (s16)((p[c * 2] << 8) | p[c * 2 + 1]);
            out[c] = v / 32767.0f;
            break;
        }
        case RSX_VTX_TYPE_SINT16: {
            const s16 v = (s16)((p[c * 2] << 8) | p[c * 2 + 1]);
            out[c] = (float)v;
            break;
        }
        default:
            return 0; /* cmp32 etc: unhandled */
        }
    }
    return 1;
}

/* Fetches ATTR0 into position and ATTR3 into color for one vertex index.
 * Returns 0 only when the position stream itself is unavailable (mirrors
 * replay_main.c's fetch_one(): a missing/disabled diffuse just falls back
 * to its VTX_ATTR_4F default, forced to white when that default was never
 * written, so untinted draws show geometry instead of black). */
static int fetch_one(const rsx_dispatch* rsx, u32 base, u32 vert, rmac_vertex* out)
{
    float pos[4], col[4];
    if (!fetch_attr(rsx, 0, base, vert, pos))
        return 0;

    if (!fetch_attr(rsx, 3, base, vert, col)) {
        rsx_dsp_vertex_default(rsx, 3, col);
        if (col[0] == 0.0f && col[1] == 0.0f && col[2] == 0.0f && col[3] == 1.0f)
            col[0] = col[1] = col[2] = 1.0f;
    }

    out->x = pos[0]; out->y = pos[1]; out->z = pos[2];
    out->r = col[0]; out->g = col[1]; out->b = col[2]; out->a = col[3];
    return 1;
}

/* ---------------------------------------------------------------------------
 * Dispatcher sink: clears + draw accumulation (ported from replay_main.c's
 * sink_clear/begin/draw_arrays/draw_index_array/end/flip)
 * -----------------------------------------------------------------------*/

typedef struct {
    const char* outdir;
    u32 frame_no;
    u32 clear_count, draw_count, drawn_verts;
    u32 skipped_prims[16];

    /* Batches accumulated inside one begin/end; vertex data is fetched at
     * END (not at the batch method) since the capture applies the draw's
     * memory blocks between the batch and BEGIN_END(0). */
    struct { u32 first, count; } arr[256], idx[256];
    u32 n_arr, n_idx;

    rmac_vertex* verts;
    u32 n_verts, cap_verts;
    int fetch_ok;

    const rxs_stream* stream;
} sink_ctx;

static void push_vert(sink_ctx* c, const rmac_vertex* v)
{
    if (c->n_verts >= c->cap_verts) {
        c->cap_verts = c->cap_verts ? c->cap_verts * 2 : 4096;
        c->verts = realloc(c->verts, c->cap_verts * sizeof(rmac_vertex));
    }
    c->verts[c->n_verts++] = *v;
}

static void sink_clear(void* user, const rsx_dispatch* rsx, u32 mask)
{
    sink_ctx* c = user;
    c->clear_count++;
    if (!(mask & (RSX_CLEAR_COLOR_R | RSX_CLEAR_COLOR_G | RSX_CLEAR_COLOR_B | RSX_CLEAR_COLOR_A)))
        return; /* depth/stencil-only clear; no depth buffer in this milestone */

    const u32 argb = rsx_dsp_clear_color(rsx);
    const float r = ((argb >> 16) & 0xFF) / 255.0f;
    const float g = ((argb >> 8) & 0xFF) / 255.0f;
    const float b = (argb & 0xFF) / 255.0f;
    const float a = ((argb >> 24) & 0xFF) / 255.0f;

    const u32 si = current_surface(rsx);
    printf("[replay] clear #%u mask=0x%02X argb=0x%08X surface=%u\n",
           c->clear_count, mask, argb, si);
    metal_clear(si, r, g, b, a);
}

static void sink_begin(void* user, const rsx_dispatch* rsx, u32 prim)
{
    (void)rsx; (void)prim;
    sink_ctx* c = user;
    c->n_arr = c->n_idx = 0;
    c->n_verts = 0;
    c->fetch_ok = 1;
}

static void sink_draw_arrays(void* user, const rsx_dispatch* rsx, u32 first, u32 count)
{
    (void)rsx;
    sink_ctx* c = user;
    if (c->n_arr < 256) {
        c->arr[c->n_arr].first = first;
        c->arr[c->n_arr].count = count;
        c->n_arr++;
    }
}

static void sink_draw_index_array(void* user, const rsx_dispatch* rsx, u32 first, u32 count)
{
    (void)rsx;
    sink_ctx* c = user;
    if (c->n_idx < 256) {
        c->idx[c->n_idx].first = first;
        c->idx[c->n_idx].count = count;
        c->n_idx++;
    }
}

static void fetch_batches(sink_ctx* c, const rsx_dispatch* rsx)
{
    const u32 base = rsx_dsp_vertex_data_base_offset(rsx);
    const u32 base_index = rsx_dsp_vertex_data_base_index(rsx);

    for (u32 r = 0; r < c->n_arr && c->fetch_ok; r++) {
        for (u32 i = 0; i < c->arr[r].count && c->fetch_ok; i++) {
            rmac_vertex v;
            if (!fetch_one(rsx, base, base_index + c->arr[r].first + i, &v)) {
                c->fetch_ok = 0;
                break;
            }
            push_vert(c, &v);
        }
    }

    if (!c->n_idx)
        return;
    rsx_dsp_index_array ia;
    rsx_dsp_get_index_array(rsx, &ia);
    for (u32 r = 0; r < c->n_idx && c->fetch_ok; r++) {
        for (u32 i = 0; i < c->idx[r].count && c->fetch_ok; i++) {
            const u32 esz = ia.is_u32 ? 4 : 2;
            const u8* ip = guest_ptr(ia.location, ia.offset + (c->idx[r].first + i) * esz);
            if (!ip) {
                c->fetch_ok = 0;
                break;
            }
            const u32 index = ia.is_u32
                ? (((u32)ip[0] << 24) | ((u32)ip[1] << 16) | ((u32)ip[2] << 8) | ip[3])
                : (u32)((ip[0] << 8) | ip[1]);
            rmac_vertex v;
            if (!fetch_one(rsx, base, base_index + index, &v)) {
                c->fetch_ok = 0;
                break;
            }
            push_vert(c, &v);
        }
    }
}

/* Expand the accumulated primitive to a triangle list, build the stage-2
 * fallback transform, and draw. Ported from replay_main.c's sink_end(). */
static void sink_end(void* user, const rsx_dispatch* rsx)
{
    sink_ctx* c = user;
    const u32 prim = rsx->current_primitive;
    fetch_batches(c, rsx);
    if (!c->n_verts || !c->fetch_ok) {
        if (!c->fetch_ok && prim < 16)
            c->skipped_prims[prim]++;
        return;
    }

    rmac_vertex* tri = NULL;
    u32 n_tri = 0;
    switch (prim) {
    case PRIM_TRIANGLES:
        tri = c->verts;
        n_tri = c->n_verts - c->n_verts % 3;
        break;
    case PRIM_TRIANGLE_STRIP:
        if (c->n_verts < 3) return;
        n_tri = (c->n_verts - 2) * 3;
        tri = malloc(n_tri * sizeof(rmac_vertex));
        for (u32 i = 0; i + 2 < c->n_verts; i++) {
            tri[i * 3 + 0] = c->verts[i];
            tri[i * 3 + 1] = c->verts[i + 1];
            tri[i * 3 + 2] = c->verts[i + 2];
        }
        break;
    case PRIM_TRIANGLE_FAN:
        if (c->n_verts < 3) return;
        n_tri = (c->n_verts - 2) * 3;
        tri = malloc(n_tri * sizeof(rmac_vertex));
        for (u32 i = 1; i + 1 < c->n_verts; i++) {
            tri[(i - 1) * 3 + 0] = c->verts[0];
            tri[(i - 1) * 3 + 1] = c->verts[i];
            tri[(i - 1) * 3 + 2] = c->verts[i + 1];
        }
        break;
    case PRIM_QUADS: {
        const u32 quads = c->n_verts / 4;
        if (!quads) return;
        n_tri = quads * 6;
        tri = malloc(n_tri * sizeof(rmac_vertex));
        for (u32 q = 0; q < quads; q++) {
            const rmac_vertex* v = &c->verts[q * 4];
            rmac_vertex* t = &tri[q * 6];
            t[0] = v[0]; t[1] = v[1]; t[2] = v[2];
            t[3] = v[2]; t[4] = v[3]; t[5] = v[0];
        }
        break;
    }
    default:
        if (prim < 16)
            c->skipped_prims[prim]++;
        return;
    }

    if (!n_tri) {
        if (tri != c->verts) free(tri);
        return;
    }

    /* Fallback transform columns: uploaded constants c0..c3 as the MVP when
     * present, else the RSX viewport scale/translate inverse (stage 2) --
     * same formula and column layout as replay_main.c's sink_end(). */
    float cols[16];
    int have_mvp = 0;
    for (u32 s = 0; s < 4; s++) {
        memcpy(&cols[s * 4], rsx_dsp_constant(rsx, s), 16);
        for (u32 k = 0; k < 4; k++)
            if (cols[s * 4 + k] != 0.0f)
                have_mvp = 1;
    }
    if (!have_mvp) {
        rsx_dsp_viewport vp;
        rsx_dsp_get_viewport(rsx, &vp);
        const float w = vp.w ? (float)vp.w : (float)g_width;
        const float h = vp.h ? (float)vp.h : (float)g_height;
        memset(cols, 0, sizeof(cols));
        cols[0]  = 2.0f / w;          /* c0.x */
        cols[5]  = -2.0f / h;         /* c1.y */
        cols[10] = 1.0f;              /* c2.z */
        cols[12] = -1.0f;             /* c3.x */
        cols[13] = 1.0f;              /* c3.y */
        cols[15] = 1.0f;              /* c3.w */
    }

    const u32 target = current_surface(rsx);
    if (c->draw_count < 40) {
        printf("[draw %2u] prim=%u verts=%u surf=%u %s v0=(%.2f %.2f %.2f) c0=(%.2f %.2f %.2f)\n",
               c->draw_count, prim, n_tri, target, have_mvp ? "mvp" : "fb-vp",
               tri[0].x, tri[0].y, tri[0].z, tri[0].r, tri[0].g, tri[0].b);
    }

    metal_draw_triangles(target, tri, n_tri, cols);

    c->draw_count++;
    c->drawn_verts += n_tri;
    if (tri != c->verts)
        free(tri);
}

static void sink_flip(void* user, const rsx_dispatch* rsx, u32 arg)
{
    (void)rsx;
    sink_ctx* c = user;
    const rxs_stream* s = c->stream;
    const u32 buf = arg & 7;
    const u32 scan_offset = buf < s->disp_count ? s->disp[buf].offset : 0;

    printf("[replay] flip(buffer %u @0x%X) -> frame %u (%u clears, %u draws, %u verts, %u surfaces)\n",
           buf, scan_offset, c->frame_no, c->clear_count, c->draw_count, c->drawn_verts, g_n_surfaces);

    const u32 si = surface_get(RSX_LOCATION_LOCAL, scan_offset);
    char path[4096];
    snprintf(path, sizeof(path), "%s/frame_%03u.png", c->outdir, c->frame_no);
    metal_readback_png(si, path);
    c->frame_no++;
}

/* ---------------------------------------------------------------------------
 * main
 * -----------------------------------------------------------------------*/

int main(int argc, char** argv)
{
    @autoreleasepool {
        const char* path = NULL;
        const char* outdir = ".";
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--out") && i + 1 < argc) outdir = argv[++i];
            else path = argv[i];
        }
        if (!path) {
            printf("usage: %s <stream.rxs> [--out dir]\n", argv[0]);
            return 2;
        }

        rxs_stream s = {0};
        if (rxs_load(path, &s))
            return 1;
        printf("[replay] %u records, %u blocks, display %ux%u\n",
               s.n_records, s.n_blocks, s.disp_w, s.disp_h);

        g_arena[0] = calloc(1, ARENA_SIZE);
        g_arena[1] = calloc(1, ARENA_SIZE);
        if (!g_arena[0] || !g_arena[1]) {
            printf("arena alloc failed\n");
            return 1;
        }

        g_width  = s.disp_w ? s.disp_w : 1280;
        g_height = s.disp_h ? s.disp_h : 720;

        if (gpu_init())
            return 1;

        mkdir(outdir, 0755);

        static rsx_dispatch rsx;
        sink_ctx ctx = {0};
        ctx.outdir = outdir;
        ctx.stream = &s;
        rsx_dispatch_sink sink = {0};
        sink.user = &ctx;
        sink.clear = sink_clear;
        sink.begin = sink_begin;
        sink.end = sink_end;
        sink.draw_arrays = sink_draw_arrays;
        sink.draw_index_array = sink_draw_index_array;
        sink.flip = sink_flip;
        rsx_dispatch_init(&rsx, &sink);
        rsx_dispatch_seed_registers(&rsx, s.regs, s.reg_words);
        rsx_dispatch_seed_transform_program(&rsx, s.vp, s.vp_words);

        for (u32 i = 0; i < s.n_records; i++) {
            const u32 a = s.records[i * 2];
            const u32 b = s.records[i * 2 + 1];
            if (a & 0x80000000u)
                apply_block(&s, b);
            else
                rsx_dispatch_method(&rsx, a, b);
        }

        if (ctx.frame_no == 0) {
            printf("[replay] no flip in stream; dumping the current color target\n");
            char fpath[4096];
            snprintf(fpath, sizeof(fpath), "%s/frame_000.png", outdir);
            metal_readback_png(current_surface(&rsx), fpath);
        }

        for (u32 p = 0; p < 16; p++)
            if (ctx.skipped_prims[p])
                printf("[replay] skipped %u draws of primitive %u\n", ctx.skipped_prims[p], p);

        printf("[replay] done: %u clears, %u draws, %u verts, %u flips\n",
               ctx.clear_count, ctx.draw_count, ctx.drawn_verts, ctx.frame_no);
    }
    return 0;
}
