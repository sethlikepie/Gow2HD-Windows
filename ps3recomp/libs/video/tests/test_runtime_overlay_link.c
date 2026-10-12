/*
 * Link-only smoke: an executable that links ONLY libps3recomp_runtime.a (plus
 * the frameworks/libs games/gow2/build_macos.sh passes) must resolve every
 * symbol the overlay core needs, ImGui included. cellPad.o references
 * rsx_overlay.o, so every host that links the runtime archive pulls the
 * overlay in; before the ImGui objects were folded into the archive this link
 * failed with ~24 undefined ImGui:: symbols.
 *
 * The symbols below are the per-game PPU boot scaffold's (runtime/ppu,
 * lift objects) -- the part of a real host that is NOT in the runtime
 * archive. They are stand-ins so this test links without a lift; none of
 * them is executed by rsx_overlay_init/shutdown.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "rsx_overlay.h"

/* Per-game scaffold stand-ins (data). */
unsigned long long g_last_hle_caller_guest, g_last_hle_caller_lr, g_last_hle_nid;
unsigned long long g_ps3_rperma_full, g_scream_tick_wait;
unsigned char* vm_base;
/* Per-game scaffold stand-ins (code). Never called by this test. */
#define STUB(name) long name(void); long name(void) { abort(); }
STUB(ppu_blockmark_name) STUB(ppu_giant_hle_only_on) STUB(ppu_giant_lock_acquire)
STUB(ppu_giant_lock_acquire_prio) STUB(ppu_giant_lock_held_by_caller)
STUB(ppu_giant_lock_release) STUB(ppu_giant_stat_take) STUB(ppu_guest_range_committed)
STUB(ppu_register_committed_range) STUB(ppu_rsv_register) STUB(ppu_rsv_unregister)
STUB(ppu_vm_count_take) STUB(ppu_vm_size) STUB(ps3_ms) STUB(ps3_scan_vram_occupancy)
STUB(ps3_vm_note_gameplay_draws) STUB(ps3_watch_store_bulk) STUB(vm_read32)
STUB(vm_read8) STUB(vm_write16) STUB(vm_write32)

int main(void)
{
    char path[] = "/tmp/rsx-overlay-link-XXXXXX";
    int fd = mkstemp(path);
    int ok;

    if (fd < 0)
        return 2;
    close(fd);
    unlink(path);
    ok = rsx_overlay_init(path);
    rsx_overlay_build_draw_data();
    rsx_overlay_shutdown();
    unlink(path);
    printf("%s runtime archive links the overlay core (init=%d)\n",
           ok ? "[PASS]" : "[FAIL]", ok);
    return ok ? 0 : 1;
}
