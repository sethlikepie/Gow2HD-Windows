#!/usr/bin/env python3
"""engine_fixes_v63_hlecache_wstore.py -- v63: (a) cached HLE NID dispatch (WINPORT_HLE_CACHE=1),
(b) inline guest STORES in the lifted code (WINPORT_VM_INLINE_W=1, only together with WINPORT_RSV_CAS_ONLY=1).

WHY (research/P1_perf_v55.md, run P2scn [TPROF] main thread in the save scene, v62 all switches):
  ps3_hle_call_impl 8.9%: every HLE import call scans g_ctx[] (up to 256) linearly and then the generated NID table
  (ps3_nid_table_find, linear) -> per call hundreds of compares.
  ppu_rsv_on_store 9.2% + vm_write32 3.5% + vm_write64 3.3%: every guest store is 2 out-of-line calls even on the
  post-gameplay fast path (bswap store + store-count preempt tick; with RSV_CAS_ONLY nothing else).
WHAT:
 (a) ppu_hle.cpp: 1024-slot direct-mapped cache nid -> (g_ctx index | NID-table entry), validated by a generation
     (g_ctx_count, g_hle_inited) so late registrations invalidate it. Same handler, same trace hooks, same unresolved
     path. HLE calls run under the giant lock (no new races). Default 0 = old linear scans.
 (b) ppu_recomp.h (lifted TUs only, like v61 reads): vm_write8/16/32/64 -> inline wp63_wNN: if g_wp63_inline_w &&
     gameplay gate && committed 16 MB block: big-endian store + a per-thread store counter that calls the kit's
     preempt point every 2M stores (same cadence as ppu_rsv_on_store without PS3_GIANT_FAIR); else the real call.
     g_wp63_inline_w = WINPORT_VM_INLINE_W=1 && WINPORT_RSV_CAS_ONLY=1 && kit write fast path allowed && !PS3_GIANT_FAIR.
     Skipped vs the out-of-line fast path: nothing functional (it is exactly store + rsv_on_store, and rsv_on_store
     with CAS_ONLY is just the tick); the v32 GCMDIAG wait-point write watch no longer sees lifted stores (diag only).
     Needs a lift rebuild (FORCE_REBUILD_LIFT=1).
UNDO: unset the switches (or restore *.pre_v63 and rebuild the lift)."""
import pathlib, shutil, sys
from _lift_util import rep1, _rw, _wr

TAG = "winport v63"

HDR = r'''
/* winport v63: inline post-gameplay fast path for guest STORES (engine_fixes_v63_hlecache_wstore.py).
 * Lifted TUs only (-DWP_LIFT_TU from build_windows.sh): runtime files re-declare vm_writeNN themselves. */
#if defined(WP_LIFT_TU) && !defined(WP61_NO_INLINE)
#ifdef __cplusplus
extern "C" {
#endif
extern int g_wp63_inline_w;
extern __thread unsigned long g_wp63_tick;
void wp63_tick_cold(void);
#ifdef __cplusplus
}
#endif
static inline int wp63_ok(uint32_t x)
{ return __builtin_expect(g_wp63_inline_w && g_ps3_vm_gameplay && g_wp61_ok16[x >> 24], 1); }
static inline void wp63_tick(void)
{ if (__builtin_expect(((++g_wp63_tick) & 0x1FFFFFul) == 0, 0)) wp63_tick_cold(); }
static inline void wp63_w8(uint64_t a, uint8_t v)
{ uint32_t x = (uint32_t)a; if (wp63_ok(x)) { vm_base[x] = v; wp63_tick(); return; } (vm_write8)(a, v); }
static inline void wp63_w16(uint64_t a, uint16_t v)
{ uint32_t x = (uint32_t)a; if (wp63_ok(x)) { uint16_t r = __builtin_bswap16(v); __builtin_memcpy(vm_base + x, &r, 2); wp63_tick(); return; } (vm_write16)(a, v); }
static inline void wp63_w32(uint64_t a, uint32_t v)
{ uint32_t x = (uint32_t)a; if (wp63_ok(x)) { uint32_t r = __builtin_bswap32(v); __builtin_memcpy(vm_base + x, &r, 4); wp63_tick(); return; } (vm_write32)(a, v); }
static inline void wp63_w64(uint64_t a, uint64_t v)
{ uint32_t x = (uint32_t)a; if (wp63_ok(x)) { uint64_t r = __builtin_bswap64(v); __builtin_memcpy(vm_base + x, &r, 8); wp63_tick(); return; } (vm_write64)(a, v); }
#define vm_write8(a, v)  wp63_w8((a), (v))
#define vm_write16(a, v) wp63_w16((a), (v))
#define vm_write32(a, v) wp63_w32((a), (v))
#define vm_write64(a, v) wp63_w64((a), (v))
#endif /* WP61_NO_INLINE */
'''

