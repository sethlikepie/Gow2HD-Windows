#include "spu_recomp.h"
#include "spu_helpers.h"
#include "spu_dma.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Backing buffer for the SPU's "main memory" view. The DMA path in
 * spu_dma.h dereferences vm_base + (uint32_t)ea, so any host pointer
 * works as long as the EA values used by the test land inside it. */
static uint8_t g_mem[1024];
uint8_t* vm_base = g_mem;

/* Stubs for the hardened-DMA bounds check. spu_dma.h's mfc_do_transfer now
 * references the PPU VM manager's committed-range guard; that lives in the boot
 * binary, not in a unit test, so provide test doubles here. ppu_vm_size = 0
 * disables the range guard for the happy-path GET below (which reads EA 0 <
 * 0x10000); the bounds-regression block flips it on to exercise the rejects. */
uint32_t ppu_vm_size = 0;
static int g_commit_ok = 1;
int ppu_guest_range_committed(uint32_t addr, uint32_t n) {
    (void)addr; (void)n; return g_commit_ok;
}

static mfc_engine g_mfc;
static uint32_t g_out = 0;
static int g_wrote = 0;

u128 spu_rdch(spu_context* ctx, uint32_t ch) { (void)ctx; (void)ch; return spu_zero(); }
uint32_t spu_rchcnt(spu_context* ctx, uint32_t ch) { (void)ctx; (void)ch; return 1; }
void spu_wrch(spu_context* ctx, uint32_t channel, u128 value) {
    if (channel == SPU_WrOutMbox) { g_out = value._u32[0]; g_wrote = 1; return; }
    /* All other MFC-bound channels go through the real DMA engine in
     * spu_dma.h. mfc_channel_write handles staging registers AND the
     * MFC_Cmd write that triggers the transfer. */
    mfc_channel_write(&g_mfc, ctx, channel, value._u32[0]);
}
void spu_indirect_branch(spu_context* ctx) {
    (void)ctx; fprintf(stderr, "FAIL: unexpected indirect branch\n");
}
void spu_register_function(uint32_t addr, void (*fn)(spu_context*)) { (void)addr; (void)fn; }


/* O lifter emite `g_spu_trampoline_fn` para o salto entre fragmentos, e
 * spu_run_lifted_job drena-o. Em build completo quem o define e
 * runtime/spu/spu_channels.c; estes harnesses stubam a ABI de canais em vez de
 * o linkar, logo tem de fornecer o simbolo. */
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*) = 0;

int main(void) {
    /* Pre-fill "main memory" at offset 0 with a recognizable BE pattern.
     * After a 16-byte GET into LS+0x100, the SPU's lqd should read the
     * first word as 0xDEADBEEF (big-endian byte order: DE AD BE EF). */
    g_mem[0] = 0xDE; g_mem[1] = 0xAD; g_mem[2] = 0xBE; g_mem[3] = 0xEF;

    spu_context ctx;
    spu_context_init(&ctx, 0);
    mfc_engine_init(&g_mfc);

    spu_func_00000000(&ctx);

    const uint32_t kExpected = 0xDEADBEEFu;
    /* Walk every stage so a partial pass is diagnostic. */
    uint32_t ls_word = spu_ls_read32(&ctx, 0x100);
    int dma_ok = (ls_word == kExpected);
    int wrch_ok = (g_wrote && g_out == kExpected);

    printf("  DMA -> LS[0x100] = 0x%08X (expected 0x%08X)  %s\n",
           ls_word, kExpected, dma_ok ? "OK" : "FAIL");
    printf("  lqd r10 -> wrch  = 0x%08X (expected 0x%08X)  %s\n",
           g_out, kExpected, wrch_ok ? "OK" : "FAIL");

    if (dma_ok && !wrch_ok && g_out == 0xEFBEADDEu) {
        printf("  -> known endianness gap: spu_ls_read128 does raw memcpy,\n"
               "     so _u32[0] on an LE host returns the BE bytes byte-swapped.\n"
               "     spu_ls_read32 uses explicit BE assembly (and works).\n");
    }

    /* ---- hardened-DMA bounds regression -------------------------------
     * Every out-of-bounds/uncommitted transfer must be rejected (return -1)
     * WITHOUT faulting: a garbage descriptor EA otherwise faults inside the
     * memcpy and takes the host down. Exercise the guard directly. */
    ppu_vm_size = 0x20000u;   /* enable the range guard (address space bound) */
    int b_ok = 1;
    /* GET size 0 -> reject */
    if (mfc_do_transfer(&ctx, 0x100, 0x10000, 0, MFC_GET_CMD) != -1) b_ok = 0;
    /* GET past end of VM (ea + size > vm_size) -> reject */
    if (mfc_do_transfer(&ctx, 0x100, 0x20000 - 16, 64, MFC_GET_CMD) != -1) b_ok = 0;
    /* GET into the reserved low page (ea < 0x10000) -> reject */
    if (mfc_do_transfer(&ctx, 0x100, 0x40, 16, MFC_GET_CMD) != -1) b_ok = 0;
    /* GET in range but uncommitted -> reject (genuinely hits the commit branch) */
    g_commit_ok = 0;
    if (mfc_do_transfer(&ctx, 0x100, 0x10000, 16, MFC_GET_CMD) != -1) b_ok = 0;
    g_commit_ok = 1;
    /* DMA-list with an out-of-range element EA -> reject (no AV) */
    { spu_ls_write32(&ctx, 0x200, (16u));           /* elem: size=16, no flags */
      spu_ls_write32(&ctx, 0x204, 0x00000010u);     /* elem: EA low = 0x10 (reserved) */
      ctx.mfc_lsa = 0x100;
      if (mfc_do_list_transfer(&ctx, 0x200, 0, 8, MFC_GETL_CMD) != -1) b_ok = 0; }

    printf("  bounds guard (size0/OOB/low-EA/uncommitted/list)  %s\n",
           b_ok ? "OK" : "FAIL");

    return (dma_ok && wrch_ok && b_ok) ? 0 : 1;
}
