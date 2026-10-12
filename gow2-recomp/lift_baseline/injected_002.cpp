extern "C" void f2b_stream_ensure(uint32_t type_sys);
extern "C" void f2b_stream_eof_try_complete(uint32_t type_sys);
/* FIOS-STOP-YIELD decls */
extern "C" void ppu_giant_lock_release(void);
extern "C" void ppu_giant_lock_acquire(void);
#include <unistd.h>
/* FLIPPATH-PROBE extern (defs in ppu_recomp_000.cpp) */
void ps3_flipp_on_enter(int id, ppu_context* ctx);
/* PRESENTLOOP-PROBE extern (defs in ppu_recomp_000.cpp) */
void ps3_plp_on_enter(int id, ppu_context* ctx);
/* MENUPRESENT-PROBE extern (defs in ppu_recomp_000.cpp) */
void ps3_mp_on_enter(int id, ppu_context* ctx);
/* SCHEDARM-PROBE extern (defs in ppu_recomp_000.cpp) */
void ps3_sa_on_enter(int id, ppu_context* ctx);
#include <stdlib.h>

/* BCTR-TAIL: despacho de `bctr` (salto, SEM link). Ver
 * recomp_mid_v2/patch_bctr_tail.py e ps3_indirect_tail em
 * ps3recomp/runtime/ppu/ppu_loader.cpp. */
extern "C" void ps3_indirect_tail(ppu_context* ctx);
