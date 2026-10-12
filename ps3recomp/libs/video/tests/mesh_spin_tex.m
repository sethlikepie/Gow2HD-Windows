/*
 * ps3recomp - real-time windowed, TEXTURED Metal mesh viewer (macOS/arm64)
 *
 * Extends mesh_spin.m (same dir: SDL2 window + Metal + Y-spin loop + depth)
 * with UV texturing of the real GoW2 Kratos mesh, using our own decoded skin
 * texture (decode_hero_tex.py, same dir -> /tmp/kratos_tex.png). Mesh load,
 * smooth-normal computation, 4x4 matrix helpers, MSL shader skeleton and the
 * Metal pipeline setup are adapted from mesh_spin.m / mesh_view.m (same dir);
 * everything UV/texture/dynamic-bbox-specific below is new. This is a NEW
 * file -- mesh_spin.m, mesh_view.m and rsx_metal_backend.m are NOT edited.
 *
 * Input: /tmp/kratos_tex.mesh, a "KMH2" binary dump (position+texcoord
 * interleaved, see kmesh_load below) -- already extracted ahead of time, NOT
 * re-extracted here. Texture: /tmp/kratos_tex.png, written by
 * decode_hero_tex.py (same dir) from the real WAD asset "~000kratos01_1".
 *
 * Dynamic bbox framing: unlike mesh_spin.m (which hardcodes center=(0,18.6,
 * -0.4) and scale=1/20 for its own position-only mesh dump), this mesh's
 * bounding box is computed at load time and the LONGEST axis is rotated to
 * be the vertical (screen-up) one -- see kmesh_bbox() / the upAxis selection
 * in main(). For /tmp/kratos_tex.mesh specifically, X is the long axis
 * (~37 units, roughly feet=0 -> head=37) and gets remapped to world +Y via a
 * fixed Z-axis rotation (mat4_rotate_z, new here -- mesh_spin.m/mesh_view.m
 * only had rotate_x/rotate_y since their hardcoded mesh was already Y-up).
 *
 * PNG I/O is fully self-contained (no ImageIO/CoreGraphics/zlib linkage --
 * the build command below only links Metal/Foundation/QuartzCore + SDL2):
 * both decode_hero_tex.py's writer and write_png() here use RFC1950 zlib
 * streams with STORED (uncompressed) DEFLATE blocks only -- same technique
 * mesh_view.m's write_png() already uses ("no zlib/ImageIO dependency") --
 * so load_png_rgba() below only needs to walk PNG chunks + copy stored
 * blocks, no real Huffman/LZ77 inflate. If the texture fails to load for any
 * reason, rendering falls back to the plain clay-shaded pipeline (mesh_fs_clay)
 * instead of failing.
 *
 * Build:  ./build_mesh_spin_tex.sh
 * Run (ficheiros pre-extraidos):
 *         /tmp/mesh_spin_tex /tmp/kratos_tex.mesh
 * Run (100% NATIVO, direto do arquivo do jogo; detecta pelo magic 'PSAR' e
 *      carrega psarc -> WAD -> malha+textura em memoria via psarc_read.c +
 *      wad_assets.c, deswizzle do proprio engine -- zero Python):
 *         /tmp/mesh_spin_tex <gow2.psarc> [wad/r_hero01.wad_ps3] [2d29cb] [~000kratos01_1]
 *         (ESC or close the window to quit; drag a corner to resize)
 * Headless smoke: MESH_SPIN_FRAMES=120 /tmp/mesh_spin_tex /tmp/kratos_tex.mesh
 * Offscreen verification frame (no window/display needed):
 *         /tmp/mesh_spin_tex --png /tmp/kratos_tex.mesh
 *         -> writes /tmp/kratosout/mesh_tex.png
 */
#ifndef __APPLE__
#error mesh_spin_tex is macOS/Metal-only
#endif

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CAMetalDisplayLink.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_metal.h>
#include "psarc_read.h"
#include "wad_assets.h"
#include <math.h>
#include <simd/simd.h>   /* matrizes 4x4 SIMD nativas (NEON), layout = float4x4 do Metal */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------------------
 * Mesh load (KMH2: position+texcoord interleaved) + smooth normals + bbox
 * -----------------------------------------------------------------------*/
typedef struct {
    uint32_t nverts;
    float*   vtx;   /* nverts * 5 (px,py,pz,u,v), interleaved, stride 20B */
    float*   nrm;   /* nverts * 3 (x,y,z), tightly packed, computed below */
    uint32_t ntris;
    uint32_t* idx;  /* ntris * 3 */
} kmesh;

static int kmesh_load(const char* path, kmesh* m) {
    memset(m, 0, sizeof(*m));
    FILE* f = fopen(path, "rb");
    if (!f) { printf("[mesh] cannot open %s\n", path); return -1; }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "KMH2", 4) != 0) {
        printf("[mesh] %s: bad magic (expected KMH2)\n", path);
        fclose(f); return -1;
    }
    if (fread(&m->nverts, sizeof(uint32_t), 1, f) != 1) { fclose(f); return -1; }
    m->vtx = malloc((size_t)m->nverts * 5 * sizeof(float));
    if (fread(m->vtx, sizeof(float) * 5, m->nverts, f) != m->nverts) {
        printf("[mesh] %s: truncated vertex data\n", path);
        fclose(f); return -1;
    }
    if (fread(&m->ntris, sizeof(uint32_t), 1, f) != 1) { fclose(f); return -1; }
    m->idx = malloc((size_t)m->ntris * 3 * sizeof(uint32_t));
    if (fread(m->idx, sizeof(uint32_t) * 3, m->ntris, f) != m->ntris) {
        printf("[mesh] %s: truncated triangle data\n", path);
        fclose(f); return -1;
    }
    fclose(f);
    printf("[mesh] %s: %u verts, %u tris (KMH2 pos+uv)\n", path, m->nverts, m->ntris);
    return 0;
}

/* Same accumulate-then-normalize scheme as mesh_spin.m/mesh_view.m, adapted
 * to read positions from the interleaved (stride-5-float) vtx array. */
static void kmesh_compute_normals(kmesh* m) {
    m->nrm = calloc((size_t)m->nverts * 3, sizeof(float));
    for (uint32_t t = 0; t < m->ntris; t++) {
        const uint32_t i0 = m->idx[t*3+0], i1 = m->idx[t*3+1], i2 = m->idx[t*3+2];
        if (i0 >= m->nverts || i1 >= m->nverts || i2 >= m->nverts) continue;
        const float* p0 = &m->vtx[i0*5];
        const float* p1 = &m->vtx[i1*5];
        const float* p2 = &m->vtx[i2*5];
        const float e1[3] = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
        const float e2[3] = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
        const float fn[3] = {
            e1[1]*e2[2] - e1[2]*e2[1],
            e1[2]*e2[0] - e1[0]*e2[2],
            e1[0]*e2[1] - e1[1]*e2[0],
        };
        for (int k = 0; k < 3; k++) {
            m->nrm[i0*3+k] += fn[k]; m->nrm[i1*3+k] += fn[k]; m->nrm[i2*3+k] += fn[k];
        }
    }
    for (uint32_t v = 0; v < m->nverts; v++) {
        float* n = &m->nrm[v*3];
        const float len = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (len > 1e-8f) { n[0] /= len; n[1] /= len; n[2] /= len; }
        else { n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f; }
    }
}

/* Actual min/max over all 3 axes -- replaces mesh_spin.m's hardcoded center. */
static void kmesh_bbox(const kmesh* m, float mn[3], float mx[3]) {
    mn[0] = mn[1] = mn[2] = INFINITY;
    mx[0] = mx[1] = mx[2] = -INFINITY;
    for (uint32_t v = 0; v < m->nverts; v++) {
        const float* p = &m->vtx[v*5];
        for (int k = 0; k < 3; k++) {
            if (p[k] < mn[k]) mn[k] = p[k];
            if (p[k] > mx[k]) mx[k] = p[k];
        }
    }
}

/* ---- column-major 4x4 matrices (same convention as mesh_spin.m/mesh_view.m) */
static void mat4_identity(float m[16]) { memset(m,0,64); m[0]=m[5]=m[10]=m[15]=1; }
/* Multiply nativo SIMD (matrix_multiply = NEON no Apple Silicon). float[16]
 * column-major tem o mesmo layout de simd_float4x4; carrego por memcpy (evita
 * UB de alinhamento e trata aliasing o==a/o==b, pois copio antes de escrever).
 * Hot path: chamado ~2x123/frame no FK do rig. */
static void mat4_multiply(float o[16], const float a[16], const float b[16]) {
    simd_float4x4 A, B; memcpy(&A, a, 64); memcpy(&B, b, 64);
    simd_float4x4 R = matrix_multiply(A, B);   /* R = A * B, column-major */
    memcpy(o, &R, 64);
}
static void mat4_translate(float m[16], float x, float y, float z){ mat4_identity(m); m[12]=x; m[13]=y; m[14]=z; }
static void mat4_scale(float m[16], float s){ mat4_identity(m); m[0]=s; m[5]=s; m[10]=s; }
static void mat4_rotate_x(float m[16], float r){ mat4_identity(m); float c=cosf(r),s=sinf(r); m[5]=c; m[6]=s; m[9]=-s; m[10]=c; }
static void mat4_rotate_y(float m[16], float r){ mat4_identity(m); float c=cosf(r),s=sinf(r); m[0]=c; m[2]=-s; m[8]=s; m[10]=c; }
/* New here (mesh_spin.m/mesh_view.m never needed it -- their mesh was already
 * Y-up): rotates the XY plane, used to remap a non-Y longest axis to +Y. */
static void mat4_rotate_z(float m[16], float r){ mat4_identity(m); float c=cosf(r),s=sinf(r); m[0]=c; m[1]=s; m[4]=-s; m[5]=c; }
static void mat4_perspective(float m[16], float fovy, float aspect, float zn, float zf){
    memset(m,0,64); float f=1.0f/tanf(fovy*0.5f);
    m[0]=f/aspect; m[5]=f; m[10]=zf/(zn-zf); m[11]=-1; m[14]=(zf*zn)/(zn-zf);
}
static float deg2rad(float d){ return d*(float)M_PI/180.0f; }
/* inversa afim geral (3x3 por cofatores + translacao) -- p/ invBind do rig */
static void mat4_affine_inverse(const float m[16], float o[16]) {
    const float a=m[0],b=m[4],c=m[8], d=m[1],e=m[5],f=m[9], g=m[2],h=m[6],i=m[10];
    float A=e*i-f*h, B=f*g-d*i, C=d*h-e*g;
    float det=a*A+b*B+c*C;
    if (fabsf(det)<1e-12f) det=1e-12f;
    float id=1.0f/det;
    o[0]=A*id; o[4]=(c*h-b*i)*id; o[8]=(b*f-c*e)*id;
    o[1]=B*id; o[5]=(a*i-c*g)*id; o[9]=(c*d-a*f)*id;
    o[2]=C*id; o[6]=(b*g-a*h)*id; o[10]=(a*e-b*d)*id;
    o[3]=o[7]=o[11]=0.0f; o[15]=1.0f;
    o[12]=-(o[0]*m[12]+o[4]*m[13]+o[8]*m[14]);
    o[13]=-(o[1]*m[12]+o[5]*m[13]+o[9]*m[14]);
    o[14]=-(o[2]*m[12]+o[6]*m[13]+o[10]*m[14]);
}
static void mat4_apply_point(const float m[16], const float p[3], float out[3]) {
    simd_float4x4 M; memcpy(&M, m, 64);
    simd_float4 r = simd_mul(M, simd_make_float4(p[0], p[1], p[2], 1.0f));
    out[0]=r.x; out[1]=r.y; out[2]=r.z;
}

