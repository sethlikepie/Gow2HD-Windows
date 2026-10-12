/*
 * ps3recomp - real-time windowed Metal mesh viewer (macOS/arm64)
 *
 * Shows a real GoW2 Kratos mesh SPINNING around its vertical (Y) axis in an
 * on-screen SDL2 window, solid with a depth buffer + two-sided diffuse light.
 * The mesh load, smooth-normal computation, 4x4 matrix helpers, MSL shaders and
 * the Metal pipeline are the same as the offscreen mesh_view.m (same dir); this
 * file swaps the offscreen texture + PNG for an SDL2/CAMetalLayer swapchain and
 * a per-frame animation loop. SDL2 windowing mirrors rsx_metal_backend.m.
 *
 * The window opens on the screen of whoever LAUNCHES the process.
 *
 * Build:  ./build_mesh_spin.sh   (clang + Metal/QuartzCore + SDL2 via pkg-config)
 * Run:    /tmp/mesh_spin /tmp/kratos.mesh
 *         (ESC or close the window to quit; drag a corner to resize)
 * Headless smoke: MESH_SPIN_FRAMES=120 /tmp/mesh_spin /tmp/kratos.mesh
 */
#ifndef __APPLE__
#error mesh_spin is macOS/Metal-only
#endif

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_metal.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Mesh load (KMSH) + smooth normals (from mesh_view.m) --------------- */
typedef struct { uint32_t nverts; float* pos; float* nrm; uint32_t ntris; uint32_t* idx; } kmesh;

static int kmesh_load(const char* path, kmesh* m) {
    memset(m, 0, sizeof(*m));
    FILE* f = fopen(path, "rb");
    if (!f) { printf("[mesh] cannot open %s\n", path); return -1; }
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "KMSH", 4) != 0) { printf("[mesh] bad magic\n"); fclose(f); return -1; }
    if (fread(&m->nverts, 4, 1, f) != 1) { fclose(f); return -1; }
    m->pos = malloc((size_t)m->nverts * 3 * sizeof(float));
    if (fread(m->pos, sizeof(float) * 3, m->nverts, f) != m->nverts) { fclose(f); return -1; }
    if (fread(&m->ntris, 4, 1, f) != 1) { fclose(f); return -1; }
    m->idx = malloc((size_t)m->ntris * 3 * sizeof(uint32_t));
    if (fread(m->idx, sizeof(uint32_t) * 3, m->ntris, f) != m->ntris) { fclose(f); return -1; }
    fclose(f);
    printf("[mesh] %s: %u verts, %u tris\n", path, m->nverts, m->ntris);
    return 0;
}

static void kmesh_compute_normals(kmesh* m) {
    m->nrm = calloc((size_t)m->nverts * 3, sizeof(float));
    for (uint32_t t = 0; t < m->ntris; t++) {
        uint32_t i0 = m->idx[t*3+0], i1 = m->idx[t*3+1], i2 = m->idx[t*3+2];
        if (i0 >= m->nverts || i1 >= m->nverts || i2 >= m->nverts) continue;
        const float *p0 = &m->pos[i0*3], *p1 = &m->pos[i1*3], *p2 = &m->pos[i2*3];
        float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
        float e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};
        float fn[3] = {e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0]};
        for (int k = 0; k < 3; k++) { m->nrm[i0*3+k]+=fn[k]; m->nrm[i1*3+k]+=fn[k]; m->nrm[i2*3+k]+=fn[k]; }
    }
    for (uint32_t v = 0; v < m->nverts; v++) {
        float* n = &m->nrm[v*3]; float L = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if (L > 1e-8f) { n[0]/=L; n[1]/=L; n[2]/=L; } else { n[0]=0; n[1]=1; n[2]=0; }
    }
}

