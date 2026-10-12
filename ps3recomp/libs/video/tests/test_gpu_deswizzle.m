/*
 * TDD: o deswizzle na GPU (rsx_metal_deswizzle.h) tem de ser BYTE-EXATO igual
 * a versao CPU do backend real (rsx_deswizzle_texels em rsx_texture_deswizzle.h).
 * Se um unico byte diferir, integrar no backend mudaria o pixel -> falha.
 *
 * Testa dims power-of-two (256x256, 64x64) e non-power-of-two (48x96), que
 * exercitam a extensao pow2 do Morton. Fonte swizzled sintetica determinista.
 *
 * Build: ./build_gpu_deswizzle.sh   (clang -fobjc-arc + -framework Metal)
 */
#import <Metal/Metal.h>
#include "rsx_texture_deswizzle.h"   /* CPU: rsx_deswizzle_texels / rsx_morton_padded_texels */
#include "rsx_metal_deswizzle.h"     /* GPU: rmd_init / rmd_run */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

static int case_dim(rmd_ctx* c, id<MTLCommandQueue> q, uint32_t w, uint32_t h) {
    uint32_t padded = rsx_morton_padded_texels(w, h);      /* texels da fonte pow2 */
    size_t src_len = (size_t)padded * 4;
    uint8_t* src = malloc(src_len);
    for (size_t i = 0; i < src_len; i++) src[i] = (uint8_t)(i * 2654435761u >> 13); /* padrao determinista */

    size_t dst_len = (size_t)w * h * 4;
    uint8_t* cpu = malloc(dst_len);
    uint8_t* gpu = malloc(dst_len);
    rsx_deswizzle_texels(src, cpu, w, h, 4);               /* referencia CPU */
    int rc = rmd_run(c, q, src, src_len, gpu, w, h);       /* GPU */

    int ok = (rc == 0) && (memcmp(cpu, gpu, dst_len) == 0);
    if (!ok) {
        g_fail++;
        size_t first = 0; for (; first < dst_len && cpu[first] == gpu[first]; first++) {}
        fprintf(stderr, "FAIL %ux%u: rc=%d 1o byte diferente @%zu (cpu=%u gpu=%u)\n",
                w, h, rc, first, first < dst_len ? cpu[first] : 0, first < dst_len ? gpu[first] : 0);
    } else {
        printf("  ok %ux%u: GPU deswizzle == CPU (%zu bytes)\n", w, h, dst_len);
    }
    free(src); free(cpu); free(gpu);
    return ok;
}

int main(void) {
@autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) { fprintf(stderr, "sem device Metal\n"); return 2; }
    id<MTLCommandQueue> q = [dev newCommandQueue];
    rmd_ctx c;
    if (rmd_init(&c, dev)) { fprintf(stderr, "rmd_init falhou\n"); return 2; }
    printf("[gpu-deswizzle] device: %s\n", dev.name.UTF8String);

    case_dim(&c, q, 256, 256);   /* textura real do jogo (Kratos skin) */
    case_dim(&c, q, 64, 64);
    case_dim(&c, q, 128, 128);
    case_dim(&c, q, 48, 96);     /* non-power-of-two: exercita a extensao pow2 */
    case_dim(&c, q, 32, 8);

    fprintf(stderr, "\n%s\n", g_fail ? "== FALHAS ==" : "== GPU deswizzle byte-exato vs CPU ==");
    return g_fail ? 1 : 0;
}
}
