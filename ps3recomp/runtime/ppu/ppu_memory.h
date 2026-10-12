/*
 * ps3recomp - PPU memory access for recompiled code
 *
 * All PS3 memory is big-endian.  These functions translate a 32-bit PS3
 * virtual address to a host pointer via vm_base, then perform a load or
 * store with the appropriate byte swap.
 *
 * Atomic operations (lwarx/stwcx) are emulated with C11 atomics.
 */

#ifndef PPU_MEMORY_H
#define PPU_MEMORY_H

#include "../../include/ps3emu/endian.h"
#include "ppu_context.h"

#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
/* Para a vigia de memoria baixa la' em baixo: getenv/strtoul/atoi, fprintf e
 * dladdr. Todos POSIX, e o alvo deste header e' macOS/Linux. */
#include <stdlib.h>
#include <stdio.h>
#ifndef _WIN32
#include <dlfcn.h>   /* dladdr: so' POSIX -- ver o fallback no reporter */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Base pointer -- set by the VM manager at startup.
 *
 * Host pointer to the beginning of the PS3 32-bit address space mapping.
 * All guest address translation is:  host_ptr = vm_base + guest_addr
 * -----------------------------------------------------------------------*/
extern uint8_t* vm_base;

/* Guest range physically-committed check (defined in ppu_loader.cpp, which
 * tracks the sparse VirtualAlloc(MEM_COMMIT) ranges boot_main.cpp actually
 * backs). A garbage guest address computed from bad data (uninitialized
 * handle, failed-allocation value used as a pointer, etc.) can be
 * "logically" inside the 0xE0000000 guest space yet fall in a
 * reserved-but-never-committed gap, causing a genuine host access
 * violation that crashes the whole process. Returns non-zero if [addr,
 * addr+n) is safe to touch. Cheap: a handful of integer range comparisons,
 * no syscall. */
extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);

/* ---------------------------------------------------------------------------
 * Address translation
 * -----------------------------------------------------------------------*/
/* Vigia de TRADUCAO em memoria baixa (PS3_WATCH_LOWPTR=N, default 0 = OFF).
 *
 * Porque nao chega a vigia de vm_write*: medido no E16, ZERO escritas do guest
 * em [0,0x1000) -- e mesmo assim a string "/r_perma.wad" esta' la'. Quem
 * escreve e' codigo HOST (HLE) a fazer memcpy para dentro da memoria do guest,
 * e esse caminho nao passa por vm_write*, passa por aqui: vm_translate e' o
 * unico sitio por onde TODA a traducao host<->guest tem de passar.
 *
 * Apanha leituras e escritas -- de proposito. Nesta fase interessa saber QUEM
 * toca em [0, N), nao so' quem escreve.
 * Ver docs/re_sessions/2026-08-05-E16-nao-foi-o-guest.md.
 *
 * Declarada aqui em cima e definida junto da outra vigia, mais abaixo. */
static inline uint32_t ppu_lowptr_watch_limit(void);
void ppu_lowptr_translate_hit(uint32_t addr, void* ra);

static inline uint32_t ppu_watch_ea(void);

static inline void* vm_translate(uint32_t addr)
{
    uint32_t _wea = ppu_watch_ea();
    if (__builtin_expect(addr < ppu_lowptr_watch_limit()
                         || (_wea != 0u && addr == _wea), 0))
        ppu_lowptr_translate_hit(addr, __builtin_return_address(0));
    return (void*)(vm_base + addr);
}

static inline uint8_t* vm_ptr8(uint32_t addr)  { return (uint8_t*)vm_translate(addr); }
static inline uint16_t* vm_ptr16(uint32_t addr) { return (uint16_t*)vm_translate(addr); }
static inline uint32_t* vm_ptr32(uint32_t addr) { return (uint32_t*)vm_translate(addr); }
static inline uint64_t* vm_ptr64(uint32_t addr) { return (uint64_t*)vm_translate(addr); }

/* ---------------------------------------------------------------------------
 * Loads -- read from big-endian guest memory, return host-endian value.
 * -----------------------------------------------------------------------*/
