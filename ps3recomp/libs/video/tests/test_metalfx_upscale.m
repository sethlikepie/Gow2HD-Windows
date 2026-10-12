/*
 * TDD: o upscaler MetalFX (rsx_metal_upscale.h) tem de produzir uma textura de
 * saida com as dimensoes de destino EXATAS e preservar o conteudo espacial do
 * input -- um upscaler espacial nao pode inventar cor a partir de um campo
 * uniforme, nem trocar cantos de regiao. Estes invariantes sao deterministas
 * mesmo que o filtro interno da Apple nao seja byte-exato.
 *
 * Casos:
 *   1) invariancia de cor solida: input 640x360 vermelho -> saida 1280x720 toda
 *      vermelha (grelha de pontos). Se um ponto nao for vermelho, o scaler
 *      corrompeu o campo.
 *   2) preservacao de regiao: input metade-topo vermelho / metade-baixo azul ->
 *      cantos da saida seguem a regiao de origem (TL/TR vermelho, BL/BR azul).
 *   3) dimensoes: rmu_prepare devolve textura com out_w x out_h exatos.
 *
 * Se MetalFX nao existir (HW/OS < 13) o teste faz skip explicito (exit 0) em vez
 * de falhar -- o backend degrada para o blit/sample existente.
 *
 * Build: ./build_metalfx_upscale.sh   (clang -fobjc-arc + -framework MetalFX)
 */
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#include "rsx_metal_upscale.h"   /* rmu_init / rmu_available / rmu_prepare / rmu_encode */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

/* BGRA8Unorm em memoria = {B,G,R,A}. */
static const uint8_t RED[4]  = { 0, 0, 255, 255 };
static const uint8_t BLUE[4] = { 255, 0, 0, 255 };

/* Cria textura BGRA8 Shared preenchida com um padrao (top/bottom). Se um_cor!=0
 * preenche tudo com 'top'. */
