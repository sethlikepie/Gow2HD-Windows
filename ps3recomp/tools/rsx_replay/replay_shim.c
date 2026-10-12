/* replay_shim.c -- the runtime surface the production Metal RSX backend and
 * rsx_commands.c need, provided by the offline replay host (rsx_replay)
 * instead of the PPU runtime / cellGcmSys.c.
 *
 * Real (replay-backed) symbols:
 *   vm_base                          4 GiB anonymous reservation (zero-filled);
 *                                    PAGE records are copied into it.
 *   ppu_guest_range_committed        RSX local range, the recorded IO window
 *                                    [io_address, +io_size), EA pages the
 *                                    recorded EATABLE maps (the IO window as the
 *                                    RSX sees it), or 1 MiB pages a PAGE / RANGE
 *                                    record wrote (the recorder only captures
 *                                    committed guest memory, so bytes the
 *                                    capture carries were committed live).
 *   gcm_io_offset_to_ea              the recorded EATABLE, same fallback
 *                                    (ioAddress + off) as cellGcmSys.c.
 *   cellGcmGetDisplayInfo /
 *   cellGcmGetCurrentDisplayBufferId recorded DISPLAY records / last PRESENT id.
 *
 * Everything else is a no-op / zero stub: host overlays (prompts, boot logos,
 * movie HLE), pad autoplay, SPU PUT map, perf/stat counters, trace/checkpoint
 * probes and the recorder's providers. Each is diagnostic or host content that
 * must NOT draw into a replayed frame (see the report of Task 4 for the list). */
#include "ps3emu/ps3types.h"
#include "cellGcmSys.h"
#include "rsx_capture.h"
#include "rsx_host_content.h"
#include "spu_put_map.h"
#include "host_prompts.h"
#include "host_prompt_mesh.h"
#include "ps3_trace.h"
#include "ps3_checkpoint.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* ---------------------------------------------------------------------------
 * Replay-backed state (set by rsx_replay_main.c)
 * -----------------------------------------------------------------------*/

uint8_t* vm_base = NULL;

static u32 s_io_address, s_io_size, s_local_address, s_local_size;
static u16 s_io_to_ea[65536];
static u8  s_ea_mapped[4096];                  /* 1 MiB EA pages the EATABLE maps */
static u8  s_ea_written[4096];                 /* 1 MiB EA pages a PAGE / RANGE wrote */
static CellGcmDisplayInfo s_display[CELL_GCM_MAX_DISPLAY_BUFFER_NUM];
static u32 s_current_display_id;

