/*
 * Host-side file overlay for user mods.
 *
 * Off unless PS3_MODS_DIR is set. Enabled pack names come from
 * PS3_MODS_ENABLED (comma-separated, last wins). If that var is unset,
 * every subdirectory of PS3_MODS_DIR is enabled. An empty
 * PS3_MODS_ENABLED means none.
 */
#ifndef PS3EMU_PS3_MODS_H
#define PS3EMU_PS3_MODS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Drop cached env so the next overlay call re-reads PS3_MODS_*. Tests only. */
void ps3_mods_reset(void);

/* If a enabled mod has a regular file or directory for the same relative
 * path under vfs_root, rewrite host_path and return 1. Otherwise 0 and
 * host_path is unchanged. */
int ps3_mods_overlay_host(const char* vfs_root, char* host_path, size_t cap);

/* Movie-cache shape: match basename (and .m2v / .wad_ps3 if no extension)
 * inside each enabled mod. Returns 1 and fills out[] on hit. */
int ps3_mods_overlay_basename(const char* base, char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
