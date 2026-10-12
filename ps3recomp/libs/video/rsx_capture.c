/* rsx_capture: writer/reader implementation. See rsx_capture.h for the file
 * format and API contract. Portable C17: stdio + string only. */
#include "rsx_capture.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* _fseeki64/_ftelli64 -> fseeko/ftello on POSIX (see cellFs.c, movie_hle.c,
 * sys_fs.c, ppu_fs.cpp): plain fseek/ftell use `long`, which is 32-bit on
 * Windows/LLP64, so a capture file at or beyond 2 GiB (easily reached by a
 * multi-frame capture: one frame's pages alone can be ~300 MB) could not be
 * opened there. */
#include "ps3emu/host_compat.h"

/* --- shared helpers --- */

/* Little-endian read/write of a u32 from/into a raw byte buffer. On the
 * (little-endian) hosts we build for, this is just a memcpy; no byte-swap
 * is implemented (see the file header comment in rsx_capture.h). */
static u32 rsxc_rd_u32(const u8* p)
{
    u32 v;
    memcpy(&v, p, sizeof v);
    return v;
}

/* --- writer --- */

typedef struct {
    u32 subch, method, value;
} rsxc_method_entry;

_Static_assert(sizeof(rsxc_method_entry) == 12u,
                "rsxc_method_entry must be exactly 12 bytes (no padding): "
                "the file format and the reader assume 3 packed u32 fields");

#define RSXC_METHOD_BUF_CAP 4096u

struct rsxc_writer {
    FILE* f;
    rsxc_method_entry methods[RSXC_METHOD_BUF_CAP];
    u32 method_count;
    int error;
};

static int rsxc_wr_u32(FILE* f, u32 v)
{
    return fwrite(&v, sizeof v, 1, f) == 1 ? 0 : -1;
}

/* Writes one { tag; len; payload[len] } record verbatim. */
static int rsxc_wr_record(rsxc_writer* w, u32 tag, const void* payload, u32 len)
{
    if (!w || w->error) return -1;
    if (rsxc_wr_u32(w->f, tag) != 0 || rsxc_wr_u32(w->f, len) != 0) {
        w->error = 1;
        return -1;
    }
    if (len != 0u && fwrite(payload, 1, len, w->f) != (size_t)len) {
        w->error = 1;
        return -1;
    }
    return 0;
}

/* Emits the buffered methods as one METHODS record (no-op if empty). Called
 * whenever the buffer fills, before any other record type is written, and
 * at close -- so records always appear in the order the caller issued
 * them. */
static int rsxc_flush_methods(rsxc_writer* w)
{
    if (!w || w->error) return -1;
    if (w->method_count == 0u) return 0;
    const u32 len = w->method_count * (u32)sizeof(rsxc_method_entry);
    if (rsxc_wr_u32(w->f, RSXC_TAG_METHODS) != 0 || rsxc_wr_u32(w->f, len) != 0) {
        w->error = 1;
        return -1;
    }
    const size_t bytes = (size_t)w->method_count * sizeof(rsxc_method_entry);
    if (fwrite(w->methods, 1, bytes, w->f) != bytes) {
        w->error = 1;
        return -1;
    }
    w->method_count = 0u;
    return 0;
}

rsxc_writer* rsxc_open_write(const char* path, const rsxc_header* h)
{
    if (!path || !h) return NULL;
    FILE* f = fopen(path, "wb");
    if (!f) return NULL;
    rsxc_writer* w = (rsxc_writer*)calloc(1, sizeof(rsxc_writer));
    if (!w) {
        fclose(f);
        return NULL;
    }
    w->f = f;
    if (rsxc_wr_record(w, RSXC_TAG_HEADER, h, (u32)sizeof(*h)) != 0) {
        fclose(w->f);
        free(w);
        return NULL;
    }
    return w;
}

int rsxc_write_eatable(rsxc_writer* w, const u16* io_to_ea)
{
    if (!w || rsxc_flush_methods(w) != 0) return -1;
    return rsxc_wr_record(w, RSXC_TAG_EATABLE, io_to_ea, 65536u * (u32)sizeof(u16));
}

int rsxc_write_display(rsxc_writer* w, u32 id, u32 offset, u32 pitch, u32 width, u32 height)
{
    if (!w || rsxc_flush_methods(w) != 0) return -1;
    u32 payload[5];
    payload[0] = id;
    payload[1] = offset;
    payload[2] = pitch;
    payload[3] = width;
    payload[4] = height;
    return rsxc_wr_record(w, RSXC_TAG_DISPLAY, payload, (u32)sizeof(payload));
}

int rsxc_write_frame(rsxc_writer* w, u32 frame_index, u32 pending_batch, const void* state, u32 state_size)
{
    if (!w || rsxc_flush_methods(w) != 0) return -1;
    if (w->error) return -1;
    const u32 len = 8u + state_size;
    if (rsxc_wr_u32(w->f, RSXC_TAG_FRAME) != 0 || rsxc_wr_u32(w->f, len) != 0) {
        w->error = 1;
        return -1;
    }
    if (rsxc_wr_u32(w->f, frame_index) != 0 || rsxc_wr_u32(w->f, pending_batch) != 0) {
        w->error = 1;
        return -1;
    }
    if (state_size != 0u && fwrite(state, 1, state_size, w->f) != (size_t)state_size) {
        w->error = 1;
        return -1;
    }
    return 0;
}

int rsxc_write_page(rsxc_writer* w, u32 ea, const u8* data)
{
    if (!w || rsxc_flush_methods(w) != 0) return -1;
    if (w->error) return -1;
    const u32 len = 4u + RSXC_PAGE;
    if (rsxc_wr_u32(w->f, RSXC_TAG_PAGE) != 0 || rsxc_wr_u32(w->f, len) != 0) {
        w->error = 1;
        return -1;
    }
    if (rsxc_wr_u32(w->f, ea) != 0) {
        w->error = 1;
        return -1;
    }
    if (fwrite(data, 1, RSXC_PAGE, w->f) != (size_t)RSXC_PAGE) {
        w->error = 1;
        return -1;
    }
    return 0;
}

int rsxc_write_method(rsxc_writer* w, u32 subch, u32 method, u32 value)
{
    if (!w || w->error) return -1;
    if (w->method_count >= RSXC_METHOD_BUF_CAP) {
        if (rsxc_flush_methods(w) != 0) return -1;
    }
    w->methods[w->method_count].subch = subch;
    w->methods[w->method_count].method = method;
    w->methods[w->method_count].value = value;
    w->method_count++;
    return 0;
}

int rsxc_write_present(rsxc_writer* w, u32 buffer_id)
{
    if (!w || rsxc_flush_methods(w) != 0) return -1;
    return rsxc_wr_record(w, RSXC_TAG_PRESENT, &buffer_id, (u32)sizeof(buffer_id));
}

int rsxc_write_range(rsxc_writer* w, u32 ea, const u8* data, u32 len)
{
    if (len == 0u || len > RSXC_CHUNK) return -1;
    if (!w || rsxc_flush_methods(w) != 0) return -1;
    if (w->error) return -1;
    const u32 rec_len = 8u + len;
    if (rsxc_wr_u32(w->f, RSXC_TAG_RANGE) != 0 || rsxc_wr_u32(w->f, rec_len) != 0) {
        w->error = 1;
        return -1;
    }
    if (rsxc_wr_u32(w->f, ea) != 0 || rsxc_wr_u32(w->f, len) != 0) {
        w->error = 1;
        return -1;
    }
    if (fwrite(data, 1, len, w->f) != (size_t)len) {
        w->error = 1;
        return -1;
    }
    return 0;
}

int rsxc_close_write(rsxc_writer* w, u32 frames_written)
{
    if (!w) return -1;
    int failed = w->error;
    if (!failed && rsxc_flush_methods(w) != 0) failed = 1;
    if (!failed && rsxc_wr_record(w, RSXC_TAG_END, &frames_written, (u32)sizeof(frames_written)) != 0) failed = 1;
    if (w->f && fclose(w->f) != 0) failed = 1;
    free(w);
    return failed ? -1 : 0;
}

/* --- reader --- */

struct rsxc_reader {
    u8* buf;
    size_t size;
    size_t pos; /* offset of the next record to hand out via rsxc_next */
};

static void rsxc_err(char* err, u32 errsz, const char* fmt, ...)
{
    if (!err || errsz == 0u) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, (size_t)errsz, fmt, ap);
    va_end(ap);
}

