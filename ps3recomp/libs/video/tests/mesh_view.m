/*
 * ps3recomp - standalone Metal mesh viewer (macOS/arm64)
 *
 * Headless offscreen renderer that draws a real GoW2 Kratos mesh SOLID, with
 * a depth buffer and per-pixel diffuse lighting -- the "render better with
 * Metal" upgrade over the vertex-color point-cloud/painter replay in
 * replay_mac.m (same directory). Device/queue/offscreen-texture setup and
 * the PNG writer are adapted from replay_mac.m (our own code, reused as
 * scaffolding); everything mesh/lighting/matrix-specific below is new.
 *
 * Input: a tiny binary dump produced ahead of time by gen_wad_mesh_rxs.py
 * (already extracted from the GoW2 WAD -- NOT re-extracted here):
 *   char[4]  magic "KMSH"
 *   u32      nverts, then nverts * (f32 x, f32 y, f32 z)   positions
 *   u32      ntris,  then ntris  * (u32 i0, u32 i1, u32 i2) triangle indices
 *
 * Pipeline: load positions+indices -> compute smooth per-vertex normals on
 * the CPU (area-weighted face-normal accumulation) -> upload to MTLBuffers
 * -> render into an offscreen BGRA8 color texture + Depth32Float depth
 * texture (depth test/write on) with a small MSL shader pair (MVP transform,
 * two-sided Lambertian diffuse) -> read the color texture back -> write PNG.
 *
 * Build:
 *   clang -fobjc-arc -std=c17 -O2 mesh_view.m \
 *       -framework Metal -framework Foundation -framework CoreGraphics \
 *       -o mesh_view
 *
 * Usage:
 *   mesh_view [/tmp/kratos.mesh]      (writes /tmp/kratosout/mesh_view.png)
 */

#ifndef __APPLE__
#error mesh_view is macOS/Metal-only
#endif

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------------------
 * Mesh load (KMSH) + CPU-side smooth normal computation
 * -----------------------------------------------------------------------*/

typedef struct {
    uint32_t nverts;
    float*   pos;      /* nverts * 3 (x,y,z) */
    float*   nrm;      /* nverts * 3 (x,y,z), computed below */
    uint32_t ntris;
    uint32_t* idx;     /* ntris * 3 */
} kmesh;

static int kmesh_load(const char* path, kmesh* m)
{
    memset(m, 0, sizeof(*m));
    FILE* f = fopen(path, "rb");
    if (!f) { printf("[mesh] cannot open %s\n", path); return -1; }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "KMSH", 4) != 0) {
        printf("[mesh] %s: bad magic (expected KMSH)\n", path);
        fclose(f);
        return -1;
    }

    if (fread(&m->nverts, sizeof(uint32_t), 1, f) != 1) {
        printf("[mesh] %s: truncated vertex count\n", path);
        fclose(f);
        return -1;
    }
    m->pos = malloc((size_t)m->nverts * 3 * sizeof(float));
    if (fread(m->pos, sizeof(float) * 3, m->nverts, f) != m->nverts) {
        printf("[mesh] %s: truncated vertex data\n", path);
        fclose(f);
        return -1;
    }

    if (fread(&m->ntris, sizeof(uint32_t), 1, f) != 1) {
        printf("[mesh] %s: truncated triangle count\n", path);
        fclose(f);
        return -1;
    }
    m->idx = malloc((size_t)m->ntris * 3 * sizeof(uint32_t));
    if (fread(m->idx, sizeof(uint32_t) * 3, m->ntris, f) != m->ntris) {
        printf("[mesh] %s: truncated triangle data\n", path);
        fclose(f);
        return -1;
    }

    fclose(f);
    printf("[mesh] %s: %u verts, %u tris\n", path, m->nverts, m->ntris);
    return 0;
}

/* Smooth per-vertex normals: zero the array, accumulate each triangle's raw
 * (unnormalized -> area-weighted) face normal -- cross(e1,e2) -- into its 3
 * vertices, then normalize once per vertex. Vertices touched by no valid
 * triangle keep a safe default (+Y) instead of producing NaN. */