/* ---------------------------------------------------------------------------
 * MSL shaders: MVP transform + two-sided Lambertian diffuse, now sampling a
 * UV texture. Two fragment entry points share one vertex function: mesh_fs_tex
 * (textured) and mesh_fs_clay (the original solid clay look) -- picked at
 * runtime in main() depending on whether the PNG texture loaded OK, so a
 * missing/broken texture degrades to a still-correct solid render instead of
 * failing.
 * -----------------------------------------------------------------------*/
static const char* k_msl_source =
    "#include <metal_stdlib>\n using namespace metal;\n"
    "struct VSIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]]; };\n"
    "struct VSOut { float4 position [[position]]; float3 normal; float2 uv; };\n"
    "struct Uniforms { float4x4 mvp; float4x4 mv; };\n"
    "vertex VSOut mesh_vs(VSIn in [[stage_in]], constant Uniforms& u [[buffer(2)]]) {\n"
    "  VSOut o; o.position = u.mvp * float4(in.position,1.0);\n"
    "  float3x3 nm = float3x3(u.mv[0].xyz,u.mv[1].xyz,u.mv[2].xyz);\n"
    "  o.normal = nm*in.normal; o.uv = in.uv; return o; }\n"
    "fragment float4 mesh_fs_tex(VSOut in [[stage_in]],\n"
    "                             texture2d<float> tex [[texture(0)]],\n"
    "                             sampler samp [[sampler(0)]]) {\n"
    "  float3 N=normalize(in.normal); float3 L=normalize(float3(0.35,0.5,0.8));\n"
    "  float d=abs(dot(N,L));\n"
    "  float4 texColor = tex.sample(samp, in.uv);\n"
    "  if (texColor.a < 0.5) discard_fragment();\n"   /* recorte alpha (bordas rasgadas) */
    "  float3 color = texColor.rgb * (0.30 + 0.70*d);\n"
    "  return float4(color,1.0); }\n"
    "fragment float4 mesh_fs_clay(VSOut in [[stage_in]]) {\n"
    "  float3 N=normalize(in.normal); float3 L=normalize(float3(0.35,0.5,0.8));\n"
    "  float d=abs(dot(N,L)); float3 clay=float3(0.72,0.63,0.55);\n"
    "  return float4(clay*(0.22+0.78*d),1.0); }\n"
    /* skinning na GPU (matrix palette 2 bones): posicao e normal no vertex
     * shader, matrizes em buffer(4) -- corpo usa S=G*invBind em [0,nb),
     * props/anexos usam G em [nb,2nb) via indice de bone deslocado */
    "struct SkinVSIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]];\n"
    "                  float2 uv [[attribute(2)]]; float3 skin [[attribute(3)]]; };\n"
    "vertex VSOut mesh_vs_skin(SkinVSIn in [[stage_in]], constant Uniforms& u [[buffer(2)]],\n"
    "                          const device float4x4* mats [[buffer(4)]]) {\n"
    "  int b0 = int(in.skin.x), b1 = int(in.skin.y); float w = in.skin.z;\n"
    "  float4 p = float4(in.position, 1.0);\n"
    "  float3 pos = (mats[b0]*p).xyz*w + (mats[b1]*p).xyz*(1.0-w);\n"
    "  float3x3 r0 = float3x3(mats[b0][0].xyz, mats[b0][1].xyz, mats[b0][2].xyz);\n"
    "  float3x3 r1 = float3x3(mats[b1][0].xyz, mats[b1][1].xyz, mats[b1][2].xyz);\n"
    "  float3 n = normalize(w*(r0*in.normal) + (1.0-w)*(r1*in.normal));\n"
    "  VSOut o; o.position = u.mvp*float4(pos,1.0);\n"
    "  float3x3 nm = float3x3(u.mv[0].xyz,u.mv[1].xyz,u.mv[2].xyz);\n"
    "  o.normal = nm*n; o.uv = in.uv; return o; }\n"
    "vertex VSOut line_vs(uint vid [[vertex_id]],\n"
    "                     const device packed_float3* pts [[buffer(0)]],\n"
    "                     constant Uniforms& u [[buffer(2)]]) {\n"
    "  VSOut o; o.position = u.mvp * float4(float3(pts[vid]),1.0);\n"
    "  o.normal = float3(0,0,1); o.uv = float2(0); return o; }\n"
    "fragment float4 line_fs(VSOut in [[stage_in]]) {\n"
    "  return float4(0.25,1.0,0.45,1.0); }\n";

typedef struct { float mvp[16]; float mv[16]; } uniforms_t;

/* ---------------------------------------------------------------------------
 * Minimal self-contained PNG reader (pairs with decode_hero_tex.py's writer
 * and with write_png() below): RFC1950 zlib stream, STORED DEFLATE blocks
 * ONLY (no Huffman/LZ77), PNG filter type 0 (None) on every scanline. Both
 * writers this program's PNGs come from guarantee that layout, so we don't
 * need a general inflate here -- if either assumption doesn't hold, load
 * fails cleanly and the caller falls back to clay shading.
 * -----------------------------------------------------------------------*/
static uint32_t get_u32be(const uint8_t* p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3];
}

static uint8_t* load_png_rgba(const char* path, uint32_t* out_w, uint32_t* out_h) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("[png] cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz < 8) { fclose(f); return NULL; }
    uint8_t* buf = malloc((size_t)fsz);
    size_t got = fread(buf, 1, (size_t)fsz, f);
    fclose(f);
    if (got != (size_t)fsz) { printf("[png] short read %s\n", path); free(buf); return NULL; }

    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    if (memcmp(buf, sig, 8) != 0) { printf("[png] bad signature %s\n", path); free(buf); return NULL; }

    uint32_t w = 0, h = 0; uint8_t bitdepth = 0, colortype = 0;
    uint8_t* idat = NULL; size_t idat_len = 0;
    int have_ihdr = 0, have_iend = 0;
    size_t off = 8;
    while (off + 8 <= (size_t)fsz) {
        uint32_t clen = get_u32be(buf + off);
        const uint8_t* ctype = buf + off + 4;
        size_t data_off = off + 8;
        if (data_off + (size_t)clen + 4 > (size_t)fsz) { printf("[png] truncated chunk\n"); break; }
        if (memcmp(ctype, "IHDR", 4) == 0 && clen >= 13) {
            w = get_u32be(buf + data_off + 0);
            h = get_u32be(buf + data_off + 4);
            bitdepth  = buf[data_off + 8];
            colortype = buf[data_off + 9];
            have_ihdr = 1;
        } else if (memcmp(ctype, "IDAT", 4) == 0) {
            uint8_t* grown = realloc(idat, idat_len + clen);
            if (!grown) { free(idat); free(buf); return NULL; }
            idat = grown;
            memcpy(idat + idat_len, buf + data_off, clen);
            idat_len += clen;
        } else if (memcmp(ctype, "IEND", 4) == 0) {
            have_iend = 1;
            break;
        }
        off = data_off + clen + 4;
    }
    free(buf);

    if (!have_ihdr || !idat || !have_iend) {
        printf("[png] %s: missing IHDR/IDAT/IEND\n", path);
        free(idat); return NULL;
    }
    if (bitdepth != 8 || colortype != 6) {
        printf("[png] %s: unsupported bitdepth=%u colortype=%u (need 8-bit RGBA)\n", path, bitdepth, colortype);
        free(idat); return NULL;
    }
    if (w == 0 || h == 0 || w > 8192 || h > 8192) {
        printf("[png] %s: bad dimensions %ux%u\n", path, w, h);
        free(idat); return NULL;
    }

    const size_t stride = (size_t)w * 4 + 1;
    const size_t raw_want = stride * h;
    uint8_t* raw = malloc(raw_want);
    size_t raw_len = 0;
    size_t p = 2; /* skip 2-byte zlib header (CMF/FLG) */
    int ok = 1;
    while (p < idat_len) {
        uint8_t bh = idat[p++];
        int bfinal = bh & 1;
        int btype  = (bh >> 1) & 3;
        if (btype != 0) { printf("[png] %s: unsupported deflate block type %d (need stored)\n", path, btype); ok = 0; break; }
        if (p + 4 > idat_len) { ok = 0; break; }
        uint16_t len  = (uint16_t)(idat[p] | (idat[p+1] << 8));
        uint16_t nlen = (uint16_t)(idat[p+2] | (idat[p+3] << 8));
        p += 4;
        if ((uint16_t)~len != nlen) { printf("[png] %s: stored block LEN/NLEN mismatch\n", path); ok = 0; break; }
        if (p + len > idat_len || raw_len + len > raw_want) { ok = 0; break; }
        memcpy(raw + raw_len, idat + p, len);
        raw_len += len;
        p += len;
        if (bfinal) break;
    }
    free(idat);
    if (!ok || raw_len != raw_want) {
        printf("[png] %s: stored-block inflate failed (got %zu want %zu)\n", path, raw_len, raw_want);
        free(raw); return NULL;
    }

    uint8_t* rgba = malloc((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* row = raw + (size_t)y * stride;
        if (row[0] != 0) {
            printf("[png] %s: unsupported filter type %u on row %u\n", path, row[0], y);
            free(raw); free(rgba); return NULL;
        }
        memcpy(rgba + (size_t)y * w * 4, row + 1, (size_t)w * 4);
    }
    free(raw);

    *out_w = w; *out_h = h;
    printf("[png] loaded %s %ux%u\n", path, w, h);
    return rgba;
}

/* ---------------------------------------------------------------------------
 * Minimal self-contained PNG writer, for the --png offscreen verification
 * frame. Ported from mesh_view.m's write_png (own CRC32/Adler32, IDAT as
 * stored/uncompressed zlib blocks -- new copy in this new file, mesh_view.m
 * itself is not touched).
 * -----------------------------------------------------------------------*/
static uint32_t g_crc_table[256];
static int g_crc_ready;
static void crc32_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_table[n] = c;
    }
    g_crc_ready = 1;
}
static uint32_t crc32_update(uint32_t crc, const uint8_t* buf, size_t len) {
    if (!g_crc_ready) crc32_init();
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) crc = g_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}
static uint32_t adler32_calc(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0; const uint32_t MOD = 65521; size_t i = 0;
    while (i < len) {
        size_t chunk = (len - i < 5552) ? (len - i) : 5552;
        for (size_t j = 0; j < chunk; j++, i++) { a += data[i]; b += a; }
        a %= MOD; b %= MOD;
    }
    return (b << 16) | a;
}
static void put_u32be(uint8_t* p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static void write_chunk(FILE* f, const char* type, const uint8_t* data, uint32_t len) {
    uint8_t lenbuf[4]; put_u32be(lenbuf, len);
    fwrite(lenbuf, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t crc = crc32_update(0, (const uint8_t*)type, 4);
    if (len) crc = crc32_update(crc, data, len);
    uint8_t crcbuf[4]; put_u32be(crcbuf, crc);
    fwrite(crcbuf, 1, 4, f);
}
static int write_png(const char* path, const uint8_t* rgba, uint32_t w, uint32_t h) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("[png] cannot write %s\n", path); return -1; }
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13];
    put_u32be(ihdr+0, w); put_u32be(ihdr+4, h);
    ihdr[8]=8; ihdr[9]=6; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;
    write_chunk(f, "IHDR", ihdr, sizeof(ihdr));

    const size_t row_bytes = (size_t)w*4 + 1;
    const size_t raw_size = row_bytes * h;
    uint8_t* raw = malloc(raw_size ? raw_size : 1);
    for (uint32_t y = 0; y < h; y++) {
        uint8_t* row = raw + (size_t)y*row_bytes;
        row[0] = 0;
        memcpy(row+1, rgba + (size_t)y*w*4, (size_t)w*4);
    }

    const size_t max_deflate = raw_size + (raw_size/65535 + 1)*5 + 6;
    uint8_t* zbuf = malloc(max_deflate);
    size_t zp = 0;
    zbuf[zp++]=0x78; zbuf[zp++]=0x01;
    size_t off = 0;
    do {
        size_t block = raw_size - off;
        if (block > 65535) block = 65535;
        const uint8_t bfinal = (off+block >= raw_size) ? 1 : 0;
        zbuf[zp++] = bfinal;
        const uint16_t len = (uint16_t)block; const uint16_t nlen = (uint16_t)~len;
        zbuf[zp++]=(uint8_t)(len&0xFF); zbuf[zp++]=(uint8_t)(len>>8);
        zbuf[zp++]=(uint8_t)(nlen&0xFF); zbuf[zp++]=(uint8_t)(nlen>>8);
        memcpy(zbuf+zp, raw+off, block); zp += block; off += block;
    } while (off < raw_size);
    const uint32_t adler = adler32_calc(raw, raw_size);
    put_u32be(zbuf+zp, adler); zp += 4;

    write_chunk(f, "IDAT", zbuf, (uint32_t)zp);
    write_chunk(f, "IEND", NULL, 0);
    free(raw); free(zbuf); fclose(f);
    printf("[png] wrote %s (%ux%u)\n", path, w, h);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Shared setup: mesh, dynamic bbox/up-axis, Metal device/pso/texture/buffers.
 * Returns 0 on success, filling every out-param the two run modes need.
 * -----------------------------------------------------------------------*/