rsxc_reader* rsxc_open_read(const char* path, u32 expected_state_size, rsxc_header* out_h, char* err, u32 errsz)
{
    if (err && errsz) err[0] = '\0';
    if (!path) {
        rsxc_err(err, errsz, "rsx_capture: null path");
        return NULL;
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        rsxc_err(err, errsz, "rsx_capture: cannot open '%s'", path);
        return NULL;
    }
    if (_fseeki64(f, 0, SEEK_END) != 0) {
        fclose(f);
        rsxc_err(err, errsz, "rsx_capture: seek(END) failed");
        return NULL;
    }
    const long long fsize = _ftelli64(f);
    if (fsize < 0) {
        fclose(f);
        rsxc_err(err, errsz, "rsx_capture: ftell failed");
        return NULL;
    }
    if (_fseeki64(f, 0, SEEK_SET) != 0) {
        fclose(f);
        rsxc_err(err, errsz, "rsx_capture: seek(SET) failed");
        return NULL;
    }
    if ((unsigned long long)fsize > (unsigned long long)SIZE_MAX) {
        fclose(f);
        rsxc_err(err, errsz, "rsx_capture: file too large for this host (%lld bytes)", fsize);
        return NULL;
    }
    const size_t size = (size_t)fsize;
    u8* buf = NULL;
    if (size != 0u) {
        buf = (u8*)malloc(size);
        if (!buf) {
            fclose(f);
            rsxc_err(err, errsz, "rsx_capture: out of memory (%zu bytes)", size);
            return NULL;
        }
        if (fread(buf, 1, size, f) != size) {
            fclose(f);
            free(buf);
            rsxc_err(err, errsz, "rsx_capture: short read on '%s'", path);
            return NULL;
        }
    }
    fclose(f);

    /* Header record: must be the very first record and exactly
     * sizeof(rsxc_header) bytes long. Every length below is checked against
     * the bytes actually remaining before it is trusted, so a corrupt/
     * truncated file is refused instead of read out of bounds. */
    if (size < 8u) {
        free(buf);
        rsxc_err(err, errsz, "rsx_capture: file too small to hold a header");
        return NULL;
    }
    const u32 tag0 = rsxc_rd_u32(buf);
    const u32 len0 = rsxc_rd_u32(buf + 4);
    if (tag0 != RSXC_TAG_HEADER || len0 != (u32)sizeof(rsxc_header) ||
        (u64)8u + (u64)len0 > (u64)size) {
        free(buf);
        rsxc_err(err, errsz, "rsx_capture: missing or malformed header record");
        return NULL;
    }
    rsxc_header hdr;
    memcpy(&hdr, buf + 8, sizeof hdr);

    /* Scan every record boundary to the end of the file: the last complete
     * record must be END, and no bytes may be left over after it. */
    size_t pos = 8u + (size_t)len0;
    int have_end = 0;
    while (pos + 8u <= size) {
        const u32 tag = rsxc_rd_u32(buf + pos);
        const u32 len = rsxc_rd_u32(buf + pos + 4);
        const u64 rec_total = (u64)8u + (u64)len;
        if (rec_total > (u64)(size - pos)) {
            free(buf);
            rsxc_err(err, errsz, "rsx_capture: truncated record (tag %u) at offset %zu",
                      (unsigned)tag, pos);
            return NULL;
        }
        pos += (size_t)rec_total;
        if (tag == RSXC_TAG_END) {
            have_end = 1;
            break;
        }
    }
    if (!have_end || pos != size) {
        free(buf);
        rsxc_err(err, errsz, "rsx_capture: truncated capture (no END record)");
        return NULL;
    }

    if (hdr.magic != RSXC_MAGIC) {
        free(buf);
        rsxc_err(err, errsz, "rsx_capture: bad magic 0x%08X", (unsigned)hdr.magic);
        return NULL;
    }
    if (hdr.version != 1u && hdr.version != 2u) {
        free(buf);
        rsxc_err(err, errsz, "rsx_capture: unsupported version %u", (unsigned)hdr.version);
        return NULL;
    }
    if (hdr.state_size != expected_state_size) {
        free(buf);
        rsxc_err(err, errsz, "rsx_capture: state_size mismatch (file has %u, expected %u)",
                  (unsigned)hdr.state_size, (unsigned)expected_state_size);
        return NULL;
    }

    rsxc_reader* r = (rsxc_reader*)calloc(1, sizeof(rsxc_reader));
    if (!r) {
        free(buf);
        rsxc_err(err, errsz, "rsx_capture: out of memory");
        return NULL;
    }
    r->buf = buf;
    r->size = size;
    r->pos = 8u + (size_t)len0;
    if (out_h) *out_h = hdr;
    return r;
}

