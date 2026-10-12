/*
 * ps3recomp - MEDIA/EOS trace hook (bringup-instrumentacao-fase0, Task 5
 * review fix, Important #1).
 *
 * Pos-review: runtime/ppu/ppu_loader.cpp's vm_read8() EOS branch used to emit
 * "src=natural" as a hardcoded literal. That is a claim this hook cannot back
 * up: vm_read8() only sees the guest read the movie-player "done" byte
 * ([obj+0x744], via g_movie_eos_ea) flip to 1 -- it has NO visibility into WHY
 * that address got armed. The arm decision (a genuine end-of-overlay vs. a
 * forced/timeout arm) is made entirely by whoever sets g_movie_eos_ea, and in
 * the real macOS boot that is the SIBLING repo (../gow2-recomp/
 * movie_eos_arm.c, its own sampler thread) -- a repo this engine cannot see
 * and does not control. This repo's only armer (runtime/ppu/tests/
 * boot_main.cpp) is a Win32-only test harness, not the production arm path.
 *
 * The honest fix is to not assert natural/forced from this site at all:
 * report src=unknown unconditionally.
 *
 * IMPORTANTE para a Fase 6 -- NAO ha um unico sinal [PS3T] que distinga
 * end-of-stream real de forcado. Ha DOIS gates de forca independentes:
 *   - PS3_VDEC_FORCE_SEQDONE_MS: passa pelo vdec_callback (libs/codec/cellVdec.c)
 *     e SAI etiquetado em MEDIA/SEQDONE src=forced (test_cellvdec_force_gate.c).
 *   - PS3_MOVIE_DONE_MS: arma g_movie_eos_ea DIRECTAMENTE no produtor time-based
 *     do ../gow2-recomp/movie_eos_arm.c e NUNCA passa pelo SEQDONE forcado do
 *     vdec -- nesse caminho o SEQDONE nem dispara. Logo ler o src= do SEQDONE
 *     NAO chega para apanhar o forcamento a nivel de filme.
 * Para ter o sinal completo, a Fase 6 tem de plumbar a razao de arm a partir do
 * movie_eos_arm.c (o armador sabe: "overlay done" = natural vs "time-based done"
 * = forced) com um mecanismo proprio, revisto e testavel -- nao a partir deste
 * sitio vm_read8, que so ve o byte armado, nunca o porque.
 *
 * Deliberately NOT inventing that cross-repo contract here: este sitio nao o
 * consegue testar end-to-end. Uma tentativa anterior (um global mutavel
 * g_movie_eos_arm_reason partilhado pelos dois repos) foi revertida por ficar
 * definida-mas-nunca-lida; nenhum vestigio dela deve permanecer.
 *
 * Factored out of ppu_loader.cpp (a protected, hand-maintained file -- surgical
 * edits only) into its own tiny translation unit, with no dependency beyond
 * ps3_trace.h, so this exact branch -- the real production code vm_read8()
 * calls -- is unit-testable without linking the whole loader (thousands of
 * lines, dozens of unrelated HLE/runtime dependencies). Pure function: takes
 * both the read address and the armed EOS address as arguments rather than
 * reaching for g_movie_eos_ea itself, so a test can drive it with no globals
 * to mock.
 *
 * G8: does not alter PS3_MOVIE_EOS/PS3_VDEC_FORCE_SEQDONE_MS semantics --
 * purely observational, no behaviour change when PS3_TRACE_MEDIA is unset
 * (G2: ps3_trace_on() short-circuits before this file is even reached).
 */
#include <stdint.h>
#include "ps3_trace.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns 1 and emits the honest "[PS3T] ... | MEDIA | EOS | ea=0x%08X
 * src=unknown" line (gated by PS3_TRACE_MEDIA, see PS3_TRACE) if `a` is the
 * armed EOS address (eos_ea != 0 && a == eos_ea); returns 0 and emits nothing
 * otherwise. Callers still do their own checkpoint/return-1 handling around
 * this -- this function ONLY owns the MEDIA trace line's content.
 */
int ps3_movie_eos_media_trace(uint32_t a, uint32_t eos_ea)
{
    if (!eos_ea || a != eos_ea) return 0;
    PS3_TRACE(PS3_TS_MEDIA, "vm_read8", "EOS", "ea=0x%08X src=unknown", a);
    return 1;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
