/* trace_link_stubs.h -- shared link-time stubs for harnesses that pull in
 * runtime/trace/ps3_checkpoint.c (directly, or transitively via a production
 * .c that calls ps3_checkpoint_fire()).
 *
 * Correcao pos-review (bringup-instrumentacao-fase0, Task 4 Minor #3):
 * ps3_checkpoint.c declares ps3_timebase_now() and the three SNAPSHOT
 * accessors (ps3_ppu_thread_alive_count/ps3_sys_memory_used/
 * ps3_gcm_flip_count) `extern` and calls all four unconditionally inside
 * ps3_checkpoint_fire() -- so ANY harness that links ps3_checkpoint.c needs
 * definitions for them, regardless of whether the relevant PS3_TRACE_xxx /
 * PS3_CP_xxx gate is on at runtime. In a real boot these live in
 * runtime/ppu/ppu_loader.cpp (generated per-game, not linkable standalone)
 * or in sys_ppu_thread.c/sys_memory.c/cellGcmSys.c; offline harnesses stub
 * them instead of dragging those in.
 *
 * Before this file existed, the same four one-line definitions + this same
 * ~7-line comment were pasted into eight harnesses (Tasks 1-4):
 * runtime/spu/tests/test_workload_main.c, test_seh_isolation_main.c,
 * test_spurs_e2e_main.c, libs/spurs/tests/test_lfq_inflate_main.c,
 * libs/video/tests/rsx_backend_smoke.c, test_movie_io.c,
 * test_fios_aread_hle.c, runtime/ppu/tests/test_fs_movie_io.cpp -- a ninth
 * instrumented module would have made it nine. Extracted here so adding one
 * is a one-line `#include`.
 *
 * Include this ONCE per test binary/link unit -- it DEFINES these symbols
 * (not just declares them), so including it from two translation units that
 * end up linked into the same executable is a duplicate-symbol error, same
 * as pasting the block twice would have been.
 *
 * Works from both C and C++ (the latter needs the `extern "C"` linkage so
 * ps3_checkpoint.c's C-mangled `extern` declarations resolve against these).
 */
#ifndef PS3RECOMP_TRACE_LINK_STUBS_H
#define PS3RECOMP_TRACE_LINK_STUBS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t ps3_timebase_now(void)           { return 1; }
uint32_t ps3_ppu_thread_alive_count(void) { return 0; }
uint32_t ps3_sys_memory_used(void)        { return 0; }
uint32_t ps3_gcm_flip_count(void)         { return 0; }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PS3RECOMP_TRACE_LINK_STUBS_H */