static id<MTLTexture> make_input(id<MTLDevice> dev, uint32_t w, uint32_t h,
                                 const uint8_t top[4], const uint8_t bot[4],
                                 int um_cor) {
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> t = [dev newTextureWithDescriptor:td];
    if (!t) return nil;
    uint8_t* px = malloc((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* c = (um_cor || y < h / 2) ? top : bot;
        for (uint32_t x = 0; x < w; x++)
            memcpy(px + ((size_t)y * w + x) * 4, c, 4);
    }
    [t replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0
           withBytes:px bytesPerRow:(NSUInteger)w * 4];
    free(px);
    return t;
}

/* Le a saida (Private) para um buffer host via blit -> textura Shared. */
static uint8_t* readback(id<MTLDevice> dev, id<MTLCommandQueue> q,
                         id<MTLTexture> src) {
    uint32_t w = (uint32_t)src.width, h = (uint32_t)src.height;
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> dst = [dev newTextureWithDescriptor:td];
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:src sourceSlice:0 sourceLevel:0
           sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(w, h, 1)
              toTexture:dst destinationSlice:0 destinationLevel:0
      destinationOrigin:MTLOriginMake(0, 0, 0)];
    [bl endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    uint8_t* out = malloc((size_t)w * h * 4);
    [dst getBytes:out bytesPerRow:(NSUInteger)w * 4
       fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    return out;
}

static int is_red(const uint8_t* p)  { return p[2] > 200 && p[0] < 60; }
static int is_blue(const uint8_t* p) { return p[0] > 200 && p[2] < 60; }

static const uint8_t* at(const uint8_t* buf, uint32_t w, uint32_t x, uint32_t y) {
    return buf + ((size_t)y * w + x) * 4;
}

int main(void) {
@autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) { fprintf(stderr, "sem device Metal\n"); return 2; }
    id<MTLCommandQueue> q = [dev newCommandQueue];

    rmu_ctx c;
    if (rmu_init(&c, dev) != 0) { fprintf(stderr, "rmu_init falhou\n"); return 2; }
    printf("[metalfx] device: %s\n", dev.name.UTF8String);

    if (!rmu_available(&c)) {
        printf("[metalfx] MetalFX indisponivel neste device/OS -- SKIP (backend "
               "degrada para blit/sample). exit 0\n");
        return 0;
    }
    printf("[metalfx] MetalFX suportado -- a validar upscale 640x360 -> 1280x720\n");

    const uint32_t IW = 640, IH = 360, OW = 1280, OH = 720;

    /* Caso 3 (dimensoes) + caso 1 (cor solida). */
    {
        id<MTLTexture> in = make_input(dev, IW, IH, RED, RED, 1);
        id<MTLTexture> out = rmu_prepare(&c, MTLPixelFormatBGRA8Unorm, IW, IH, OW, OH);
        if (!out) { fprintf(stderr, "FAIL: rmu_prepare devolveu nil\n"); g_fail++; }
        else if (out.width != OW || out.height != OH) {
            fprintf(stderr, "FAIL dims: out=%lux%lu esperado %ux%u\n",
                    (unsigned long)out.width, (unsigned long)out.height, OW, OH);
            g_fail++;
        } else {
            id<MTLCommandBuffer> cb = [q commandBuffer];
            rmu_encode(&c, cb, in);
            [cb commit];
            [cb waitUntilCompleted];
            uint8_t* buf = readback(dev, q, out);
            int bad = 0;
            for (uint32_t gy = 1; gy < 8 && !bad; gy++)
                for (uint32_t gx = 1; gx < 8 && !bad; gx++) {
                    const uint8_t* p = at(buf, OW, gx * OW / 8, gy * OH / 8);
                    if (!is_red(p)) {
                        fprintf(stderr, "FAIL cor-solida: (%u,%u) BGRA=%u,%u,%u,%u\n",
                                gx * OW / 8, gy * OH / 8, p[0], p[1], p[2], p[3]);
                        bad = 1;
                    }
                }
            if (bad) g_fail++;
            else printf("  ok dims 1280x720 + campo vermelho uniforme preservado\n");
            free(buf);
        }
    }

    /* Caso 2 (preservacao de regiao: topo vermelho, baixo azul). */
    {
        id<MTLTexture> in = make_input(dev, IW, IH, RED, BLUE, 0);
        id<MTLTexture> out = rmu_prepare(&c, MTLPixelFormatBGRA8Unorm, IW, IH, OW, OH);
        id<MTLCommandBuffer> cb = [q commandBuffer];
        rmu_encode(&c, cb, in);
        [cb commit];
        [cb waitUntilCompleted];
        uint8_t* buf = readback(dev, q, out);
        /* Pontos bem dentro de cada regiao (longe da fronteira em y=360). */
        struct { uint32_t x, y; int red; const char* nm; } pts[] = {
            {   20,   20, 1, "TL" }, { OW - 20,   20, 1, "TR" },
            {   20, OH - 20, 0, "BL" }, { OW - 20, OH - 20, 0, "BR" },
        };
        int bad = 0;
        for (int i = 0; i < 4; i++) {
            const uint8_t* p = at(buf, OW, pts[i].x, pts[i].y);
            int ok = pts[i].red ? is_red(p) : is_blue(p);
            if (!ok) {
                fprintf(stderr, "FAIL regiao %s: esperado %s, BGRA=%u,%u,%u,%u\n",
                        pts[i].nm, pts[i].red ? "vermelho" : "azul",
                        p[0], p[1], p[2], p[3]);
                bad = 1;
            }
        }
        if (bad) g_fail++;
        else printf("  ok regioes preservadas (TL/TR vermelho, BL/BR azul)\n");
        free(buf);
    }

    fprintf(stderr, "\n%s\n",
            g_fail ? "== FALHAS ==" : "== MetalFX upscale: dims + conteudo OK ==");
    return g_fail ? 1 : 0;
}
}
