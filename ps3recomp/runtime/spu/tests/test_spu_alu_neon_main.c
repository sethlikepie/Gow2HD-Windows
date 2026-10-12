/*
 * E466 — as ajudantes da ULA do SPU em NEON dao o MESMO resultado que as
 * escalares, para entradas pseudo-aleatorias.
 *
 * Porque este teste existe: a conversao mexe em semantica (comparacoes com
 * sinal contra sem sinal, subtraccao invertida do sf/sfi, selb com a mascara no
 * argumento certo). Um erro aqui nao da' erro de compilacao nem crash -- da'
 * geometria errada no jogo, que e' exactamente a classe de bug que esta sessao
 * passou horas a perseguir noutro sitio. As versoes _ref ficam no header para
 * servirem de oraculo.
 *
 * Numa plataforma sem SPU_HELPERS_NEON as duas chamadas sao a mesma funcao e o
 * teste passa trivialmente -- o que e' o resultado correcto.
 */
#include "spu_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_fail;

/* LCG deterministico: sem Math.random, o teste e' reprodutivel. */
static uint32_t rnd_state = 0x12345678u;
static uint32_t rnd(void)
{
    rnd_state = rnd_state * 1664525u + 1013904223u;
    return rnd_state;
}

static u128 rnd_u128(void)
{
    u128 v;
    for (int i = 0; i < 4; i++)
        v._u32[i] = rnd();
    return v;
}

/* Valores de fronteira alem dos aleatorios: 0, todos-1, so' o bit de sinal, e o
 * maior positivo -- e' onde as comparacoes com/sem sinal divergem. */
static u128 edge_u128(int k)
{
    u128 v;
    static const uint32_t pats[6] = {
        0x00000000u, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0x00000001u, 0xFFFF0000u
    };
    for (int i = 0; i < 4; i++)
        v._u32[i] = pats[(k + i) % 6];
    return v;
}

static void eq(const char* nome, u128 got, u128 want)
{
    if (memcmp(&got, &want, sizeof(u128)) == 0)
        return;
    g_fail++;
    if (g_fail <= 8)
        fprintf(stderr,
                "FAIL %s\n  neon = %08X %08X %08X %08X\n  ref  = %08X %08X %08X %08X\n",
                nome, got._u32[0], got._u32[1], got._u32[2], got._u32[3],
                want._u32[0], want._u32[1], want._u32[2], want._u32[3]);
}

int main(void)
{
    for (int iter = 0; iter < 2000; iter++) {
        const u128 a = (iter < 12) ? edge_u128(iter) : rnd_u128();
        const u128 b = (iter < 12) ? edge_u128(iter + 3) : rnd_u128();
        const u128 c = rnd_u128();
        const int32_t imm = (int32_t)rnd();

#define SAME2(f)  eq(#f, f(a, b), f##_ref(a, b))
#define SAMEI(f)  eq(#f, f(a, imm), f##_ref(a, imm))
#define SAME1(f)  eq(#f, f(a), f##_ref(a))

        SAME2(spu_and);   SAME2(spu_or);    SAME2(spu_xor);
        SAME2(spu_nand);  SAME2(spu_nor);   SAME2(spu_andc);  SAME2(spu_orc);
        SAMEI(spu_andi);  SAMEI(spu_ori);   SAMEI(spu_xori);

        SAME2(spu_a);     SAME2(spu_sf);    SAME2(spu_ah);    SAME2(spu_sfh);
        SAMEI(spu_ai);    SAMEI(spu_ahi);   SAMEI(spu_sfi);   SAMEI(spu_sfhi);

        SAME2(spu_cgt);   SAME2(spu_cgth);  SAME2(spu_cgtb);
        SAME2(spu_clgt);  SAME2(spu_clgth); SAME2(spu_clgtb);
        SAME2(spu_ceq);   SAME2(spu_ceqh);  SAME2(spu_ceqb);
        SAMEI(spu_ceqi);  SAMEI(spu_cgti);  SAMEI(spu_clgti);

        SAME1(spu_clz);   SAME1(spu_cntb);

        eq("spu_selb", spu_selb(a, b, c), spu_selb_ref(a, b, c));

#undef SAME2
#undef SAMEI
#undef SAME1
    }

    printf("test_spu_alu_neon: %s (%d divergencias)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