typedef struct {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLRenderPipelineState> psoTex;
    id<MTLRenderPipelineState> psoClay;
    id<MTLDepthStencilState> dss;
    id<MTLTexture> tex;
    id<MTLSamplerState> samp;
    int haveTexture;
    id<MTLBuffer> posBuf; /* interleaved pos+uv, buffer0 */
    id<MTLBuffer> nrmBuf; /* buffer1 */
    id<MTLBuffer> idxBuf;
    kmesh m;
    float center[3];
    float scale;
    int upAxis;      /* 0=X 1=Y 2=Z -- axis remapped to world +Y */
    float maxExtent;
    /* --skin-test (modo psarc): skinning REAL do WAD (2 bones/vertice +
     * peso) animado com bones PROCEDURAIS (o ANMX real ainda nao foi RE'd;
     * isto valida o caminho de deformacao nativo, nao a animacao do jogo).
     * Os indices BONI sao LOCAIS a paleta do segmento (aliasing entre partes
     * do corpo); a desambiguacao aqui e por COMPONENTE CONEXA: vertices
     * ligados por arestas com o mesmo bone dominante = um cluster com pivo
     * proprio (perna != torso mesmo com o mesmo indice local). */
    int      haveSkin;
    uint8_t* skinB0;
    uint8_t* skinB1;
    float*   skinW;
    float*   bindVtx;   /* copia da bind pose (nverts*5) */
    int*     vcomp0;    /* cluster do bone principal, por vertice */
    int*     vcomp1;    /* cluster do bone secundario (-1 = sem cluster) */
    float*   compPivot; /* ncomp * (x,y,z): centroide do cluster */
    int      ncomp;
    /* --rig: MONTAGEM REAL -- esqueleto do WAD (nomes+hierarquia+bind local),
     * skinning por bone GLOBAL (segmentos provados), FK + G*invBind. A pose
     * e procedural em JUNTAS REAIS (ANMX codec ainda em RE); em t=0 a malha
     * reproduz EXATA a bind pose (prova de roundtrip logada no arranque). */
    int       haveRealRig;
    wad_rig_t wrig;
    float*    invBind;    /* nbones*16: inverso do bind GLOBAL */
    float*    poseL;      /* nbones*16: local da pose corrente (scratch) */
    float*    poseG;      /* nbones*16: global da pose corrente */
    float*    skinM;      /* nbones*16: poseG * invBind */
    id<MTLBuffer> boneLineBuf;              /* overlay do esqueleto */
    id<MTLRenderPipelineState> psoLine;
    id<MTLDepthStencilState> dssLine;
    uint32_t  nBoneLines;
    /* multi-material (RE: lista MAT apos segmentos + TXR_file->GFX):
     * sub0 corpo->kratos01_2, sub1 saia + sub2 rosto->kratos01_1,
     * sub3 maos/pernas + sub4 baixo->kratos01_3 */
    uint8_t*  texRGBA[3];
    uint32_t  texW[3], texH[3];
    id<MTLTexture> texSlot[3];
    int       nTexSlots;
    uint8_t   subSlot[8];
    /* prop anexado (Laminas, mesh1): verts em espaco LOCAL do bone ->
     * p' = G[bone]*p (attach), sem invBind */
    int       haveProp;
    kmesh     prop;
    float*    propBind;
    uint8_t*  propG0;
    id<MTLBuffer> propPosBuf, propNrmBuf, propIdxBuf;
    /* GPU skinning + triple buffering (padrao Apple de sync CPU/GPU) + MSAA */
    id<MTLRenderPipelineState> psoSkinTex, psoSkinClay;
    id<MTLBuffer> boneDataBuf;   /* corpo: float3 (g0, g1, w) por vertice */
    id<MTLBuffer> propBoneBuf;   /* prop: float3 (nb+g0, nb+g0, 1) */
    id<MTLBuffer> matsBuf[3];    /* anel: 2*nbones float4x4 (S | G) */
    id<MTLBuffer> lineBufRing[3];
    dispatch_semaphore_t frameSem;
    int       frameSlot;
} rig_t;

#define RIG_MSAA 4

#define SKIN_MAX_COMPS 512

/* Nucleo comum aos modos de carga (ficheiros KMH2+PNG ou psarc nativo):
 * rig->m ja preenchida pelo caller; rgba (RGBA8, opcional) e ADOPTADO e
 * libertado aqui apos o upload para a MTLTexture. */