static void kmesh_compute_normals(kmesh* m)
{
    m->nrm = calloc((size_t)m->nverts * 3, sizeof(float));

    for (uint32_t t = 0; t < m->ntris; t++) {
        const uint32_t i0 = m->idx[t * 3 + 0];
        const uint32_t i1 = m->idx[t * 3 + 1];
        const uint32_t i2 = m->idx[t * 3 + 2];
        if (i0 >= m->nverts || i1 >= m->nverts || i2 >= m->nverts)
            continue;

        const float* p0 = &m->pos[i0 * 3];
        const float* p1 = &m->pos[i1 * 3];
        const float* p2 = &m->pos[i2 * 3];
        const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
        const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
        const float fn[3] = {
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0],
        };

        for (int k = 0; k < 3; k++) {
            m->nrm[i0 * 3 + k] += fn[k];
            m->nrm[i1 * 3 + k] += fn[k];
            m->nrm[i2 * 3 + k] += fn[k];
        }
    }

    for (uint32_t v = 0; v < m->nverts; v++) {
        float* n = &m->nrm[v * 3];
        const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-8f) {
            n[0] /= len; n[1] /= len; n[2] /= len;
        } else {
            n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Column-major 4x4 matrix helpers (m[col*4+row], matches MSL float4x4's
 * in-memory layout when loaded from a raw uniform buffer -- see Uniforms
 * below / replay_mac.m's Xform for the same convention).
 * -----------------------------------------------------------------------*/

static void mat4_identity(float m[16])
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* out = a * b (b's transform applied first). `out` may alias `a` and/or `b`. */
static void mat4_multiply(float out[16], const float a[16], const float b[16])
{
    float r[16];
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
                sum += a[k * 4 + row] * b[col * 4 + k];
            r[col * 4 + row] = sum;
        }
    }
    memcpy(out, r, sizeof(r));
}

static void mat4_translate(float m[16], float tx, float ty, float tz)
{
    mat4_identity(m);
    m[12] = tx; m[13] = ty; m[14] = tz;
}

static void mat4_scale(float m[16], float s)
{
    mat4_identity(m);
    m[0] = s; m[5] = s; m[10] = s;
}

static void mat4_rotate_x(float m[16], float radians)
{
    mat4_identity(m);
    const float c = cosf(radians), s = sinf(radians);
    m[5] = c;  m[6] = s;
    m[9] = -s; m[10] = c;
}

static void mat4_rotate_y(float m[16], float radians)
{
    mat4_identity(m);
    const float c = cosf(radians), s = sinf(radians);
    m[0] = c; m[2] = -s;
    m[8] = s; m[10] = c;
}

/* Metal-style perspective: right-handed view space (camera looks down -Z),
 * NDC depth in [0,1] (near->0, far->1), matching MTLPixelFormatDepth32Float
 * + MTLCompareFunctionLess with a 1.0 clear. */
static void mat4_perspective(float m[16], float fovy_radians, float aspect, float znear, float zfar)
{
    memset(m, 0, 16 * sizeof(float));
    const float f = 1.0f / tanf(fovy_radians * 0.5f);
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = zfar / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (zfar * znear) / (znear - zfar);
}

static float deg2rad(float d) { return d * (float)M_PI / 180.0f; }

/* ---------------------------------------------------------------------------
 * Minimal self-contained PNG writer (adapted from replay_mac.m's write_png:
 * IDAT as valid-but-uncompressed zlib "stored" deflate blocks, no zlib/
 * ImageIO dependency). Takes tightly packed top-down RGBA8.
 * -----------------------------------------------------------------------*/

static uint32_t g_crc_table[256];
static int g_crc_ready;

static void crc32_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_table[n] = c;
    }
    g_crc_ready = 1;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t* buf, size_t len)
{
    if (!g_crc_ready) crc32_init();
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = g_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t adler32_calc(const uint8_t* data, size_t len)
{
    uint32_t a = 1, b = 0;
    const uint32_t MOD = 65521;
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

static void put_u32be(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void write_chunk(FILE* f, const char* type, const uint8_t* data, uint32_t len)
{
    uint8_t lenbuf[4];
    put_u32be(lenbuf, len);
    fwrite(lenbuf, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len)
        fwrite(data, 1, len, f);
    uint32_t crc = crc32_update(0, (const uint8_t*)type, 4);
    if (len)
        crc = crc32_update(crc, data, len);
    uint8_t crcbuf[4];
    put_u32be(crcbuf, crc);
    fwrite(crcbuf, 1, 4, f);
}

static int write_png(const char* path, const uint8_t* rgba, uint32_t w, uint32_t h)
{
    FILE* f = fopen(path, "wb");
    if (!f) { printf("[png] cannot write %s\n", path); return -1; }

    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13];
    put_u32be(ihdr + 0, w);
    put_u32be(ihdr + 4, h);
    ihdr[8]  = 8;  /* bit depth        */
    ihdr[9]  = 6;  /* color type: RGBA */
    ihdr[10] = 0;  /* compression      */
    ihdr[11] = 0;  /* filter           */
    ihdr[12] = 0;  /* interlace        */
    write_chunk(f, "IHDR", ihdr, sizeof(ihdr));

    const size_t row_bytes = (size_t)w * 4 + 1;
    const size_t raw_size = row_bytes * h;
    uint8_t* raw = malloc(raw_size ? raw_size : 1);
    for (uint32_t y = 0; y < h; y++) {
        uint8_t* row = raw + (size_t)y * row_bytes;
        row[0] = 0;
        memcpy(row + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
    }

    const size_t max_deflate = raw_size + (raw_size / 65535 + 1) * 5 + 6;
    uint8_t* zbuf = malloc(max_deflate);
    size_t zp = 0;
    zbuf[zp++] = 0x78; zbuf[zp++] = 0x01;
    size_t off = 0;
    do {
        size_t block = raw_size - off;
        if (block > 65535) block = 65535;
        const uint8_t bfinal = (off + block >= raw_size) ? 1 : 0;
        zbuf[zp++] = bfinal;
        const uint16_t len = (uint16_t)block;
        const uint16_t nlen = (uint16_t)~len;
        zbuf[zp++] = (uint8_t)(len & 0xFF);  zbuf[zp++] = (uint8_t)(len >> 8);
        zbuf[zp++] = (uint8_t)(nlen & 0xFF); zbuf[zp++] = (uint8_t)(nlen >> 8);
        memcpy(zbuf + zp, raw + off, block);
        zp += block;
        off += block;
    } while (off < raw_size);
    const uint32_t adler = adler32_calc(raw, raw_size);
    put_u32be(zbuf + zp, adler); zp += 4;

    write_chunk(f, "IDAT", zbuf, (uint32_t)zp);
    write_chunk(f, "IEND", NULL, 0);

    free(raw);
    free(zbuf);
    fclose(f);
    printf("[png] wrote %s (%ux%u)\n", path, w, h);
    return 0;
}

/* ---------------------------------------------------------------------------
 * MSL shaders: MVP transform + two-sided Lambertian diffuse.
 * -----------------------------------------------------------------------*/

static const char* k_msl_source =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VSIn {\n"
    "    float3 position [[attribute(0)]];\n"
    "    float3 normal   [[attribute(1)]];\n"
    "};\n"
    "struct VSOut {\n"
    "    float4 position [[position]];\n"
    "    float3 normal;\n"
    "};\n"
    "struct Uniforms {\n"
    "    float4x4 mvp;\n"
    "    float4x4 mv;\n"
    "};\n"
    "vertex VSOut mesh_vs(VSIn in [[stage_in]], constant Uniforms& u [[buffer(2)]]) {\n"
    "    VSOut out;\n"
    "    out.position = u.mvp * float4(in.position, 1.0);\n"
    "    float3x3 normalMat = float3x3(u.mv[0].xyz, u.mv[1].xyz, u.mv[2].xyz);\n"
    "    out.normal = normalMat * in.normal;\n"
    "    return out;\n"
    "}\n"
    "fragment float4 mesh_fs(VSOut in [[stage_in]]) {\n"
    "    float3 N = normalize(in.normal);\n"
    "    float3 L = normalize(float3(0.35, 0.5, 0.8));\n"
    "    float d = abs(dot(N, L));\n"
    "    float3 clay = float3(0.72, 0.63, 0.55);\n"
    "    float3 color = clay * (0.22 + 0.78 * d);\n"
    "    return float4(color, 1.0);\n"
    "}\n";

/* Uniform buffer layout mirrors the MSL `Uniforms` struct byte-for-byte:
 * two column-major float4x4 (4 float4 columns each), contiguous. */
typedef struct {
    float mvp[16];
    float mv[16];
} uniforms_t;

/* ---------------------------------------------------------------------------
 * main
 * -----------------------------------------------------------------------*/

#define OUT_W 512u
#define OUT_H 512u

int main(int argc, char** argv)
{
    @autoreleasepool {
        const char* mesh_path = (argc > 1) ? argv[1] : "/tmp/kratos.mesh";
        const char* out_dir  = "/tmp/kratosout";
        const char* out_path = "/tmp/kratosout/mesh_view.png";

        kmesh m;
        if (kmesh_load(mesh_path, &m))
            return 1;
        kmesh_compute_normals(&m);

        /* ---- MVP: Model (translate -center, scale, Ry, Rx) -> View
         * (translate Z -2.6) -> Perspective (fovY 45, aspect 1, 0.1..10). ---- */
        const float center[3] = { 0.0f, 18.6f, -0.4f };
        float T[16], S[16], Ry[16], Rx[16], V[16], P[16];
        mat4_translate(T, -center[0], -center[1], -center[2]);
        mat4_scale(S, 1.0f / 20.0f);
        mat4_rotate_y(Ry, deg2rad(25.0f));
        mat4_rotate_x(Rx, deg2rad(12.0f));
        mat4_translate(V, 0.0f, 0.0f, -2.6f);
        mat4_perspective(P, deg2rad(45.0f), 1.0f, 0.1f, 10.0f);

        float model[16], tmp[16], mv[16], mvp[16];
        mat4_multiply(tmp, S, T);        /* tmp   = Scale * Translate            */
        mat4_multiply(tmp, Ry, tmp);     /* tmp   = Ry * Scale * Translate       */
        mat4_multiply(model, Rx, tmp);   /* model = Rx * Ry * Scale * Translate  */
        mat4_multiply(mv, V, model);     /* mv    = View * Model                 */
        mat4_multiply(mvp, P, mv);       /* mvp   = Proj * View * Model          */

        uniforms_t uniforms;
        memcpy(uniforms.mvp, mvp, sizeof(mvp));
        memcpy(uniforms.mv, mv, sizeof(mv));

        /* ---- Metal device / library / PSO / depth-stencil state ---- */
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) { printf("[gpu] no Metal device available\n"); return 1; }
        printf("[gpu] device: %s\n", device.name.UTF8String);

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) { printf("[gpu] command queue create failed\n"); return 1; }

        NSError* err = nil;
        NSString* src = [NSString stringWithUTF8String:k_msl_source];
        id<MTLLibrary> library = [device newLibraryWithSource:src options:nil error:&err];
        if (!library) {
            printf("[gpu] MSL compile failed: %s\n", err ? err.localizedDescription.UTF8String : "?");
            return 1;
        }
        id<MTLFunction> vfn = [library newFunctionWithName:@"mesh_vs"];
        id<MTLFunction> ffn = [library newFunctionWithName:@"mesh_fs"];
        if (!vfn || !ffn) { printf("[gpu] MSL entry points missing\n"); return 1; }

        MTLVertexDescriptor* vdesc = [MTLVertexDescriptor vertexDescriptor];
        vdesc.attributes[0].format = MTLVertexFormatFloat3;   /* position */
        vdesc.attributes[0].offset = 0;
        vdesc.attributes[0].bufferIndex = 0;
        vdesc.attributes[1].format = MTLVertexFormatFloat3;   /* normal   */
        vdesc.attributes[1].offset = 0;
        vdesc.attributes[1].bufferIndex = 1;
        vdesc.layouts[0].stride = 3 * sizeof(float);
        vdesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        vdesc.layouts[1].stride = 3 * sizeof(float);
        vdesc.layouts[1].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* pdesc = [MTLRenderPipelineDescriptor new];
        pdesc.vertexFunction = vfn;
        pdesc.fragmentFunction = ffn;
        pdesc.vertexDescriptor = vdesc;
        pdesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pdesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:pdesc error:&err];
        if (!pso) {
            printf("[gpu] PSO create failed: %s\n", err ? err.localizedDescription.UTF8String : "?");
            return 1;
        }

        MTLDepthStencilDescriptor* dsdesc = [MTLDepthStencilDescriptor new];
        dsdesc.depthCompareFunction = MTLCompareFunctionLess;
        dsdesc.depthWriteEnabled = YES;
        id<MTLDepthStencilState> dss = [device newDepthStencilStateWithDescriptor:dsdesc];

        /* ---- Offscreen color (readable, Shared) + depth (Private) textures ---- */
        MTLTextureDescriptor* ctd =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                 width:OUT_W
                                                                height:OUT_H
                                                             mipmapped:NO];
        ctd.usage = MTLTextureUsageRenderTarget;
        ctd.storageMode = MTLStorageModeShared;
        id<MTLTexture> colorTex = [device newTextureWithDescriptor:ctd];

        MTLTextureDescriptor* dtd =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                 width:OUT_W
                                                                height:OUT_H
                                                             mipmapped:NO];
        dtd.usage = MTLTextureUsageRenderTarget;
        dtd.storageMode = MTLStorageModePrivate;
        id<MTLTexture> depthTex = [device newTextureWithDescriptor:dtd];

        if (!colorTex || !depthTex) { printf("[gpu] offscreen texture create failed\n"); return 1; }

        /* ---- Vertex/index buffers (shared storage, per spec) ---- */
        id<MTLBuffer> posBuf = [device newBufferWithBytes:m.pos
                                                     length:(NSUInteger)m.nverts * 3 * sizeof(float)
                                                    options:MTLResourceStorageModeShared];
        id<MTLBuffer> nrmBuf = [device newBufferWithBytes:m.nrm
                                                     length:(NSUInteger)m.nverts * 3 * sizeof(float)
                                                    options:MTLResourceStorageModeShared];
        id<MTLBuffer> idxBuf = [device newBufferWithBytes:m.idx
                                                     length:(NSUInteger)m.ntris * 3 * sizeof(uint32_t)
                                                    options:MTLResourceStorageModeShared];
        if (!posBuf || !nrmBuf || !idxBuf) { printf("[gpu] buffer alloc failed\n"); return 1; }

        /* ---- Render: clear dark navy + depth 1.0, draw indexed triangles.
         * No culling (safest for a first correct image regardless of the
         * source mesh's winding convention); two-sided abs(dot(N,L)) in the
         * fragment shader keeps back-facing triangles lit, not black. ---- */
        MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = colorTex;
        rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.12, 0.25, 1.0);
        rpd.depthAttachment.texture = depthTex;
        rpd.depthAttachment.loadAction = MTLLoadActionClear;
        rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
        rpd.depthAttachment.clearDepth = 1.0;

        id<MTLCommandBuffer> cmd = [queue commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
        [enc setRenderPipelineState:pso];
        [enc setDepthStencilState:dss];
        [enc setCullMode:MTLCullModeNone];
        [enc setViewport:(MTLViewport){0.0, 0.0, (double)OUT_W, (double)OUT_H, 0.0, 1.0}];
        [enc setVertexBuffer:posBuf offset:0 atIndex:0];
        [enc setVertexBuffer:nrmBuf offset:0 atIndex:1];
        [enc setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:2];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                         indexCount:(NSUInteger)m.ntris * 3
                          indexType:MTLIndexTypeUInt32
                        indexBuffer:idxBuf
                  indexBufferOffset:0];
        [enc endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];

        /* ---- Read back BGRA8 -> RGBA8, write PNG ---- */
        mkdir(out_dir, 0755);

        const size_t pitch = (size_t)OUT_W * 4;
        uint8_t* bgra = malloc(pitch * OUT_H);
        [colorTex getBytes:bgra bytesPerRow:pitch fromRegion:MTLRegionMake2D(0, 0, OUT_W, OUT_H) mipmapLevel:0];

        uint8_t* rgba = malloc(pitch * OUT_H);
        for (size_t i = 0; i < (size_t)OUT_W * OUT_H; i++) {
            rgba[i * 4 + 0] = bgra[i * 4 + 2]; /* R */
            rgba[i * 4 + 1] = bgra[i * 4 + 1]; /* G */
            rgba[i * 4 + 2] = bgra[i * 4 + 0]; /* B */
            rgba[i * 4 + 3] = bgra[i * 4 + 3]; /* A */
        }
        write_png(out_path, rgba, OUT_W, OUT_H);

        /* ---- Self-check: fill percentage + center-column contiguous run,
         * so a solid-vs-hollow/point-cloud regression is caught right here
         * instead of only by eyeballing the PNG. ---- */
        const uint8_t bg[3] = {
            (uint8_t)lroundf(0.06f * 255.0f),
            (uint8_t)lroundf(0.12f * 255.0f),
            (uint8_t)lroundf(0.25f * 255.0f),
        };
        const int EPS = 10;
        uint32_t filled_total = 0;
        for (size_t i = 0; i < (size_t)OUT_W * OUT_H; i++) {
            const int dr = (int)rgba[i * 4 + 0] - bg[0];
            const int dg = (int)rgba[i * 4 + 1] - bg[1];
            const int db = (int)rgba[i * 4 + 2] - bg[2];
            if (abs(dr) > EPS || abs(dg) > EPS || abs(db) > EPS)
                filled_total++;
        }

        const uint32_t cx = OUT_W / 2;
        uint32_t run_best = 0, run_cur = 0, run_best_start = 0, run_start = 0;
        uint8_t lum_min = 255, lum_max = 0;
        for (uint32_t y = 0; y < OUT_H; y++) {
            const size_t i = (size_t)y * OUT_W + cx;
            const int dr = (int)rgba[i * 4 + 0] - bg[0];
            const int dg = (int)rgba[i * 4 + 1] - bg[1];
            const int db = (int)rgba[i * 4 + 2] - bg[2];
            const int is_filled = (abs(dr) > EPS || abs(dg) > EPS || abs(db) > EPS);
            if (is_filled) {
                if (run_cur == 0) run_start = y;
                run_cur++;
                const uint8_t r = rgba[i * 4 + 0], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
                const uint8_t lum = (uint8_t)((r * 77 + g * 151 + b * 28) >> 8);
                if (lum < lum_min) lum_min = lum;
                if (lum > lum_max) lum_max = lum;
                if (run_cur > run_best) { run_best = run_cur; run_best_start = run_start; }
            } else {
                run_cur = 0;
            }
        }

        printf("[check] fill=%.1f%% (%u/%u px) center-column largest run=%u/%u px (rows %u..%u) luminance[min=%u max=%u]\n",
               100.0 * filled_total / ((double)OUT_W * OUT_H), filled_total, OUT_W * OUT_H,
               run_best, OUT_H, run_best_start, run_best_start + run_best,
               (unsigned)lum_min, (unsigned)lum_max);

        free(bgra);
        free(rgba);
    }
    return 0;
}