LOADER_GLOBALS = r'''extern "C" { int g_wp63_inline_w = 0; __thread unsigned long g_wp63_tick = 0; }   /* winport v63 */
static void ppu_rsv_preempt_cold(void);
extern "C" void wp63_tick_cold(void) { ppu_rsv_preempt_cold(); }   /* winport v63: lifted inline stores' 2M-store preempt point */
'''

HLE_OLD = r'''    for (uint32_t i = 0; i < g_ctx_count; i++)
        if (g_ctx[i].nid == nid) {
            if (hle_trace_mode()) hle_trace_log(g_ctx[i].name, ctx);
            g_ctx[i].fn(ctx); return;
        }

    ps3_nid_entry* e = g_hle_inited ? ps3_nid_table_find(&g_hle_nids, nid) : nullptr;
'''
HLE_NEW = r'''    ps3_nid_entry* e = nullptr;
    static int wp63_hc = -1;   /* winport v63: WINPORT_HLE_CACHE=1 -> cached nid lookup (engine_fixes_v63_hlecache_wstore.py) */
    if (wp63_hc < 0) { const char* ev = getenv("WINPORT_HLE_CACHE"); wp63_hc = (ev && ev[0] == '1') ? 1 : 0;
                       if (wp63_hc) { fprintf(stderr, "[HLECACHE63] cached HLE NID dispatch\n"); fflush(stderr); } }
    if (wp63_hc) {
        struct Wp63Slot { uint32_t nid, gen; int idx; ps3_nid_entry* e; };
        static Wp63Slot s_wp63[1024];
        const uint32_t gen = (g_ctx_count << 1) | (g_hle_inited ? 1u : 0u);
        Wp63Slot* c = &s_wp63[(nid * 2654435761u) >> 22];
        if (c->nid == nid && c->gen == gen && (c->idx >= 0 || c->e)) {
            if (c->idx >= 0) { if (hle_trace_mode()) hle_trace_log(g_ctx[c->idx].name, ctx); g_ctx[c->idx].fn(ctx); return; }
            e = c->e;
        } else {
            for (uint32_t i = 0; i < g_ctx_count; i++)
                if (g_ctx[i].nid == nid) {
                    c->nid = nid; c->gen = gen; c->idx = (int)i; c->e = nullptr;
                    if (hle_trace_mode()) hle_trace_log(g_ctx[i].name, ctx);
                    g_ctx[i].fn(ctx); return;
                }
            e = g_hle_inited ? ps3_nid_table_find(&g_hle_nids, nid) : nullptr;
            if (e && e->handler) { c->nid = nid; c->gen = gen; c->idx = -1; c->e = e; }
        }
    } else {
        for (uint32_t i = 0; i < g_ctx_count; i++)
            if (g_ctx[i].nid == nid) {
                if (hle_trace_mode()) hle_trace_log(g_ctx[i].name, ctx);
                g_ctx[i].fn(ctx); return;
            }
        e = g_hle_inited ? ps3_nid_table_find(&g_hle_nids, nid) : nullptr;
    }
'''