/* ---- column-major 4x4 matrices (from mesh_view.m) ---------------------- */
static void mat4_identity(float m[16]) { memset(m,0,64); m[0]=m[5]=m[10]=m[15]=1; }
static void mat4_multiply(float o[16], const float a[16], const float b[16]) {
    float r[16];
    for (int c=0;c<4;c++) for (int row=0;row<4;row++){ float s=0; for(int k=0;k<4;k++) s+=a[k*4+row]*b[c*4+k]; r[c*4+row]=s; }
    memcpy(o,r,64);
}
static void mat4_translate(float m[16], float x, float y, float z){ mat4_identity(m); m[12]=x; m[13]=y; m[14]=z; }
static void mat4_scale(float m[16], float s){ mat4_identity(m); m[0]=s; m[5]=s; m[10]=s; }
static void mat4_rotate_x(float m[16], float r){ mat4_identity(m); float c=cosf(r),s=sinf(r); m[5]=c; m[6]=s; m[9]=-s; m[10]=c; }
static void mat4_rotate_y(float m[16], float r){ mat4_identity(m); float c=cosf(r),s=sinf(r); m[0]=c; m[2]=-s; m[8]=s; m[10]=c; }
static void mat4_perspective(float m[16], float fovy, float aspect, float zn, float zf){
    memset(m,0,64); float f=1.0f/tanf(fovy*0.5f);
    m[0]=f/aspect; m[5]=f; m[10]=zf/(zn-zf); m[11]=-1; m[14]=(zf*zn)/(zn-zf);
}
static float deg2rad(float d){ return d*(float)M_PI/180.0f; }

/* ---- MSL shaders (identical to mesh_view.m) ---------------------------- */
static const char* k_msl_source =
    "#include <metal_stdlib>\n using namespace metal;\n"
    "struct VSIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; };\n"
    "struct VSOut { float4 position [[position]]; float3 normal; };\n"
    "struct Uniforms { float4x4 mvp; float4x4 mv; };\n"
    "vertex VSOut mesh_vs(VSIn in [[stage_in]], constant Uniforms& u [[buffer(2)]]) {\n"
    "  VSOut o; o.position = u.mvp * float4(in.position,1.0);\n"
    "  float3x3 nm = float3x3(u.mv[0].xyz,u.mv[1].xyz,u.mv[2].xyz); o.normal = nm*in.normal; return o; }\n"
    "fragment float4 mesh_fs(VSOut in [[stage_in]]) {\n"
    "  float3 N=normalize(in.normal); float3 L=normalize(float3(0.35,0.5,0.8));\n"
    "  float d=abs(dot(N,L)); float3 clay=float3(0.72,0.63,0.55);\n"
    "  return float4(clay*(0.22+0.78*d),1.0); }\n";

typedef struct { float mvp[16]; float mv[16]; } uniforms_t;

