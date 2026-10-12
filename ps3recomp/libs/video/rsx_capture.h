/* rsx_capture: portable C file format for RSX frame captures.
 *
 * A capture is a little-endian stream of records:
 *   { u32 tag; u32 len; u8 payload[len]; }
 * repeated until an END record (a file without END is truncated / corrupt).
 *
 * Used by the in-game recorder (Task 2, gated PS3_RSX_CAPTURE, OFF by
 * default) to dump guest memory pages + RSX method stream for one or more
 * frames, and by the offline replay host (Task 4) to feed the production
 * Metal backend outside the emulator, for deterministic live-vs-replay
 * comparison.
 *
 * Portable C17 only: stdio + string, no platform-specific calls (this file
 * is globbed into the runtime on every platform via libs/video sources).
 * The format is little-endian; on the little-endian hosts we build for, writing
 * a native u32/u16 is already little-endian, so no byte-swapping code is
 * included here -- if this is ever built on a big-endian host, every
 * multi-byte field write/read below needs an explicit swap.
 */
#ifndef PS3RECOMP_RSX_CAPTURE_H
#define PS3RECOMP_RSX_CAPTURE_H

#include "ps3emu/ps3types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RSXC_MAGIC   0x43585352u   /* "RSXC" */
#define RSXC_VERSION 2u            /* v2 adds RANGE (tag 9); the reader accepts 1 and 2 */
#define RSXC_PAGE    65536u        /* capture page size in bytes (64 KiB) */
#define RSXC_CHUNK   4096u         /* RANGE dedupe chunk size in bytes (4 KiB) */

enum {
    RSXC_TAG_HEADER  = 1,  /* rsxc_header */
    RSXC_TAG_EATABLE = 2,  /* u16 io_to_ea[65536] (1 MiB pages, 0xFFFF = none) */
    RSXC_TAG_DISPLAY = 3,  /* u32 id, offset, pitch, width, height */
    RSXC_TAG_FRAME   = 4,  /* u32 frame_index, u32 pending_batch, u8 state[sizeof(rsx_state)] */
    RSXC_TAG_PAGE    = 5,  /* u32 ea, u8 data[RSXC_PAGE] */
    RSXC_TAG_METHODS = 6,  /* n x { u32 subch, u32 method, u32 value } */
    RSXC_TAG_PRESENT = 7,  /* u32 buffer_id */
    RSXC_TAG_END     = 8,  /* u32 frames_written (a file without END is truncated) */
    RSXC_TAG_RANGE   = 9   /* u32 ea, u32 len (<= RSXC_CHUNK), u8 data[len] */
};

typedef struct {
    u32 magic, version, state_size;
    u32 io_address, io_size, local_address, local_size, page_size;
} rsxc_header;

/* --- writer --- */

typedef struct rsxc_writer rsxc_writer;

/* Creates path, writes the HEADER record. Returns NULL on error (cannot
 * create the file). */
rsxc_writer* rsxc_open_write(const char* path, const rsxc_header* h);

int  rsxc_write_eatable(rsxc_writer* w, const u16* io_to_ea /* 65536 entries */);
int  rsxc_write_display(rsxc_writer* w, u32 id, u32 offset, u32 pitch, u32 width, u32 height);
int  rsxc_write_frame(rsxc_writer* w, u32 frame_index, u32 pending_batch, const void* state, u32 state_size);
int  rsxc_write_page(rsxc_writer* w, u32 ea, const u8* data /* RSXC_PAGE bytes */);

/* Buffered: methods accumulate and are flushed as RSXC_TAG_METHODS records
 * (either when the internal buffer fills or at rsxc_close_write). */
int  rsxc_write_method(rsxc_writer* w, u32 subch, u32 method, u32 value);

int  rsxc_write_present(rsxc_writer* w, u32 buffer_id);

/* RANGE record (v2): a guest byte range a backend callback read, used by the
 * granular recorder (Task 3) instead of whole RSXC_PAGE snapshots. len must
 * be in [1, RSXC_CHUNK]; 0 or > RSXC_CHUNK is rejected (-1). Like every other
 * record, buffered methods are flushed first so file order matches call
 * order. */
int  rsxc_write_range(rsxc_writer* w, u32 ea, const u8* data, u32 len);

/* Flushes any buffered methods, writes END, closes the file. Returns 0 on
 * success, non-zero on I/O error. w is freed either way. */
int  rsxc_close_write(rsxc_writer* w, u32 frames_written);

/* --- reader --- */

typedef struct {
    u32 tag, len;
    const u8* data;  /* valid until the next rsxc_next / rsxc_close_read call */
} rsxc_record;

typedef struct rsxc_reader rsxc_reader;

/* Reads the whole file into memory, validates magic/version/state_size
 * (against expected_state_size) and that the file ends with an END record.
 * On success returns a reader and fills *out_h with the header. On error
 * returns NULL and writes a human-readable message into err (size errsz). */
rsxc_reader* rsxc_open_read(const char* path, u32 expected_state_size,
                             rsxc_header* out_h, char* err, u32 errsz);

/* Advances to the next record. Returns 1 with *rec filled, 0 at END (no
 * more records), -1 on corruption (never reads past the buffer). */
int  rsxc_next(rsxc_reader* r, rsxc_record* rec);

