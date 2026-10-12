/*
 * ps3recomp - Track B live NV4097 draw path (LAYER 2 -> live backend)
 *
 * The offline capture-replay harness (libs/video/tests/replay_main.c) proved a
 * full NV4097 -> D3D12 draw pipeline: the rsx_dispatch register-file model
 * feeds an NV40 VP/FP -> HLSL decompiler and a PSO/sampler/texture engine that
 * renders real geometry into a color surface. This module lifts that engine out
 * of the harness so a port's own live FIFO consumer (the code that walks the
 * game's NV4097 command stream and executes each method) can drive it with the
 * game's own method stream instead of a captured .rxs file, rendering into a
 * D3D12 present surface.
 *
 * Wiring contract for a port (see the reference wiring in a game project's
 * import_overrides.cpp equivalent -- the source module that owns the RSX
 * command processor and the present window):
 *
 *   1. Window/HWND: create (or reuse) a Win32 window on a dedicated thread that
 *      also pumps its message loop, and get an HWND to it. Whatever backend
 *      already opens that window (e.g. a null/GDI clear-color backend) must
 *      expose an accessor for the HWND and a way to suppress its own present
 *      once the live engine takes over -- see rsx_null_backend.h's
 *      rsx_null_backend_get_hwnd()/rsx_null_backend_suppress_present() in this
 *      tree for the shape of that accessor pair.
 *   2. Init: call rsx_live_draw_init(hwnd, width, height, guest_ptr_fn,
 *      guest_user) once, after the window exists. It is safe to call when
 *      rsx_live_draw_enabled() is false (returns 0, stays inert) and safe to
 *      call unconditionally -- gate on rsx_live_draw_enabled() only if the
 *      caller wants to skip D3D12 device creation entirely. On success (0),
 *      the caller should suppress whatever present path it was using before
 *      (this engine now owns Present() on that HWND).
 *   3. Guest memory resolver: supply a rsx_live_guest_ptr_fn that maps an RSX
 *      (location, offset) pair -- location 0 = RSX local/VRAM carve, 1 = main
 *      memory reached through the GCM IO map -- to a host pointer into the
 *      port's guest address space, or NULL if the region is invalid/unmapped.
 *      This is the same mapping the port's own semaphore/DMA address resolver
 *      already needs for the RSX command processor, so it is usually a thin
 *      wrapper around that existing logic.
 *   4. Feed methods: from the FIFO consumer's per-method dispatch, call
 *      rsx_live_draw_method(method, arg) for every NV4097 method write, before
 *      (or instead of) any other method handling this engine already covers
 *      (clears, begin/end, draw batches, program/constant uploads, flip). It
 *      is a no-op when disabled or uninitialized, so the call site does not
 *      need its own gate.
 *   5. Present: the engine self-presents on the driver's flip method (0xE944)
 *      via its own sink, so the FIFO consumer does not need a separate present
 *      call for the live path; it only needs to keep forwarding methods.
 *   6. Movie mode (optional): if the port also has a host-side video decoder
 *      that needs to own the window (e.g. an FMV/cutscene codec), call
 *      rsx_live_draw_set_movie_mode(1) before it starts presenting frames via
 *      rsx_live_draw_present_rgba(), and set it back to 0 when the movie ends,
 *      so the guest's own draws do not race the movie's presents on the same
 *      command list.
 *   7. Shutdown: call rsx_live_draw_shutdown() to release the D3D12 device and
 *      resources (best-effort; process teardown also reclaims them).
 *
 * Data flow (live):
 *   game writes NV4097 methods to the FIFO ring
 *     -> the port's FIFO consumer method dispatch                [port-owned]
 *        -> rsx_live_draw_method(method, arg)                    [this module]
 *           -> rsx_dispatch_method(...)  (register file + BEGIN/END/DRAW/FLIP)
 *              -> sink callbacks -> D3D12 PSO + draw into the flip surface
 *   flip method (0xE944) -> rsx_live_draw_present(buffer_id) -> Present()
 *
 * The whole path is gated by the RSX_LIVE_DRAW env flag (see docs/FLAGS.md):
 * when unset/"0" the live draw engine is inert and the runtime keeps its
 * existing (null / clear-color) present, so this is a pure add-on with a kill
 * switch. RSX_LIVE_DUMP additionally dumps the first few presented frames as
 * PPM files for diffing against a reference (see rsx_live_draw.c).
 *
 * Clean-room: method numbers/semantics from envytools rnndb + Mesa nv30 +
 * psdevwiki; RPCS3 consulted only as a read-only fact oracle. No GPL code.
 *
 * Windows/D3D12 only; a no-op stub compiles elsewhere.
 */

#ifndef PS3RECOMP_RSX_LIVE_DRAW_H
#define PS3RECOMP_RSX_LIVE_DRAW_H

#include "ps3emu/ps3types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Guest-memory resolver: given an RSX location (0 = RSX local VRAM,
 * 1 = main/IO memory) and a byte offset, return a host pointer to at least
 * `min_bytes` of readable guest memory, or NULL if the region is invalid.
 * The runtime supplies this (it owns the vm_base map + the gcm local carve).
 * The replay harness supplies its own arena-backed version. */
typedef const u8* (*rsx_live_guest_ptr_fn)(void* user, u32 location, u32 offset,
                                           u32 min_bytes);

/* Whether the live draw path is enabled this run (reads RSX_LIVE_DRAW once).
 * Cheap; safe to call from the hot method hook. */
int  rsx_live_draw_enabled(void);

/* Bring up the D3D12 device + swap chain bound to an existing HWND (the
 * runtime already opens the present window). Returns 0 on success. Safe to
 * call when disabled (returns 0 and stays inert). `hwnd` is an HWND cast to
 * void* to keep this header windows.h-free. */
int  rsx_live_draw_init(void* hwnd, u32 width, u32 height,
                        rsx_live_guest_ptr_fn guest_ptr, void* guest_user);

/* Seed the dispatcher's register file / transform program from a captured or
 * live initial context (optional; the live path can also just stream). */
void rsx_live_draw_seed_registers(const u32* regs, u32 count);
void rsx_live_draw_seed_transform_program(const u32* words, u32 count);

/* Feed one NV method write. No-op when disabled or uninitialized. */
void rsx_live_draw_method(u32 method, u32 arg);

/* Present the given display buffer id (called from the flip path). */
void rsx_live_draw_present(u32 buffer_id);

/* Movie playback: while on, the guest method stream is ignored so a host-decoded
 * movie can own the window without the guest's draws racing g.list. */
void rsx_live_draw_set_movie_mode(int on);

/* Present a host-decoded RGBA8 frame (w*h*4, row pitch w*4) straight to the
 * swap-chain backbuffer. Call from one thread with movie mode on. */
void rsx_live_draw_present_rgba(const uint8_t* rgba, u32 w, u32 h);

/* Count of REAL game frames the live-draw engine has presented (the game's own
 * 0xE944 flips carrying accumulated draws) -- distinct from the null backend's
 * per-vblank "flips" present count, which keeps ticking even when t1 has stalled
 * and stopped producing. Cheap; safe to poll from the title path. */
u32  rsx_live_draw_get_frames(void);

/* Release all D3D12 resources. */
void rsx_live_draw_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif /* PS3RECOMP_RSX_LIVE_DRAW_H */