/* E229: ponto de preempcao temporal do giant lock (ver ppu_loader.cpp). */
#ifdef __cplusplus
extern "C" {
#endif
extern volatile int g_ppu_preempt_req;
void ppu_preempt_check(void);
#ifndef PPU_PREEMPT_STRONG
/* Definicoes fracas (testes/executaveis sem runtime/ppu); ppu_loader.cpp define as fortes. */
__attribute__((weak)) volatile int g_ppu_preempt_req = 0;
__attribute__((weak)) void ppu_preempt_check(void) {}
#endif
#ifdef __cplusplus
}
#endif
#define PPU_PREEMPT_POLL() do { if (__builtin_expect(__atomic_load_n(&g_ppu_preempt_req, __ATOMIC_RELAXED), 0)) ppu_preempt_check(); } while (0)
static inline uint8_t vm_read8(uint32_t addr)
{
    PPU_PREEMPT_POLL();
    if (!ppu_guest_range_committed(addr, 1)) return 0;
    return *vm_ptr8(addr);
}

static inline uint16_t vm_read16(uint32_t addr)
{
    PPU_PREEMPT_POLL();
    uint16_t raw;
    if (!ppu_guest_range_committed(addr, 2)) return 0;
    memcpy(&raw, vm_ptr8(addr), sizeof(raw));
    return ps3_bswap16(raw);
}

static inline uint32_t vm_read32(uint32_t addr)
{
    PPU_PREEMPT_POLL();
    uint32_t raw;
    if (!ppu_guest_range_committed(addr, 4)) return 0;
    memcpy(&raw, vm_ptr8(addr), sizeof(raw));
    return ps3_bswap32(raw);
}

static inline uint64_t vm_read64(uint32_t addr)
{
    PPU_PREEMPT_POLL();
    uint64_t raw;
    if (!ppu_guest_range_committed(addr, 8)) return 0;
    memcpy(&raw, vm_ptr8(addr), sizeof(raw));
    return ps3_bswap64(raw);
}

static inline float vm_read_f32(uint32_t addr)
{
    uint32_t raw;
    if (!ppu_guest_range_committed(addr, 4)) return 0.0f;
    memcpy(&raw, vm_ptr8(addr), sizeof(raw));
    raw = ps3_bswap32(raw);
    float result;
    memcpy(&result, &raw, sizeof(result));
    return result;
}

static inline double vm_read_f64(uint32_t addr)
{
    uint64_t raw;
    if (!ppu_guest_range_committed(addr, 8)) return 0.0;
    memcpy(&raw, vm_ptr8(addr), sizeof(raw));
    raw = ps3_bswap64(raw);
    double result;
    memcpy(&result, &raw, sizeof(result));
    return result;
}

/* ---------------------------------------------------------------------------
 * Low-memory write watch -- who writes through a NULL pointer?
 *
 * Guest low memory IS committed, so a write through a null-ish pointer lands
 * silently and only shows up much later, as text being consumed as a pointer.
 * Measured 2026-08-05: the string "/r_perma.wad" sits at guest address 4, and
 * *(4) / *(9) are then used as `this` and as a free-list head
 * (docs/re_sessions/2026-08-05-E15-a-string-no-endereco-4.md).
 *
 * The limit is 0 unless PS3_WATCH_LOWMEM is set, so the hot-path test is one
 * load + compare of a cached static -- a no-op on the baseline. OFF by
 * default, like every other probe in this tree.
 *
 * Tudo aqui e' header-only e de ligacao fraca DE PROPOSITO. O CMake exclui
 * runtime/ppu/ da libps3recomp_runtime.a, e os testes unitarios linkam so' o
 * proprio .o com -lm -- nao puxam a lib nem o ppu_loader.o. Uma declaracao
 * extern partiria esses links no instante em que o header a mencionasse
 * (medido: test_ppu_fpu_lmw a falhar com "_ppu_lowmem_write_hit not found").
 * -----------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif

/* Limite da vigia de TRADUCAO -- ver o bloco junto de vm_translate. */
static inline uint32_t ppu_lowptr_watch_limit(void)
{
    static uint32_t lim = 0xFFFFFFFFu;          /* sentinela: ainda nao lido */
    if (__builtin_expect(lim == 0xFFFFFFFFu, 0)) {
        const char* e = getenv("PS3_WATCH_LOWPTR");
        lim = (e && e[0]) ? (uint32_t)strtoul(e, NULL, 0) : 0u;
    }
    return lim;
}

