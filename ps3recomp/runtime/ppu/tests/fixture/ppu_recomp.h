/*
 * Fixture stand-in for the per-game generated "ppu_recomp.h".
 *
 * The real header is emitted by the lifter into the game's recomp directory
 * (e.g. gow2-recomp/recomp_macos_v2/ppu_recomp.h) and is therefore NOT part of
 * this repo -- a unit test that needed it could only run next to a lifted game.
 *
 * Everything the runtime/ppu translation units actually consume from that
 * header is the ppu_context layout plus the func_entry type, and it is already
 * defined canonically in runtime/ppu/ppu_context.h. So the fixture just
 * re-exports it. A test compiled with -I this-directory sees the same struct
 * the engine sees, with no game data present.
 */
#ifndef PPU_RECOMP_H
#define PPU_RECOMP_H

#include "../../ppu_context.h"

typedef struct { uint64_t addr; void (*func)(ppu_context*); const char* name; } func_entry;

#endif /* PPU_RECOMP_H */
