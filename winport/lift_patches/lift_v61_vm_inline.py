#!/usr/bin/env python3
"""engine_fixes_v61_vm_inline.py -- v61: inline guest-memory READS in the lifted code (WINPORT_VM_INLINE=1).

WHY (research/P1_perf_v55.md, run C3prof [TPROF] main thread with v59 async): vm_read32 11% + vm_read64 3.8% +
vm_read16 1.3% of the main PPU thread are out-of-line CALLS (the lifted chunks have 66k vm_read32 / 22k vm_read64 /
3k vm_read16 call sites in ppu_recomp_003.cpp alone) whose post-gameplay fast path is only gate + 16 MB block table +
load + bswap. (The Mac author measured no gain from inlining on Apple Silicon; x86 call overhead differs -> A/B.)
WHAT: ppu_loader.cpp exports the block table (g_wp61_ok16 -> g_vm_ok16) and a flag g_wp61_inline (WINPORT_VM_INLINE=1
and the kit's read fast path allowed, i.e. g_vm_r_fast); the lifted header ppu_recomp.h gets inline wp61_r16/32/64
doing EXACTLY the out-of-line fast path (flag && g_ps3_vm_gameplay && block committed -> load + bswap), else the real
vm_readNN call (boot, diagnostics, uncommitted addresses: unchanged). vm_read8 and all writes stay out of line
(movie-EOS byte hook; stores keep the reservation/preempt tick). The lifted chunks must be REBUILT
(FORCE_REBUILD_LIFT=1: the build cache only looks at .cpp timestamps). Default 0 = same behaviour as before (one extra
predictable branch per read).
UNDO: unset WINPORT_VM_INLINE (or restore ppu_recomp.h.pre_v61 and rebuild the lift)."""
import pathlib, shutil, sys
from _lift_util import rep1, _rw, _wr

TAG = "winport v61-vminline"

HDR = r'''
/* winport v61-vminline: inline post-gameplay fast path for guest reads (engine_fixes_v61_vm_inline.py).
 * ppu_loader.cpp defines the real accessors and sets WP61_NO_INLINE before including this header. */
#ifndef WP61_NO_INLINE
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned char* g_wp61_ok16;
extern int g_wp61_inline;
extern volatile int g_ps3_vm_gameplay;
extern uint8_t* vm_base;
#ifdef __cplusplus
}
#endif
static inline int wp61_ok(uint32_t x)
{ return __builtin_expect(g_wp61_inline && g_ps3_vm_gameplay && g_wp61_ok16[x >> 24], 1); }
static inline uint16_t wp61_r16(uint64_t a)
{ uint32_t x = (uint32_t)a; if (wp61_ok(x)) { uint16_t v; __builtin_memcpy(&v, vm_base + x, 2); return __builtin_bswap16(v); } return (vm_read16)(a); }
static inline uint32_t wp61_r32(uint64_t a)
{ uint32_t x = (uint32_t)a; if (wp61_ok(x)) { uint32_t v; __builtin_memcpy(&v, vm_base + x, 4); return __builtin_bswap32(v); } return (vm_read32)(a); }
static inline uint64_t wp61_r64(uint64_t a)
{ uint32_t x = (uint32_t)a; if (wp61_ok(x)) { uint64_t v; __builtin_memcpy(&v, vm_base + x, 8); return __builtin_bswap64(v); } return (vm_read64)(a); }
#define vm_read16(a) wp61_r16(a)
#define vm_read32(a) wp61_r32(a)
#define vm_read64(a) wp61_r64(a)
#endif /* WP61_NO_INLINE */
'''


def apply_all(engine):
    engine = pathlib.Path(engine)
    p = engine / "runtime/ppu/ppu_loader.cpp"
    s = _rw(p)
    if TAG in s:
        print("v61-vminline: ppu_loader.cpp already patched")
    else:
        s = rep1(s, "static unsigned char g_vm_ok16[256];\n",
                 "static unsigned char g_vm_ok16[256];\n"
                 "extern \"C\" { unsigned char* g_wp61_ok16 = g_vm_ok16; int g_wp61_inline = 0; }   /* " + TAG + " */\n", "export")
        s = rep1(s, "    e = getenv(\"WINPORT_VM_FAST8\");",
                 "    e = getenv(\"WINPORT_VM_INLINE\"); g_wp61_inline = (e && e[0] == '1' && g_vm_r_fast) ? 1 : 0;   /* " + TAG + " */\n"
                 "    if (g_wp61_inline) { fprintf(stderr, \"[VMINLINE61] lifted code reads guest memory inline after the gameplay gate\\n\"); fflush(stderr); }\n"
                 "    e = getenv(\"WINPORT_VM_FAST8\");", "init")
        _wr(p, s)
        print("v61-vminline: patched runtime/ppu/ppu_loader.cpp")
    s = _rw(p)
    if "v61-vminline-guard" not in s:   # the loader defines vm_readNN itself: no inline macros there
        s = rep1(s, '#include "ppu_recomp.h"     /* ppu_context, func decls, ppu_recomp_register */\n',
                 '#define WP61_NO_INLINE 1   /* winport v61-vminline-guard */\n'
                 '#include "ppu_recomp.h"     /* ppu_context, func decls, ppu_recomp_register */\n', "guard")
        _wr(p, s)
        print("v61-vminline: guard added to ppu_loader.cpp")
    h = engine.parent / "gow2-recomp" / "recomp_macos_e435" / "ppu_recomp.h"
    t = h.read_text(encoding="utf-8", errors="surrogateescape")
    if TAG in t:
        print("v61-vminline: ppu_recomp.h already patched")
        return
    anchor = "void     vm_write64(uint64_t addr, uint64_t val);\n#ifdef __cplusplus\n}\n#endif\n"
    if t.count(anchor) != 1:
        sys.exit(f"v61-vminline: ppu_recomp.h anchor found {t.count(anchor)}x (want 1)")
    bak = h.with_name(h.name + ".pre_v61")
    if not bak.exists():
        shutil.copy2(h, bak)
    t = t.replace(anchor, anchor + HDR, 1)
    h.write_text(t, encoding="utf-8", errors="surrogateescape", newline="")
    print(f"v61-vminline: patched {h} -- rebuild the lift with FORCE_REBUILD_LIFT=1")


if __name__ == "__main__":
    apply_all(sys.argv[1])
