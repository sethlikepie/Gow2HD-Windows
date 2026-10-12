/*
 * TDD do ring SPSC do backend CoreAudio (libs/audio/coreaudio_ring.h).
 * Valida a matematica de indices: escrita/leitura, wrap-around, overflow
 * (near-full deixa 1 slot), underflow (leitura parcial), e um round-trip
 * produtor->consumidor byte-exacto. Single-threaded: exercita as fronteiras
 * sem precisar de threads.
 *
 * Build: cc -std=c11 -I../.. test_coreaudio_ring.c -o test_coreaudio_ring
 */
#include "../coreaudio_ring.h"
#include <stdio.h>

static int g_pass, g_fail;
static const char* g_cur = "(none)";
#define TEST(n) g_cur = n
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; \
    fprintf(stderr, "FAIL: %s (linha %d)\n", g_cur, __LINE__); } } while (0)

int main(void) {
    float storage[8];
    ca_ring q;

    TEST("vazio no init");
    ca_ring_init(&q, storage, 8);
    CHECK(ca_ring_count(&q) == 0);
    CHECK(ca_ring_space(&q) == 7);          /* cap-1 (um slot reservado) */

    TEST("escrita e contagem");
    float in3[3] = {1.0f, 2.0f, 3.0f};
    CHECK(ca_ring_write(&q, in3, 3) == 3);
    CHECK(ca_ring_count(&q) == 3);
    CHECK(ca_ring_space(&q) == 4);

    TEST("leitura devolve o que foi escrito, em ordem");
    float out3[3] = {0};
    CHECK(ca_ring_read(&q, out3, 3) == 3);
    CHECK(out3[0] == 1.0f && out3[1] == 2.0f && out3[2] == 3.0f);
    CHECK(ca_ring_count(&q) == 0);

    TEST("underflow: le menos do que pede");
    float out5[5] = {9,9,9,9,9};
    float in2[2] = {7.0f, 8.0f};
    ca_ring_write(&q, in2, 2);
    CHECK(ca_ring_read(&q, out5, 5) == 2);  /* so 2 disponiveis */
    CHECK(out5[0] == 7.0f && out5[1] == 8.0f);

    TEST("overflow: escreve no maximo cap-1");
    ca_ring_init(&q, storage, 8);
    float big[10] = {0,1,2,3,4,5,6,7,8,9};
    CHECK(ca_ring_write(&q, big, 10) == 7); /* cap-1 = 7 */
    CHECK(ca_ring_space(&q) == 0);
    CHECK(ca_ring_write(&q, big, 1) == 0);  /* cheio */

    TEST("wrap-around: escrever/ler cruzando o fim do buffer");
    ca_ring_init(&q, storage, 8);
    float a4[4] = {10,11,12,13};
    ca_ring_write(&q, a4, 4);               /* w=4 */
    float tmp[4];
    ca_ring_read(&q, tmp, 4);               /* r=4, vazio */
    float b6[6] = {20,21,22,23,24,25};
    CHECK(ca_ring_write(&q, b6, 6) == 6);   /* escreve 4..7 e 0..1 (wrap) */
    float out6[6] = {0};
    CHECK(ca_ring_read(&q, out6, 6) == 6);
    int ok = 1;
    for (int i = 0; i < 6; i++) ok &= (out6[i] == (float)(20 + i));
    CHECK(ok);

    TEST("round-trip longo com wraps repetidos (stereo)");
    ca_ring_init(&q, storage, 8);
    int mismatch = 0;
    float next_w = 0, next_r = 0;
    for (int iter = 0; iter < 1000; iter++) {
        float pair[2] = { next_w, next_w + 0.5f };
        if (ca_ring_write(&q, pair, 2) == 2) next_w += 1.0f;
        float rp[2];
        if (ca_ring_read(&q, rp, 2) == 2) {
            if (rp[0] != next_r || rp[1] != next_r + 0.5f) mismatch++;
            next_r += 1.0f;
        }
    }
    CHECK(mismatch == 0);

    fprintf(stderr, "\n%s: %d passed, %d failed\n", g_fail ? "RED" : "GREEN", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
