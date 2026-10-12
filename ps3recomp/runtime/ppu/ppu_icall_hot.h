#ifndef PPU_ICALL_HOT_H
#define PPU_ICALL_HOT_H

/*
 * One-entry CTR cache for a single lift TU (ppu_recomp_000.cpp / func_001569E4
 * vtable bctrl). A global ppu_lookup cache already lost fps (21→19); this is
 * the same compare, but only at the 00248F3C→001569E4 icall sites.
 *
 * Hit: call the last looked-up fn + drain trampolines (same as the success
 * path in ps3_indirect_call). Miss: full ps3_indirect_call, then remember
 * ppu_lookup_fn(addr) (NULL on HLE/ASCII miss → next time still misses).
 */
#include <stdint.h>

struct ps3_icall_hot_state {
    uint32_t last_addr;
    void (*last_fn)(void *);
};

static inline int ps3_icall_hot_hit(const struct ps3_icall_hot_state *st, uint32_t addr)
{
    return st->last_fn != 0 && st->last_addr == addr;
}

static inline void ps3_icall_hot_remember(struct ps3_icall_hot_state *st,
                                          uint32_t addr, void (*fn)(void *))
{
    st->last_addr = addr;
    st->last_fn = fn;
}

#if defined(__cplusplus) && !defined(PPU_ICALL_HOT_NO_WRAP)

/* Included from a lift TU after ppu_context, ps3_indirect_call, g_trampoline_fn. */
extern "C" void (*ppu_lookup_fn(uint32_t addr))(ppu_context*);

static thread_local ps3_icall_hot_state g_ps3_icall_hot;

static inline void ps3_indirect_call_cached(ppu_context* ctx)
{
    uint32_t a = (uint32_t)ctx->ctr;
    if (ps3_icall_hot_hit(&g_ps3_icall_hot, a)) {
        ((void (*)(ppu_context*))g_ps3_icall_hot.last_fn)(ctx);
        while (g_trampoline_fn) {
            void (*tf)(void*) = g_trampoline_fn;
            g_trampoline_fn = 0;
            tf(ctx);
        }
        return;
    }
    ps3_indirect_call(ctx);
    ps3_icall_hot_remember(&g_ps3_icall_hot, a,
                           (void (*)(void*))ppu_lookup_fn(a));
}

#define ps3_indirect_call(c) ps3_indirect_call_cached(c)

#endif /* C++ wrap */

#endif
