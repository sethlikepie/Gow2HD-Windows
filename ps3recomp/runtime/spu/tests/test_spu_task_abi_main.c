/* Teste de ABI do CellSpursTaskArgument (u128) — harness do lado do host.
 *
 * Fixa o contrato de runtime/spu/spu_lifted_job.h (`spu_run_lifted_job_ex`):
 * o argumento de uma task SPURS e UM valor de 128 bits carregado INTEIRO para
 * r3 — as 4 words big-endian de `args_ea` caem em ctx.gpr[3]._u32[0..3]. NAO e
 * o EA em si (ABI escalar refutada no master), nem as words espalhadas por
 * r3..r6 (segunda tentativa refutada; deixava os registos base a zero e mandava
 * a task para loops de DMA na base 0).
 *
 * O buraco que este teste fecha: test_workload e test_adapter so verificam a
 * word 0, portanto passam tanto com a ABI certa como com uma implementacao que
 * so entregue a primeira word. GoW2 le o EA da sua LFQueue da word 1, logo
 * "word 0 certa" nao e cobertura suficiente. Aqui as QUATRO words sao fixadas,
 * nos tres ramos que o runtime documenta:
 *
 *   [A] job simples          — r3 = 16 bytes BE lidos de args_ea
 *   [B] task de taskset      — r3 = snapshot em LS 0x2780 (args_ea IGNORADO),
 *                              r4 = { spurs EA (u64 alto), taskset->args }
 *   [C] args_ea fora do range — fallback documentado: r3 = { args_ea, 0, 0, 0 }
 *
 * O job SPU (gen_test_spu_task_abi.py) guarda r3 e r4 inteiros e faz DMA PUT
 * dos 32 bytes para EA 0x10400, que esta harness le de volta.
 */
#include "spu_recomp.h"
#include "spu_helpers.h"
#include "spu_dma.h"
#include "spu_lifted_job.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* EAs combinadas com o gerador (RESULT_EA) e escolhidas acima de 0x10000
 * porque a guarda de range de spu_dma.h rejeita destinos abaixo disso quando
 * ppu_vm_size != 0 (o caso [C] liga-a). */
#define kGuestSize  0x11000u
#define kResultEa   0x10400u    /* 32 bytes: r3 (0..15) e r4 (16..31) */
#define kArgsEaA    0x10100u
#define kArgsEaB    0x10200u
#define kArgsEaBad  0x10FF8u    /* +16 passa kGuestSize -> fora do range */

/* Memoria principal vista pelo SPU (o DMA deref vm_base + ea). A folga de 64
 * bytes no fim e proposital: se a guarda `spu_args_ea_in_range` regredir, a
 * leitura de kArgsEaBad continua dentro do objeto — o teste falha por VALOR
 * errado em vez de por comportamento indefinido. */
static uint8_t g_mem[kGuestSize + 64];
uint8_t* vm_base = g_mem;
static mfc_engine g_mfc;

/* Local store do "SPU thread" para o caso [B] (o ramo de taskset so dispara
 * com local_store != NULL). */
static uint8_t g_ls[SPU_LS_SIZE];

u128 spu_rdch(spu_context* ctx, uint32_t ch) {
    (void)ctx;
    u128 r = spu_zero();
    if (ch == MFC_RdTagStat) r._u32[0] = 0xFFFFFFFFu;  /* DMA sincrono -> feito */
    return r;
}
uint32_t spu_rchcnt(spu_context* ctx, uint32_t ch) { (void)ctx; (void)ch; return 1; }
void spu_wrch(spu_context* ctx, uint32_t channel, u128 value) {
    mfc_channel_write(&g_mfc, ctx, channel, value._u32[0]);
}
void spu_indirect_branch(spu_context* ctx) {
    (void)ctx; fprintf(stderr, "FAIL: unexpected indirect branch\n");
}
void spu_register_function(uint32_t addr, void (*fn)(spu_context*)) { (void)addr; (void)fn; }

/* O lifter emite `g_spu_trampoline_fn` para o salto entre fragmentos, e
 * spu_run_lifted_job drena-o. Em build completo quem o define e
 * runtime/spu/spu_channels.c; estes harnesses stubam a ABI de canais em vez de
 * a linkar, logo tem de fornecer o simbolo. */
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*) = 0;

/* Tamanho do espaco do guest, que o loader define em build completo. Convencao
 * das harnesses: 0 desliga as guardas de range. O caso [C] liga-a de proposito
 * durante uma corrida e volta a por a 0. */
uint32_t ppu_vm_size = 0;
int ppu_guest_range_committed(uint32_t addr, uint32_t n) { (void)addr; (void)n; return 1; }

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static void wbe32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static void wbe128(uint32_t ea, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
    wbe32(&g_mem[ea +  0], w0); wbe32(&g_mem[ea +  4], w1);
    wbe32(&g_mem[ea +  8], w2); wbe32(&g_mem[ea + 12], w3);
}

/* Corre o job e devolve, por out-param, as 4 words de r3 e as 4 de r4 tal como
 * o SPU as viu (via DMA PUT). */
static void run_job(uint8_t* local_store, uint32_t args_ea,
                    uint64_t r4_hi64, uint64_t r4_lo64,
                    uint32_t r3_out[4], uint32_t r4_out[4]) {
    memset(&g_mem[kResultEa], 0, 32);
    spu_run_lifted_job_ex(spu_func_00000000, local_store, args_ea, r4_hi64, r4_lo64);
    for (int i = 0; i < 4; i++) {
        r3_out[i] = be32(&g_mem[kResultEa + 4 * i]);
        r4_out[i] = be32(&g_mem[kResultEa + 16 + 4 * i]);
    }
}