int rsxc_next(rsxc_reader* r, rsxc_record* rec)
{
    if (!r || !rec) return -1;
    if (r->pos + 8u > r->size) return -1; /* no room left for a record header */
    const u32 tag = rsxc_rd_u32(r->buf + r->pos);
    const u32 len = rsxc_rd_u32(r->buf + r->pos + 4);
    const u64 rec_total = (u64)8u + (u64)len;
    if (rec_total > (u64)(r->size - r->pos)) return -1;
    if (tag == RSXC_TAG_END) return 0;
    if (tag == RSXC_TAG_RANGE) {
        /* Payload is { u32 ea; u32 len; u8 data[len]; }. Corruption (a hand-
         * edited or bit-flipped file) is rejected instead of trusting the
         * inner len field: len < 8 means there is no room for the inner
         * fields at all; otherwise the inner len must be in [1, RSXC_CHUNK]
         * (rsxc_write_range never emits 0) and the outer record length must
         * be exactly 8 + inner len. */
        if (len < 8u) return -1;
        const u32 inner_len = rsxc_rd_u32(r->buf + r->pos + 8u + 4u);
        if (inner_len < 1u || inner_len > RSXC_CHUNK || len != 8u + inner_len) return -1;
    }
    rec->tag = tag;
    rec->len = len;
    rec->data = r->buf + r->pos + 8u;
    r->pos += (size_t)rec_total;
    return 1;
}

void rsxc_close_read(rsxc_reader* r)
{
    if (!r) return;
    free(r->buf);
    free(r);
}

u64 rsxc_hash_bytes(const u8* p, u32 len)
{
    u64 h = 1469598103934665603ull; /* FNV-1a 64-bit offset basis */
    const u32 words = len / (u32)sizeof(u64);
    for (u32 i = 0; i < words; i++) {
        u64 w;
        memcpy(&w, p + (size_t)i * sizeof(u64), sizeof w);
        h ^= w;
        h *= 0x100000001B3ull;
    }
    const u32 tail_off = words * (u32)sizeof(u64);
    const u32 tail_len = len - tail_off;
    if (tail_len != 0u) {
        u64 w = 0;
        memcpy(&w, p + tail_off, tail_len);   /* zero-padded: only tail_len bytes copied */
        h ^= w;
        h *= 0x100000001B3ull;
    }
    h ^= (u64)len;
    h *= 0x100000001B3ull;
    return h;
}

u64 rsxc_hash_page(const u8* data)
{
    /* RSXC_PAGE (64 KiB) is a multiple of 8, so the tail branch above never
     * triggers here -- identical to the original word-only implementation. */
    return rsxc_hash_bytes(data, RSXC_PAGE);
}

/* --- chunk dedupe table (v2) ---
 *
 * Open addressing (linear probing), key = ea / RSXC_CHUNK, value = last-seen
 * content hash, with a separate occupancy byte array (0 == empty) so a key
 * of 0 is a valid, distinguishable entry. Grows by doubling whenever an
 * insert would push the load factor above 0.5, keeping probe sequences
 * short. On a failed grow (malloc), the table is left exactly as it was
 * (still fully usable) and the caller is told "changed" for the entry that
 * could not be recorded -- correct-but-conservative, never silently drops a
 * range. */

#define RSXC_CHUNK_TABLE_INIT_CAP 16u   /* power of two */

struct rsxc_chunk_table {
    u32* keys;
    u64* hashes;
    u8*  occ;
    u32  capacity;
    u32  count;
};

/* 32-bit integer mix (splitmix32-style finalizer): spreads the low bits of
 * a chunk index (which increments by 1 as an address walks a range) across
 * the whole table instead of clustering consecutive keys in consecutive
 * slots. */