def apply_all(engine):
    engine = pathlib.Path(engine)
    # (a) HLE cache
    p = engine / "runtime/ppu/ppu_hle.cpp"
    s = _rw(p)
    if TAG + ": WINPORT_HLE_CACHE" in s:
        print("v63: ppu_hle.cpp already patched")
    else:
        bak = p.with_name(p.name + ".pre_v63")
        if not bak.exists(): shutil.copy2(p, bak)
        s = rep1(s, HLE_OLD, HLE_NEW, "hle dispatch")
        _wr(p, s)
        print("v63: patched runtime/ppu/ppu_hle.cpp")
    # (b) loader exports + flag
    p = engine / "runtime/ppu/ppu_loader.cpp"
    s = _rw(p)
    if "g_wp63_inline_w" in s:
        print("v63: ppu_loader.cpp already patched")
    else:
        bak = p.with_name(p.name + ".pre_v63")
        if not bak.exists(): shutil.copy2(p, bak)
        s = rep1(s, "static __thread unsigned long g_store_preempt_tick = 0;\n",
                 "static __thread unsigned long g_store_preempt_tick = 0;\n" + LOADER_GLOBALS, "globals")
        s = rep1(s, "    e = getenv(\"WINPORT_VM_INLINE\");",
                 "    { const char* w = getenv(\"WINPORT_VM_INLINE_W\"); const char* c = getenv(\"WINPORT_RSV_CAS_ONLY\");   /* winport v63 */\n"
                 "      g_wp63_inline_w = (w && w[0] == '1' && c && c[0] == '1' && g_vm_w_fast && !g_giant_fair) ? 1 : 0;\n"
                 "      if (w && w[0] == '1') { fprintf(stderr, \"[VMINLINEW63] lifted inline stores %s\\n\", g_wp63_inline_w ? \"ON\" : \"OFF (needs WINPORT_RSV_CAS_ONLY=1, no PS3_GIANT_FAIR)\"); fflush(stderr); } }\n"
                 "    e = getenv(\"WINPORT_VM_INLINE\");", "init")
        _wr(p, s)
        print("v63: patched runtime/ppu/ppu_loader.cpp")
    s = _rw(p)
    if "winport v63-decl" not in s:   # the flag is defined further down than ppu_vm_hot_gates_init uses it
        s = rep1(s, "extern \"C\" { unsigned char* g_wp61_ok16 = g_vm_ok16; int g_wp61_inline = 0; }",
                 "extern \"C\" { unsigned char* g_wp61_ok16 = g_vm_ok16; int g_wp61_inline = 0; }\n"
                 "extern \"C\" int g_wp63_inline_w;   /* winport v63-decl */", "decl")
        _wr(p, s)
        print("v63: forward declaration added to ppu_loader.cpp")
    # (b) lifted header
    h = engine.parent / "gow2-recomp" / "recomp_macos_e435" / "ppu_recomp.h"
    t = h.read_text(encoding="utf-8", errors="surrogateescape")
    if "wp63_w32" in t:
        print("v63: ppu_recomp.h already patched")
        return
    anchor = "#define vm_read64(a) wp61_r64(a)\n#endif /* WP61_NO_INLINE */\n"
    if t.count(anchor) != 1:
        sys.exit(f"v63: ppu_recomp.h anchor (v61 block) found {t.count(anchor)}x (want 1) -- apply v61 first")
    bak = h.with_name(h.name + ".pre_v63")
    if not bak.exists(): shutil.copy2(h, bak)
    t = t.replace(anchor, anchor + HDR, 1)
    h.write_text(t, encoding="utf-8", errors="surrogateescape", newline="")
    print(f"v63: patched {h} -- rebuild the lift with FORCE_REBUILD_LIFT=1")


if __name__ == "__main__":
    apply_all(sys.argv[1])