__attribute__((weak))
void ppu_lowptr_translate_hit(uint32_t addr, void* ra)
{
    static unsigned long long hits = 0;
    static int cap = -1;
    if (cap < 0) {
        const char* c = getenv("PS3_WATCH_LOWPTR_CAP");
        cap = (c && c[0]) ? atoi(c) : 200;
    }
    if (++hits > (unsigned long long)cap) return;

    /* A -O0 nada e' inlined, por isso o ra do nivel 0 aponta SEMPRE para dentro
     * de vm_ptr8 e nao identifica ninguem (defeito declarado no E17). Subimos
     * um nivel para apanhar o chamador real -- e imprimimos os dois, para se
     * ver quando o nivel 1 nao existe. */
    char who[128], who1[128];
#ifndef _WIN32
    Dl_info di;
    if (dladdr(ra, &di) && di.dli_sname)
        snprintf(who, sizeof(who), "%s+0x%lX", di.dli_sname,
                 (unsigned long)((const char*)ra - (const char*)di.dli_saddr));
    else
#endif
        snprintf(who, sizeof(who), "%p", ra);

    void* ra1 = __builtin_return_address(1);
    who1[0] = '\0';
#ifndef _WIN32
    Dl_info d1;
    if (ra1 && dladdr(ra1, &d1) && d1.dli_sname)
        snprintf(who1, sizeof(who1), "%s+0x%lX", d1.dli_sname,
                 (unsigned long)((const char*)ra1 - (const char*)d1.dli_saddr));
    else
#endif
        snprintf(who1, sizeof(who1), "%p", ra1);

    fprintf(stderr, "[LOWPTR] #%llu translate 0x%08X ra=%s via=%s\n",
            hits, addr, who1, who);
    fflush(stderr);
}

/* Endereco EXACTO a vigiar (PS3_WATCH_EA=0xADDR, default 0 = OFF). A vigia de
 * [0,N) so' serve para ponteiros nulos; quando ja' se sabe QUAL a celula que
 * muda -- medido no E20: *(0x00868D48 + 0xC), na tabela de tipos -- o que faz
 * falta e' vigiar essa e mais nenhuma. */
static inline uint32_t ppu_watch_ea(void)
{
    static uint32_t ea = 0xFFFFFFFFu;           /* sentinela: ainda nao lido */
    if (__builtin_expect(ea == 0xFFFFFFFFu, 0)) {
        const char* e = getenv("PS3_WATCH_EA");
        ea = (e && e[0]) ? (uint32_t)strtoul(e, NULL, 0) : 0u;
    }
    return ea;
}

/* Limite da vigia. Static local com inicializador constante -> sem guard
 * variable, logo o custo no hot path e' um load + compare previsto. */
static inline uint32_t ppu_lowmem_watch_limit(void)
{
    static uint32_t lim = 0xFFFFFFFFu;          /* sentinela: ainda nao lido */
    if (__builtin_expect(lim == 0xFFFFFFFFu, 0)) {
        const char* e = getenv("PS3_WATCH_LOWMEM");
        lim = (e && e[0]) ? (uint32_t)strtoul(e, NULL, 0) : 0u;
    }
    return lim;
}