int replay_shim_vm_init(void)
{
    void* p = mmap(NULL, (size_t)1 << 32, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return -1;
    vm_base = (uint8_t*)p;
    memset(s_io_to_ea, 0xFF, sizeof s_io_to_ea);
    return 0;
}

void replay_shim_set_windows(u32 io_address, u32 io_size, u32 local_address, u32 local_size)
{
    s_io_address = io_address;       s_io_size = io_size;
    s_local_address = local_address; s_local_size = local_size;
}

void replay_shim_set_eatable(const u16* io_to_ea /* 65536 entries */)
{
    memcpy(s_io_to_ea, io_to_ea, sizeof s_io_to_ea);
    memset(s_ea_mapped, 0, sizeof s_ea_mapped);
    for (u32 i = 0; i < 65536u; i++)
        if (s_io_to_ea[i] != 0xFFFFu && s_io_to_ea[i] < 4096u) s_ea_mapped[s_io_to_ea[i]] = 1;
}

int replay_shim_set_display(u32 id, u32 offset, u32 pitch, u32 width, u32 height)
{
    if (id >= CELL_GCM_MAX_DISPLAY_BUFFER_NUM) return -1;
    s_display[id].offset = offset;
    s_display[id].pitch  = pitch;
    s_display[id].width  = width;
    s_display[id].height = height;
    return 0;
}

void replay_shim_set_present_id(u32 id) { s_current_display_id = id; }

/* PAGE / RANGE: [ea, ea+len) now holds captured guest bytes. The caller has
 * already checked ea + len <= 4 GiB. Kept apart from s_ea_mapped, which a
 * later EATABLE record resets. */
void replay_shim_mark_committed(u32 ea, u32 len)
{
    if (len == 0) return;
    const u64 end = (u64)ea + len;
    for (u64 p = (u64)ea >> 20; p <= (end - 1) >> 20; p++) s_ea_written[p] = 1;
}

static int in_range(u64 a, u64 n, u64 base, u64 size)
{
    return size && a >= base && a + n <= base + size;
}

int ppu_guest_range_committed(uint32_t addr, uint32_t n)
{
    const u64 a = addr, end = (u64)addr + n;
    if (end > ((u64)1 << 32)) return 0;
    if (in_range(a, n, s_local_address, s_local_size)) return 1;
    if (in_range(a, n, s_io_address, s_io_size)) return 1;
    if (n == 0) return s_ea_mapped[a >> 20] | s_ea_written[a >> 20];
    for (u64 p = a >> 20; p <= (end - 1) >> 20; p++)
        if (!s_ea_mapped[p] && !s_ea_written[p]) return 0;
    return 1;
}

int gcm_io_offset_to_ea(u32 ioOffset, u32* out_ea)
{
    if (!out_ea) return 0;
    const u32 page = ioOffset >> 20;
    if (page < 65536u && s_io_to_ea[page] != 0xFFFFu) {
        *out_ea = ((u32)s_io_to_ea[page] << 20) | (ioOffset & 0xFFFFFu);
        return 1;
    }
    /* Legacy fallback, as cellGcmSys.c gcm_io_offset_to_ea. */
    if (s_io_address != 0 && ioOffset < s_io_size) {
        *out_ea = s_io_address + ioOffset;
        return 1;
    }
    return 0;
}

CellGcmDisplayInfo* cellGcmGetDisplayInfo(u32 index)
{
    if (index >= CELL_GCM_MAX_DISPLAY_BUFFER_NUM) return NULL;
    return &s_display[index];
}

u32 cellGcmGetCurrentDisplayBufferId(void) { return s_current_display_id; }

/* ---------------------------------------------------------------------------
 * Stubs: host content / overlays (must not draw into a replayed frame)
 * -----------------------------------------------------------------------*/

void rsx_host_boot_logo_tick(void) {}
void rsx_host_movie_hle_set(int on) { (void)on; }
void rsx_metal_movie_playback_end(void) {}
int  movie_clock_arms_logos(void) { return 0; }

int host_prompts_enabled(void) { return 0; }
host_prompt_set host_prompts_current(void) { return HOST_PROMPT_SET_PS; }
int host_prompt_face_mesh_grown(host_prompt_face f, host_prompt_set s, float a, float g,
                                host_prompt_mesh* o)
{ (void)f; (void)s; (void)a; (void)g; (void)o; return 0; }
int host_prompt_backing_mesh_part(host_prompt_face f, host_prompt_set s, int p,
                                  host_prompt_mesh* o)
{ (void)f; (void)s; (void)p; (void)o; return 0; }
int host_prompt_keycap_layer(host_prompt_face f, host_prompt_set s, int l, float a,
                             host_prompt_mesh* o)
{ (void)f; (void)s; (void)l; (void)a; (void)o; return 0; }
int host_prompt_shoulder_text(host_prompt_face f, host_prompt_set s, float adv,
                              host_prompt_text* o)
{ (void)f; (void)s; (void)adv; (void)o; return 0; }
float host_prompt_font_advance(char c) { (void)c; return 0.0f; }

void pad_autoplay_note_gameplay(void) {}
void ps3_vm_note_gameplay_draws(unsigned draws) { (void)draws; }

/* ---------------------------------------------------------------------------
 * Stubs: diagnostics / perf counters (no PPU, SPU or FIFO walker here)
 * -----------------------------------------------------------------------*/

int  spu_put_map_on(void) { return 0; }
void spu_put_map_summary(void) {}
int  spu_put_owner(unsigned int ea, spu_put_rec* out) { (void)ea; (void)out; return 0; }
unsigned long long spu_put_seq(void) { return 0; }
unsigned long long g_spu_abort_unknown = 0;

void gcm_fifo_kick_take(unsigned long long* kicks, unsigned long long* words)
{
    if (kicks) *kicks = 0;
    if (words) *words = 0;
}
double gcm_perf_take_dec_ms(void) { return 0.0; }
void ppu_giant_stat_take(double* wait_ms, double* hold_ms, long long* acq)
{
    if (wait_ms) *wait_ms = 0.0;
    if (hold_ms) *hold_ms = 0.0;
    if (acq) *acq = 0;
}
void ppu_vm_count_take(long long* rd, long long* wr)
{
    if (rd) *rd = 0;
    if (wr) *wr = 0;
}

int  ps3_trace_on(ps3_trace_subsys s) { (void)s; return 0; }
void ps3_trace_emit(ps3_trace_subsys s, const char* fn, const char* event, const char* fmt, ...)
{ (void)s; (void)fn; (void)event; (void)fmt; }
void ps3_checkpoint_fire(ps3_checkpoint_id id, const char* fn, const char* fmt, ...)
{ (void)id; (void)fn; (void)fmt; }

/* rsx_capture.c's recorder providers: the replay never records (rsx_replay
 * unsets PS3_RSX_CAPTURE), so these are never reached; zero answers. */
const struct rsx_state* gcm_capture_rsx_state(void) { return NULL; }
void gcm_capture_io_info(u32* a, u32* s, const u16** t)
{
    if (a) *a = 0;
    if (s) *s = 0;
    if (t) *t = NULL;
}
void gcm_capture_local_info(u32* a, u32* s)
{
    if (a) *a = 0;
    if (s) *s = 0;
}
void gcm_capture_display_table(void (*emit)(u32, u32, u32, u32, u32)) { (void)emit; }