int main(int argc, char** argv) {
@autoreleasepool {
    const char* mesh_path = (argc > 1) ? argv[1] : "/tmp/kratos.mesh";
    const char* fenv = getenv("MESH_SPIN_FRAMES");
    const long max_frames = fenv ? atol(fenv) : 0;   /* 0 = corre ate fechar */

    kmesh m;
    if (kmesh_load(mesh_path, &m)) return 1;
    kmesh_compute_normals(&m);

    /* SDL2 window + Metal layer */
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) { printf("[sdl] init: %s\n", SDL_GetError()); return 2; }
    SDL_Window* win = SDL_CreateWindow("Kratos - GoW2 (recomp, Metal)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 800,
        SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { printf("[sdl] CreateWindow: %s (sem display? corre num ecra real)\n", SDL_GetError()); return 2; }
    SDL_MetalView mview = SDL_Metal_CreateView(win);
    if (!mview) { printf("[sdl] Metal_CreateView: %s\n", SDL_GetError()); return 2; }
    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(mview);

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) { printf("[gpu] no Metal device\n"); return 1; }
    printf("[gpu] device: %s\n", device.name.UTF8String);
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;

    id<MTLCommandQueue> queue = [device newCommandQueue];
    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:[NSString stringWithUTF8String:k_msl_source] options:nil error:&err];
    if (!lib) { printf("[gpu] MSL: %s\n", err ? err.localizedDescription.UTF8String : "?"); return 1; }

    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    vd.attributes[0].format = MTLVertexFormatFloat3; vd.attributes[0].offset = 0; vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat3; vd.attributes[1].offset = 0; vd.attributes[1].bufferIndex = 1;
    vd.layouts[0].stride = 12; vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    vd.layouts[1].stride = 12; vd.layouts[1].stepFunction = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"mesh_vs"];
    pd.fragmentFunction = [lib newFunctionWithName:@"mesh_fs"];
    pd.vertexDescriptor = vd;
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!pso) { printf("[gpu] PSO: %s\n", err ? err.localizedDescription.UTF8String : "?"); return 1; }

    MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionLess; dd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> dss = [device newDepthStencilStateWithDescriptor:dd];

    id<MTLBuffer> posBuf = [device newBufferWithBytes:m.pos length:(NSUInteger)m.nverts*12 options:MTLResourceStorageModeShared];
    id<MTLBuffer> nrmBuf = [device newBufferWithBytes:m.nrm length:(NSUInteger)m.nverts*12 options:MTLResourceStorageModeShared];
    id<MTLBuffer> idxBuf = [device newBufferWithBytes:m.idx length:(NSUInteger)m.ntris*12 options:MTLResourceStorageModeShared];

    /* partes fixas do modelo/vista (Ry roda por frame) */
    const float center[3] = {0.0f, 18.6f, -0.4f};
    float T[16], S[16], Rx[16], V[16];
    mat4_translate(T, -center[0], -center[1], -center[2]);
    mat4_scale(S, 1.0f/20.0f);
    mat4_rotate_x(Rx, deg2rad(12.0f));
    mat4_translate(V, 0.0f, 0.0f, -2.6f);

    id<MTLTexture> depthTex = nil;
    NSUInteger dtw = 0, dth = 0;
    long frame = 0; int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
        }
        int pw = 0, ph = 0; SDL_Metal_GetDrawableSize(win, &pw, &ph);
        if (pw <= 0 || ph <= 0) { SDL_Delay(8); continue; }
        layer.drawableSize = CGSizeMake(pw, ph);
        if (!depthTex || dtw != (NSUInteger)pw || dth != (NSUInteger)ph) {
            MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:pw height:ph mipmapped:NO];
            td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModePrivate;
            depthTex = [device newTextureWithDescriptor:td]; dtw = pw; dth = ph;
        }

        /* spin: uma volta a cada ~7s, com inclinacao fixa Rx */
        float angle = (float)SDL_GetTicks64() * 0.0009f;
        float Ry[16], model[16], tmp[16], mv[16], mvp[16], P[16];
        mat4_rotate_y(Ry, angle);
        mat4_multiply(tmp, S, T); mat4_multiply(tmp, Ry, tmp); mat4_multiply(model, Rx, tmp);
        mat4_multiply(mv, V, model);
        mat4_perspective(P, deg2rad(45.0f), (float)pw/(float)ph, 0.1f, 10.0f);
        mat4_multiply(mvp, P, mv);
        uniforms_t u; memcpy(u.mvp, mvp, 64); memcpy(u.mv, mv, 64);

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (!drawable) { SDL_Delay(4); continue; }

        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = drawable.texture;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.12, 0.25, 1.0);
        rp.depthAttachment.texture = depthTex;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.storeAction = MTLStoreActionDontCare;
        rp.depthAttachment.clearDepth = 1.0;

        id<MTLCommandBuffer> cmd = [queue commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
        [enc setRenderPipelineState:pso];
        [enc setDepthStencilState:dss];
        [enc setCullMode:MTLCullModeNone];
        [enc setViewport:(MTLViewport){0, 0, (double)pw, (double)ph, 0, 1}];
        [enc setVertexBuffer:posBuf offset:0 atIndex:0];
        [enc setVertexBuffer:nrmBuf offset:0 atIndex:1];
        [enc setVertexBytes:&u length:sizeof(u) atIndex:2];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:(NSUInteger)m.ntris*3
                         indexType:MTLIndexTypeUInt32 indexBuffer:idxBuf indexBufferOffset:0];
        [enc endEncoding];
        [cmd presentDrawable:drawable];
        [cmd commit];

        frame++;
        if (max_frames && frame >= max_frames) { printf("[spin] headless smoke: %ld frames ok\n", frame); running = 0; }
    }
    SDL_Metal_DestroyView(mview);
    SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    printf("[spin] fim (%ld frames)\n", frame);
    return 0;
}
}
