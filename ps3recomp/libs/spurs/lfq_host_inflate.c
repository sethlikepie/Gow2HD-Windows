/*
 * ps3recomp - host-side fulfiller for GoW2's edgezlib inflate commands.
 *
 * FIOS's dearchiver hands 32-byte inflate commands to the edgezlib SPU task
 * through the ANY2ANY control LFQueue. Until the v2 pop protocol state is
 * fully mirrored by the PPU push, the lifted task cannot see those elements,
 * so the render thread (which waits synchronously on the dearchive op) polls
 * forever. This fulfils the command on the host instead: raw-deflate the
 * source into the destination and complete the op.
 *
 * Element layout (BE words, reverse-engineered live -- see leva 16j/16k in
 * the project memory):
 *   +0x00 src EA        (points past the 2-byte zlib header -> raw deflate)
 *   +0x04 request ptr   (original mediathread stack buffer)
 *   +0x08 compressed size (bytes at src)
 *   +0x0C opcode/kind   (0x0C observed for inflate)
 *   +0x10 op-struct EA  (unaligned: byte flag inside the dearchive op)
 *   +0x14 dest EA       (128-aligned output buffer)
 *   +0x18 max output    (0x10000 = one psarc block)
 *   +0x1C window bits   (0xFFF4 = -12 -> raw deflate, 4 KB window)
 *
 * Inflate engine: stb_image's zlib decoder (already vendored in
 * libs/codec/stb_image.h) with parse_header=0.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Declarations only — STB_IMAGE_IMPLEMENTATION lives in cellPngDec.c.
 * Defining it here too doubles every stbi_* symbol at link time. */
#define STBI_NO_STDIO
#include "../codec/stb_image.h"

extern unsigned char* vm_base;
extern int lfq_ea_readable(uint32_t ea, uint32_t len);   /* cellSpursLFQueue.c */

