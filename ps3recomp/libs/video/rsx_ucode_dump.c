#include "rsx_ucode_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Single-threaded caller: metal_set_shader() (and therefore rsx_ucode_dump())
 * only ever runs on the game/PPU thread that parses the RSX FIFO synchronously
 * (rsx_commands.c:1064). No lock is taken here on purpose.
 */

static int  s_state = -1;          /* -1 unread, 0 off, 1 on */
static char s_dir[512];

/* In-memory "already handled this hash" sets, open-addressing with linear
 * probing. Hash 0 is a valid key (an empty/degenerate program), so presence
 * is tracked with a separate occupancy flag per slot rather than a sentinel
 * key value. Capacity is a power of two so `key & (CAP-1)` is a cheap probe
 * start and the +1 linear step still visits every slot before wrapping.
 *
 * This turns the per-set_shader-call cost back into what it should be: a
 * handful of array reads on the hot path, and a filesystem fopen() only the
 * first time a given (fp|vp) hash is ever seen in the process -- not once
 * per draw call forever (measured: 642/1232 MAIN-thread samples in
 * __open_nocancel with the old always-fopen version, audible stutter).
 */
#define UCODE_SEEN_CAP 4096u /* power of two */

typedef struct {
    u32           key[UCODE_SEEN_CAP];
    unsigned char used[UCODE_SEEN_CAP];
    u32           count;
    int           full_warned;
} ucode_seen_set;

static ucode_seen_set s_fp_seen;
static ucode_seen_set s_vp_seen;

static void seen_reset(ucode_seen_set* s)
{
    memset(s->used, 0, sizeof s->used);
    s->count = 0;
    s->full_warned = 0;
}

/* Returns 1 if `key` was not seen before (now recorded -- caller should do
 * the filesystem write), 0 if it was already seen, or if the set is full
 * (caller must NOT touch the filesystem in that case either). */
static int seen_test_and_set(ucode_seen_set* s, u32 key, const char* kind)
{
    const u32 mask = UCODE_SEEN_CAP - 1u;
    u32 idx = key & mask;
    for (u32 probe = 0; probe < UCODE_SEEN_CAP; probe++) {
        u32 i = (idx + probe) & mask;
        if (!s->used[i]) {
            s->used[i] = 1;
            s->key[i]  = key;
            s->count++;
            return 1;
        }
        if (s->key[i] == key)
            return 0;
    }
    /* Full: every slot occupied by a different key. Never fall back to
     * per-call fopen() -- warn once and stop dumping new hashes of this kind. */
    if (!s->full_warned) {
        s->full_warned = 1;
        fprintf(stderr,
            "[RSX] PS3_RSX_DUMP_UCODE: %s set full, further programs not dumped\n",
            kind);
    }
    return 0;
}

void rsx_ucode_dump_reset_for_test(void)
{
    s_state = -1;
    seen_reset(&s_fp_seen);
    seen_reset(&s_vp_seen);
}

static int dump_on(void)
{
    if (s_state < 0) {
        const char* e = getenv("PS3_RSX_DUMP_UCODE");
        s_state = (e && *e && strlen(e) < sizeof(s_dir) - 32) ? 1 : 0;
        if (s_state) {
            snprintf(s_dir, sizeof s_dir, "%s", e);
            fprintf(stderr, "[RSX] PS3_RSX_DUMP_UCODE -> %s\n", s_dir);
        }
    }
    return s_state;
}

/* Write once per path; an existing file (or an unwritable dir) is left alone.
 * Only reached once per unique hash per process now (gated by seen_test_and_set
 * above), which also means files a PREVIOUS process run already wrote are left
 * intact instead of being fopen()-probed on every draw. */
static void write_once(const char* path, const void* data, size_t n)
{
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return; }
    f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, n, f);
    fclose(f);
}

void rsx_ucode_dump(const u8* fp, u32 fp_bytes, u32 fp_hash,
                    const u32* vp_words, u32 vp_nwords, u32 vp_hash)
{
    if (!dump_on()) return;
    char path[600];
    if (fp && fp_bytes && seen_test_and_set(&s_fp_seen, fp_hash, "fp")) {
        snprintf(path, sizeof path, "%s/%08X.fp", s_dir, fp_hash);
        write_once(path, fp, fp_bytes);
    }
    if (vp_words && vp_nwords && seen_test_and_set(&s_vp_seen, vp_hash, "vp")) {
        u8 buf[512 * 16];
        u32 n = vp_nwords * 4u;
        if (n > sizeof buf) n = sizeof buf;
        for (u32 i = 0; i < n / 4u; i++) {
            u32 w = vp_words[i];
            buf[i * 4 + 0] = (u8)w;         buf[i * 4 + 1] = (u8)(w >> 8);
            buf[i * 4 + 2] = (u8)(w >> 16); buf[i * 4 + 3] = (u8)(w >> 24);
        }
        snprintf(path, sizeof path, "%s/%08X.vp", s_dir, vp_hash);
        write_once(path, buf, n);
    }
}
