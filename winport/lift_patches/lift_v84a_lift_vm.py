#!/usr/bin/env python3
"""engine_fixes_v84a_lift_vm.py -- v84a: cheaper inline guest memory access in the lifted PPU code (compile time).

WHY (research/S4_ppu_main_thread.md): at 2x the save scene is main-thread compute bound (~760 ms/s in lifted code).
  Disassembly of the hottest lifted functions (v83b, e.g. func_00183008): every inlined guest store is ~22
  instructions -- 4 .refptr indirections (g_wp63_inline_w, g_ps3_vm_gameplay, g_wp61_ok16, vm_base), 3 flag tests, a
  3-load TLS chain for the 2M-store preempt counter -- and every guest load ~13. vm_read8 (lbz) is still an out-of-line
  call. Worse, the memcpy-typed guest store may alias the ppu_context, so ctx->gpr[1] and the stored register are
  reloaded from memory after every guest store.
WHAT (identical guest-visible behaviour; same slow paths, same conditions, same preempt cadence):
  ppu_recomp.h (lifted header):
   - g_wp84_rb / g_wp84_r8 / g_wp84_wb [256]: per 16 MB block, vm_base when the block may take the inline path, else
     NULL. The table entries are exactly the old conditions, precomputed: rb = WINPORT_VM_INLINE && gameplay gate &&
     committed block (v61), r8 = WINPORT_VM_FAST8 fast path && gate && committed (v55, plus the movie-EOS address
     check kept inline), wb = WINPORT_VM_INLINE_W(+RSV_CAS_ONLY) && gate && committed (v63).
   - guest loads/stores through packed one-field structs: type-based alias analysis then knows a guest access never
     touches the ppu_context (a host struct outside guest memory).
   - vm_read8 is inlined too (it was a call per lbz).
   - the per-thread 2M-store preempt counter moves from TLS into the context's tail padding (wp84_tick; sizeof
     unchanged = 1104, checked by static_assert) -- one counter per guest thread as before.
  ppu_loader.cpp: the tables + wp84_tables_rebuild(), called from ppu_vm_hot_gates_init, vm_ok16_rebuild (new
     committed block) and the gameplay gate (ppu_vm_fast_policy.c, 0 -> 1 once).
  Needs a lift rebuild (FORCE_REBUILD_LIFT=1). -DWP84_OLD_INLINE in the lift compile = the v61/v63 code again.
UNDO: restore *.pre_v84a (3 files) or build the lift with -DWP84_OLD_INLINE; old objects kept in win/obj/lift_obj_v83b."""
import pathlib, shutil, sys
from _lift_util import rep1, _rw, _wr

TAG = "winport v84a"

HDR_STRUCT_OLD = "    int      reserve_valid; /* lwarx/stwcx. reservation flag    */\n} ppu_context;\n"
HDR_STRUCT_NEW = ("    int      reserve_valid; /* lwarx/stwcx. reservation flag    */\n"
                  "    uint32_t wp84_tick;     /* winport v84a: lifted-store preempt counter, in the tail padding */\n"
                  "} ppu_context;\n"
                  "#ifdef __cplusplus   /* winport v84a: the runtime's ppu_context has no wp84_tick -- the size must not change */\n"
                  "static_assert(sizeof(ppu_context) == 1104 && __builtin_offsetof(ppu_context, wp84_tick) == 1100, \"winport v84a ppu_context layout\");\n"
                  "#endif\n")

V61_DEFS_OLD = "#define vm_read16(a) wp61_r16(a)\n#define vm_read32(a) wp61_r32(a)\n#define vm_read64(a) wp61_r64(a)\n#endif /* WP61_NO_INLINE */\n"
V61_DEFS_NEW = ("#ifdef WP84_OLD_INLINE   /* winport v84a: v61 macros only in the old-inline build */\n"
                "#define vm_read16(a) wp61_r16(a)\n#define vm_read32(a) wp61_r32(a)\n#define vm_read64(a) wp61_r64(a)\n#endif\n"
                "#endif /* WP61_NO_INLINE */\n")
V63_GUARD_OLD = "#if defined(WP_LIFT_TU) && !defined(WP61_NO_INLINE)\n"
V63_GUARD_NEW = "#if defined(WP_LIFT_TU) && !defined(WP61_NO_INLINE) && defined(WP84_OLD_INLINE)   /* winport v84a: v63 only in the old-inline build */\n"
V63_END = "#define vm_write64(a, v) wp63_w64((a), (v))\n#endif /* WP61_NO_INLINE */\n"

