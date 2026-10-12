/* SPU bring-up prototype — host (PPU-side) harness.
 *
 * Runs the lifted SPU program from gen_test_bringup.py and verifies the two
 * halves of the PPU<->SPU coordination handshake that flOw's SPURS boot needs:
 *   1. the SPU produced output in shared "main memory" via a DMA PUT, and
 *   2. the SPU signalled completion to the PPU via the outbound mailbox.
 *
 * This is the isolated unit the SPU-workstream scope calls for: a real SPU job
 * driven from the PPU side, communicating back via DMA + a completion signal,
 * decoupled from flOw's boot. The next layer wraps this in cellSpurs /
 * cellSync-LFQueue / event-queue semantics.
 */
#include "spu_recomp.h"
#include "spu_helpers.h"
#include "spu_dma.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Backing buffer for the SPU's "main memory" view (DMA dereferences vm_base+ea). */
static uint8_t g_mem[1024];
uint8_t* vm_base = g_mem;

static mfc_engine g_mfc;
static uint32_t   g_mbox = 0;   /* SPU -> PPU outbound mailbox */
static int        g_mbox_wrote = 0;

u128 spu_rdch(spu_context* ctx, uint32_t ch) { (void)ctx; (void)ch; return spu_zero(); }
uint32_t spu_rchcnt(spu_context* ctx, uint32_t ch) { (void)ctx; (void)ch; return 1; }

void spu_wrch(spu_context* ctx, uint32_t channel, u128 value) {
    if (channel == SPU_WrOutMbox) {           /* SPU signals the PPU */
        g_mbox = value._u32[0];
        g_mbox_wrote = 1;
        return;
    }
    /* MFC channels -> the real DMA engine (LS <-> main memory). */
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

int main(void) {
    memset(g_mem, 0, sizeof(g_mem));

    spu_context ctx;
    spu_context_init(&ctx, 0);
    mfc_engine_init(&g_mfc);

    /* Drive the SPU job from the PPU side. The lifted program writes 0x1234
     * into LS, DMA-PUTs it to main memory at EA 0, and signals via mailbox. */
    spu_func_00000000(&ctx);

    const uint32_t kExpect = 0x00001234u;
    uint32_t put_val = be32(&g_mem[0]);                       /* SPU -> main memory */
    int put_ok  = (put_val == kExpect);
    int mbox_ok = (g_mbox_wrote && g_mbox == kExpect);        /* SPU -> PPU signal  */

    printf("  [DMA PUT ] SPU -> main_mem[0] = 0x%08X (expected 0x%08X)  %s\n",
           put_val, kExpect, put_ok ? "OK" : "FAIL");
    printf("  [MAILBOX ] SPU -> PPU signal  = 0x%08X (wrote=%d)         %s\n",
           g_mbox, g_mbox_wrote, mbox_ok ? "OK" : "FAIL");

    if (put_ok && mbox_ok)
        printf("  PASS: SPU job produced shared-memory output AND signalled PPU completion.\n");
    return (put_ok && mbox_ok) ? 0 : 1;
}