/* Compara as 4 words e imprime lado a lado; devolve 1 se todas baterem. */
static int check4(const char* tag, const uint32_t got[4], const uint32_t exp[4]) {
    int ok = 1;
    for (int i = 0; i < 4; i++) ok &= (got[i] == exp[i]);
    printf("  [%-11s] got %08X %08X %08X %08X\n", tag,
           got[0], got[1], got[2], got[3]);
    printf("  %-14s exp %08X %08X %08X %08X   %s\n", "",
           exp[0], exp[1], exp[2], exp[3], ok ? "OK" : "FAIL");
    return ok;
}

int main(void) {
    mfc_engine_init(&g_mfc);
    memset(g_mem, 0, sizeof(g_mem));
    int ok = 1;
    uint32_t r3[4], r4[4];

    /* ---------------------------------------------------------------- [A] --
     * Job simples: r3 tem de ser as 4 words BE que vivem em args_ea. As words
     * sao todas diferentes entre si e nenhuma e igual a kArgsEaA, portanto:
     *   - a ABI escalar refutada (r3 = args_ea) falha na word 0;
     *   - "so a word 0 chega" falha nas words 1..3;
     *   - words trocadas de ordem falham na comparacao posicional.        */
    {
        const uint32_t exp[4] = { 0x1111AAAAu, 0x2222BBBBu, 0x3333CCCCu, 0x4444DDDDu };
        wbe128(kArgsEaA, exp[0], exp[1], exp[2], exp[3]);
        run_job(NULL, kArgsEaA, 0, 0, r3, r4);

        int words_ok = check4("A r3 u128", r3, exp);
        /* Refutacao explicita da ABI antiga: r3 nao pode ser o ponteiro. */
        int not_ea = (r3[0] != kArgsEaA);
        printf("  [A nao-e-EA ] r3.w0=0x%08X != args_ea=0x%08X               %s\n",
               r3[0], kArgsEaA, not_ea ? "OK" : "FAIL");
        const uint32_t z[4] = { 0, 0, 0, 0 };
        int r4_zero = check4("A r4 zero", r4, z);
        ok &= words_ok & not_ea & r4_zero;
    }

    /* ---------------------------------------------------------------- [B] --
     * Task de taskset: o argumento vem do SNAPSHOT em LS 0x2780 (o que
     * spu_task_ls_setup la poe a partir de TaskInfo), nao de args_ea — o
     * dispatch e assincrono e a stack do guest que tinha o argumento ja foi
     * reescrita quando a task corre. args_ea aponta para words DIFERENTES de
     * proposito: se o runtime as ler, o teste falha.                      */
    {
        const uint32_t exp[4] = { 0xAAAA0001u, 0xBBBB0002u, 0xCCCC0003u, 0xDDDD0004u };
        const uint64_t hi = 0x1122334455667788ull, lo = 0x99AABBCCDDEEFF00ull;
        const uint32_t exp_r4[4] = { 0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u };
        const uint32_t decoy[4] = { 0xDEAD0000u, 0xDEAD0001u, 0xDEAD0002u, 0xDEAD0003u };

        memset(g_ls, 0, sizeof(g_ls));
        for (int i = 0; i < 4; i++) {
            uint8_t* p = &g_ls[0x2780 + 4 * i];
            p[0] = (uint8_t)(exp[i] >> 24); p[1] = (uint8_t)(exp[i] >> 16);
            p[2] = (uint8_t)(exp[i] >> 8);  p[3] = (uint8_t)exp[i];
        }
        wbe128(kArgsEaB, decoy[0], decoy[1], decoy[2], decoy[3]);
        run_job(g_ls, kArgsEaB, hi, lo, r3, r4);

        int words_ok = check4("B r3 LS2780", r3, exp);
        int not_decoy = (r3[0] != decoy[0]);
        printf("  [B ign args ] r3.w0=0x%08X != EA payload 0x%08X           %s\n",
               r3[0], decoy[0], not_decoy ? "OK" : "FAIL");
        int r4_ok = check4("B r4 spurs", r4, exp_r4);
        ok &= words_ok & not_decoy & r4_ok;
    }

    /* ---------------------------------------------------------------- [C] --
     * args_ea fora do espaco do guest: `spu_args_ea_in_range` tem de rejeitar
     * e cair no fallback documentado (r3 = { args_ea, 0, 0, 0 }) em vez de ler
     * fora. A guarda ja esteve tautologica (`args_ea < 0x100000000u` num
     * uint32_t), ou seja o load corria SEM verificacao nenhuma.            */
    {
        const uint32_t exp[4] = { kArgsEaBad, 0, 0, 0 };
        ppu_vm_size = kGuestSize;      /* liga a guarda de range */
        run_job(NULL, kArgsEaBad, 0, 0, r3, r4);
        ppu_vm_size = 0;               /* convencao das harnesses: desligada */

        int words_ok = check4("C fallback", r3, exp);
        ok &= words_ok;
    }

    if (ok)
        printf("  PASS: CellSpursTaskArgument u128 — 4 words em r3 nos 3 ramos + r4.\n");
    return ok ? 0 : 1;
}
