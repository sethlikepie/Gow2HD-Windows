/*
 * O mixer do cellAudio tem de ler floats big-endian do buffer guest e
 * fazer o downmix 7.1 do RPCS3 (LFE fora do estereo). Este teste nao
 * abre o dispositivo.
 *
 * Build: cc -std=c11 -I../../include test_audio_port_mix.c -o /tmp/test_audio_port_mix
 */
#include "../audio_port_mix.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;
static const char* g_cur = "(none)";
#define TEST(n) g_cur = n
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; \
    fprintf(stderr, "FAIL: %s (linha %d)\n", g_cur, __LINE__); } } while (0)

static int near(float a, float b)
{
    float d = a - b;
    if (d < 0) d = -d;
    return d < 0.0001f;
}

static void store_be_f32(unsigned char* p, float f)
{
    uint32_t raw;
    memcpy(&raw, &f, 4);
    raw = ps3_bswap32(raw);
    memcpy(p, &raw, 4);
}

int main(void)
{
    TEST("float 1.0 big-endian decodifica para 1");
    unsigned char be[4] = { 0x3f, 0x80, 0x00, 0x00 };
    CHECK(audio_mix_load_be_f32(be) == 1.0f);

    TEST("float -1.0 big-endian");
    unsigned char ben[4] = { 0xbf, 0x80, 0x00, 0x00 };
    CHECK(audio_mix_load_be_f32(ben) == -1.0f);

    TEST("u64 do readIndex sai big-endian");
    unsigned char idx[8];
    audio_mix_store_u64_be(idx, 1);
    CHECK(idx[0] == 0 && idx[1] == 0 && idx[2] == 0 && idx[3] == 0);
    CHECK(idx[4] == 0 && idx[5] == 0 && idx[6] == 0 && idx[7] == 1);
    audio_mix_store_u64_be(idx, 0);
    int z = 1;
    for (int i = 0; i < 8; i++) z &= (idx[i] == 0);
    CHECK(z);

    TEST("indice seguinte da a volta em nBlock");
    CHECK(audio_mix_next_block(0, 8) == 1);
    CHECK(audio_mix_next_block(7, 8) == 0);
    CHECK(audio_mix_next_block(31, 32) == 0);
    CHECK(audio_mix_block_floats(8) == 2048);
    CHECK(audio_mix_block_floats(2) == 512);

    TEST("2 canais: esquerda e direita, level a meio");
    float src2[512];
    memset(src2, 0, sizeof src2);
    src2[0] = 1.0f;
    src2[1] = -0.5f;
    float st[512];
    memset(st, 0, sizeof st);
    audio_mix_add_port(st, src2, 2, 0.5f);
    CHECK(near(st[0], 0.5f));
    CHECK(near(st[1], -0.25f));

    TEST("level 0 toca a volume cheio");
    memset(st, 0, sizeof st);
    audio_mix_add_port(st, src2, 2, 0.0f);
    CHECK(near(st[0], 1.0f));

    TEST("8 canais: LFE nao entra no estereo, L vai a -3 dB");
    float src8[2048];
    memset(src8, 0, sizeof src8);
    src8[0] = 1.0f; /* L */
    src8[3] = 1.0f; /* LFE */
    memset(st, 0, sizeof st);
    audio_mix_add_port(st, src8, 8, 1.0f);
    CHECK(near(st[0], 0.707f));
    CHECK(near(st[1], 0.0f));

    TEST("centro reparte 0.5 para cada lado");
    memset(src8, 0, sizeof src8);
    src8[2] = 1.0f;
    memset(st, 0, sizeof st);
    audio_mix_add_port(st, src8, 8, 1.0f);
    CHECK(near(st[0], 0.5f));
    CHECK(near(st[1], 0.5f));

    TEST("side esquerdo e rear direito");
    memset(src8, 0, sizeof src8);
    src8[4] = 1.0f; /* side L */
    src8[7] = 1.0f; /* rear R */
    memset(st, 0, sizeof st);
    audio_mix_add_port(st, src8, 8, 1.0f);
    CHECK(near(st[0], 0.5f));
    CHECK(near(st[1], 0.5f));

    TEST("sentinela -0.0 e posicoes de tag do RPCS3");
    CHECK(audio_mix_tag_pos(8, 0) == 2);
    CHECK(audio_mix_tag_pos(8, 1) == 411);
    CHECK(audio_mix_tag_pos(2, 0) == 1);
    unsigned char tagb[4];
    uint32_t negbits = 0x80000000u;
    float neg0;
    memcpy(&neg0, &negbits, 4);
    audio_mix_store_f32_be(tagb, neg0);
    CHECK(tagb[0] == 0x80 && tagb[1] == 0 && tagb[2] == 0 && tagb[3] == 0);
    CHECK(audio_mix_is_neg_zero(audio_mix_load_be_f32(tagb)));
    CHECK(!audio_mix_is_neg_zero(0.0f));
    CHECK(!audio_mix_is_neg_zero(1.0f));

    TEST("bloco guest big-endian decodifica antes do mix");
    unsigned char raw[8];
    store_be_f32(raw, 0.25f);
    store_be_f32(raw + 4, -0.25f);
    float dec[2];
    audio_mix_decode_be_block(dec, raw, 2);
    CHECK(near(dec[0], 0.25f));
    CHECK(near(dec[1], -0.25f));

    if (g_fail) {
        fprintf(stderr, "%d falhas, %d ok\n", g_fail, g_pass);
        return 1;
    }
    printf("%d ok\n", g_pass);
    return 0;
}