static int rig_build_core(rig_t* rig, uint8_t* rgba, uint32_t tw, uint32_t th) {
    kmesh_compute_normals(&rig->m);

    float mn[3], mx[3];
    kmesh_bbox(&rig->m, mn, mx);
    rig->center[0] = (mn[0]+mx[0]) * 0.5f;
    rig->center[1] = (mn[1]+mx[1]) * 0.5f;
    rig->center[2] = (mn[2]+mx[2]) * 0.5f;
    const float ext[3] = { mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2] };
    rig->upAxis = 1; rig->maxExtent = ext[1];
    if (ext[0] > rig->maxExtent) { rig->maxExtent = ext[0]; rig->upAxis = 0; }
    if (ext[2] > rig->maxExtent) { rig->maxExtent = ext[2]; rig->upAxis = 2; }
    rig->scale = 1.6f / rig->maxExtent;
    printf("[mesh] bbox X[%.2f,%.2f] Y[%.2f,%.2f] Z[%.2f,%.2f] center=(%.3f,%.3f,%.3f) "
           "upAxis=%c maxExtent=%.3f scale=%.5f\n",
           mn[0],mx[0], mn[1],mx[1], mn[2],mx[2],
           rig->center[0],rig->center[1],rig->center[2],
           "XYZ"[rig->upAxis], rig->maxExtent, rig->scale);

    rig->device = MTLCreateSystemDefaultDevice();
    if (!rig->device) { printf("[gpu] no Metal device\n"); return -1; }
    printf("[gpu] device: %s\n", rig->device.name.UTF8String);
    rig->queue = [rig->device newCommandQueue];

    NSError* err = nil;
    id<MTLLibrary> lib = [rig->device newLibraryWithSource:[NSString stringWithUTF8String:k_msl_source] options:nil error:&err];
    if (!lib) { printf("[gpu] MSL: %s\n", err ? err.localizedDescription.UTF8String : "?"); return -1; }

    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    vd.attributes[0].format = MTLVertexFormatFloat3; vd.attributes[0].offset = 0;  vd.attributes[0].bufferIndex = 0; /* position */
    vd.attributes[1].format = MTLVertexFormatFloat3; vd.attributes[1].offset = 0;  vd.attributes[1].bufferIndex = 1; /* normal */
    vd.attributes[2].format = MTLVertexFormatFloat2; vd.attributes[2].offset = 12; vd.attributes[2].bufferIndex = 0; /* uv (buffer0 @+12) */
    vd.layouts[0].stride = 20; vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex; /* interleaved pos+uv */
    vd.layouts[1].stride = 12; vd.layouts[1].stepFunction = MTLVertexStepFunctionPerVertex; /* normals */

    id<MTLFunction> vfn = [lib newFunctionWithName:@"mesh_vs"];
    id<MTLFunction> ffnTex  = [lib newFunctionWithName:@"mesh_fs_tex"];
    id<MTLFunction> ffnClay = [lib newFunctionWithName:@"mesh_fs_clay"];
    if (!vfn || !ffnTex || !ffnClay) { printf("[gpu] MSL entry points missing\n"); return -1; }

    MTLRenderPipelineDescriptor* pdTex = [MTLRenderPipelineDescriptor new];
    pdTex.vertexFunction = vfn; pdTex.fragmentFunction = ffnTex; pdTex.vertexDescriptor = vd;
    pdTex.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pdTex.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    rig->psoTex = [rig->device newRenderPipelineStateWithDescriptor:pdTex error:&err];
    if (!rig->psoTex) { printf("[gpu] PSO(tex): %s\n", err ? err.localizedDescription.UTF8String : "?"); return -1; }

    MTLRenderPipelineDescriptor* pdClay = [MTLRenderPipelineDescriptor new];
    pdClay.vertexFunction = vfn; pdClay.fragmentFunction = ffnClay; pdClay.vertexDescriptor = vd;
    pdClay.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pdClay.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    rig->psoClay = [rig->device newRenderPipelineStateWithDescriptor:pdClay error:&err];
    if (!rig->psoClay) { printf("[gpu] PSO(clay): %s\n", err ? err.localizedDescription.UTF8String : "?"); return -1; }

    MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionLess; dd.depthWriteEnabled = YES;
    rig->dss = [rig->device newDepthStencilStateWithDescriptor:dd];

    if (rgba && tw && th) {
        /* mipmapped + trilinear: o dithering baked das texturas PS2 vira
         * grao nos mips -- igual ao console, que nunca mostra o nivel 0 puro */
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                        width:tw height:th mipmapped:YES];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        rig->tex = [rig->device newTextureWithDescriptor:td];
        [rig->tex replaceRegion:MTLRegionMake2D(0,0,tw,th) mipmapLevel:0 withBytes:rgba bytesPerRow:(NSUInteger)tw*4];
        free(rgba);
        id<MTLCommandBuffer> mipCmd = [rig->queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [mipCmd blitCommandEncoder];
        [blit generateMipmapsForTexture:rig->tex];
        [blit endEncoding];
        [mipCmd commit];

        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = MTLSamplerMinMagFilterLinear;
        sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.mipFilter = MTLSamplerMipFilterLinear;
        sd.sAddressMode = MTLSamplerAddressModeRepeat;
        sd.tAddressMode = MTLSamplerAddressModeRepeat;
        rig->samp = [rig->device newSamplerStateWithDescriptor:sd];
        rig->haveTexture = (rig->tex && rig->samp) ? 1 : 0;
        printf("[tex] %ux%u -> %s\n", tw, th, rig->haveTexture ? "bound" : "FAILED to create MTLTexture/sampler");
    } else {
        free(rgba);
        rig->haveTexture = 0;
    }
    if (!rig->haveTexture) printf("[tex] fallback ACTIVE: rendering with mesh_fs_clay (no texture)\n");

    rig->posBuf = [rig->device newBufferWithBytes:rig->m.vtx length:(NSUInteger)rig->m.nverts*5*sizeof(float) options:MTLResourceStorageModeShared];
    rig->nrmBuf = [rig->device newBufferWithBytes:rig->m.nrm length:(NSUInteger)rig->m.nverts*3*sizeof(float) options:MTLResourceStorageModeShared];
    rig->idxBuf = [rig->device newBufferWithBytes:rig->m.idx length:(NSUInteger)rig->m.ntris*3*sizeof(uint32_t) options:MTLResourceStorageModeShared];
    if (!rig->posBuf || !rig->nrmBuf || !rig->idxBuf) { printf("[gpu] buffer alloc failed\n"); return -1; }

    if (rig->haveRealRig) {   /* pipelines do rig (MSAA 4x) + anel de frames */
        uint32_t nb = rig->wrig.nbones;
        id<MTLFunction> lv = [lib newFunctionWithName:@"line_vs"];
        id<MTLFunction> lf = [lib newFunctionWithName:@"line_fs"];
        MTLRenderPipelineDescriptor* pdL = [MTLRenderPipelineDescriptor new];
        pdL.vertexFunction = lv; pdL.fragmentFunction = lf;
        pdL.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pdL.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        pdL.rasterSampleCount = RIG_MSAA;
        rig->psoLine = [rig->device newRenderPipelineStateWithDescriptor:pdL error:&err];
        MTLDepthStencilDescriptor* ddL = [MTLDepthStencilDescriptor new];
        ddL.depthCompareFunction = MTLCompareFunctionAlways;
        ddL.depthWriteEnabled = NO;
        rig->dssLine = [rig->device newDepthStencilStateWithDescriptor:ddL];

        /* skinning na GPU: vertex descriptor com atributo skin (buffer 3) */
        MTLVertexDescriptor* vds = [MTLVertexDescriptor vertexDescriptor];
        vds.attributes[0].format = MTLVertexFormatFloat3; vds.attributes[0].offset = 0;  vds.attributes[0].bufferIndex = 0;
        vds.attributes[1].format = MTLVertexFormatFloat3; vds.attributes[1].offset = 0;  vds.attributes[1].bufferIndex = 1;
        vds.attributes[2].format = MTLVertexFormatFloat2; vds.attributes[2].offset = 12; vds.attributes[2].bufferIndex = 0;
        vds.attributes[3].format = MTLVertexFormatFloat3; vds.attributes[3].offset = 0;  vds.attributes[3].bufferIndex = 3;
        vds.layouts[0].stride = 20; vds.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        vds.layouts[1].stride = 12; vds.layouts[1].stepFunction = MTLVertexStepFunctionPerVertex;
        vds.layouts[3].stride = 12; vds.layouts[3].stepFunction = MTLVertexStepFunctionPerVertex;
        id<MTLFunction> svf = [lib newFunctionWithName:@"mesh_vs_skin"];
        MTLRenderPipelineDescriptor* pdS = [MTLRenderPipelineDescriptor new];
        pdS.vertexFunction = svf; pdS.fragmentFunction = ffnTex; pdS.vertexDescriptor = vds;
        pdS.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pdS.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        pdS.rasterSampleCount = RIG_MSAA;
        rig->psoSkinTex = [rig->device newRenderPipelineStateWithDescriptor:pdS error:&err];
        pdS.fragmentFunction = ffnClay;
        rig->psoSkinClay = [rig->device newRenderPipelineStateWithDescriptor:pdS error:&err];
        if (!rig->psoSkinTex || !rig->psoSkinClay || !rig->psoLine) {
            printf("[rig] PSO de skinning falhou: %s\n", err ? err.localizedDescription.UTF8String : "?");
            return -1;
        }

        /* dados de skin por vertice (float3 g0,g1,w) + anel triplo de
         * matrizes/linhas com semaforo (sync CPU/GPU, padrao Apple) */
        float* bd = malloc((size_t)rig->m.nverts * 3 * sizeof(float));
        for (uint32_t v = 0; v < rig->m.nverts; v++) {
            bd[v*3+0] = (float)rig->wrig.g0[v];
            bd[v*3+1] = (float)rig->wrig.g1[v];
            float w = rig->wrig.w[v];
            bd[v*3+2] = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
        }
        rig->boneDataBuf = [rig->device newBufferWithBytes:bd
            length:(NSUInteger)rig->m.nverts*3*sizeof(float) options:MTLResourceStorageModeShared];
        free(bd);
        for (int s = 0; s < 3; s++) {
            rig->matsBuf[s] = [rig->device newBufferWithLength:(NSUInteger)nb*2*16*sizeof(float)
                                                       options:MTLResourceStorageModeShared];
            rig->lineBufRing[s] = [rig->device newBufferWithLength:(NSUInteger)nb*2*3*sizeof(float)
                                                           options:MTLResourceStorageModeShared];
        }
        rig->frameSem = dispatch_semaphore_create(3);
        rig->frameSlot = 0;

        for (int s = 0; s < rig->nTexSlots; s++) {   /* texturas por material */
            if (!rig->texRGBA[s]) continue;
            MTLTextureDescriptor* td = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                              width:rig->texW[s] height:rig->texH[s] mipmapped:NO];
            td.usage = MTLTextureUsageShaderRead;
            td.storageMode = MTLStorageModeShared;
            rig->texSlot[s] = [rig->device newTextureWithDescriptor:td];
            [rig->texSlot[s] replaceRegion:MTLRegionMake2D(0,0,rig->texW[s],rig->texH[s])
                               mipmapLevel:0 withBytes:rig->texRGBA[s]
                               bytesPerRow:(NSUInteger)rig->texW[s]*4];
            free(rig->texRGBA[s]);
            rig->texRGBA[s] = NULL;
        }
        if (rig->haveProp) {   /* anexos: buffers proprios, mesma textura */
            kmesh_compute_normals(&rig->prop);
            rig->propPosBuf = [rig->device newBufferWithBytes:rig->prop.vtx
                length:(NSUInteger)rig->prop.nverts*5*sizeof(float) options:MTLResourceStorageModeShared];
            rig->propNrmBuf = [rig->device newBufferWithBytes:rig->prop.nrm
                length:(NSUInteger)rig->prop.nverts*3*sizeof(float) options:MTLResourceStorageModeShared];
            rig->propIdxBuf = [rig->device newBufferWithBytes:rig->prop.idx
                length:(NSUInteger)rig->prop.ntris*3*sizeof(uint32_t) options:MTLResourceStorageModeShared];
            /* anexo rigido na GPU: bone G = indice deslocado de nb, w=1 */
            float* pb = malloc((size_t)rig->prop.nverts * 3 * sizeof(float));
            for (uint32_t v = 0; v < rig->prop.nverts; v++) {
                pb[v*3+0] = (float)(nb + rig->propG0[v]);
                pb[v*3+1] = (float)(nb + rig->propG0[v]);
                pb[v*3+2] = 1.0f;
            }
            rig->propBoneBuf = [rig->device newBufferWithBytes:pb
                length:(NSUInteger)rig->prop.nverts*3*sizeof(float) options:MTLResourceStorageModeShared];
            free(pb);
        }
    }
    return 0;
}

