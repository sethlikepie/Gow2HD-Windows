/* test_spurs_e2e_main.c — END-TO-END SPURS execution test.
 *
 * Proves the full Marco 3 chain, with the REAL lifted SPU job (not a C stub):
 *
 *   cellSpursInitialize           -> starts the SPURS HLE kernel thread
 *   spu_workload_register(fp,...)  -> the title's lifted SPU set
 *   cellSpursAddWorkload(image)    -> kernel.submit (async, off this thread)
 *   [kernel thread] dispatch       -> spu_workload_dispatch -> load LS -> RUN job
 *   job: DMA PUT r3 + LS marker    -> main memory
 *   spurs_kernel_drain             -> block until the workload finished
 *   verify g_mem                   -> the SPU job actually ran via SPURS
 *   cellSpursFinalize              -> kernel drain + join
 *
 * Reuses the workload test's lifted job (out_workload/spu_recomp.c) and baked
 * image (test_workload_image.h). This is the SPURS-bring-up Marco 3 acceptance
 * criterion: "a synthetic workload submitted, executed, and its result appears
 * in host memory."
 */
#include "spu_recomp.h"
#include "spu_helpers.h"
#include "spu_dma.h"
#include "spu_workload.h"
#include "cellSpurs.h"
#include "spurs_kernel.h"
#include "test_workload_image.h"
#include "ps3_checkpoint.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Regression fix (bringup-instrumentacao-fase0, Tasks 1-2 vs. run_tests.sh):
 * spu_workload.c calls ps3_checkpoint_fire() (FIRST_SPU_JOB) and cellSpurs.c/
 * spurs_kernel.c call PS3_TRACE(PS3_TS_SPURS, ...) (Task 4), so this harness
 * links the shared $CHECKPOINT_SRC (ps3_trace.c + ps3_checkpoint.c -- see
 * run_tests.sh, superset covers both needs). Post-review fix (Task 4
 * Minor #3): the four stubs ps3_checkpoint.c needs now live in a shared
 * header -- see runtime/trace/tests/trace_link_stubs.h. */
#include "trace_link_stubs.h"

/* SPU "main memory" view (DMA dereferences vm_base + ea). */
static uint8_t g_mem[1024];
uint8_t* vm_base = g_mem;
static mfc_engine g_mfc;

u128 spu_rdch(spu_context* ctx, uint32_t ch) {
    (void)ctx;
    u128 r = spu_zero();
    if (ch == 24) r._u32[0] = 0xFFFFFFFFu;   /* MFC_RdTagStat: synchronous -> done */
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


static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* O lifter emite `g_spu_trampoline_fn` para o salto entre fragmentos, e
 * spu_run_lifted_job drena-o. Em build completo quem o define e
 * runtime/spu/spu_channels.c; estes harnesses stubam a ABI de canais em vez de
 * o linkar, logo tem de fornecer o simbolo. */
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*) = 0;

/* spu_dma.h valida o destino do DMA com esta funcao, que em build completo vem
 * de runtime/ppu/ppu_loader.cpp. Os harnesses usam um buffer proprio como
 * memoria do guest e nao linkam o loader, entao aceitam tudo. */
/* Tamanho do espaco do guest, que o loader define em build completo. Os
 * harnesses tem o seu proprio buffer, entao anunciam-no aqui. */
uint32_t ppu_vm_size = 0;

int ppu_guest_range_committed(uint32_t addr, uint32_t n) { (void)addr; (void)n; return 1; }

/* O giant lock do PPU vive em runtime/ppu/ppu_loader.cpp, que estes harnesses
 * nao linkam. So o cellSpursEventFlagWait o usa (larga-o durante a espera para
 * nao congelar as outras threads PPU) e este teste nao exercita event flags,
 * mas o simbolo tem de existir porque o cellSpurs.c inteiro entra no link.
 * Sem PPU nenhum a serializar, nao ha nada para trancar: no-ops. */
void ppu_giant_lock_release(void) { }
void ppu_giant_lock_acquire_prio(void) { }

/* vm_write32 tambem vem do ppu_loader.cpp. O cellSpurs usa-o para devolver o
 * task id ao guest; aqui escreve-se no mesmo g_mem que serve de memoria do
 * guest, em big-endian, que e a convencao do vm_* real. */
void vm_write32(unsigned long long addr, unsigned int val)
{
    /* Grita em vez de descartar: um EA fora do g_mem escrito em silencio
     * aparece la a frente como "o argumento chegou a zero", que se persegue
     * como bug de ABI quando e so o harness a ser pequeno. */
    if (addr + 4 > sizeof g_mem) {
        fprintf(stderr, "[harness] vm_write32 fora do g_mem: ea=0x%llX (max 0x%zX)\n",
                addr, sizeof g_mem);
        return;
    }
    uint8_t* p = &g_mem[addr];
    p[0] = (uint8_t)(val >> 24); p[1] = (uint8_t)(val >> 16);
    p[2] = (uint8_t)(val >> 8);  p[3] = (uint8_t)val;
}

/* cellSpursGetWorkloadInfo escreve o campo data (u64 BE) via vm_write64; mesma
 * convencao big-endian do vm_* real, que nao linkamos aqui. */
void vm_write64(unsigned long long addr, unsigned long long val)
{
    if (addr + 8 > sizeof g_mem) {
        fprintf(stderr, "[harness] vm_write64 fora do g_mem: ea=0x%llX (max 0x%zX)\n",
                addr, sizeof g_mem);
        return;
    }
    uint8_t* p = &g_mem[addr];
    p[0] = (uint8_t)(val >> 56); p[1] = (uint8_t)(val >> 48);
    p[2] = (uint8_t)(val >> 40); p[3] = (uint8_t)(val >> 32);
    p[4] = (uint8_t)(val >> 24); p[5] = (uint8_t)(val >> 16);
    p[6] = (uint8_t)(val >> 8);  p[7] = (uint8_t)val;
}

int main(void) {
    mfc_engine_init(&g_mfc);
    int ok = 1;

    /* Register the title's lifted SPU set (here: the one test job). */
    uint64_t fp = spu_workload_fingerprint(kWorkloadElf, kWorkloadElfSize);
    spu_workload_register(fp, spu_func_00000000, "e2e_workload");

    /* Bring up SPURS — this starts the HLE kernel thread. */
    CellSpurs spurs;
    s32 rc = cellSpursInitialize(&spurs, 2, 0, 0, 0);
    int init_ok = (rc == CELL_OK) && spurs_kernel_running();
    printf("  [INIT       ] cellSpursInitialize rc=%d kernel_running=%d   %s\n",
           rc, spurs_kernel_running(), init_ok ? "OK" : "FAIL");
    ok &= init_ok;

    /* Submit the workload through the real SPURS API. The kernel runs it on its
     * own thread; results must NOT be present until we drain. */
    /* ABI da task SPURS: o argumento passado ao AddWorkload e o EA ONDE VIVE o
     * CellSpursTaskArgument de 128 bits, e r3 recebe esse valor carregado
     * inteiro -- nao o EA. O job reenvia r3 word 0, logo e a word 0 do
     * argumento que tem de chegar. Ver runtime/spu/spu_lifted_job.h:40.
     * Este teste vinha do master a passar o valor directamente: nunca chegou a
     * correr la (nao estava ligado a nenhuma suite), entao ficou preso a
     * convencao antiga, tal como o test_adapter e o test_workload. */
    const uint32_t kArgEa = 0x100u;   /* dentro do g_mem[1024], longe de [0] e [16] */
    const uint32_t kArg   = 0x0000ABCDu;
    memset(g_mem, 0, sizeof(g_mem));
    vm_write32(kArgEa, kArg);

    CellSpursWorkloadId wid = 0xFFFFFFFFu;
    rc = cellSpursAddWorkload(&spurs, &wid, kWorkloadElf, kWorkloadElfSize,
                              (u64)kArgEa, NULL, 0, 8);
    int add_ok = (rc == CELL_OK);
    printf("  [ADD        ] cellSpursAddWorkload rc=%d wid=%u                %s\n",
           rc, wid, add_ok ? "OK" : "FAIL");
    ok &= add_ok;

    int drained = spurs_kernel_drain(5000);
    printf("  [DRAIN      ] completed=%u/%u                                  %s\n",
           spurs_kernel_completed(), spurs_kernel_submitted(),
           drained ? "OK" : "FAIL");
    ok &= drained;

    uint32_t got_arg    = be32(&g_mem[0]);    /* job DMA'd r3 (task arg) here   */
    uint32_t got_marker = be32(&g_mem[16]);   /* job DMA'd the baked LS marker  */
    int arg_ok    = (got_arg == kArg);
    int marker_ok = (got_marker == kWorkloadMarker);
    printf("  [JOB ARG    ] r3 -> mem = 0x%08X (expected 0x%08X)         %s\n",
           got_arg, kArg, arg_ok ? "OK" : "FAIL");
    printf("  [JOB RAN    ] LS marker = 0x%08X (expected 0x%08X)         %s\n",
           got_marker, kWorkloadMarker, marker_ok ? "OK" : "FAIL");
    ok &= arg_ok & marker_ok;

    cellSpursFinalize(&spurs);
    int fin_ok = !spurs_kernel_running();
    printf("  [FINALIZE   ] kernel joined, running=%d                       %s\n",
           spurs_kernel_running(), fin_ok ? "OK" : "FAIL");
    ok &= fin_ok;

    if (ok)
        printf("  PASS: SPURS workload executed end-to-end (API -> kernel -> SPU job -> DMA).\n");
    else
        printf("  FAIL: SPURS e2e test failed.\n");
    return ok ? 0 : 1;
}