__attribute__((weak))
void ppu_lowmem_write_hit(uint32_t addr, uint32_t n, uint64_t val, void* ra)
{
    static unsigned long long hits = 0;
    static int cap = -1;
    if (cap < 0) {
        const char* c = getenv("PS3_WATCH_LOWMEM_CAP");
        cap = (c && c[0]) ? atoi(c) : 200;
    }
    if (++hits > (unsigned long long)cap) return;

    /* Simbolizar o ra da' o func_<EA guest> envolvente -- e' quem escreve.
     * Um ponteiro host cru nao serve entre corridas (ASLR). Mesmo metodo do
     * reporter de UNCOMMITTED em ppu_loader.cpp. */
    char who[128];
#ifndef _WIN32
    Dl_info di;
    if (dladdr(ra, &di) && di.dli_sname)
        snprintf(who, sizeof(who), "%s+0x%lX", di.dli_sname,
                 (unsigned long)((const char*)ra - (const char*)di.dli_saddr));
    else
#endif
        snprintf(who, sizeof(who), "%p", ra);   /* Win32: sem dladdr */

    /* Os bytes interessam tanto como o valor: a hipotese e' um strcpy atraves
     * de nulo, e ai' o valor le'-se como texto. */
    char asc[9]; int k = 0;
    for (int b = (int)n - 1; b >= 0 && k < 8; --b) {
        unsigned ch = (unsigned)((val >> (8 * b)) & 0xFFu);
        asc[k++] = (ch >= 32 && ch < 127) ? (char)ch : '.';
    }
    asc[k] = '\0';

    fprintf(stderr, "[LOWMEM] #%llu write%u 0x%08X = 0x%0*llX \"%s\" ra=%s\n",
            hits, n * 8u, addr, (int)(n * 2), (unsigned long long)val, asc, who);
    fflush(stderr);
}

#ifdef __cplusplus
}
#endif

#define PPU_LOWMEM_WATCH(a, n, v)                                            \
    do {                                                                     \
        uint32_t _wea = ppu_watch_ea();                                      \
        if (__builtin_expect((a) < ppu_lowmem_watch_limit()                  \
                             || (_wea != 0u && (a) == _wea), 0))             \
            ppu_lowmem_write_hit((a), (n), (uint64_t)(v),                    \
                                 __builtin_return_address(0));               \
    } while (0)

/* ---------------------------------------------------------------------------
 * Stores -- write host-endian value into big-endian guest memory.
 * -----------------------------------------------------------------------*/
static inline void vm_write8(uint32_t addr, uint8_t val)
{
    PPU_LOWMEM_WATCH(addr, 1, val);
    if (!ppu_guest_range_committed(addr, 1)) return;
    *vm_ptr8(addr) = val;
}

static inline void vm_write16(uint32_t addr, uint16_t val)
{
    PPU_LOWMEM_WATCH(addr, 2, val);
    if (!ppu_guest_range_committed(addr, 2)) return;
    uint16_t raw = ps3_bswap16(val);
    memcpy(vm_ptr8(addr), &raw, sizeof(raw));
}

static inline void vm_write32(uint32_t addr, uint32_t val)
{
    PPU_LOWMEM_WATCH(addr, 4, val);
    if (!ppu_guest_range_committed(addr, 4)) return;
    uint32_t raw = ps3_bswap32(val);
    memcpy(vm_ptr8(addr), &raw, sizeof(raw));
}

static inline void vm_write64(uint32_t addr, uint64_t val)
{
    PPU_LOWMEM_WATCH(addr, 8, val);
    if (!ppu_guest_range_committed(addr, 8)) return;
    uint64_t raw = ps3_bswap64(val);
    memcpy(vm_ptr8(addr), &raw, sizeof(raw));
}

static inline void vm_write_f32(uint32_t addr, float val)
{
    uint32_t tmp;
    memcpy(&tmp, &val, sizeof(tmp));
    vm_write32(addr, tmp);
}

static inline void vm_write_f64(uint32_t addr, double val)
{
    uint64_t tmp;
    memcpy(&tmp, &val, sizeof(tmp));
    vm_write64(addr, tmp);
}

/* ---------------------------------------------------------------------------
 * Block copies (for string / bulk data movement)
 * -----------------------------------------------------------------------*/
static inline void vm_memcpy_from(void* host_dst, uint32_t guest_src, size_t len)
{
    memcpy(host_dst, vm_ptr8(guest_src), len);
}

