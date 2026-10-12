/*
 * ps3recomp - game patches in the spirit of RPCS3's patch.yml.
 *
 * The launcher resolves the patches the player enabled into a flat file, one
 * write per line, and points PS3_PATCH_FILE at it:
 *
 *     # patch: Aspect Ratio 21:9
 *     bef32 0x0053EECC 2.3333333
 *     be32  0x002C1A88 0x38000001
 *     utf8  0x00600000 "text"
 *
 * Types follow RPCS3: byte, be16, be32, be64, bef32, bef64, le16, le32, le64,
 * lef32, lef64, utf8 (no terminator) and c_utf8 (NUL terminated).
 *
 * Applied once, right after the ELF segments are copied into guest memory.
 * A write that lands in an executable segment still goes to memory, but it
 * cannot change behaviour: the code there was already recompiled ahead of
 * time. Those are reported as "code patch" so the player knows a re-lift is
 * needed for them to work.
 */
#ifndef PS3_PATCH_H
#define PS3_PATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS3_PATCH_MAX_BYTES 256

typedef struct ps3_patch_write {
    uint32_t addr;
    uint32_t len;                         /* bytes in data[] */
    uint8_t  data[PS3_PATCH_MAX_BYTES];   /* already in guest (big-endian) order */
} ps3_patch_write;

typedef struct ps3_patch_range {
    uint32_t lo, hi;                      /* [lo, hi) */
} ps3_patch_range;

typedef struct ps3_patch_stats {
    int applied;       /* data writes that took effect */
    int code;          /* writes into executable segments (no effect) */
    int rejected;      /* out of range or malformed lines */
} ps3_patch_stats;

/* Parses one line. Returns 1 and fills *out for a write, 0 for a blank or
 * comment line, -1 for a malformed line. Pure: no I/O, no globals. */
int ps3_patch_parse_line(const char* line, ps3_patch_write* out);

/* Applies every line of `path` to guest memory, streaming it line by line.
 * A write must fall entirely inside one of the `loaded` ELF segments (so a
 * typo can never touch unmapped memory); `exec` lists the executable ones. */
ps3_patch_stats ps3_patch_apply_file(const char* path, uint8_t* mem,
                                     const ps3_patch_range* loaded, int nloaded,
                                     const ps3_patch_range* exec, int nexec);

#ifdef __cplusplus
}
#endif

#endif /* PS3_PATCH_H */