static u32 rsxc_chunk_mix(u32 key)
{
    u32 h = key;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

static rsxc_chunk_table* rsxc_chunk_table_alloc(u32 capacity)
{
    rsxc_chunk_table* t = (rsxc_chunk_table*)malloc(sizeof(rsxc_chunk_table));
    if (!t) return NULL;
    t->keys = (u32*)malloc((size_t)capacity * sizeof(u32));
    t->hashes = (u64*)malloc((size_t)capacity * sizeof(u64));
    t->occ = (u8*)calloc(capacity, sizeof(u8));
    if (!t->keys || !t->hashes || !t->occ) {
        free(t->keys);
        free(t->hashes);
        free(t->occ);
        free(t);
        return NULL;
    }
    t->capacity = capacity;
    t->count = 0u;
    return t;
}

rsxc_chunk_table* rsxc_chunks_new(void)
{
    return rsxc_chunk_table_alloc(RSXC_CHUNK_TABLE_INIT_CAP);
}

void rsxc_chunks_free(rsxc_chunk_table* t)
{
    if (!t) return;
    free(t->keys);
    free(t->hashes);
    free(t->occ);
    free(t);
}

/* Inserts (key, hash) assuming key is not already present and the table has
 * room to keep the load factor <= 0.5 after the insert (caller's job). */
static void rsxc_chunk_insert_raw(rsxc_chunk_table* t, u32 key, u64 hash)
{
    const u32 mask = t->capacity - 1u;
    u32 idx = rsxc_chunk_mix(key) & mask;
    while (t->occ[idx]) idx = (idx + 1u) & mask;
    t->occ[idx] = 1u;
    t->keys[idx] = key;
    t->hashes[idx] = hash;
    t->count++;
}

/* Doubles capacity and rehashes every occupied slot. Returns 0 on malloc
 * failure, leaving t completely unchanged (still valid at its old
 * capacity). */
static int rsxc_chunk_grow(rsxc_chunk_table* t)
{
    const u32 new_cap = t->capacity * 2u;
    rsxc_chunk_table* nt = rsxc_chunk_table_alloc(new_cap);
    if (!nt) return 0;
    for (u32 i = 0; i < t->capacity; i++) {
        if (t->occ[i]) rsxc_chunk_insert_raw(nt, t->keys[i], t->hashes[i]);
    }
    free(t->keys);
    free(t->hashes);
    free(t->occ);
    t->keys = nt->keys;
    t->hashes = nt->hashes;
    t->occ = nt->occ;
    t->capacity = nt->capacity;
    t->count = nt->count;
    free(nt);
    return 1;
}

int rsxc_chunks_changed(rsxc_chunk_table* t, u32 chunk_ea, u64 hash)
{
    if (!t) return 1;   /* no table (e.g. rsxc_chunks_new() failed): always emit */
    const u32 key = chunk_ea / RSXC_CHUNK;
    u32 mask = t->capacity - 1u;
    u32 idx = rsxc_chunk_mix(key) & mask;
    while (t->occ[idx]) {
        if (t->keys[idx] == key) {
            if (t->hashes[idx] == hash) return 0;
            t->hashes[idx] = hash;
            return 1;
        }
        idx = (idx + 1u) & mask;
    }
    /* Not found: a new chunk. Grow first if inserting it would push the
     * load factor above 0.5; if growth fails, still report "changed" (the
     * caller must emit the range) but leave the table as-is -- it stays
     * usable, this chunk just won't be cached and will report "changed"
     * again next time, which is safe, only less efficient. */
    if ((t->count + 1u) * 2u > t->capacity) {
        if (!rsxc_chunk_grow(t)) return 1;
    }
    rsxc_chunk_insert_raw(t, key, hash);
    return 1;
}

/* =====================================================================
 * Recorder (Task 2): PS3_RSX_CAPTURE=<path>:<min_draws>:<frames>.
 *
 * Probe, OFF by default. Every hook starts with one branch on the cached
 * g_rsx_capture_state; with the env unset the state settles at 0 on the first
 * call and no provider, lock or allocation is ever touched again.
 *
 * Threading: presents can come from the guest thread (forced flips in
 * cellGcmSys.c) while the FIFO walker runs on the RSX wake thread, so the
 * writer is guarded by a mutex. The mutex is only taken when the state is
 * armed/recording (1/2) -- the OFF (0) and DONE (3) paths stay lock-free.
 * ===================================================================== */
#include "rsx_commands.h"   /* rsx_state (sizeof for the header ABI guard), rsx_backend */
#include "rsx_capture_ranges.h"

#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION s_rc_cs;
static volatile LONG s_rc_cs_state = 0;   /* 0=uninit 1=initializing 2=ready */
static void rc_lock(void)
{
    if (s_rc_cs_state != 2) {
        if (InterlockedCompareExchange(&s_rc_cs_state, 1, 0) == 0) {
            InitializeCriticalSection(&s_rc_cs);
            s_rc_cs_state = 2;
        } else {
            while (s_rc_cs_state != 2) Sleep(0);
        }
    }
    EnterCriticalSection(&s_rc_cs);
}
static void rc_unlock(void) { LeaveCriticalSection(&s_rc_cs); }
#else
#include <pthread.h>
static pthread_mutex_t s_rc_mtx = PTHREAD_MUTEX_INITIALIZER;
static void rc_lock(void)   { pthread_mutex_lock(&s_rc_mtx); }
static void rc_unlock(void) { pthread_mutex_unlock(&s_rc_mtx); }
#endif

extern uint8_t* vm_base;   /* runtime/memory/vm.h: host_ptr = vm_base + guest_addr */

/* ppu_memory.h / spu_dma.h / cellGcmSys.c: is [addr, addr+n) inside a
 * registered committed range? s_config.localSize is hardcoded to 256 MB
 * (cellGcmSys.c) but the host only commits PS3_VM_RSX_MB of RSX local
 * memory -- the rest (and, if not fully committed, the IO window) is
 * PROT_NONE. Reading it would SIGBUS the game, so every page the walk below
 * touches must be checked first. */
extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);

enum { RC_UNREAD = -1, RC_OFF = 0, RC_ARMED = 1, RC_RECORDING = 2, RC_DONE = 3 };

volatile int g_rsx_capture_state = RC_UNREAD;

#define RC_HASH_NONE  0xFFFFFFFFFFFFFFFFull  /* page never seen */
#define RC_HASH_DIRTY 0xFFFFFFFFFFFFFFFEull  /* a RANGE changed the replay's copy: rewrite the PAGE */

static char         s_rc_path[1024];
static u32          s_rc_min_draws, s_rc_frames, s_rc_frames_done;
static u32          s_rc_prev_logical;          /* rsx_stats_logical() at the last present */
static rsxc_writer* s_rc_w;
static u64*         s_rc_hash;                  /* local pages then io-window pages */
static u32          s_rc_local_addr, s_rc_local_pages, s_rc_io_addr, s_rc_io_pages;
static u64          s_rc_zero_hash;
static u32          s_rc_pages_written, s_rc_methods;
static u32          s_rc_pages_skipped;             /* uncommitted pages the walk did not touch */
static u32          s_rc_forced;                    /* forced presents while RECORDING (finding 2) */
static rsx_capture_dump_fn s_rc_dump;

/* --- Task 3 (V2.0b): backend interposer ---------------------------------
 * While RECORDING, the registered backend is replaced by s_rc_wrap: a copy of
 * it (same userdata, same callbacks) whose set_shader / bind_texture /
 * draw_arrays / draw_indexed first emit, as RANGE records, the guest memory
 * the real callback is about to read, then forward to s_rc_real. The walker
 * records a method AFTER its dispatch (cellGcmSys.c), so every RANGE a
 * method's dispatch emitted precedes that method in the file. s_rc_real is
 * never cleared: a callback already in flight through the wrapper when the
 * capture closes still forwards to the real backend. */
