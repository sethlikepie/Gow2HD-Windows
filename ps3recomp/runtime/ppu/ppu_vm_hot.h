#ifndef PPU_VM_HOT_H
#define PPU_VM_HOT_H

/*
 * Thin vm_read32 for lifted TUs (func_00248F3C / 00156D78 do dozens per call).
 * The out-of-line ppu_loader vm_read32 is a multi-hundred-line diagnostic
 * function; even its "fast" path is still a call. This inlines load+bswap.
 * Functional steps kept: preempt poll, committed-range check, BE load.
 * Writes stay on the fat path (type15 stomp / rsv / tblsize).
 */
#include <stdint.h>
#include <string.h>
#include "ppu_vm_fast_policy.h"

#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t* vm_base __attribute__((visibility("hidden")));
extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);
/* Acessores COMPLETOS do ppu_loader: o caminho quente cai neles enquanto o
 * jogo nao entrou em gameplay. Medido 2026-09-18: usar o caminho fino desde o
 * arranque faz o jogo parar em draws=2 em 2/2 corridas -- o acessor completo
 * tem, do lado da LEITURA, ganchos de que o boot depende (hook do EOS do
 * filme, restauro de PT, guarda de type15/tblsize). */
uint32_t vm_read32(uint64_t addr);
uint64_t vm_read64(uint64_t addr);
void     vm_write32(uint64_t addr, uint32_t val);
/* Verificacao de faixa INLINE (ver o comentario junto de g_committed_* em
 * ppu_loader.cpp). Mesma semantica da versao fora de linha, incluindo o
 * "nada registado ainda -> nao guardar" do arranque. O load do contador e'
 * RELAXED e nao ACQUIRE: as faixas sao registadas no arranque, antes de haver
 * codigo liftado a correr, e a criacao das threads guest ja' e' um ponto de
 * sincronizacao que publica os arrays. */
#define PPU_MAX_COMMITTED_RANGES_HOT 8   /* tem de bater com PPU_MAX_COMMITTED_RANGES em ppu_loader.cpp */
extern uint32_t g_committed_lo[PPU_MAX_COMMITTED_RANGES_HOT];
extern uint32_t g_committed_hi[PPU_MAX_COMMITTED_RANGES_HOT];
extern int      g_committed_count;
static inline int ppu_range_committed_hot(uint32_t a, uint32_t n)
{
    const uint64_t end = (uint64_t)a + n;
    const int count = __atomic_load_n(&g_committed_count, __ATOMIC_RELAXED);
    for (int i = 0; i < count; i++)
        if (a >= g_committed_lo[i] && end <= g_committed_hi[i]) return 1;
    return count == 0;   /* arranque: nada registado -> deixar passar */
}
extern volatile int g_ppu_preempt_req;
void ppu_preempt_check(void);
#ifdef __cplusplus
}
#endif

/* Thin body with no gameplay gate. Used by vm_read32_hot (lift inlines) and
 * by the out-of-line vm_read32 wrapper (ppu_vm_gameplay.c) so fat diagnostics
 * stay in a cold symbol. */
static inline uint32_t vm_read32_thin(uint64_t addr)
{
    uint32_t a = (uint32_t)addr;
    if (__builtin_expect(__atomic_load_n(&g_ppu_preempt_req, __ATOMIC_RELAXED), 0))
        ppu_preempt_check();
    if (__builtin_expect(!ppu_range_committed_hot(a, 4u), 0))
        return 0;
    uint32_t v;
    memcpy(&v, vm_base + a, 4);
    return __builtin_bswap32(v);
}

static inline uint32_t vm_read32_hot(uint64_t addr)
{
    if (__builtin_expect(!ps3_vm_gameplay_ready_hot(), 0)) return vm_read32(addr);
    return vm_read32_thin(addr);
}

static inline uint64_t vm_read64_hot(uint64_t addr)
{
    uint32_t a = (uint32_t)addr;
    if (__builtin_expect(!ps3_vm_gameplay_ready_hot(), 0)) return vm_read64(addr);
    if (__builtin_expect(__atomic_load_n(&g_ppu_preempt_req, __ATOMIC_RELAXED), 0))
        ppu_preempt_check();
    if (__builtin_expect(!ppu_range_committed_hot(a, 8u), 0))
        return 0;
    uint64_t v;
    memcpy(&v, vm_base + a, 8);
    return __builtin_bswap64(v);
}

/* Same thin steps as the reads: preempt, committed-range, BE store.
 * Out-of-line vm_write64_hot in ppu_loader also does oob/tblsize/rsv;
 * stack/TOC spills in func_001569E4 do not hit those.
 * PPU_LOADER_TU skips the inline so ppu_loader.cpp can define the fat symbol. */
#ifndef PPU_LOADER_TU
static inline void vm_write64_hot(uint64_t addr, uint64_t val)
{
    uint32_t a = (uint32_t)addr;
    /* sem portao: este e' usado so' por patches pontuais, ver o comentario acima */
    if (__builtin_expect(__atomic_load_n(&g_ppu_preempt_req, __ATOMIC_RELAXED), 0))
        ppu_preempt_check();
    if (__builtin_expect(!ppu_range_committed_hot(a, 8u), 0))
        return;
    uint64_t raw = __builtin_bswap64(val);
    memcpy(vm_base + a, &raw, 8);
}
#endif

#ifndef PPU_VM_HOT_NO_MACRO
#define vm_read32(addr) vm_read32_hot((addr))
#endif

#endif
