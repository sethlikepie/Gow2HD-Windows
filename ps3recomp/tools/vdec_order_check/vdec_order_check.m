/*
 * vdec_order_check -- guarda contra a regressao da ordem das imagens do cellVdec (macOS).
 *
 * Alimenta libs/codec/cellVdec_vt.m com os primeiros AUs de um .m2v (o SEU, extraido do jogo;
 * nada de dados do jogo no repo) e mede a diferenca media de luma entre imagens seguidas.
 * Na ordem de exibicao a curva e' continua; na ordem de descodificacao (I0 P3 B1 B2 ...) ha'
 * um serrilhado de periodo 3 (o filme anda para a frente e para tras).
 *
 * Build:  clang -fobjc-arc -O1 tools/vdec_order_check/vdec_order_check.m libs/codec/cellVdec_vt.m \
 *           -framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework Foundation \
 *           -o /tmp/vdec_order_check
 * Uso:    /tmp/vdec_order_check <filme.m2v> [largura altura]      (introhud.m2v: 640 448)
 * Saida:  exit 0 = ordem de exibicao; exit 1 = serrilhado (regressao); exit 2 = erro.
 * Medido 2026-09-23 (introhud.m2v): OK salto medio ~0,3 | regressao (PS3_VDEC_VT_DECODE_ORDER=1) ~5.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int  vdec_vt_open(unsigned w, unsigned h);
int  vdec_vt_decode(const uint8_t* au, size_t n, uint64_t pts90k);
int  vdec_vt_pop(uint8_t* dst, unsigned w, unsigned h);
void vdec_vt_flush(void);
void vdec_vt_close(void);

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "uso: %s <filme.m2v> [w h]\n", argv[0]); return 2; }
    unsigned W = argc > 3 ? (unsigned)atoi(argv[2]) : 640, H = argc > 3 ? (unsigned)atoi(argv[3]) : 448;
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    size_t cap = 8u << 20; uint8_t* b = malloc(cap); size_t n = fread(b, 1, cap, f); fclose(f);
    enum { MAXPIC = 120 };
    size_t st[MAXPIC + 2]; int k = 0;
    for (size_t i = 0; i + 4 < n && k < MAXPIC + 1; i++)
        if (!b[i] && !b[i + 1] && b[i + 2] == 1 && b[i + 3] == 0) st[k++] = i;   /* picture start code */
    if (k < 20) { fprintf(stderr, "poucos picture headers (%d)\n", k); return 2; }
    st[0] = 0;   /* o 1.o AU leva o sequence header */
    if (!vdec_vt_open(W, H)) return 2;
    size_t fb = (size_t)W * H * 3 / 2;
    uint8_t *prev = malloc(fb), *cur = malloc(fb);
    double d[MAXPIC]; int nd = 0, got = 0;
    for (int i = 0; i + 1 < k; i++) {
        vdec_vt_decode(b + st[i], st[i + 1] - st[i], (uint64_t)i * 3003);
        if ((i & 7) == 7) vdec_vt_flush();
        while (vdec_vt_pop(cur, W, H)) {
            if (got && nd < MAXPIC) {
                double s = 0; for (size_t p = 0; p < (size_t)W * H; p++) s += abs(cur[p] - prev[p]);
                d[nd++] = s / ((double)W * H);
            }
            memcpy(prev, cur, fb); got++;
        }
    }
    vdec_vt_close();
    double jump = 0; for (int i = 1; i < nd; i++) jump += d[i] > d[i - 1] ? d[i] - d[i - 1] : d[i - 1] - d[i];
    jump /= (nd > 1 ? nd - 1 : 1);
    int bad = jump > 2.0;
    printf("imagens=%d salto_medio=%.2f -> %s\n", got, jump, bad ? "REGRESSAO: ordem de descodificacao" : "OK: ordem de exibicao");
    return bad;
}
