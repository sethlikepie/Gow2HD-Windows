/*
 * ps3recomp - host-side fulfiller for GoW2's FIOS asynchronous read op.
 *
 * The game's bulk asset stream (R_PermA.wad_ps3 and friends) is submitted
 * through a guest FIOS async-read op instead of cellFsRead, so the movie_io
 * host data path in movie_hle.c -- which already has the bytes sitting in the
 * pre-extracted cache -- never gets asked for them. This helper is the seam
 * that lets the patched entry of func_002B3D1C hand the op to the host:
 * pread the bytes out of the cache, drop them in the guest ring the op points
 * at, and complete the op with the same field transitions the guest's own poll
 * (func_002B45D0) already understands.
 *
 * ---------------------------------------------------------------------------
 * OP LAYOUT -- SINGLE SOURCE OF TRUTH, AND NOT FULLY PROVEN YET
 * ---------------------------------------------------------------------------
 * Every offset the helper touches is named here and nowhere else, because the
 * layout below is *indicative*, not measured:
 *
 *   +0x08 status   -- written with the submit result; the poll reads it.
 *                     PROVENANCE: static lift (func_002B3E0C writes it,
 *                     func_002B45D0 reads it) + the Windows hook notes in
 *                     docs/gow2-asset-pipeline-map.md ("op+0x8 = 0 (done)").
 *   +0x0C flags    -- the natural body ORs 0x4000 in ("in-flight"); the poll
 *                     tests bits here. PROVENANCE: static lift only. The
 *                     0x4000 bit is the LEAST proven field in this set.
 *   +0x10 limit    -- end/limit of the op's window. PROVENANCE: static lift.
 *   +0x14 cursor   -- advanced by the byte count. PROVENANCE: static lift +
 *                     the Windows notes ("op+0x14 = pos + n"). Whether this is
 *                     the FILE position or a ring-window cursor is exactly
 *                     what the live PS3_TRACE_AREAD probe (plan Task 2) has to
 *                     settle -- it is used here as the file position, which is
 *                     what the Windows hook did.
 *
 * Call ABI at the hook site: r3 = op EA, r4 = destination EA, r5 = byte count.
 *
 * When the probe measurement lands, correcting the constants below is the only
 * edit the implementation needs. The unit test re-states the same numbers in a
 * single _Static_assert block ("LAYOUT LOCK") so the correction is a two-file,
 * five-line change and a stale layout cannot pass unnoticed.
 *
 * Deliberately NOT gated on the in-flight bit being set: it is the weakest of
 * the four fields, and refusing ops that lack it would make the whole path go
 * silently inert if the probe disagrees. The bit is only cleared (masked off)
 * if present.
 */

#ifndef PS3_FIOS_AREAD_HLE_H
#define PS3_FIOS_AREAD_HLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- op field offsets (see the provenance block above) ------------------ */
#define PS3_AREAD_OP_STATUS      0x08u
#define PS3_AREAD_OP_FLAGS       0x0Cu
#define PS3_AREAD_OP_LIMIT       0x10u
#define PS3_AREAD_OP_CURSOR      0x14u

/* Bytes of the op the helper reads or writes: +0x08 .. +0x17. Used for the
 * guest-VM range check, so it must cover every offset named above. */
#define PS3_AREAD_OP_SPAN        0x18u

#define PS3_AREAD_FLAG_INFLIGHT  0x4000u
#define PS3_AREAD_STATUS_DONE    0u

/* Largest single request the helper will serve. The Windows hook was observed
 * asking for 64 KB - 147 KB per op; anything past a few MB is a garbage op,
 * not a stream read, and gets declined before a byte is moved. */
#define PS3_AREAD_MAX_BYTES      (4u * 1024u * 1024u)

/*
 * Fulfil one async read from the host.
 *
 *   op_ea     guest EA of the op container (r3 at the hook site)
 *   dst_ea    guest EA to fill (r4) -- the game's stream ring
 *   n         bytes requested (r5)
 *   movie_fd  movie_io FD token backing the stream, 0 if unknown
 *
 * Returns the number of bytes written into the guest ring, or 0 meaning "not
 * handled" -- the caller must then fall through to the guest's own body.
 *
 * ALL OR NOTHING. The helper either serves the full (limit-clamped) request
 * and completes the op, or it declines and leaves guest memory BYTE-FOR-BYTE
 * untouched. A read that comes back short or fails is a decline: it never
 * writes a partial ring and never marks the op done, because a synchronous
 * waiter released over a half-filled buffer reads garbage and cannot tell.
 * (Same hazard the sibling lfq_host_inflate fulfiller is tested against.)
 *
 * Declines, all without touching guest memory: PS3_MOVIE_IO off, no vm_base,
 * null/absurd arguments, an FD that is not a movie_io token, an op or
 * destination outside the guest VM, an op whose cursor already reached its
 * limit, and any read that does not return the full request.
 */
unsigned ps3_fios_aread_hle(uint32_t op_ea, uint32_t dst_ea, uint32_t n,
                            unsigned movie_fd);

#ifdef __cplusplus
}
#endif

#endif /* PS3_FIOS_AREAD_HLE_H */
