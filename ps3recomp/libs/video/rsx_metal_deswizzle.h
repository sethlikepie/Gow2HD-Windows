/*
 * ps3recomp - GPU (Metal compute) deswizzle de texturas RSX (Morton -> linear).
 *
 * Offload para a GPU do que o backend real (rsx_metal_backend.m,
 * metal_bind_texture) hoje faz na CPU por CADA bind de textura via
 * rsx_deswizzle_texels() -- um hot path no jogo real. O kernel reproduz
 * EXACTAMENTE a mesma regra de enderecamento (rsx_morton_offset), verificada
 * byte-a-byte contra a versao CPU (tests/test_gpu_deswizzle.m).
 *
 * Uso (backend): rmd_init(device) uma vez; por bind, rmd_run(...) com os bytes
 * swizzled do guest -> buffer linear pronto para upload/copy na MTLTexture.
 * Formato: 32bpp (ARGB8/BGRA8), 4 bytes/texel tratados como uint -- o caso
 * dominante do GoW2; formatos block-compressed nao sao Morton (o caller ja os
 * copia verbatim, tal como na versao CPU).
 */
#ifndef PS3RECOMP_RSX_METAL_DESWIZZLE_H
#define PS3RECOMP_RSX_METAL_DESWIZZLE_H

#import <Metal/Metal.h>
#include <simd/simd.h>   /* simd_uint2 == uint2 do MSL (layout/alinhamento) */
#include <stdint.h>

/* MSL: mesma aritmetica de rsx_texture_deswizzle.h (rsx_log2_ceil +
 * rsx_morton_offset), um thread por texel de saida. */
static NSString* const RMD_MSL_SOURCE = @""
"#include <metal_stdlib>\n using namespace metal;\n"
"static uint rmd_log2_ceil(uint n){ uint l=0; while((1u<<l)<n) l++; return l; }\n"
"static uint rmd_morton(uint x,uint y,uint lw,uint lh){\n"
"  uint off=0,shift=0,xi=0,yi=0,rx=lw,ry=lh;\n"
"  while(rx||ry){ if(rx){off|=((x>>xi)&1u)<<shift; shift++;xi++;rx--;}\n"
"                 if(ry){off|=((y>>yi)&1u)<<shift; shift++;yi++;ry--;} }\n"
"  return off; }\n"
"kernel void rmd_deswizzle32(device const uint* src [[buffer(0)]],\n"
"                            device uint* dst       [[buffer(1)]],\n"
"                            constant uint2& dim    [[buffer(2)]],\n"
"                            uint2 gid [[thread_position_in_grid]]) {\n"
"  if (gid.x >= dim.x || gid.y >= dim.y) return;\n"
"  uint lw = rmd_log2_ceil(dim.x), lh = rmd_log2_ceil(dim.y);\n"
"  uint s = rmd_morton(gid.x, gid.y, lw, lh);\n"
"  dst[gid.y*dim.x + gid.x] = src[s];\n"
"}\n"
"kernel void rmd_deswizzle32_tex(device const uint* src [[buffer(0)]],\n"
"                               constant uint2& dim    [[buffer(1)]],\n"
"                               texture2d<float, access::write> dst [[texture(0)]],\n"
"                               uint2 gid [[thread_position_in_grid]]) {\n"
"  if (gid.x >= dim.x || gid.y >= dim.y) return;\n"
"  uint lw = rmd_log2_ceil(dim.x), lh = rmd_log2_ceil(dim.y);\n"
"  uint s = rmd_morton(gid.x, gid.y, lw, lh);\n"
"  uint t = src[s];\n"
"  float a = float(t & 255u) / 255.0;\n"
"  float r = float((t >> 8) & 255u) / 255.0;\n"
"  float g = float((t >> 16) & 255u) / 255.0;\n"
"  float b = float((t >> 24) & 255u) / 255.0;\n"
"  dst.write(float4(r, g, b, a), gid);\n"
"}\n";

typedef struct {
    id<MTLComputePipelineState> pso;
    id<MTLComputePipelineState> pso_tex;
    id<MTLDevice> device;
} rmd_ctx;

static inline int rmd_init(rmd_ctx* c, id<MTLDevice> device) {
    c->device = device;
    c->pso_tex = nil;
    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:RMD_MSL_SOURCE options:nil error:&err];
    if (!lib) { NSLog(@"[rmd] MSL: %@", err); return -1; }
    id<MTLFunction> fn = [lib newFunctionWithName:@"rmd_deswizzle32"];
    if (!fn) return -1;
    c->pso = [device newComputePipelineStateWithFunction:fn error:&err];
    if (!c->pso) { NSLog(@"[rmd] PSO: %@", err); return -1; }
    id<MTLFunction> fnt = [lib newFunctionWithName:@"rmd_deswizzle32_tex"];
    if (fnt)
        c->pso_tex = [device newComputePipelineStateWithFunction:fnt error:&err];
    return 0;
}