extern int gcm_io_offset_to_ea(u32 ioOffset, u32* out_ea);   /* cellGcmSys.c */
static rsx_backend        s_rc_real;          /* copy of the backend registered at rc_start */
static rsx_backend        s_rc_wrap;          /* s_rc_real with the four read callbacks wrapped */
static rsx_backend*       s_rc_real_ptr;      /* the pointer rc_start replaced (re-registered at close) */
static int                s_rc_wrapped;       /* s_rc_wrap is the registered backend */
static rsxc_chunk_table*  s_rc_chunks;        /* chunk ea -> hash of what the REPLAY holds there */
static u32                s_rc_ranges_written, s_rc_chunks_skipped;
static int                s_rc_vbase = -1;    /* metal_vertex_base_on(): PS3_RSX_VERTEX_BASE, '0' = off */

/* Splits "<path>:<min>:<frames>" at the last two ':' (a Windows drive letter
 * keeps its colon). Returns 1 on success. */
static int rc_parse(const char* e)
{
    const char* c2 = strrchr(e, ':');
    if (!c2 || c2 == e) return 0;
    const char* c1 = c2 - 1;
    while (c1 > e && *c1 != ':') c1--;
    if (*c1 != ':' || c1 == e) return 0;
    const size_t n = (size_t)(c1 - e);
    if (n >= sizeof s_rc_path) return 0;
    char* end = NULL;
    const unsigned long md = strtoul(c1 + 1, &end, 10);
    if (end != c2) return 0;
    const unsigned long fr = strtoul(c2 + 1, &end, 10);
    if (*end != '\0' || fr == 0 || fr > 100000ul || md > 0xFFFFFFFFul) return 0;
    memcpy(s_rc_path, e, n);
    s_rc_path[n] = '\0';
    s_rc_min_draws = (u32)md;
    s_rc_frames = (u32)fr;
    return 1;
}

int rsx_capture_enabled(void)
{
    int st = g_rsx_capture_state;
    if (st == RC_UNREAD) {
        const char* e = getenv("PS3_RSX_CAPTURE");
        if (!e || !*e) st = RC_OFF;
        else if (!rc_parse(e)) {
            fprintf(stderr, "[RSXCAP] invalid PS3_RSX_CAPTURE='%s' (want <path>:<min_draws>:<frames>) -- capture off\n", e);
            st = RC_OFF;
        } else {
            fprintf(stderr, "[RSXCAP] armed: path=%s min_draws=%u frames=%u\n",
                    s_rc_path, s_rc_min_draws, s_rc_frames);
            st = RC_ARMED;
        }
        g_rsx_capture_state = st;
    }
    return st != RC_OFF;
}

void rsx_capture_set_dump_hook(rsx_capture_dump_fn fn) { s_rc_dump = fn; }

void rsx_capture_reset_for_tests(void)
{
    g_rsx_capture_state = RC_UNREAD;
    s_rc_w = NULL;
    free(s_rc_hash);
    s_rc_hash = NULL;
    s_rc_frames_done = s_rc_prev_logical = s_rc_pages_written = s_rc_methods = 0;
    s_rc_pages_skipped = s_rc_forced = 0;
    if (s_rc_wrapped && rsx_get_backend() == &s_rc_wrap) rsx_set_backend(s_rc_real_ptr);
    s_rc_wrapped = 0;
    s_rc_real_ptr = NULL;
    rsxc_chunks_free(s_rc_chunks);
    s_rc_chunks = NULL;
    s_rc_ranges_written = s_rc_chunks_skipped = 0;
}

static void rc_emit_display(u32 id, u32 off, u32 pitch, u32 w, u32 h)
{
    if (s_rc_w) rsxc_write_display(s_rc_w, id, off, pitch, w, h);
}

/* Aborts the capture (I/O error): closes what was written and goes DONE. */
/* Re-registers the real backend (lock held). Only when the wrapper is still
 * the registered one: a backend swapped in meanwhile (shutdown, fallback)
 * is left alone. */
static void rc_uninstall(void)
{
    if (s_rc_wrapped) {
        if (rsx_get_backend() == &s_rc_wrap)
            rsx_set_backend(s_rc_real_ptr);
        else
            fprintf(stderr, "[RSXCAP] WARNING: backend replaced while recording (interposer dropped) -- "
                            "capture may lack RANGE records (%s)\n", s_rc_path);
    }
    s_rc_wrapped = 0;
}

/* Everything a finished or aborted capture frees (lock held). */
static void rc_teardown(void)
{
    rc_uninstall();
    free(s_rc_hash);
    s_rc_hash = NULL;
    rsxc_chunks_free(s_rc_chunks);
    s_rc_chunks = NULL;
}

static void rc_fail(const char* what)
{
    fprintf(stderr, "[RSXCAP] %s -- capture aborted (%s)\n", what, s_rc_path);
    if (s_rc_w) rsxc_close_write(s_rc_w, s_rc_frames_done);
    s_rc_w = NULL;
    g_rsx_capture_state = RC_DONE;
    rc_teardown();
}

static void rc_install(void);