HDR_V84 = r'''
/* winport v84a: cheaper inline guest memory access in the lifted code (engine_fixes_v84a_lift_vm.py).
 * One table lookup per access instead of 3-4 global loads: g_wp84_rb/r8/wb[addr >> 24] = vm_base when the 16 MB block
 * may take the inline path (exactly the v55/v61/v63 conditions: switch on && gameplay gate && committed block), else
 * NULL -> the real vm_readNN/vm_writeNN call (unchanged slow path). ppu_loader.cpp rebuilds the tables when an input
 * changes. Guest accesses go through packed one-field structs so type-based alias analysis knows they never touch the
 * ppu_context (host struct) -> ctx->gpr[] can stay in host registers across guest stores. The 2M-store preempt counter
 * is ctx->wp84_tick (per guest thread, like v63's TLS counter). -DWP84_OLD_INLINE compiles the v61/v63 code instead. */
#if !defined(WP61_NO_INLINE) && !defined(WP84_OLD_INLINE)
#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t* g_wp84_rb[256];
extern uint8_t* g_wp84_r8[256];
extern uint32_t g_movie_eos_ea;
#ifdef __cplusplus
}
#endif
typedef struct __attribute__((packed)) { uint8_t  v; } wp84_m8;
typedef struct __attribute__((packed)) { uint16_t v; } wp84_m16;
typedef struct __attribute__((packed)) { uint32_t v; } wp84_m32;
typedef struct __attribute__((packed)) { uint64_t v; } wp84_m64;
static inline uint8_t wp84_r8(uint64_t a)
{ uint32_t x = (uint32_t)a; uint8_t* b = g_wp84_r8[x >> 24];
  if (__builtin_expect(b != 0 && x != g_movie_eos_ea, 1)) return ((const wp84_m8*)(b + x))->v; return (vm_read8)(a); }
static inline uint16_t wp84_r16(uint64_t a)
{ uint32_t x = (uint32_t)a; uint8_t* b = g_wp84_rb[x >> 24];
  if (__builtin_expect(b != 0, 1)) return __builtin_bswap16(((const wp84_m16*)(b + x))->v); return (vm_read16)(a); }
static inline uint32_t wp84_r32(uint64_t a)
{ uint32_t x = (uint32_t)a; uint8_t* b = g_wp84_rb[x >> 24];
  if (__builtin_expect(b != 0, 1)) return __builtin_bswap32(((const wp84_m32*)(b + x))->v); return (vm_read32)(a); }
static inline uint64_t wp84_r64(uint64_t a)
{ uint32_t x = (uint32_t)a; uint8_t* b = g_wp84_rb[x >> 24];
  if (__builtin_expect(b != 0, 1)) return __builtin_bswap64(((const wp84_m64*)(b + x))->v); return (vm_read64)(a); }
#define vm_read8(a)  wp84_r8(a)
#define vm_read16(a) wp84_r16(a)
#define vm_read32(a) wp84_r32(a)
#define vm_read64(a) wp84_r64(a)
#endif /* v84a reads */
#if defined(WP_LIFT_TU) && !defined(WP61_NO_INLINE) && !defined(WP84_OLD_INLINE)
#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t* g_wp84_wb[256];
void wp63_tick_cold(void);
#ifdef __cplusplus
}
#endif
/* statement macros (the lift only uses vm_writeNN as statements); `ctx` is the lifted function's context. Helpers
 * without a context (a few host helpers in the chunks) count in a file-scope dummy context instead. */
static ppu_context wp84_nctx;
static ppu_context* const ctx = &wp84_nctx;
#define WP84_W(T, MT, BS, wa, wv, slow) do { const uint64_t wp84_a = (wa); const T wp84_v = (T)(wv); \
    const uint32_t wp84_x = (uint32_t)wp84_a; uint8_t* wp84_b = g_wp84_wb[wp84_x >> 24]; \
    if (__builtin_expect(wp84_b != 0, 1)) { ((MT*)(wp84_b + wp84_x))->v = BS(wp84_v); \
        if (__builtin_expect(((++ctx->wp84_tick) & 0x1FFFFFu) == 0, 0)) wp63_tick_cold(); } \
    else (slow)(wp84_a, wp84_v); } while (0)
#define WP84_BS8(x8) (x8)
#define vm_write8(wa, wv)  WP84_W(uint8_t,  wp84_m8,  WP84_BS8,          wa, wv, vm_write8)
#define vm_write16(wa, wv) WP84_W(uint16_t, wp84_m16, __builtin_bswap16, wa, wv, vm_write16)
#define vm_write32(wa, wv) WP84_W(uint32_t, wp84_m32, __builtin_bswap32, wa, wv, vm_write32)
#define vm_write64(wa, wv) WP84_W(uint64_t, wp84_m64, __builtin_bswap64, wa, wv, vm_write64)
#endif /* v84a stores */
'''

LOADER_TABLES = r'''
/* winport v84a: per-16 MB-block base tables for the lifted code's inline guest access (engine_fixes_v84a_lift_vm.py).
 * Entry = vm_base when the old inline condition holds for the block, else NULL. Rebuilt on every input change. */
extern "C" { uint8_t* g_wp84_rb[256]; uint8_t* g_wp84_r8[256]; uint8_t* g_wp84_wb[256]; }
extern "C" void wp84_tables_rebuild(void);
'''

