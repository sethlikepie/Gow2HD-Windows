/*
 * Lock-free snapshot of the movie_io WAD stream, for diagnostics readers
 * (the runtime overlay) that must never wait on the movie_io mutex: that mutex
 * is held across fread, and the overlay reads from the window/event thread.
 *
 * One writer at a time: every movie_io_status_* writer call is made while the
 * caller holds its own movie_io lock. Readers use the sequence counter and
 * retry; the name is stored byte-wise through relaxed atomics so a concurrent
 * write never makes the copy a data race. Nothing here points at guest memory.
 *
 * Header-only so every host that compiles movie_hle.c gets it without a new
 * translation unit in its build list.
 */
#ifndef PS3RECOMP_MOVIE_IO_STATUS_H
#define PS3RECOMP_MOVIE_IO_STATUS_H

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define MOVIE_IO_STATUS_NAME_CAPACITY 48u

typedef struct movie_io_wad_view {
    int seen;                 /* a WAD was opened through movie_io this session */
    int open;                 /* that most recent WAD is still open */
    unsigned open_wads;       /* WAD slots open right now */
    unsigned total_opens;     /* WAD opens this session */
    unsigned long long size;  /* bytes, most recent WAD */
    unsigned long long bytes_read; /* bytes delivered from it (re-reads count) */
    char name[MOVIE_IO_STATUS_NAME_CAPACITY]; /* basename, NUL-terminated */
} movie_io_wad_view;

typedef struct movie_io_status_store {
    atomic_uint sequence;
    atomic_int seen;
    atomic_int open;
    atomic_int slot;          /* movie_io slot of the most recent WAD */
    atomic_uint open_wads;
    atomic_uint total_opens;
    atomic_ullong size;
    atomic_ullong bytes_read;
    atomic_uchar name[MOVIE_IO_STATUS_NAME_CAPACITY];
} movie_io_status_store;

/* movie_hle.c: the process-wide WAD stream snapshot. */
int movie_io_wad_status(movie_io_wad_view* out);

static inline void movie_io_status_begin_(movie_io_status_store* s)
{
    atomic_fetch_add_explicit(&s->sequence, 1u, memory_order_seq_cst);
}

static inline void movie_io_status_end_(movie_io_status_store* s)
{
    atomic_fetch_add_explicit(&s->sequence, 1u, memory_order_seq_cst);
}

static inline const char* movie_io_status_basename_(const char* path)
{
    const char* base = path;
    const char* p;
    for (p = path; *p; ++p)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    return base;
}

/* A WAD opened in movie_io slot `slot`. */
static inline void movie_io_status_open(movie_io_status_store* s, int slot,
                                        const char* path, unsigned long long size)
{
    const char* base = movie_io_status_basename_(path ? path : "");
    size_t i = 0;

    movie_io_status_begin_(s);
    for (; base[i] && i + 1 < MOVIE_IO_STATUS_NAME_CAPACITY; ++i)
        atomic_store_explicit(&s->name[i], (unsigned char)base[i], memory_order_relaxed);
    atomic_store_explicit(&s->name[i], 0u, memory_order_relaxed);
    atomic_store_explicit(&s->slot, slot, memory_order_relaxed);
    atomic_store_explicit(&s->size, size, memory_order_relaxed);
    atomic_store_explicit(&s->bytes_read, 0ull, memory_order_relaxed);
    atomic_store_explicit(&s->open, 1, memory_order_relaxed);
    atomic_store_explicit(&s->seen, 1, memory_order_relaxed);
    atomic_store_explicit(&s->open_wads,
        atomic_load_explicit(&s->open_wads, memory_order_relaxed) + 1u, memory_order_relaxed);
    atomic_store_explicit(&s->total_opens,
        atomic_load_explicit(&s->total_opens, memory_order_relaxed) + 1u, memory_order_relaxed);
    movie_io_status_end_(s);
}

/* Cumulative bytes delivered from slot `slot`; ignored unless it is the most
 * recent WAD. */
static inline void movie_io_status_read(movie_io_status_store* s, int slot,
                                        unsigned long long bytes_read)
{
    if (!atomic_load_explicit(&s->open, memory_order_relaxed) ||
        atomic_load_explicit(&s->slot, memory_order_relaxed) != slot)
        return;
    movie_io_status_begin_(s);
    atomic_store_explicit(&s->bytes_read, bytes_read, memory_order_relaxed);
    movie_io_status_end_(s);
}

/* A WAD slot closed, with its final byte count. */
static inline void movie_io_status_close(movie_io_status_store* s, int slot,
                                         unsigned long long bytes_read)
{
    unsigned open_wads = atomic_load_explicit(&s->open_wads, memory_order_relaxed);

    movie_io_status_begin_(s);
    if (open_wads > 0)
        atomic_store_explicit(&s->open_wads, open_wads - 1u, memory_order_relaxed);
    if (atomic_load_explicit(&s->open, memory_order_relaxed) &&
        atomic_load_explicit(&s->slot, memory_order_relaxed) == slot) {
        atomic_store_explicit(&s->bytes_read, bytes_read, memory_order_relaxed);
        atomic_store_explicit(&s->open, 0, memory_order_relaxed);
    }
    movie_io_status_end_(s);
}

/* Coherent copy; returns out->seen. Safe from any thread, never blocks on
 * the writer's lock. */
static inline int movie_io_status_read_view(movie_io_status_store* s, movie_io_wad_view* out)
{
    unsigned before;
    size_t i;

    for (;;) {
        before = atomic_load_explicit(&s->sequence, memory_order_seq_cst);
        if (before & 1u)
            continue;
        out->seen = atomic_load_explicit(&s->seen, memory_order_relaxed);
        out->open = atomic_load_explicit(&s->open, memory_order_relaxed);
        out->open_wads = atomic_load_explicit(&s->open_wads, memory_order_relaxed);
        out->total_opens = atomic_load_explicit(&s->total_opens, memory_order_relaxed);
        out->size = atomic_load_explicit(&s->size, memory_order_relaxed);
        out->bytes_read = atomic_load_explicit(&s->bytes_read, memory_order_relaxed);
        for (i = 0; i < MOVIE_IO_STATUS_NAME_CAPACITY; ++i)
            out->name[i] = (char)atomic_load_explicit(&s->name[i], memory_order_relaxed);
        atomic_thread_fence(memory_order_seq_cst);
        if (before == atomic_load_explicit(&s->sequence, memory_order_seq_cst))
            break;
    }
    out->name[MOVIE_IO_STATUS_NAME_CAPACITY - 1] = '\0';
    return out->seen;
}

#endif