static uint32_t hi_rd32(const uint8_t* p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* Returns 1 when the element was recognized and fulfilled host-side
 * (caller should still deliver/log as it sees fit), 0 otherwise. */
int lfq_host_inflate_try(const uint8_t* elem)
{
    /* E138 (2026-09-02): layout REAL do EdgeZlibInflateQElement, lido do anel
     * da fila no RPCS3 (0x33008200, 4 elementos) e confirmado pelas asserts
     * de edgezlib_inflate_queue.cpp presentes no ELF (pWorkToDoCounter 4-aligned,
     * outputUncompSkipBeginSize + outputUncompSkipEndSize <= expectedUncompressedSize):
     *   +0x00 eaInputCompressedData      (aponta para depois do header zlib de 2 bytes)
     *   +0x04 eaOutputUncompressedData   <-- destino REAL (antes lia-se +0x14: o EVENT FLAG)
     *   +0x08 inputCompressedSize
     *   +0x0C outputUncompressedSize     <-- bytes a escrever (antes: "kind==0x0C" -- era o 12 do header do psarc)
     *   +0x10 eaWorkToDoCounter (u32, & ~3) -- decrementado na conclusao
     *   +0x14 eaEventFlag (CellSpursEventFlag)
     *   +0x18 hi16 eventFlagBits | lo16 outputUncompSkipBeginSize
     *   +0x1C hi16 outputUncompSkipEndSize | lo16 0
     * O layout antigo escrevia 64 KB (os "eventFlagBits" 0x10000) por cima do event
     * flag, da fila e do scheduler FIOS (0x43009270): medido com PS3_WATCH_STORE
     * (E137) -- o scheduler ficava a zeros, "attempt to unlock invalid mutex",
     * mediathreads a morrer, alloc de ops a falhar (HOST-POP), closes de WAD
     * perdidos, r_shella "no free slot". E so' o 1o elemento (out=12) era servido:
     * os blocos de 64 KB do psarc eram rejeitados como "kind!=0x0C". */
    uint32_t src  = hi_rd32(elem + 0x00);
    uint32_t dst  = hi_rd32(elem + 0x04);
    uint32_t csz  = hi_rd32(elem + 0x08);
    uint32_t outn = hi_rd32(elem + 0x0C);
    uint32_t wtd  = hi_rd32(elem + 0x10);
    uint32_t evf  = hi_rd32(elem + 0x14);
    uint32_t w18  = hi_rd32(elem + 0x18);
    uint32_t w1c  = hi_rd32(elem + 0x1C);
    uint32_t bits = w18 >> 16, skip_begin = w18 & 0xFFFFu, skip_end = w1c >> 16;
    uint32_t kind = outn; /* compat com HI_REJECT */
    uint32_t opea = wtd, dmax = outn;

#define HI_REJECT(why) do { \
        static int on = -1; if (on < 0) on = getenv("PS3_TRACE_HOSTINFL") ? 1 : 0; \
        if (on) fprintf(stderr, \
            "[HOSTINFL-NO] %s kind=0x%X src=0x%08X csz=%u dst=0x%08X dmax=%u op=0x%08X\n", \
            (why), kind, src, csz, dst, dmax, opea); \
        return 0; } while (0)

    if (!src || !dst || !csz || csz > 0x20000 || !outn || outn > 0x100000)
        HI_REJECT("campo fora de gama");
    if (src >= 0xFFF00000u || dst >= 0xFFF00000u) HI_REJECT("EA alto demais");
    uint32_t expected = skip_begin + outn + skip_end;
    if (expected > 0x100000) HI_REJECT("expected > 1MB");
    if (!lfq_ea_readable(src, csz) || !lfq_ea_readable(dst, outn))
        HI_REJECT("EA nao legivel");

    /* Inflate para um buffer host temporario do tamanho esperado, e copiar
     * so' a janela [skip_begin, skip_begin+outn) para o destino guest. */
    char* tmp = (char*)malloc(expected ? expected : 1);
    if (!tmp) HI_REJECT("sem memoria host");
    /* winport v27: WINPORT_INFL_UNLOCK=1 -- decode without the PPU giant lock (see engine_fixes_v27.py) */
    static int s_wp_unl = -1; if (s_wp_unl < 0) { const char* e = getenv("WINPORT_INFL_UNLOCK"); s_wp_unl = (e && e[0] == '1') ? 1 : 0; }
    extern int ppu_giant_lock_held_by_caller(void); extern void ppu_giant_lock_release(void); extern void ppu_giant_lock_acquire(void);
    int wp_unl = s_wp_unl && ppu_giant_lock_held_by_caller();
    if (wp_unl) ppu_giant_lock_release();
    int outlen = stbi_zlib_decode_noheader_buffer(tmp, (int)expected, (const char*)(vm_base + src), (int)csz);
    if (outlen <= 0 && csz > 2 && vm_base[src] == 0x78) /* header zlib presente: saltar */
        outlen = stbi_zlib_decode_noheader_buffer(tmp, (int)expected, (const char*)(vm_base + src + 2), (int)csz - 2);
    if (wp_unl) ppu_giant_lock_acquire();
    uint32_t copied = 0;
    if (outlen > 0 && (uint32_t)outlen > skip_begin) {
        copied = (uint32_t)outlen - skip_begin; if (copied > outn) copied = outn;
        memcpy(vm_base + dst, tmp + skip_begin, copied);
    }
    free(tmp);

    static int s_wp_tr = -1; if (s_wp_tr < 0) s_wp_tr = getenv("PS3_TRACE_HOSTINFL") ? 1 : 0;   /* winport v27: was unconditional */
    if (s_wp_tr) fprintf(stderr, "[HOSTINFL] src=0x%08X csz=%u dst=0x%08X out=%u skip=%u/%u expected=%u -> inflated=%d copied=%u wtd=0x%08X evf=0x%08X bits=0x%X\n",
            src, csz, dst, outn, skip_begin, skip_end, expected, outlen, copied, wtd, evf, bits);
    if (outlen <= 0) return 0;

    /* Conclusao fiel ao SPU: decrementar o contador de trabalho (u32 BE, 4-aligned)
     * e assinalar o event flag SPURS com os bits do elemento. */
    if (wtd) {
        uint32_t cea = wtd & ~3u;
        if (cea < 0xFFF00000u && lfq_ea_readable(cea, 4)) {
            uint32_t v = hi_rd32(vm_base + cea);
            if (v) { v--; vm_base[cea] = (uint8_t)(v >> 24); vm_base[cea+1] = (uint8_t)(v >> 16); vm_base[cea+2] = (uint8_t)(v >> 8); vm_base[cea+3] = (uint8_t)v; }
        }
    }
    if (evf && bits) {
        extern int cellSpursEventFlagSet(void* eventFlag, unsigned short bits);
        int rc = cellSpursEventFlagSet((void*)(uintptr_t)evf, (unsigned short)bits);
        if (s_wp_tr) fprintf(stderr, "[HOSTINFL] evf set rc=0x%X evf words=%08X %08X %08X %08X ctr=%u\n", (unsigned)rc,
                hi_rd32(vm_base + evf), hi_rd32(vm_base + evf + 4), hi_rd32(vm_base + evf + 8), hi_rd32(vm_base + evf + 12),
                hi_rd32(vm_base + (wtd & ~3u)));
    }
    return 1;
}
