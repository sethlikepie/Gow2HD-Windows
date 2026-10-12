/* SPU workload-dispatch test — host (PPU-side) harness.
 *
 * Validates runtime/spu/spu_workload.c, the cellSpurs dispatch brick:
 *   - cross-language FINGERPRINT agreement (C FNV-1a == the Python extractor's),
 *   - the SPU ELF -> local-store LOADER (a marker baked into the image is read
 *     back by the SPU job, proving the LS was loaded and not just zeroed),
 *   - the SPURS task ABI (the job's arg EA propagates into r3),
 *   - a registry MISS for an unregistered image returns 0 (caller can fall back).
 *
 * The SPU job (gen_test_workload.py) forwards both r3 and the LS marker to main
 * memory via DMA PUT; we register its lifted entry under the image fingerprint,
 * dispatch the raw image, and check the results.
 */
#include "spu_recomp.h"
#include "spu_helpers.h"
#include "spu_dma.h"
#include "spu_workload.h"
#include "test_workload_image.h"
#include "ps3_checkpoint.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Regression fix (bringup-instrumentacao-fase0, Tasks 1-2 vs. run_tests.sh):
 * spu_workload.c now calls ps3_checkpoint_fire() (FIRST_SPU_JOB), so this
 * harness links the shared $CHECKPOINT_SRC (ps3_trace.c + ps3_checkpoint.c --
 * see run_tests.sh). Correcao pos-review (Task 4 Minor #3): os quatro stubs
 * que ps3_checkpoint.c precisa (ps3_timebase_now + 3 accessors de SNAPSHOT)
 * ficam num header partilhado em vez de colados aqui — ver
 * runtime/trace/tests/trace_link_stubs.h para o porque. */
#include "trace_link_stubs.h"

/* SPU "main memory" view (DMA dereferences vm_base + ea). */
static uint8_t g_mem[1024];
uint8_t* vm_base = g_mem;
static mfc_engine g_mfc;

u128 spu_rdch(spu_context* ctx, uint32_t ch) {
    (void)ctx;
    u128 r = spu_zero();
    if (ch == 24) r._u32[0] = 0xFFFFFFFFu;   /* MFC_RdTagStat: DMA synchronous -> done */
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
static void wbe32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* O lifter emite `g_spu_trampoline_fn` para o salto entre fragmentos, e
 * spu_run_lifted_job drena-o. Em build completo quem o define e
 * runtime/spu/spu_channels.c; estes harnesses stubam a ABI de canais em vez de
 * o linkar, logo tem de fornecer o simbolo. */
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*) = 0;
/* spu_dma.h's reservation path is header-only. The harness owns the channel
 * implementation, so provide its otherwise-production globals locally. */
SPU_THREAD_LOCAL uint32_t t_spu_rsv_ea = 0;
SPU_THREAD_LOCAL int t_spu_rsv_valid = 0;
SPU_THREAD_LOCAL int t_spu_atomic_stat = 0;
SPU_THREAD_LOCAL uint8_t t_spu_rsv_line[128];
void ps3_atomic_line_lock(void) {}
void ps3_atomic_line_unlock(void) {}
static int g_seen_image_id = -1;

static void capture_image_id(spu_context* ctx)
{
    g_seen_image_id = ctx->image_id;
}

/* spu_dma.h valida o destino do DMA com esta funcao, que em build completo vem
 * de runtime/ppu/ppu_loader.cpp. Os harnesses usam um buffer proprio como
 * memoria do guest e nao linkam o loader, entao aceitam tudo. */
/* Tamanho do espaco do guest, que o loader define em build completo. Os
 * harnesses tem o seu proprio buffer, entao anunciam-no aqui. */
uint32_t ppu_vm_size = 0;

int ppu_guest_range_committed(uint32_t addr, uint32_t n) { (void)addr; (void)n; return 1; }

int main(void) {
    mfc_engine_init(&g_mfc);
    int ok = 1;

    /* 1) Fingerprint: C FNV-1a must match the Python extractor's value. */
    uint64_t fp = spu_workload_fingerprint(kWorkloadElf, kWorkloadElfSize);
    int fp_ok = (fp == kWorkloadFP);
    printf("  [FINGERPRINT] C=0x%016llX  py=0x%016llX  %s\n",
           (unsigned long long)fp, (unsigned long long)kWorkloadFP,
           fp_ok ? "OK" : "FAIL");
    ok &= fp_ok;

    /* 2) MISS before registration: dispatch must report no handler. */
    memset(g_mem, 0, sizeof(g_mem));
    int miss = spu_workload_dispatch(kWorkloadElf, kWorkloadElfSize, 0);
    int miss_ok = (miss == 0);
    printf("  [MISS       ] unregistered image -> dispatch=%d            %s\n",
           miss, miss_ok ? "OK" : "FAIL");
    ok &= miss_ok;

    /* 3) The workload's image namespace must reach the lifted context. A
     * zero image id means wildcard lookup and could dispatch a same-LS-address
     * function from a different SPU binary. */
    g_seen_image_id = -1;
    spu_workload_register_image(fp, capture_image_id, "image_id_contract", 17);
    int image_hit = spu_workload_dispatch(kWorkloadElf, kWorkloadElfSize, 0);
    int image_ok = (image_hit == 1 && g_seen_image_id == 17);
    printf("  [IMAGE ID   ] dispatch=%d ctx.image_id=%d                 %s\n",
           image_hit, g_seen_image_id, image_ok ? "OK" : "FAIL");
    ok &= image_ok;

    /* 4) Register the lifted entry under the image fingerprint, then dispatch
     *    the raw image with a task arg. The job forwards r3 + the LS marker. */
    spu_workload_register(fp, spu_func_00000000, "test_workload");
    printf("  [REGISTER   ] count=%u\n", spu_workload_count());

    /* ABI da task SPURS: o 3o parametro e o EA ONDE VIVE o CellSpursTaskArgument
     * de 128 bits, e r3 recebe esse valor carregado inteiro -- nao o EA. O job
     * reenvia r3 word 0, logo e a word 0 do argumento que tem de chegar.
     * Ver runtime/spu/spu_lifted_job.h:40. */
    const uint32_t kArgEa = 0x100u;   /* dentro do g_mem[1024], longe de [0] e [16] */
    const uint32_t kArg   = 0x0000ABCDu;
    memset(g_mem, 0, sizeof(g_mem));
    wbe32(&g_mem[kArgEa], kArg);
    int hit = spu_workload_dispatch(kWorkloadElf, kWorkloadElfSize, kArgEa);

    uint32_t got_arg    = be32(&g_mem[0]);    /* LS[0x100] word0 = r3 (task arg)  */
    uint32_t got_marker = be32(&g_mem[16]);   /* LS[0x110] word0 = baked-in marker */
    int hit_ok    = (hit == 1);
    int arg_ok    = (got_arg == kArg);
    int marker_ok = (got_marker == kWorkloadMarker);

    printf("  [DISPATCH   ] hit=%d                                       %s\n",
           hit, hit_ok ? "OK" : "FAIL");
    printf("  [TASK ARG   ] r3 -> main_mem = 0x%08X (expected 0x%08X)  %s\n",
           got_arg, kArg, arg_ok ? "OK" : "FAIL");
    printf("  [LS LOADED  ] marker         = 0x%08X (expected 0x%08X)  %s\n",
           got_marker, kWorkloadMarker, marker_ok ? "OK" : "FAIL");
    ok &= hit_ok & arg_ok & marker_ok;

    if (ok)
        printf("  PASS: fingerprint + ELF->LS load + task-arg ABI + dispatch all correct.\n");
    return ok ? 0 : 1;
}