/* Called with the lock held, state ARMED. Returns 1 when now RECORDING. */
static int rc_start(void)
{
    u32 io_addr = 0, io_size = 0, local_addr = 0, local_size = 0;
    const u16* io_to_ea = NULL;
    gcm_capture_io_info(&io_addr, &io_size, &io_to_ea);
    gcm_capture_local_info(&local_addr, &local_size);
    rsxc_header h;
    h.magic = RSXC_MAGIC;
    h.version = RSXC_VERSION;
    h.state_size = (u32)sizeof(rsx_state);
    h.io_address = io_addr;
    h.io_size = io_size;
    h.local_address = local_addr;
    h.local_size = local_size;
    h.page_size = RSXC_PAGE;
    s_rc_local_addr = local_addr;
    s_rc_local_pages = local_size / RSXC_PAGE;
    s_rc_io_addr = io_addr;
    s_rc_io_pages = io_addr ? io_size / RSXC_PAGE : 0u;
    const size_t n = (size_t)s_rc_local_pages + s_rc_io_pages;
    s_rc_hash = (u64*)malloc((n ? n : 1u) * sizeof(u64));
    if (!s_rc_hash) { rc_fail("out of memory for the page-hash table"); return 0; }
    for (size_t i = 0; i < n; i++) s_rc_hash[i] = RC_HASH_NONE;
    {
        u8* z = (u8*)calloc(1, RSXC_PAGE);
        if (!z) { rc_fail("out of memory"); return 0; }
        s_rc_zero_hash = rsxc_hash_page(z);
        free(z);
    }
    s_rc_w = rsxc_open_write(s_rc_path, &h);
    if (!s_rc_w) { rc_fail("cannot create the capture file"); return 0; }
    if (io_to_ea) rsxc_write_eatable(s_rc_w, io_to_ea);
    gcm_capture_display_table(rc_emit_display);
    s_rc_frames_done = 0;
    s_rc_pages_skipped = s_rc_forced = 0;
    s_rc_ranges_written = s_rc_chunks_skipped = 0;
    /* NULL on OOM is fine: rsxc_chunks_changed(NULL, ...) always reports
     * "changed", so every read chunk is emitted (bigger file, never short). */
    s_rc_chunks = rsxc_chunks_new();
    if (s_rc_vbase < 0) {
        const char* e = getenv("PS3_RSX_VERTEX_BASE");
        s_rc_vbase = (e && e[0] == '0') ? 0 : 1;
    }
    g_rsx_capture_state = RC_RECORDING;
    rc_install();
    fprintf(stderr, "[RSXCAP] recording: prev_frame_draws=%u local=0x%08X+0x%X io=0x%08X+0x%X state_size=%u skipped=%u\n",
            s_rc_prev_logical, local_addr, local_size, io_addr, io_size, h.state_size, s_rc_pages_skipped);
    return 1;
}

/* Writes the pages of [ea, ea + npages*RSXC_PAGE) whose content changed since
 * the last snapshot (first sight: only non-zero pages; the replay starts from
 * zeroed memory). hash_base indexes s_rc_hash. */
static void rc_snapshot_range(u32 ea, u32 npages, u32 hash_base)
{
    for (u32 i = 0; i < npages; i++) {
        const u32 pea = ea + i * RSXC_PAGE;
        if (!ppu_guest_range_committed(pea, RSXC_PAGE)) {
            /* Not backed by host memory (PROT_NONE tail past PS3_VM_RSX_MB, or
             * an IO window not fully committed): touching vm_base here would
             * SIGBUS. Leave the hash slot alone (RC_HASH_NONE if never seen)
             * so a later frame that finds the page committed treats it as a
             * first sight, same as any other page. */
            s_rc_pages_skipped++;
            continue;
        }
        /* Copy first, then hash the copy: the guest may write the page
         * concurrently, and the page slot, the chunk-table seeds and the
         * PAGE bytes must all describe the same bytes -- the ones the replay
         * will hold. */
        static u8 s_pagebuf[RSXC_PAGE];
        memcpy(s_pagebuf, vm_base + pea, RSXC_PAGE);
        const u64 hv = rsxc_hash_page(s_pagebuf);
        u64* slot = &s_rc_hash[hash_base + i];
        /* RC_HASH_DIRTY: a RANGE rewrote part of this page in the replay since
         * its last PAGE, so the replay no longer holds what the slot says --
         * always rewrite (test page_rewritten_after_range). */
        const int write = (*slot == RC_HASH_NONE)  ? (hv != s_rc_zero_hash)
                        : (*slot == RC_HASH_DIRTY) ? 1
                        : (hv != *slot);
        *slot = hv;
        if (write) {
            /* After this PAGE the replay holds these bytes, so a later RANGE
             * is only needed where memory differs from them (a chunk reverted
             * to the bytes of an earlier RANGE must still be emitted -- test
             * range_after_page_snapshot). */
            rsxc_write_page(s_rc_w, pea, s_pagebuf);
            s_rc_pages_written++;
            for (u32 c = 0; c < RSXC_PAGE / RSXC_CHUNK; c++)
                (void)rsxc_chunks_changed(s_rc_chunks, pea + c * RSXC_CHUNK,
                                          rsxc_hash_bytes(s_pagebuf + c * RSXC_CHUNK, RSXC_CHUNK));
        }
    }
}

void rsx_capture_on_begin_frame(void)
{
    if (g_rsx_capture_state == RC_OFF || g_rsx_capture_state == RC_DONE) return;
    if (!rsx_capture_enabled()) return;
    rc_lock();
    if (g_rsx_capture_state == RC_ARMED && s_rc_prev_logical >= s_rc_min_draws)
        (void)rc_start();
    if (g_rsx_capture_state == RC_RECORDING && s_rc_w) {
        const struct rsx_state* st = gcm_capture_rsx_state();
        if (rsxc_write_frame(s_rc_w, s_rc_frames_done, (u32)rsx_commands_pending_batch(),
                             st, (u32)sizeof(rsx_state)) != 0) {
            rc_fail("write error (FRAME)");
        } else if (vm_base) {
            rc_snapshot_range(s_rc_local_addr, s_rc_local_pages, 0u);
            if (s_rc_io_pages) rc_snapshot_range(s_rc_io_addr, s_rc_io_pages, s_rc_local_pages);
        }
    }
    rc_unlock();
}

void rsx_capture_on_method(u32 subch, u32 method, u32 value)
{
    if (g_rsx_capture_state != RC_RECORDING) return;   /* OFF/armed/done: one branch */
    rc_lock();
    if (g_rsx_capture_state == RC_RECORDING && s_rc_w) {
        rsxc_write_method(s_rc_w, subch, method, value);
        s_rc_methods++;
    }
    rc_unlock();
}

void rsx_capture_on_display_buffer(u32 id, u32 offset, u32 pitch, u32 width, u32 height)
{
    if (g_rsx_capture_state != RC_RECORDING) return;
    rc_lock();
    if (g_rsx_capture_state == RC_RECORDING && s_rc_w)
        rsxc_write_display(s_rc_w, id, offset, pitch, width, height);
    rc_unlock();
}

/* Finding 2: hashing up to ~300 MB per captured frame can outlast
 * PS3_GCM_FLIP_WAIT_MS and trigger the forced-present path in cellGcmSys.c,
 * which shifts frame boundaries relative to an uncaptured run -- a capture
 * with forced presents is not a valid baseline for live-vs-replay
 * comparison. Call sites guard with the same cheap `g_rsx_capture_state == 2`
 * check used for rsx_capture_on_method, so this costs nothing when off. */