/* desenho da malha: por grupo de material (rig real) ou inteira */
static void rig_draw_mesh(id<MTLRenderCommandEncoder> enc, rig_t* rig) {
    const wad_rig_t* wr = &rig->wrig;
    if (rig->haveRealRig && wr->ngroups && rig->nTexSlots && rig->haveTexture) {
        for (uint32_t g = 0; g < wr->ngroups; g++) {
            id<MTLTexture> t = rig->texSlot[rig->subSlot[wr->grp_sub[g] & 7]];
            if (t) [enc setFragmentTexture:t atIndex:0];
            if (!wr->grp_count[g]) continue;
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:(NSUInteger)wr->grp_count[g]*3
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:rig->idxBuf
                     indexBufferOffset:(NSUInteger)wr->grp_first[g]*3*sizeof(uint32_t)];
        }
    } else {
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:(NSUInteger)rig->m.ntris*3
                         indexType:MTLIndexTypeUInt32 indexBuffer:rig->idxBuf indexBufferOffset:0];
    }
    if (rig->haveRealRig && rig->haveProp && rig->propPosBuf) {
        if (rig->haveTexture) [enc setFragmentTexture:rig->tex atIndex:0];
        [enc setVertexBuffer:rig->propPosBuf offset:0 atIndex:0];
        [enc setVertexBuffer:rig->propNrmBuf offset:0 atIndex:1];
        [enc setVertexBuffer:rig->propBoneBuf offset:0 atIndex:3];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:(NSUInteger)rig->prop.ntris*3
                         indexType:MTLIndexTypeUInt32 indexBuffer:rig->propIdxBuf indexBufferOffset:0];
        [enc setVertexBuffer:rig->posBuf offset:0 atIndex:0];
        [enc setVertexBuffer:rig->nrmBuf offset:0 atIndex:1];
        [enc setVertexBuffer:rig->boneDataBuf offset:0 atIndex:3];
    }
}

/* Modo ficheiros (original): KMH2 pre-extraido + PNG pre-decodificado.
 * O memset (void*) e seguro/necessario so para os campos POD -- os campos
 * __strong ARC de um local acabado de declarar ja vem zerados. */
static int rig_build(rig_t* rig, const char* mesh_path, const char* tex_path) {
    memset((void*)rig, 0, sizeof(*rig));
    if (kmesh_load(mesh_path, &rig->m)) return -1;
    uint32_t tw = 0, th = 0;
    uint8_t* rgba = load_png_rgba(tex_path, &tw, &th);
    if (!rgba) printf("[tex] could not load %s -- falling back to clay shading\n", tex_path);
    return rig_build_core(rig, rgba, tw, th);
}

/* Modo NATIVO: psarc -> WAD -> malha+textura, tudo em memoria e em C
 * (psarc_read.c + wad_assets.c; deswizzle Morton do proprio engine em
 * rsx_texture_deswizzle.h). Nenhum passo Python. */
/* ------------------------------------------------------------------------
 * --rig: esqueleto real. FK, overrides anatomicos validados por metrica
 * (dist media vertice->bone 12.19 -> 4.48; ver notes/2026-07-22-*-re.md),
 * prova de roundtrip da bind pose, e pose procedural em juntas reais.
 * ---------------------------------------------------------------------- */
static const char* k_par_fix[][2] = {
    {"lFemur", "pelvis"}, {"rFemur", "pelvis"}, {"lTibia", "lFemur"}, {"rTibia", "rFemur"},
    {"lClavicle", "vertebrae4"}, {"rClavicle", "vertebrae4"},
    {"lHumerus", "lClavicle"}, {"rHumerus", "rClavicle"},
    {"leftBladeBack", "vertebrae4"}, {"rightBladeBack", "vertebrae4"},
};

static void rig_fk(const wad_rig_t* wr, const float* localM, float* globalM) {
    uint32_t nb = wr->nbones;
    uint8_t* done = calloc(nb, 1);
    uint32_t resolved = 0, guard = 0;
    while (resolved < nb && guard++ <= nb + 2) {
        for (uint32_t i = 0; i < nb; i++) {
            if (done[i]) continue;
            int p = wr->parent[i];
            if (p < 0) { memcpy(&globalM[i*16], &localM[i*16], 64); done[i] = 1; resolved++; }
            else if (done[p]) { mat4_multiply(&globalM[i*16], &globalM[p*16], &localM[i*16]); done[i] = 1; resolved++; }
        }
    }
    free(done);
}

static int rig_real_init(rig_t* rig, const uint8_t* wad, size_t wlen, uint32_t modl_off) {
    if (wad_rig_extract(wad, wlen, modl_off, &rig->wrig) != 0) return -1;
    wad_rig_t* wr = &rig->wrig;
    for (size_t k = 0; k < sizeof k_par_fix / sizeof *k_par_fix; k++) {
        int b = wad_rig_find_bone(wr, k_par_fix[k][0]);
        int p = wad_rig_find_bone(wr, k_par_fix[k][1]);
        if (b > 0 && p >= 0) wr->parent[b] = (int16_t)p;
    }
    uint32_t nb = wr->nbones;
    rig->invBind = malloc((size_t)nb * 16 * sizeof(float));
    rig->poseL  = malloc((size_t)nb * 16 * sizeof(float));
    rig->poseG  = malloc((size_t)nb * 16 * sizeof(float));
    rig->skinM  = malloc((size_t)nb * 16 * sizeof(float));
    rig_fk(wr, wr->local_bind, rig->poseG);           /* bind GLOBAL */
    for (uint32_t i = 0; i < nb; i++)
        mat4_affine_inverse(&rig->poseG[i*16], &rig->invBind[i*16]);
    /* PROVA de roundtrip: S = Gbind * invBind == I -> malha reproduz EXATA */
    double maxerr = 0.0;
    for (uint32_t i = 0; i < nb; i++) {
        float S[16];
        mat4_multiply(S, &rig->poseG[i*16], &rig->invBind[i*16]);
        for (int k = 0; k < 16; k++) {
            float ref = (k % 5 == 0) ? 1.0f : 0.0f;   /* diagonal 4x4 */
            double e = fabsf(S[k] - ref);
            if (e > maxerr) maxerr = e;
        }
    }
    printf("[rig] roundtrip bind: max|G*invBind - I| = %.2e (%u bones)\n", maxerr, nb);
    rig->haveRealRig = (maxerr < 1e-3) ? 1 : 0;
    if (!rig->haveRealRig) printf("[rig] FALHOU roundtrip -- rig desativado\n");
    return rig->haveRealRig ? 0 : -1;
}

/* ciclo procedural em juntas REAIS: {bone, eixo(0=X,2=Z), amp, fase, base} */
static const struct { const char* name; int axis; float amp; float phase; float base; } k_pose[] = {
    {"vertebrae2", 2, 0.06f, 0.0f, 0.00f}, {"vertebrae3", 2, 0.06f, 0.3f, 0.00f},
    {"vertebrae4", 0, 0.05f, 0.6f, 0.00f}, {"neck", 0, 0.10f, 0.9f, 0.00f},
    {"head", 0, 0.12f, 1.2f, 0.00f},       {"jaw", 0, 0.10f, 0.0f, 0.06f},
    {"lHumerus", 0, 0.45f, 0.0f, 0.00f},   {"rHumerus", 0, 0.45f, 3.14159f, 0.00f},
    {"lRadius", 0, 0.30f, 0.5f, -0.30f},   {"rRadius", 0, 0.30f, 3.64f, -0.30f},
    {"lFemur", 0, 0.40f, 3.14159f, 0.00f}, {"rFemur", 0, 0.40f, 0.0f, 0.00f},
    {"lTibia", 0, 0.28f, 3.6f, 0.30f},     {"rTibia", 0, 0.28f, 0.5f, 0.30f},
};

/* Prepara UM frame do rig no slot do anel: FK na CPU (123 matrizes, ~us);
 * o skinning (posicao+normal) roda na GPU (mesh_vs_skin le matsBuf[slot]).
 * Nao toca em nenhum vertex buffer -- sem hazard CPU/GPU. */
static void rig_real_frame(rig_t* rig, float t, int slot) {
    wad_rig_t* wr = &rig->wrig;
    uint32_t nb = wr->nbones;
    memcpy(rig->poseL, wr->local_bind, (size_t)nb * 16 * sizeof(float));
    const float W = 1.7f;   /* rad/s do ciclo */
    for (size_t k = 0; k < sizeof k_pose / sizeof *k_pose; k++) {
        int b = wad_rig_find_bone(wr, k_pose[k].name);
        if (b < 0) continue;
        float ang = k_pose[k].base + k_pose[k].amp * sinf(t * W + k_pose[k].phase);
        float R[16], L[16];
        if (k_pose[k].axis == 0) mat4_rotate_x(R, ang);
        else if (k_pose[k].axis == 1) mat4_rotate_y(R, ang);
        else mat4_rotate_z(R, ang);
        mat4_multiply(L, &rig->poseL[(size_t)b*16], R);
        memcpy(&rig->poseL[(size_t)b*16], L, 64);
    }
    rig_fk(wr, rig->poseL, rig->poseG);

    float* M = (float*)rig->matsBuf[slot].contents;
    for (uint32_t i = 0; i < nb; i++)
        mat4_multiply(&M[(size_t)i*16], &rig->poseG[(size_t)i*16], &rig->invBind[(size_t)i*16]);
    memcpy(M + (size_t)nb*16, rig->poseG, (size_t)nb * 16 * sizeof(float));   /* G p/ anexos */

    float* L = (float*)rig->lineBufRing[slot].contents;
    uint32_t nl = 0;
    for (uint32_t i = 1; i < nb; i++) {
        int p = wr->parent[i];
        if (p < 0) continue;
        L[nl*6+0] = rig->poseG[(size_t)p*16+12];
        L[nl*6+1] = rig->poseG[(size_t)p*16+13];
        L[nl*6+2] = rig->poseG[(size_t)p*16+14];
        L[nl*6+3] = rig->poseG[(size_t)i*16+12];
        L[nl*6+4] = rig->poseG[(size_t)i*16+13];
        L[nl*6+5] = rig->poseG[(size_t)i*16+14];
        nl++;
    }
    rig->nBoneLines = nl;
}