static inline void vm_memcpy_to(uint32_t guest_dst, const void* host_src, size_t len)
{
    memcpy(vm_ptr8(guest_dst), host_src, len);
}

static inline void vm_memset(uint32_t guest_dst, int val, size_t len)
{
    memset(vm_ptr8(guest_dst), val, len);
}

/* ---------------------------------------------------------------------------
 * Atomic operations -- lwarx / stwcx emulation
 *
 * PowerPC reservation-based atomics:
 *   lwarx  rD, rA, rB   -- Load word and reserve (sets reservation)
 *   stwcx. rS, rA, rB   -- Store word conditional (clears reservation)
 *                           Sets CR0 EQ bit on success.
 *
 * We emulate this using C11 compare-and-swap on the host.
 * -----------------------------------------------------------------------*/

static inline uint32_t ppu_lwarx(ppu_context* ctx, uint32_t addr)
{
    /* Read the current value from memory (big-endian, so swap). */
    uint32_t raw;
    _Atomic(uint32_t)* atom = (_Atomic(uint32_t)*)vm_ptr32(addr);
    raw = atomic_load_explicit(atom, memory_order_acquire);

    ctx->reserve_addr  = addr;
    ctx->reserve_value = raw;
    ctx->reserve_valid = 1;

    return ps3_bswap32(raw);
}

static inline int ppu_stwcx(ppu_context* ctx, uint32_t addr, uint32_t val)
{
    if (!ctx->reserve_valid || ctx->reserve_addr != addr) {
        /* No reservation or address mismatch -- fail */
        ppu_cr_set(ctx, 0, PPU_CR_SO * ppu_xer_get_so(ctx));
        ctx->reserve_valid = 0;
        return 0;
    }

    uint32_t expected = (uint32_t)ctx->reserve_value;
    uint32_t desired  = ps3_bswap32(val);
    _Atomic(uint32_t)* atom = (_Atomic(uint32_t)*)vm_ptr32(addr);

    int ok = atomic_compare_exchange_strong_explicit(
        atom, &expected, desired,
        memory_order_acq_rel, memory_order_acquire);

    ctx->reserve_valid = 0;

    if (ok)
        ppu_cr_set(ctx, 0, PPU_CR_EQ | (PPU_CR_SO * ppu_xer_get_so(ctx)));
    else
        ppu_cr_set(ctx, 0, PPU_CR_SO * ppu_xer_get_so(ctx));

    return ok;
}

/* 64-bit variants: ldarx / stdcx. */
static inline uint64_t ppu_ldarx(ppu_context* ctx, uint32_t addr)
{
    _Atomic(uint64_t)* atom = (_Atomic(uint64_t)*)vm_ptr64(addr);
    uint64_t raw = atomic_load_explicit(atom, memory_order_acquire);

    ctx->reserve_addr  = addr;
    ctx->reserve_value = raw;
    ctx->reserve_valid = 1;

    return ps3_bswap64(raw);
}

static inline int ppu_stdcx(ppu_context* ctx, uint32_t addr, uint64_t val)
{
    if (!ctx->reserve_valid || ctx->reserve_addr != addr) {
        ppu_cr_set(ctx, 0, PPU_CR_SO * ppu_xer_get_so(ctx));
        ctx->reserve_valid = 0;
        return 0;
    }

    uint64_t expected = ctx->reserve_value;
    uint64_t desired  = ps3_bswap64(val);
    _Atomic(uint64_t)* atom = (_Atomic(uint64_t)*)vm_ptr64(addr);

    int ok = atomic_compare_exchange_strong_explicit(
        atom, &expected, desired,
        memory_order_acq_rel, memory_order_acquire);

    ctx->reserve_valid = 0;

    if (ok)
        ppu_cr_set(ctx, 0, PPU_CR_EQ | (PPU_CR_SO * ppu_xer_get_so(ctx)));
    else
        ppu_cr_set(ctx, 0, PPU_CR_SO * ppu_xer_get_so(ctx));

    return ok;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PPU_MEMORY_H */