void rsx_capture_note_forced_present(void)
{
    if (g_rsx_capture_state != RC_RECORDING) return;
    rc_lock();
    if (g_rsx_capture_state == RC_RECORDING) s_rc_forced++;
    rc_unlock();
}

void rsx_capture_on_present(u32 buffer_id)
{
    if (g_rsx_capture_state == RC_OFF || g_rsx_capture_state == RC_DONE) return;
    if (!rsx_capture_enabled()) return;
    int finished = 0;
    rc_lock();
    if (g_rsx_capture_state == RC_ARMED) {
        /* The backend's begin_frame resets the per-frame counter, so the arm
         * test reads the finished frame's draws here, at its present. */
        s_rc_prev_logical = rsx_stats_logical();
    } else if (g_rsx_capture_state == RC_RECORDING && s_rc_w) {
        rsxc_write_present(s_rc_w, buffer_id);
        if (++s_rc_frames_done >= s_rc_frames) {
            const int rc = rsxc_close_write(s_rc_w, s_rc_frames_done);
            s_rc_w = NULL;
            g_rsx_capture_state = RC_DONE;
            rc_teardown();   /* re-registers the real backend */
            long long bytes = -1;
            FILE* f = fopen(s_rc_path, "rb");
            if (f) {
                if (_fseeki64(f, 0, SEEK_END) == 0) bytes = (long long)_ftelli64(f);
                fclose(f);
            }
            fprintf(stderr, "[RSXCAP] done: path=%s frames=%u pages=%u ranges=%u methods=%u skipped=%u "
                    "skipped_chunks=%u forced=%u bytes=%lld%s\n",
                    s_rc_path, s_rc_frames_done, s_rc_pages_written, s_rc_ranges_written, s_rc_methods,
                    s_rc_pages_skipped, s_rc_chunks_skipped, s_rc_forced, bytes, rc ? " (CLOSE ERROR)" : "");
            if (s_rc_forced > 0u)
                fprintf(stderr, "[RSXCAP] WARNING: capture not a valid baseline (forced flips)\n");
            finished = 1;
        }
    }
    rc_unlock();
    if (finished && s_rc_dump) {
        char prefix[1100];
        snprintf(prefix, sizeof prefix, "%s", s_rc_path);
        char* slash = strrchr(prefix, '/');
        char* bslash = strrchr(prefix, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        char* dot = strrchr(prefix, '.');
        if (dot && (!slash || dot > slash)) *dot = '\0';
        strncat(prefix, "_live", sizeof prefix - strlen(prefix) - 1);
        const int dr = s_rc_dump(prefix);
        fprintf(stderr, "[RSXCAP] live dump prefix=%s rc=%d\n", prefix, dr);
    }
}

/* =====================================================================
 * Task 3 (V2.0b): the backend interposer.
 *
 * Lock order: a wrapper takes the recorder lock only around the emission and
 * releases it BEFORE forwarding to the real backend, so no backend code ever
 * runs under the recorder lock (the real present/begin_frame paths call
 * rsx_capture_on_present / rsx_capture_on_begin_frame, which take it). The
 * recorder lock is a leaf: nothing is called while holding it except the
 * writer, the range calculators, ppu_guest_range_committed and the hashers.
 * ===================================================================== */

/* Emits every committed RSXC_CHUNK-aligned chunk of [ea, ea+len) whose
 * content differs from what the replay holds (lock held, RECORDING). Whole
 * chunks are a superset of the span, which is safe. Uncommitted chunks are
 * never read in full.
 *
 * A chunk can straddle the end of the one committed range Metal actually
 * read from, or straddle two adjacent committed ranges (ppu_guest_range_committed
 * only answers true when the WHOLE probed range lies inside a single
 * registered range). Skipping such a chunk outright would silently drop
 * bytes the live backend did read. So when the full chunk is not committed,
 * fall back to the sub-span actually requested by the caller (span ∩ chunk):
 * if THAT sub-span is wholly committed, emit it verbatim as its own RANGE
 * record (the format allows len in [1, RSXC_CHUNK]). Otherwise the chunk is
 * genuinely unreadable and stays skipped. */
static long rc_page_slot(u32 ea);
static void rc_emit_span(u32 ea, u32 len)
{
    if (!len || !s_rc_w || !vm_base) return;
    u64 end = (u64)ea + len;
    if (end > 0x100000000ull) end = 0x100000000ull;
    for (u64 c = (u64)(ea & ~(RSXC_CHUNK - 1u)); c < end; c += RSXC_CHUNK) {
        const u32 cea = (u32)c;
        if (ppu_guest_range_committed(cea, RSXC_CHUNK)) {
            u8 buf[RSXC_CHUNK];   /* copy: hash and write the same bytes */
            memcpy(buf, vm_base + cea, RSXC_CHUNK);
            if (!rsxc_chunks_changed(s_rc_chunks, cea, rsxc_hash_bytes(buf, RSXC_CHUNK))) continue;
            if (rsxc_write_range(s_rc_w, cea, buf, RSXC_CHUNK) != 0) { rc_fail("write error (RANGE)"); return; }
            s_rc_ranges_written++;
            /* The replay's copy of the containing page now differs from its
             * last PAGE: force a PAGE rewrite at the next snapshot, so
             * readers the interposer does not see (blit_scaled, 2D) start
             * the next frame from live bytes. */
            const long ps = rc_page_slot(cea);
            if (ps >= 0) s_rc_hash[ps] = RC_HASH_DIRTY;
            continue;
        }
        /* Full chunk not committed: fall back to the clipped sub-span the
         * caller actually asked for. */
        const u64 c_end = c + RSXC_CHUNK;
        const u64 s_lo = c > (u64)ea ? c : (u64)ea;
        const u64 s_hi = c_end < end ? c_end : end;
        if (s_hi <= s_lo) { s_rc_chunks_skipped++; continue; }
        const u32 sea = (u32)s_lo;
        const u32 slen = (u32)(s_hi - s_lo);
        if (!ppu_guest_range_committed(sea, slen)) { s_rc_chunks_skipped++; continue; }
        u8 buf[RSXC_CHUNK];
        memcpy(buf, vm_base + sea, slen);
        /* No chunk-hash dedupe here: the hash table's key is per full
         * RSXC_CHUNK-aligned chunk, and this is a partial emission of one --
         * recording it under the full-chunk key would make a later full-chunk
         * read of the same chunk wrongly look unchanged. Always emit; a
         * duplicate partial RANGE costs a few bytes, never correctness. */
        if (rsxc_write_range(s_rc_w, sea, buf, slen) != 0) { rc_fail("write error (RANGE)"); return; }
        s_rc_ranges_written++;
        const long ps = rc_page_slot(sea);
        if (ps >= 0) s_rc_hash[ps] = RC_HASH_DIRTY;
    }
}

/* s_rc_hash index of the RSXC_PAGE containing ea (local window, then IO
 * window), or -1 when ea is in neither (no PAGE snapshot covers it). */
static long rc_page_slot(u32 ea)
{
    if (!s_rc_hash) return -1;
    if (ea >= s_rc_local_addr && (u64)(ea - s_rc_local_addr) < (u64)s_rc_local_pages * RSXC_PAGE)
        return (long)((ea - s_rc_local_addr) / RSXC_PAGE);
    if (s_rc_io_pages && ea >= s_rc_io_addr && (u64)(ea - s_rc_io_addr) < (u64)s_rc_io_pages * RSXC_PAGE)
        return (long)s_rc_local_pages + (long)((ea - s_rc_io_addr) / RSXC_PAGE);
    return -1;
}

static int rc_io2ea(u32 io_off, u32* ea) { return gcm_io_offset_to_ea(io_off, ea); }

/* FP ucode: metal_set_shader reads it at set_shader and again at every draw
 * (inline constants patched per draw). Lock held. */
static void rc_emit_fp(const rsx_state* st)
{
    rsxc_span sp;
    if (st && s_rc_w && rsxc_fp_span(st, vm_base, rc_io2ea, &sp)) rc_emit_span(sp.ea, sp.len);
}

static void rc_emit_vertices(const rsx_state* st, u32 first, u32 count)
{
    rsxc_span sp[16];
    const int n = rsxc_vertex_spans(st, first, count, s_rc_vbase, rc_io2ea, sp);
    for (int i = 0; i < n && s_rc_w; i++) rc_emit_span(sp[i].ea, sp[i].len);
}

static void rc_wrap_set_shader(void* ud, const rsx_state* st)
{
    if (g_rsx_capture_state == RC_RECORDING) {
        rc_lock();
        if (g_rsx_capture_state == RC_RECORDING) rc_emit_fp(st);
        rc_unlock();
    }
    s_rc_real.set_shader(ud, st);
}

static void rc_wrap_bind_texture(void* ud, u32 unit, const rsx_texture_state* tex)
{
    if (g_rsx_capture_state == RC_RECORDING) {
        rc_lock();
        rsxc_span sp;
        /* Every bind emits its span through rc_emit_span: the CPU/SPU can
         * rewrite a texture between two binds in the same frame, and the
         * Metal backend re-reads on any cache miss (PS3_METAL_TEX_CACHE=0,
         * per-draw-RT off, mid-frame metal_texc_victim eviction), so a
         * per-frame "already emitted this key" filter could leave the
         * replay with stale bytes for the second bind. rc_emit_span's own
         * 4 KiB chunk-hash dedupe already suppresses unchanged repeats. */
        if (g_rsx_capture_state == RC_RECORDING && s_rc_w && vm_base &&
            rsxc_tex_span(tex, rc_io2ea, &sp)) {
            rc_emit_span(sp.ea, sp.len);
        }
        rc_unlock();
    }
    s_rc_real.bind_texture(ud, unit, tex);
}

static void rc_wrap_draw_arrays(void* ud, u32 prim, u32 first, u32 count)
{
    if (g_rsx_capture_state == RC_RECORDING) {
        rc_lock();
        if (g_rsx_capture_state == RC_RECORDING && s_rc_w && vm_base) {
            const rsx_state* st = (const rsx_state*)gcm_capture_rsx_state();
            if (st) {
                rc_emit_fp(st);
                if (s_rc_w) rc_emit_vertices(st, first, count);
            }
        }
        rc_unlock();
    }
    s_rc_real.draw_arrays(ud, prim, first, count);
}

static void rc_wrap_draw_indexed(void* ud, u32 prim, u32 offset, u32 count)
{
    if (g_rsx_capture_state == RC_RECORDING) {
        rc_lock();
        if (g_rsx_capture_state == RC_RECORDING && s_rc_w && vm_base) {
            const rsx_state* st = (const rsx_state*)gcm_capture_rsx_state();
            if (st) {
                rc_emit_fp(st);
                rsxc_span isp;
                u32 vfirst = 0, nvert = 0;
                if (s_rc_w && rsxc_index_span(st, offset, count, vm_base, s_rc_vbase, rc_io2ea,
                                              &isp, &vfirst, &nvert)) {
                    rc_emit_span(isp.ea, isp.len);
                    if (s_rc_w) rc_emit_vertices(st, vfirst, nvert);
                }
            }
        }
        rc_unlock();
    }
    s_rc_real.draw_indexed(ud, prim, offset, count);
}

/* Lock held, state just set to RECORDING. Wraps only the callbacks the real
 * backend implements (a NULL callback stays NULL: the caller's own NULL
 * checks keep working). */
static void rc_install(void)
{
    rsx_backend* real = rsx_get_backend();
    if (!real || real == &s_rc_wrap) return;
    s_rc_real = *real;
    s_rc_real_ptr = real;
    s_rc_wrap = *real;
    if (real->set_shader)   s_rc_wrap.set_shader   = rc_wrap_set_shader;
    if (real->bind_texture) s_rc_wrap.bind_texture = rc_wrap_bind_texture;
    if (real->draw_arrays)  s_rc_wrap.draw_arrays  = rc_wrap_draw_arrays;
    if (real->draw_indexed) s_rc_wrap.draw_indexed = rc_wrap_draw_indexed;
    rsx_set_backend(&s_rc_wrap);
    s_rc_wrapped = 1;
}