static int rig_build_psarc(rig_t* rig, const char* psarc_path, const char* wad_inner,
                           uint32_t modl_off, const char* tex_tag, int wantSkin, int wantRig) {
    memset((void*)rig, 0, sizeof(*rig));
    psarc_t* p = psarc_open(psarc_path);
    if (!p) return -1;
    size_t wlen = 0;
    uint8_t* wad = psarc_extract(p, wad_inner, &wlen);
    psarc_close(p);
    if (!wad) { printf("[psarc] path '%s' nao encontrado em %s\n", wad_inner, psarc_path); return -1; }
    printf("[psarc] %s: '%s' extraido (%zu bytes)\n", psarc_path, wad_inner, wlen);

    wad_mesh_t wm;
    if (wad_mesh_extract(wad, wlen, modl_off, &wm) != 0) {
        printf("[wad] malha MODL@%#x invalida\n", modl_off);
        free(wad);
        return -1;
    }
    rig->m.nverts = wm.nverts;
    rig->m.vtx = wm.vtx5;          /* mesmo layout do KMH2: 5 floats/vertice */
    rig->m.ntris = wm.ntris;
    rig->m.idx = wm.idx;
    printf("[wad] malha nativa: %u verts, %u tris (MODL@%#x)\n", wm.nverts, wm.ntris, modl_off);

    if (wantRig) {
        if (rig_real_init(rig, wad, wlen, modl_off) == 0) {
            rig->bindVtx = malloc((size_t)wm.nverts * 5 * sizeof(float));
            memcpy(rig->bindVtx, wm.vtx5, (size_t)wm.nverts * 5 * sizeof(float));
            printf("[rig] montagem real: %u bones, %u grupos de material\n",
                   rig->wrig.nbones, rig->wrig.ngroups);
            /* teste data-driven (coerencia de cor normalizada por aresta):
             * TODOS os grupos deste mesh usam kratos01_1 -- multi-textura
             * ficou como maquinaria p/ outros WADs (nTexSlots=0 aqui). */
            rig->nTexSlots = 0;

            /* props/anexos: outros MODLs do WAD (mesh1 = Laminas, 500 verts,
             * espaco LOCAL do bone -> anexa via G[bone], sem invBind) */
            uint32_t modls[8];
            int nm = wad_find_modls(wad, wlen, modls, 8);
            for (int k = 0; k < nm && !rig->haveProp; k++) {
                if (modls[k] == modl_off) continue;
                wad_mesh_t pm;
                wad_rig_t pr;
                if (wad_mesh_extract(wad, wlen, modls[k], &pm) != 0) continue;
                if (wad_rig_extract(wad, wlen, modls[k], &pr) != 0) { wad_mesh_free(&pm); continue; }
                rig->prop.nverts = pm.nverts;
                rig->prop.vtx = pm.vtx5;
                rig->prop.ntris = pm.ntris;
                rig->prop.idx = pm.idx;
                rig->propBind = malloc((size_t)pm.nverts * 5 * sizeof(float));
                memcpy(rig->propBind, pm.vtx5, (size_t)pm.nverts * 5 * sizeof(float));
                rig->propG0 = malloc(pm.nverts);
                memcpy(rig->propG0, pr.g0, pm.nverts);
                int b0 = pr.g0[0];
                printf("[prop] MODL@%#x: %u verts, %u tris, bone[0]=%s (anexo local)\n",
                       modls[k], pm.nverts, pm.ntris,
                       b0 < (int)rig->wrig.nbones ? rig->wrig.names + b0 * 24 : "?");
                free(pr.local_bind); free(pr.names); free(pr.g1); free(pr.w); free(pr.g0);
                rig->haveProp = 1;
            }
        } else {
            printf("[rig] --rig indisponivel neste WAD\n");
        }
    }

    if (wantSkin && !rig->haveRealRig) {
        wad_skin_t sk;
        if (wad_skin_extract(wad, wlen, modl_off, &sk) == 0 && sk.nverts == wm.nverts) {
            rig->skinB0 = sk.b0;
            rig->skinB1 = sk.b1;
            rig->skinW = sk.w;
            rig->bindVtx = malloc((size_t)wm.nverts * 5 * sizeof(float));
            memcpy(rig->bindVtx, wm.vtx5, (size_t)wm.nverts * 5 * sizeof(float));
            rig->haveSkin = 1;
            uint8_t maxb = 0;
            for (uint32_t i = 0; i < sk.nverts; i++) {
                if (sk.b0[i] > maxb) maxb = sk.b0[i];
                if (sk.b1[i] > maxb) maxb = sk.b1[i];
            }
            printf("[skin] BONI+pesos nativos: %u verts, indices 0..%u (2 bones/vert)\n",
                   sk.nverts, maxb);
        } else {
            printf("[skin] BONI indisponivel neste MODL -- skin-test desativado\n");
        }
    }

    uint32_t tw = 0, th = 0;
    uint8_t* rgba = NULL;
    if (wad_tex_decode(wad, wlen, tex_tag, &rgba, &tw, &th) == 0)
        printf("[wad] textura nativa '%s': %ux%u\n", tex_tag, tw, th);
    else
        printf("[wad] textura '%s' nao decodificada -- fallback clay\n", tex_tag);
    free(wad);
    return rig_build_core(rig, rgba, tw, th);
}

/* ---------------------------------------------------------------------------
 * --skin-test: deformacao com os DADOS REAIS de skinning (indices BONI +
 * peso w) e bones PROCEDURAIS. Clusters por componente conexa do bone
 * dominante desfazem o aliasing dos indices locais de paleta.
 * -----------------------------------------------------------------------*/
static int uf_find(int* uf, int x) {
    while (uf[x] != x) { uf[x] = uf[uf[x]]; x = uf[x]; }
    return x;
}

static void rig_skin_init(rig_t* rig) {
    if (rig->vcomp0) return;   /* ja inicializado */
    const uint32_t n = rig->m.nverts;
    uint8_t* dom = malloc(n);                 /* bone dominante por vertice */
    int* uf = malloc((size_t)n * sizeof(int));
    for (uint32_t v = 0; v < n; v++) {
        float w = rig->skinW[v];
        dom[v] = (w >= 0.5f) ? rig->skinB0[v] : rig->skinB1[v];
        uf[v] = (int)v;
    }
    /* uniao: arestas de triangulo com o mesmo bone dominante nas 2 pontas */
    for (uint32_t t = 0; t < rig->m.ntris; t++) {
        const uint32_t* i3 = &rig->m.idx[t * 3];
        for (int e = 0; e < 3; e++) {
            uint32_t a = i3[e], b = i3[(e + 1) % 3];
            if (dom[a] == dom[b]) {
                int ra = uf_find(uf, (int)a), rb = uf_find(uf, (int)b);
                if (ra != rb) uf[ra] = rb;
            }
        }
    }
    /* numera componentes e acumula centroides */
    int* compId = malloc((size_t)n * sizeof(int));
    rig->compPivot = calloc(SKIN_MAX_COMPS * 3, sizeof(float));
    double acc[SKIN_MAX_COMPS][4];
    memset(acc, 0, sizeof acc);
    int ncomp = 0;
    for (uint32_t v = 0; v < n; v++) compId[v] = -1;
    for (uint32_t v = 0; v < n; v++) {
        int r = uf_find(uf, (int)v);
        if (compId[r] < 0) compId[r] = (ncomp < SKIN_MAX_COMPS) ? ncomp++ : 0;
        int c = compId[r];
        acc[c][0] += rig->bindVtx[v * 5 + 0];
        acc[c][1] += rig->bindVtx[v * 5 + 1];
        acc[c][2] += rig->bindVtx[v * 5 + 2];
        acc[c][3] += 1.0;
    }
    rig->ncomp = ncomp;
    for (int c = 0; c < ncomp; c++) {
        rig->compPivot[c * 3 + 0] = (float)(acc[c][0] / acc[c][3]);
        rig->compPivot[c * 3 + 1] = (float)(acc[c][1] / acc[c][3]);
        rig->compPivot[c * 3 + 2] = (float)(acc[c][2] / acc[c][3]);
    }
    /* cluster por influencia: bone dominante -> proprio cluster; bone
     * secundario -> cluster de um vizinho de 1-ring cujo dominante = esse
     * bone (senao -1 = so translacao suave, sem rotacao) */
    rig->vcomp0 = malloc((size_t)n * sizeof(int));
    rig->vcomp1 = malloc((size_t)n * sizeof(int));
    for (uint32_t v = 0; v < n; v++) {
        int self = compId[uf_find(uf, (int)v)];
        rig->vcomp0[v] = (dom[v] == rig->skinB0[v]) ? self : -1;
        rig->vcomp1[v] = (dom[v] == rig->skinB1[v]) ? self : -1;
    }
    for (uint32_t t = 0; t < rig->m.ntris; t++) {
        const uint32_t* i3 = &rig->m.idx[t * 3];
        for (int e = 0; e < 3; e++) {
            uint32_t a = i3[e], b = i3[(e + 1) % 3];
            int cb = compId[uf_find(uf, (int)b)];
            if (rig->vcomp0[a] < 0 && dom[b] == rig->skinB0[a]) rig->vcomp0[a] = cb;
            if (rig->vcomp1[a] < 0 && dom[b] == rig->skinB1[a]) rig->vcomp1[a] = cb;
            int ca = compId[uf_find(uf, (int)a)];
            if (rig->vcomp0[b] < 0 && dom[a] == rig->skinB0[b]) rig->vcomp0[b] = ca;
            if (rig->vcomp1[b] < 0 && dom[a] == rig->skinB1[b]) rig->vcomp1[b] = ca;
        }
    }
    printf("[skin] clusters conexos: %d (pivos proprios por parte do corpo)\n", ncomp);
    free(dom);
    free(uf);
    free(compId);
}

/* p' = w*T[c0](p) + (1-w)*T[c1](p); T por CLUSTER = rotacao Z no pivo +
 * drift XZ, fase pseudo-aleatoria por cluster. Amplitudes em unidades do
 * modelo (corpo ~37u). Cluster -1: segue parado (identidade). */
static void rig_skin_update(rig_t* rig, float t) {
    float ca[SKIN_MAX_COMPS], sa[SKIN_MAX_COMPS], ox[SKIN_MAX_COMPS], oz[SKIN_MAX_COMPS];
    for (int c = 0; c < rig->ncomp; c++) {
        float ph = (float)c * 0.618034f;
        ph = (ph - floorf(ph)) * 6.2831853f;
        float a = 0.28f * sinf(t * 1.8f + ph);
        ca[c] = cosf(a);
        sa[c] = sinf(a);
        ox[c] = 0.55f * sinf(t * 1.3f + ph * 1.7f);
        oz[c] = 0.35f * cosf(t * 1.1f + ph * 2.3f);
    }
    for (uint32_t v = 0; v < rig->m.nverts; v++) {
        const float* b = &rig->bindVtx[v * 5];
        float w = rig->skinW[v];
        if (w < 0.0f) w = 0.0f;
        if (w > 1.0f) w = 1.0f;
        const int   cc[2] = { rig->vcomp0[v], rig->vcomp1[v] };
        const float bw[2] = { w, 1.0f - w };
        float outp[3] = { 0, 0, 0 };
        for (int j = 0; j < 2; j++) {
            if (bw[j] <= 0.0f) continue;
            int c = cc[j];
            if (c < 0) {
                outp[0] += bw[j] * b[0];
                outp[1] += bw[j] * b[1];
                outp[2] += bw[j] * b[2];
                continue;
            }
            float dx = b[0] - rig->compPivot[c * 3 + 0];
            float dy = b[1] - rig->compPivot[c * 3 + 1];
            float rx = ca[c] * dx - sa[c] * dy + rig->compPivot[c * 3 + 0] + ox[c];
            float ry = sa[c] * dx + ca[c] * dy + rig->compPivot[c * 3 + 1];
            float rz = b[2] + oz[c];
            outp[0] += bw[j] * rx;
            outp[1] += bw[j] * ry;
            outp[2] += bw[j] * rz;
        }
        rig->m.vtx[v * 5 + 0] = outp[0];
        rig->m.vtx[v * 5 + 1] = outp[1];
        rig->m.vtx[v * 5 + 2] = outp[2];
    }
    free(rig->m.nrm);
    rig->m.nrm = NULL;
    kmesh_compute_normals(&rig->m);
    memcpy(rig->posBuf.contents, rig->m.vtx, (size_t)rig->m.nverts * 5 * sizeof(float));
    memcpy(rig->nrmBuf.contents, rig->m.nrm, (size_t)rig->m.nverts * 3 * sizeof(float));
}

