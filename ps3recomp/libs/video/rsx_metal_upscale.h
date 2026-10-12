/*
 * ps3recomp - upscaler espacial MetalFX (Port Apple, servico de HOST).
 *
 * O jogo (PS3) renderiza na sua resolucao nativa (GoW2 HD ~= 1280x720). Em ecra
 * retina/4K o backend real hoje ou rasteriza direto no drawable (custo cheio a
 * resolucao do ecra) ou faz um sample bilinear. MTLFXSpatialScaler faz o
 * upscale 720p -> resolucao do drawable com qualidade superior a bilinear
 * (edge-aware + sharpening), na GPU, sem motion vectors (pragmatico para
 * recompilacao: nao ha vetores de movimento nativos como num engine moderno).
 *
 * Fronteira Port Apple: isto e' um servico de HOST (apresentacao/scaling), logo
 * usa a API nativa da Apple. NAO altera a logica guest (o RSX continua a
 * desenhar exactamente o que o jogo manda, so a resolucao nativa).
 *
 * Uso (backend): rmu_init(dev) uma vez; por present, rmu_prepare(fmt,in,out)
 * (recria lazy so quando muda tamanho/formato) devolve a textura de saida;
 * rmu_encode(cb, color_in) codifica o upscale nessa command buffer; depois o
 * caller faz blit da saida -> drawable (mesmo tamanho).
 *
 * Gated no backend por PS3_METALFX (OFF por default). Degrada com seguranca:
 * se MetalFX nao existir (SDK/HW/OS < 13) rmu_available()==0 e o caller mantem
 * o caminho blit/sample.
 */
#ifndef PS3RECOMP_RSX_METAL_UPSCALE_H
#define PS3RECOMP_RSX_METAL_UPSCALE_H

#import <Metal/Metal.h>
#include <stdint.h>

/* Degrada em SDKs sem MetalFX: todo o modulo vira no-op (available==0). */
#if __has_include(<MetalFX/MetalFX.h>)
#  import <MetalFX/MetalFX.h>
#  define RMU_HAVE_METALFX 1
#else
#  define RMU_HAVE_METALFX 0
#endif

typedef struct {
    id<MTLDevice>          device;   /* strong (ARC) */
#if RMU_HAVE_METALFX
    id<MTLFXSpatialScaler> scaler;   /* strong; recriado quando dims/fmt mudam */
#else
    id                     scaler;
#endif
    id<MTLTexture>         output;   /* strong; resultado upscaled (Private) */
    uint32_t               in_w, in_h, out_w, out_h;
    MTLPixelFormat         fmt;
    int                    available; /* MetalFX suportado neste device/OS */
} rmu_ctx;

/* One-time: guarda device e sonda disponibilidade. 0 = OK (nao implica
 * available; ver rmu_available). */
static inline int rmu_init(rmu_ctx* c, id<MTLDevice> device) {
    if (!c || !device) return -1;
    /* Atribuicao (nao memset): sob ARC os campos strong libertam o valor
     * anterior; memset saltaria o retain/release e vazaria/over-releaseria. */
    c->device = device;
    c->scaler = nil;
    c->output = nil;
    c->in_w = c->in_h = c->out_w = c->out_h = 0;
    c->fmt = (MTLPixelFormat)0;
    c->available = 0;
#if RMU_HAVE_METALFX
    if (@available(macOS 13.0, *)) {
        c->available = [MTLFXSpatialScalerDescriptor supportsDevice:device] ? 1 : 0;
    }
#endif
    return 0;
}

/* 1 se o upscale espacial MetalFX esta' disponivel neste device/OS. */
static inline int rmu_available(const rmu_ctx* c) {
    return c && c->available;
}

/* Garante scaler + textura de saida para (in->out, fmt). Recria lazy so quando
 * algo muda. Devolve a textura de saida (out_w x out_h) ou nil (caller faz
 * fallback). */
static inline id<MTLTexture> rmu_prepare(rmu_ctx* c, MTLPixelFormat fmt,
                                         uint32_t in_w, uint32_t in_h,
                                         uint32_t out_w, uint32_t out_h) {
#if RMU_HAVE_METALFX
    if (!c || !c->available || !c->device) return nil;
    if (in_w == 0 || in_h == 0 || out_w == 0 || out_h == 0) return nil;

    if (@available(macOS 13.0, *)) {
        int changed = (c->scaler == nil) || (c->output == nil) ||
                      c->in_w != in_w || c->in_h != in_h ||
                      c->out_w != out_w || c->out_h != out_h || c->fmt != fmt;
        if (!changed) return c->output;

        MTLFXSpatialScalerDescriptor* d = [[MTLFXSpatialScalerDescriptor alloc] init];
        d.inputWidth          = in_w;
        d.inputHeight         = in_h;
        d.outputWidth         = out_w;
        d.outputHeight        = out_h;
        d.colorTextureFormat  = fmt;
        d.outputTextureFormat = fmt;
        /* Conteudo do jogo ja' esta' em espaco perceptual (gamma/sRGB-ish). */
        d.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;

        id<MTLFXSpatialScaler> scaler = [d newSpatialScalerWithDevice:c->device];
        if (!scaler) return nil;

        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                               width:out_w
                                                              height:out_h
                                                           mipmapped:NO];
        /* MetalFX escreve a saida via ShaderWrite; ShaderRead+RenderTarget
         * deixam o caller amostrar/blitar para o drawable. */
        td.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead |
                   MTLTextureUsageRenderTarget;
        td.storageMode = MTLStorageModePrivate;
        id<MTLTexture> out = [c->device newTextureWithDescriptor:td];
        if (!out) return nil;

        c->scaler = scaler;
        c->output = out;
        c->in_w = in_w; c->in_h = in_h;
        c->out_w = out_w; c->out_h = out_h;
        c->fmt = fmt;
        return c->output;
    }
    return nil;
#else
    (void)c; (void)fmt; (void)in_w; (void)in_h; (void)out_w; (void)out_h;
    return nil;
#endif
}

/* Codifica o upscale color_in -> c->output na command buffer cb. Chamar apos
 * rmu_prepare ter devolvido != nil. color_in tem de ter usage ShaderRead e as
 * dims <= (in_w,in_h) configuradas. */
static inline void rmu_encode(rmu_ctx* c, id<MTLCommandBuffer> cb,
                              id<MTLTexture> color_in) {
#if RMU_HAVE_METALFX
    if (!c || !c->available || !cb || !color_in || !c->output) return;
    if (@available(macOS 13.0, *)) {
        if (!c->scaler) return;
        c->scaler.colorTexture  = color_in;
        c->scaler.outputTexture = c->output;
        [c->scaler encodeToCommandBuffer:cb];
    }
#else
    (void)c; (void)cb; (void)color_in;
#endif
}

#endif /* PS3RECOMP_RSX_METAL_UPSCALE_H */