/* Deswizzle 32bpp: src_bytes = w_pot*h_pot*4 (extensao pow2 do Morton), escreve
 * w*h*4 bytes lineares em dst_out. Sincrono (espera a GPU). 0 = OK. */
static inline int rmd_run(rmd_ctx* c, id<MTLCommandQueue> queue,
                          const void* src_bytes, size_t src_len,
                          void* dst_out, uint32_t w, uint32_t h) {
    const size_t dst_len = (size_t)w * h * 4;
    id<MTLBuffer> src = [c->device newBufferWithBytes:src_bytes length:src_len
                                              options:MTLResourceStorageModeShared];
    id<MTLBuffer> dst = [c->device newBufferWithLength:dst_len
                                              options:MTLResourceStorageModeShared];
    if (!src || !dst) return -1;
    simd_uint2 dim = { w, h };  /* simd_uint2 == uint2 do MSL */

    id<MTLCommandBuffer> cmd = [queue commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:c->pso];
    [enc setBuffer:src offset:0 atIndex:0];
    [enc setBuffer:dst offset:0 atIndex:1];
    [enc setBytes:&dim length:sizeof dim atIndex:2];
    MTLSize grid = MTLSizeMake(w, h, 1);
    NSUInteger tw = c->pso.threadExecutionWidth;
    NSUInteger th = c->pso.maxTotalThreadsPerThreadgroup / tw;
    if (th < 1) th = 1;
    [enc dispatchThreads:grid threadsPerThreadgroup:MTLSizeMake(tw, th, 1)];
    [enc endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];

    memcpy(dst_out, dst.contents, dst_len);
    return 0;
}

/* Deferred 32bpp: copy guest Morton bytes into a shared buffer, write BGRA
 * into the MTLTexture at present (same command buffer as the draws). One
 * compute encoder burst per frame, no per-bind waitUntilCompleted. */
#ifndef RMD_MAX_JOBS
#define RMD_MAX_JOBS 256
#endif

typedef struct {
    id<MTLBuffer>  src;
    id<MTLTexture> tex;
    uint32_t       w, h;
} rmd_job;

typedef struct {
    rmd_job jobs[RMD_MAX_JOBS];
    int     n;
} rmd_queue;

static inline void rmd_queue_init(rmd_queue* q) { if (q) q->n = 0; }

static inline int rmd_queue_push(rmd_queue* q, id<MTLDevice> device,
                                 const void* src_bytes, size_t src_len,
                                 id<MTLTexture> tex, uint32_t w, uint32_t h)
{
    if (!q || !device || !src_bytes || !tex || !w || !h || q->n >= RMD_MAX_JOBS)
        return -1;
    id<MTLBuffer> src = [device newBufferWithBytes:src_bytes length:src_len
                                           options:MTLResourceStorageModeShared];
    if (!src)
        return -1;
    q->jobs[q->n].src = src;
    q->jobs[q->n].tex = tex;
    q->jobs[q->n].w = w;
    q->jobs[q->n].h = h;
    q->n++;
    return 0;
}

/* Encode every pending job onto cb. Guest ARGB-BE Morton -> BGRA8 texture. */
static inline void rmd_queue_encode(rmd_ctx* c, rmd_queue* q, id<MTLCommandBuffer> cb)
{
    if (!c || !c->pso_tex || !q || !cb || q->n <= 0)
        return;
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    if (!enc) {
        q->n = 0;
        return;
    }
    [enc setComputePipelineState:c->pso_tex];
    NSUInteger tw = c->pso.threadExecutionWidth;
    NSUInteger th = c->pso.maxTotalThreadsPerThreadgroup / tw;
    if (th < 1) th = 1;
    for (int i = 0; i < q->n; i++) {
        rmd_job* j = &q->jobs[i];
        simd_uint2 dim = { j->w, j->h };
        [enc setBuffer:j->src offset:0 atIndex:0];
        [enc setBytes:&dim length:sizeof dim atIndex:1];
        [enc setTexture:j->tex atIndex:0];
        [enc dispatchThreads:MTLSizeMake(j->w, j->h, 1)
       threadsPerThreadgroup:MTLSizeMake(tw, th, 1)];
        j->src = nil;
        j->tex = nil;
    }
    [enc endEncoding];
    q->n = 0;
}

#endif /* PS3RECOMP_RSX_METAL_DESWIZZLE_H */