/* Model matrix shared by both run modes: T (center to origin) -> S (dynamic
 * scale) -> Rup (remap longest axis to +Y, fixed per-mesh) -> Ry (spin,
 * angle=0 for the single offscreen frame) -> Rx (fixed tilt). */
static void rig_model_matrix(const rig_t* rig, float spin_radians, float tilt_radians, float out_model[16]) {
    float T[16], S[16], Rup[16], Ry[16], Rx[16], tmp[16];
    mat4_translate(T, -rig->center[0], -rig->center[1], -rig->center[2]);
    mat4_scale(S, rig->scale);
    if (rig->upAxis == 1)      mat4_identity(Rup);
    else if (rig->upAxis == 0) mat4_rotate_z(Rup, deg2rad(90.0f));   /* X -> Y */
    else                       mat4_rotate_x(Rup, deg2rad(-90.0f));  /* Z -> Y */
    mat4_rotate_y(Ry, spin_radians);
    mat4_rotate_x(Rx, tilt_radians);

    mat4_multiply(tmp, S, T);
    mat4_multiply(tmp, Rup, tmp);
    mat4_multiply(tmp, Ry, tmp);
    mat4_multiply(out_model, Rx, tmp);
}

static void rig_bind_common(id<MTLRenderCommandEncoder> enc, const rig_t* rig) {
    id<MTLRenderPipelineState> pso;
    if (rig->haveRealRig && rig->psoSkinTex)
        pso = rig->haveTexture ? rig->psoSkinTex : rig->psoSkinClay;
    else
        pso = rig->haveTexture ? rig->psoTex : rig->psoClay;
    [enc setRenderPipelineState:pso];
    [enc setDepthStencilState:rig->dss];
    [enc setCullMode:MTLCullModeNone];
    [enc setVertexBuffer:rig->posBuf offset:0 atIndex:0];
    [enc setVertexBuffer:rig->nrmBuf offset:0 atIndex:1];
    if (rig->haveRealRig && rig->psoSkinTex) {
        [enc setVertexBuffer:rig->boneDataBuf offset:0 atIndex:3];
        [enc setVertexBuffer:rig->matsBuf[rig->frameSlot] offset:0 atIndex:4];
    }
    if (rig->haveTexture) {
        [enc setFragmentTexture:rig->tex atIndex:0];
        [enc setFragmentSamplerState:rig->samp atIndex:0];
    }
}

/* ---------------------------------------------------------------------------
 * Offscreen verification mode ("--png"): one frame, fixed hero angle, no
 * window/display needed -- for headless environments (adapted from
 * mesh_view.m's offscreen render + PNG readback + fill/luminance self-check).
 * -----------------------------------------------------------------------*/
#define OUT_W 512u
#define OUT_H 512u

static int run_offscreen_png(rig_t* rig) {
    const char* out_dir  = "/tmp/kratosout";
    const char* out_path = rig->haveRealRig ? "/tmp/kratosout/mesh_rig.png"
                         : rig->haveSkin    ? "/tmp/kratosout/mesh_skin.png"
                                            : "/tmp/kratosout/mesh_tex.png";
    if (rig->haveRealRig) {
        rig->frameSlot = 0;
        rig_real_frame(rig, 1.2f, 0);   /* pose fixa p/ frame deterministico */
    } else if (rig->haveSkin) {
        rig_skin_init(rig);
        rig_skin_update(rig, 1.2f);   /* fase fixa p/ frame deterministico */
        float mn[3], mx[3];
        kmesh_bbox(&rig->m, mn, mx);
        printf("[skin] frame t=1.2: bbox deformada X[%.2f,%.2f] Y[%.2f,%.2f] Z[%.2f,%.2f]\n",
               mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);
    }

    MTLTextureDescriptor* ctd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:OUT_W height:OUT_H mipmapped:NO];
    ctd.usage = MTLTextureUsageRenderTarget; ctd.storageMode = MTLStorageModeShared;
    id<MTLTexture> colorTex = [rig->device newTextureWithDescriptor:ctd];

    MTLTextureDescriptor* dtd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:OUT_W height:OUT_H mipmapped:NO];
    dtd.usage = MTLTextureUsageRenderTarget; dtd.storageMode = MTLStorageModePrivate;
    if (rig->haveRealRig) { dtd.textureType = MTLTextureType2DMultisample; dtd.sampleCount = RIG_MSAA; }
    id<MTLTexture> depthTex = [rig->device newTextureWithDescriptor:dtd];
    id<MTLTexture> msaaColor = nil;
    if (rig->haveRealRig) {   /* MSAA 4x + resolve p/ colorTex */
        MTLTextureDescriptor* mc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:OUT_W height:OUT_H mipmapped:NO];
        mc.textureType = MTLTextureType2DMultisample; mc.sampleCount = RIG_MSAA;
        mc.usage = MTLTextureUsageRenderTarget; mc.storageMode = MTLStorageModePrivate;
        msaaColor = [rig->device newTextureWithDescriptor:mc];
    }
    if (!colorTex || !depthTex) { printf("[gpu] offscreen texture create failed\n"); return 1; }

    float model[16], V[16], P[16], mv[16], mvp[16];
    rig_model_matrix(rig, deg2rad(25.0f), deg2rad(12.0f), model);
    mat4_translate(V, 0.0f, 0.0f, -2.6f);
    mat4_perspective(P, deg2rad(45.0f), (float)OUT_W/(float)OUT_H, 0.1f, 10.0f);
    mat4_multiply(mv, V, model);
    mat4_multiply(mvp, P, mv);
    uniforms_t u; memcpy(u.mvp, mvp, 64); memcpy(u.mv, mv, 64);

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    if (msaaColor) {
        rpd.colorAttachments[0].texture = msaaColor;
        rpd.colorAttachments[0].resolveTexture = colorTex;
        rpd.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    } else {
        rpd.colorAttachments[0].texture = colorTex;
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.12, 0.25, 1.0);
    rpd.depthAttachment.texture = depthTex;
    rpd.depthAttachment.loadAction = MTLLoadActionClear;
    rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
    rpd.depthAttachment.clearDepth = 1.0;

    id<MTLCommandBuffer> cmd = [rig->queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
    rig_bind_common(enc, rig);
    [enc setViewport:(MTLViewport){0.0, 0.0, (double)OUT_W, (double)OUT_H, 0.0, 1.0}];
    [enc setVertexBytes:&u length:sizeof(u) atIndex:2];
    rig_draw_mesh(enc, rig);
    if (rig->haveRealRig && rig->psoLine && rig->nBoneLines) {
        [enc setRenderPipelineState:rig->psoLine];
        [enc setDepthStencilState:rig->dssLine];
        [enc setVertexBuffer:rig->lineBufRing[rig->frameSlot] offset:0 atIndex:0];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:2];
        [enc drawPrimitives:MTLPrimitiveTypeLine vertexStart:0 vertexCount:(NSUInteger)rig->nBoneLines*2];
    }
    [enc endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];

    mkdir(out_dir, 0755);
    const size_t pitch = (size_t)OUT_W * 4;
    uint8_t* bgra = malloc(pitch * OUT_H);
    [colorTex getBytes:bgra bytesPerRow:pitch fromRegion:MTLRegionMake2D(0,0,OUT_W,OUT_H) mipmapLevel:0];
    uint8_t* rgba = malloc(pitch * OUT_H);
    for (size_t i = 0; i < (size_t)OUT_W*OUT_H; i++) {
        rgba[i*4+0]=bgra[i*4+2]; rgba[i*4+1]=bgra[i*4+1]; rgba[i*4+2]=bgra[i*4+0]; rgba[i*4+3]=bgra[i*4+3];
    }
    write_png(out_path, rgba, OUT_W, OUT_H);

    /* Self-check (same method as mesh_view.m): fill% + center-column run +
     * luminance range, so a blank/degenerate render is caught right here. */
    const uint8_t bg[3] = { (uint8_t)lroundf(0.06f*255.0f), (uint8_t)lroundf(0.12f*255.0f), (uint8_t)lroundf(0.25f*255.0f) };
    const int EPS = 10;
    uint32_t filled_total = 0;
    for (size_t i = 0; i < (size_t)OUT_W*OUT_H; i++) {
        int dr=(int)rgba[i*4+0]-bg[0], dg=(int)rgba[i*4+1]-bg[1], db=(int)rgba[i*4+2]-bg[2];
        if (abs(dr) > EPS || abs(dg) > EPS || abs(db) > EPS) filled_total++;
    }
    printf("[check] fill=%.1f%% (%u/%u px)\n", 100.0*filled_total/((double)OUT_W*OUT_H), filled_total, OUT_W*OUT_H);

    /* Explicit pixel-variation sample: proves the texture actually maps
     * (varying colors across the figure), not a solid/garbage fill. */
    printf("[check] pixel samples (x,y)=RGBA:\n");
    static const float fx[5] = {0.35f,0.45f,0.50f,0.55f,0.65f};
    static const float fy[5] = {0.30f,0.40f,0.50f,0.60f,0.75f};
    for (int r = 0; r < 5; r++) {
        uint32_t x = (uint32_t)(fx[r]*OUT_W), y = (uint32_t)(fy[r]*OUT_H);
        size_t i = (size_t)y*OUT_W + x;
        printf("  (%u,%u)=(%u,%u,%u,%u)\n", x, y, rgba[i*4+0], rgba[i*4+1], rgba[i*4+2], rgba[i*4+3]);
    }

    free(bgra); free(rgba);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Windowed spin mode: SDL2 + CAMetalLayer swapchain + Y-spin loop + depth +
 * resize + MESH_SPIN_FRAMES headless-smoke hook (same shape as mesh_spin.m).
 * -----------------------------------------------------------------------*/
/* ---------------------------------------------------------------------------
 * Present sincronizado ao display via CAMetalDisplayLink (macOS 14+, padrao
 * Apple para animacao suave sem jitter): a GPU e' chamada exactamente na
 * cadencia do ecra, o drawable ja vem temporizado (update.drawable) e o
 * present e' agendado no instante-alvo (targetPresentationTimestamp), em vez
 * de um loop livre com SDL_GetTicks. Fallback para o loop manual quando nao ha
 * display (smoke headless) ou em macOS < 14.
 * -----------------------------------------------------------------------*/