LOADER_REBUILD = r'''
extern "C" void wp84_tables_rebuild(void)   /* winport v84a */
{
    const int gp = __atomic_load_n(&g_ps3_vm_gameplay, __ATOMIC_RELAXED) != 0;
    for (int b = 0; b < 256; b++) {
        const int ok = gp && vm_base && __atomic_load_n(&g_vm_ok16[b], __ATOMIC_ACQUIRE);
        __atomic_store_n(&g_wp84_rb[b], (ok && g_wp61_inline) ? vm_base : (uint8_t*)0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_wp84_r8[b], (ok && g_wp_fast8) ? vm_base : (uint8_t*)0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_wp84_wb[b], (ok && g_wp63_inline_w) ? vm_base : (uint8_t*)0, __ATOMIC_RELEASE);
    }
}
'''


def apply_all(engine):
    engine = pathlib.Path(engine)
    # 1. runtime: tables + rebuild hooks
    p = engine / "runtime/ppu/ppu_loader.cpp"
    s = _rw(p)
    if TAG in s:
        print("v84a: ppu_loader.cpp already patched")
    else:
        bak = p.with_name(p.name + ".pre_v84a")
        if not bak.exists(): shutil.copy2(p, bak)
        s = rep1(s, "extern \"C\" int g_wp63_inline_w;   /* winport v63-decl */",
                 "extern \"C\" int g_wp63_inline_w;   /* winport v63-decl */" + LOADER_TABLES, "tables")
        s = rep1(s, "        if (next[b]) __atomic_store_n(&g_vm_ok16[b], (unsigned char)1, __ATOMIC_RELEASE);\n}\n",
                 "        if (next[b]) __atomic_store_n(&g_vm_ok16[b], (unsigned char)1, __ATOMIC_RELEASE);\n"
                 "    wp84_tables_rebuild();   /* winport v84a */\n}\n", "ok16 rebuild")
        s = rep1(s, "    if (g_wp_fast8) { fprintf(stderr, \"[VMFAST55] 8/16-bit + cold-path fast accessors after the gameplay gate\\n\"); fflush(stderr); }\n}\n",
                 "    if (g_wp_fast8) { fprintf(stderr, \"[VMFAST55] 8/16-bit + cold-path fast accessors after the gameplay gate\\n\"); fflush(stderr); }\n"
                 "    wp84_tables_rebuild();   /* winport v84a */\n"
                 "    fprintf(stderr, \"[VMTAB84] lifted inline access via block tables (reads %d, bytes %d, stores %d)\\n\", g_wp61_inline, g_wp_fast8, g_wp63_inline_w); fflush(stderr);\n"
                 "}\n" + LOADER_REBUILD, "gates init")
        _wr(p, s)
        print("v84a: patched runtime/ppu/ppu_loader.cpp")
    p = engine / "runtime/ppu/ppu_vm_fast_policy.c"
    s = _rw(p)
    if TAG in s:
        print("v84a: ppu_vm_fast_policy.c already patched")
    else:
        bak = p.with_name(p.name + ".pre_v84a")
        if not bak.exists(): shutil.copy2(p, bak)
        s = rep1(s, "    g_ps3_vm_gameplay = 1;\n",
                 "    g_ps3_vm_gameplay = 1;\n    { extern void wp84_tables_rebuild(void); wp84_tables_rebuild(); }   /* winport v84a */\n", "gate")
        _wr(p, s)
        print("v84a: patched runtime/ppu/ppu_vm_fast_policy.c")
    # 2. lifted header
    h = engine.parent / "gow2-recomp" / "recomp_macos_e435" / "ppu_recomp.h"
    t = h.read_text(encoding="utf-8", errors="surrogateescape")
    if TAG in t:
        print("v84a: ppu_recomp.h already patched")
        return
    bak = h.with_name(h.name + ".pre_v84a")
    if not bak.exists(): shutil.copy2(h, bak)
    t = rep1(t, HDR_STRUCT_OLD, HDR_STRUCT_NEW, "struct")
    t = rep1(t, V61_DEFS_OLD, V61_DEFS_NEW, "v61 defs")
    t = rep1(t, V63_GUARD_OLD, V63_GUARD_NEW, "v63 guard")
    t = rep1(t, V63_END, V63_END + HDR_V84, "v84 block")
    h.write_text(t, encoding="utf-8", errors="surrogateescape", newline="\n")
    print("v84a: patched ppu_recomp.h (rebuild the lift: FORCE_REBUILD_LIFT=1)")


if __name__ == "__main__":
    apply_all(sys.argv[1] if len(sys.argv) > 1 else "ps3recomp")