void rsxc_close_read(rsxc_reader* r);

/* 64-bit content hash over a full RSXC_PAGE (FNV-1a-based, reads every
 * byte); used by the recorder to skip unchanged pages between frames.
 * Identical to rsxc_hash_bytes(data, RSXC_PAGE). */
u64  rsxc_hash_page(const u8* data /* RSXC_PAGE bytes */);

/* Same word-wise FNV-1a as rsxc_hash_page, for any length (a zero-padded
 * tail word covers len % 8 != 0). */
u64  rsxc_hash_bytes(const u8* p, u32 len);

/* --- chunk dedupe table (v2): key = ea / RSXC_CHUNK -> last-seen 64-bit
 * content hash. Owned by the recorder (Task 3): one rsxc_chunks_changed()
 * call per RSXC_CHUNK-aligned chunk a captured range touches, so the same
 * chunk changed by two overlapping ranges (interleaved vertex attributes,
 * an atlas bound with several formats) is only ever emitted once per actual
 * change. Open addressing, grows by doubling to keep the load factor <= 0.5.
 * On malloc failure during growth the table stays usable (old entries
 * intact) and rsxc_chunks_changed just reports "changed" without recording
 * the new entry -- never silently drops a range. */
typedef struct rsxc_chunk_table rsxc_chunk_table;

rsxc_chunk_table* rsxc_chunks_new(void);
void  rsxc_chunks_free(rsxc_chunk_table* t);

/* Returns 1 if the chunk at chunk_ea (aligned to RSXC_CHUNK; the caller
 * passes the chunk's base address) is new or its hash changed since the last
 * call for that chunk (and records the new hash), else 0. t == NULL (e.g.
 * rsxc_chunks_new() failed) always returns 1. */
int   rsxc_chunks_changed(rsxc_chunk_table* t, u32 chunk_ea, u64 hash);

/* --- recorder (Task 2): PS3_RSX_CAPTURE probe, OFF by default --- */

/* PS3_RSX_CAPTURE=<path>:<min_draws>:<frames>  e.g. "/.../rsx_captures/menu.rsxc:150:4"
 * Arms at the first begin-frame whose PREVIOUS frame had rsx_stats_logical() >=
 * min_draws (sampled at that frame's present, before the backend's begin_frame
 * resets the counter), records <frames> consecutive frames, then closes the file
 * and calls the dump hook (prefix = <path> without extension + "_live").
 * Unset = no-op: every hook returns on one cached branch, touching no provider.
 * The path is split at the LAST two ':' so Windows drive letters work.
 * v2 (V2.0b): while RECORDING the registered rsx_backend is replaced by an
 * interposer (same userdata, same callbacks) whose set_shader / bind_texture /
 * draw_arrays / draw_indexed first write, as RANGE records, the committed 4 KiB
 * chunks the real callback reads (FP ucode also at every draw; a texture on
 * the first bind of its key per frame), then forward. The real backend is
 * re-registered when the capture closes or aborts. */
int  rsx_capture_enabled(void);                          /* cached env check */
void rsx_capture_on_begin_frame(void);                   /* after backend begin_frame */
void rsx_capture_on_method(u32 subch, u32 method, u32 value); /* AFTER dispatch (RANGEs first) */
void rsx_capture_on_present(u32 buffer_id);              /* AFTER the backend presented */
void rsx_capture_on_display_buffer(u32 id, u32 offset, u32 pitch, u32 width, u32 height);

/* Called from the forced-present path(s) in cellGcmSys.c: hashing a captured
 * frame's pages can outlast PS3_GCM_FLIP_WAIT_MS and force a present, shifting
 * frame boundaries. Counted while RECORDING and reported in the "done:" line;
 * callers guard with `if (g_rsx_capture_state == 2)` so this is free when off. */
void rsx_capture_note_forced_present(void);

/* Recorder state, readable by callers that want to skip the call itself on a
 * per-method hot path: -1 env not read yet, 0 off, 1 armed, 2 recording, 3 done. */
extern volatile int g_rsx_capture_state;

/* Backends register a surface dump (native size); the recorder calls it once,
 * on the last captured present, outside its lock. */
typedef int (*rsx_capture_dump_fn)(const char* prefix);
void rsx_capture_set_dump_hook(rsx_capture_dump_fn fn);

/* Tests only: forget the cached env and any state (does not close a writer). */
void rsx_capture_reset_for_tests(void);

/* Providers the recorder calls (implemented in cellGcmSys.c / rsx_commands.c).
 * rsx_state is only used by pointer here (full type in rsx_commands.h). */
struct rsx_state;
const struct rsx_state* gcm_capture_rsx_state(void);
/* io_to_ea: 65536 entries, io offset 1 MiB page -> EA 1 MiB page (0xFFFF = none). */
void gcm_capture_io_info(u32* io_address, u32* io_size, const u16** io_to_ea);
void gcm_capture_local_info(u32* local_address, u32* local_size);
void gcm_capture_display_table(void (*emit)(u32 id, u32 off, u32 pitch, u32 w, u32 h));
int  rsx_commands_pending_batch(void);                   /* s_idx_pending || s_arr_pending */
u32  rsx_stats_logical(void);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_RSX_CAPTURE_H */