typedef struct {
    rig_t*          rig;
    SDL_Window*     win;
    CAMetalLayer*   layer;         /* __unsafe_unretained via campo separado */
    id<MTLTexture>  depthTex, msaaColor;
    NSUInteger      dtw, dth;
    long            frame, max_frames;
    int             running;
    double          t0;            /* 1o timestamp de present -> anim comeca em 0 */
    CFRunLoopRef    runloop;
} spin_ctx;

/* Recria depth/MSAA no resize e devolve o tamanho corrente do drawable. */
static int spin_ensure_targets(spin_ctx* c, int* out_pw, int* out_ph) {
    int pw = 0, ph = 0; SDL_Metal_GetDrawableSize(c->win, &pw, &ph);
    if (pw <= 0 || ph <= 0) return 0;
    c->layer.drawableSize = CGSizeMake(pw, ph);
    if (!c->depthTex || c->dtw != (NSUInteger)pw || c->dth != (NSUInteger)ph) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:pw height:ph mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModePrivate;
        if (c->rig->haveRealRig) { td.textureType = MTLTextureType2DMultisample; td.sampleCount = RIG_MSAA; }
        c->depthTex = [c->rig->device newTextureWithDescriptor:td]; c->dtw = pw; c->dth = ph;
        if (c->rig->haveRealRig) {
            MTLTextureDescriptor* mc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:pw height:ph mipmapped:NO];
            mc.textureType = MTLTextureType2DMultisample; mc.sampleCount = RIG_MSAA;
            mc.usage = MTLTextureUsageRenderTarget; mc.storageMode = MTLStorageModePrivate;
            c->msaaColor = [c->rig->device newTextureWithDescriptor:mc];
        }
    }
    *out_pw = pw; *out_ph = ph;
    return 1;
}

/* Corpo de render de UM frame (partilhado pelo display-link e pelo smoke).
 * Usa sempre presentDrawable: simples — com CAMetalDisplayLink o proprio link
 * agenda a apresentacao no instante-alvo (presentDrawable:atTime: e' proibido
 * nesse modo); no smoke o present imediato serve. */
static void spin_render_frame(spin_ctx* c, id<CAMetalDrawable> drawable,
                              double animSec) {
    rig_t* rig = c->rig;
    int pw = (int)c->dtw, ph = (int)c->dth;
    if (rig->haveRealRig) {
        dispatch_semaphore_wait(rig->frameSem, DISPATCH_TIME_FOREVER);
        rig->frameSlot = (int)(c->frame % 3);
        rig_real_frame(rig, (float)animSec, rig->frameSlot);
    } else if (rig->haveSkin) {
        rig_skin_update(rig, (float)animSec);
    }
    float angle = (float)animSec * 0.9f;
    float model[16], V[16], P[16], mv[16], mvp[16];
    rig_model_matrix(rig, angle, deg2rad(12.0f), model);
    mat4_translate(V, 0.0f, 0.0f, -2.6f);
    mat4_perspective(P, deg2rad(45.0f), (float)pw/(float)ph, 0.1f, 10.0f);
    mat4_multiply(mv, V, model);
    mat4_multiply(mvp, P, mv);
    uniforms_t u; memcpy(u.mvp, mvp, 64); memcpy(u.mv, mv, 64);

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    if (rig->haveRealRig && c->msaaColor) {
        rp.colorAttachments[0].texture = c->msaaColor;
        rp.colorAttachments[0].resolveTexture = drawable.texture;
        rp.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    } else {
        rp.colorAttachments[0].texture = drawable.texture;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.12, 0.25, 1.0);
    rp.depthAttachment.texture = c->depthTex;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionDontCare;
    rp.depthAttachment.clearDepth = 1.0;

    id<MTLCommandBuffer> cmd = [rig->queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    rig_bind_common(enc, rig);
    [enc setViewport:(MTLViewport){0, 0, (double)pw, (double)ph, 0, 1}];
    [enc setVertexBytes:&u length:sizeof(u) atIndex:2];
    rig_draw_mesh(enc, rig);
    if (rig->haveRealRig && rig->psoLine && rig->nBoneLines) {
        [enc setRenderPipelineState:rig->psoLine];
        [enc setDepthStencilState:rig->dssLine];
        [enc setVertexBuffer:rig->lineBufRing[rig->frameSlot] offset:0 atIndex:0];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:2];
        [enc drawPrimitives:MTLPrimitiveTypeLine vertexStart:0 vertexCount:(NSUInteger)rig->nBoneLines*2];
    }
    [enc endEncoding];
    [cmd presentDrawable:drawable];
    if (rig->haveRealRig && rig->frameSem) {
        dispatch_semaphore_t sem = rig->frameSem;
        [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) { (void)cb; dispatch_semaphore_signal(sem); }];
    }
    [cmd commit];
    c->frame++;
}

/* Bombeia eventos SDL; devolve 0 quando deve sair. */
static int spin_pump_events(spin_ctx* c) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) c->running = 0;
        else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) c->running = 0;
        else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) c->running = 0;
    }
    return c->running;
}

API_AVAILABLE(macos(14.0))
@interface SpinDisplayLinkDelegate : NSObject <CAMetalDisplayLinkDelegate>
@property(nonatomic) spin_ctx* ctx;
@end
@implementation SpinDisplayLinkDelegate
- (void)metalDisplayLink:(CAMetalDisplayLink*)link needsUpdate:(CAMetalDisplayLinkUpdate*)update {
    (void)link;
    spin_ctx* c = self.ctx;
    if (!spin_pump_events(c)) return;   /* o driver abaixo sai quando running=0 */
    int pw, ph;
    if (!spin_ensure_targets(c, &pw, &ph)) return;
    if (c->t0 == 0.0) c->t0 = update.targetPresentationTimestamp;
    double animSec = update.targetPresentationTimestamp - c->t0;
    spin_render_frame(c, update.drawable, animSec);   /* update.drawable ja temporizado */
}
@end

static int run_windowed_spin(rig_t* rig) {
    const char* fenv = getenv("MESH_SPIN_FRAMES");
    const long max_frames = fenv ? atol(fenv) : 0;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) { printf("[sdl] init: %s\n", SDL_GetError()); return 2; }
    SDL_Window* win = SDL_CreateWindow("Kratos - GoW2 (recomp, Metal, textured)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 800,
        SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { printf("[sdl] CreateWindow: %s (sem display? corre num ecra real)\n", SDL_GetError()); return 2; }
    SDL_MetalView mview = SDL_Metal_CreateView(win);
    if (!mview) { printf("[sdl] Metal_CreateView: %s\n", SDL_GetError()); return 2; }
    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(mview);
    layer.device = rig->device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;

    if (rig->haveSkin && !rig->haveRealRig) rig_skin_init(rig);

    spin_ctx c = {0};
    c.rig = rig; c.win = win; c.layer = layer; c.running = 1; c.max_frames = max_frames;

    /* Modo interativo: present sincronizado ao display via CAMetalDisplayLink
     * (macOS 14+). O smoke headless (MESH_SPIN_FRAMES) e macOS < 14 usam o loop
     * manual — sem display para sincronizar. */
    int used_display_link = 0;
    if (max_frames == 0) if (@available(macOS 14.0, *)) {
        CAMetalDisplayLink* dl = [[CAMetalDisplayLink alloc] initWithMetalLayer:layer];
        SpinDisplayLinkDelegate* del = [SpinDisplayLinkDelegate new];
        c.runloop = CFRunLoopGetCurrent();
        del.ctx = &c;
        dl.delegate = del;
        dl.preferredFrameRateRange = CAFrameRateRangeMake(30.0f, 120.0f, 60.0f);
        [dl addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
        printf("[spin] present sincronizado ao display (CAMetalDisplayLink)\n");
        /* Fatiar o run loop em vez de CFRunLoopRun(): o display link PAUSA
         * quando a janela nao esta no ecra (retornaria de CFRunLoopRun) — o
         * while sobrevive a pausas, e o pump de eventos aqui e' a rede de
         * seguranca para detetar quit quando o delegate nao esta a correr. */
        while (c.running) {
            @autoreleasepool {
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
                if (!spin_pump_events(&c)) break;
            }
        }
        [dl removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
        [dl invalidate];
        used_display_link = 1;
    }

    if (!used_display_link) {
        /* loop manual (smoke headless / fallback): nextDrawable + present imediato */
        while (c.running) {
            if (!spin_pump_events(&c)) break;
            int pw, ph;
            if (!spin_ensure_targets(&c, &pw, &ph)) { SDL_Delay(8); continue; }
            id<CAMetalDrawable> drawable = [layer nextDrawable];
            if (!drawable) { SDL_Delay(4); continue; }
            spin_render_frame(&c, drawable, (double)SDL_GetTicks64() * 0.001);
            if (max_frames && c.frame >= max_frames) {
                printf("[spin] headless smoke: %ld frames ok\n", c.frame);
                break;
            }
        }
    }

    SDL_Metal_DestroyView(mview);
    SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    printf("[spin] fim (%ld frames)\n", c.frame);
    return 0;
}

static int is_psarc_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    char m[4] = {0};
    size_t got = fread(m, 1, 4, f);
    fclose(f);
    return got == 4 && memcmp(m, "PSAR", 4) == 0;
}

int main(int argc, char** argv) {
@autoreleasepool {
    int wantPng = 0, wantSkin = 0, wantRig = 0;
    const char* pos[4] = {0};
    int npos = 0;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--png") == 0) wantPng = 1;
        else if (strcmp(argv[a], "--skin-test") == 0) wantSkin = 1;
        else if (strcmp(argv[a], "--rig") == 0) wantRig = 1;
        else if (npos < 4) pos[npos++] = argv[a];
    }

    rig_t rig;
    if (pos[0] && is_psarc_file(pos[0])) {
        /* nativo: <psarc> [path_wad] [modl_hex] [tag_textura] */
        const char* wad_inner = pos[1] ? pos[1] : "wad/r_hero01.wad_ps3";
        uint32_t modl_off = pos[2] ? (uint32_t)strtoul(pos[2], NULL, 16) : 0x2d29cbu;
        const char* tag = pos[3] ? pos[3] : "~000kratos01_1";
        if (rig_build_psarc(&rig, pos[0], wad_inner, modl_off, tag, wantSkin, wantRig)) return 1;
    } else {
        if (wantSkin) printf("[skin] --skin-test requer o modo psarc (BONI vem do WAD) -- ignorado\n");
        const char* mesh_path = pos[0] ? pos[0] : "/tmp/kratos_tex.mesh";
        const char* tex_path  = pos[1] ? pos[1] : "/tmp/kratos_tex.png";
        if (rig_build(&rig, mesh_path, tex_path)) return 1;
    }

    return wantPng ? run_offscreen_png(&rig) : run_windowed_spin(&rig);
}
}
