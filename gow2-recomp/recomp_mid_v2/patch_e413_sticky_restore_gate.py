#!/usr/bin/env python3
"""E413 -- gate our STICKY-RESTORE palliative in func_002B4224 behind PS3_FIOS_STICKY_RESTORE (default OFF).

What it did (July 2026, patch "sticky-restore"): if the guest done word [io+0x90] read 0 at the first poll but the
host sticky table (ppu_loader.cpp ps3_fios_sticky_*) said the producer had published done, force done=1 and WRITE 1
into guest memory. The table is keyed by the FIOS op ADDRESS; publish runs for EVERY op completion
(func_00306534), consume only in the loader's DONE path (func_002B4274).

Measured 2026-09-14 (E412): the intro movie's ops (SmLogo_v2.wav, FO 0x43018580, op 0x430095A0) are published and
never consumed. FIOS recycles op 0x430095A0 for the loader's sync open of R_LglScA; the stale entry makes the poll
report done before the FIOS scheduler has processed the open, so the op-completion case 7 (0x30CA88, which copies
*(op+0x80) into FO+0x48) never runs for it: FO+0x48 stays at file_new's zero, the loader sees rem=0 and [D] stops.
The console has no such table: the same case 7 writes 0xC00 on the FIOS thread during this open (oracle_fosize.py).

Removal of OUR palliative, faithful to the console. PS3_FIOS_STICKY_RESTORE=1 restores the old behaviour for A/B.
With PS3_TRACE_FIOSSCHED=1 a skipped restore is logged as [FIOSSCHED] STICKY-RESTORE-SKIPPED.
Idempotent (marker E413-STICKY-GATE). rc 0 ok / 2 no lift / 3 needle count != 1.
Usage: patch_e413_sticky_restore_gate.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import sys

MARK = "E413-STICKY-GATE"
NEEDLE = ("        { uint32_t _io=(uint32_t)ctx->gpr[9];\n"
          "          if ((uint32_t)ctx->gpr[0]==0u && _io && ps3_fios_sticky_peek(_io)) {\n")
REPL = ("        { uint32_t _io=(uint32_t)ctx->gpr[9];\n"
        "          /* " + MARK + ": the sticky table is keyed by op address and never consumed for ops that\n"
        "           * are not the loader's; a recycled op inherits a stale 'done' (E412/E413). OFF by default. */\n"
        "          static int _sr=-1; if(_sr<0){ extern char* getenv(const char*);\n"
        "            const char* _e=getenv(\"PS3_FIOS_STICKY_RESTORE\"); _sr=(_e&&*_e&&*_e!='0')?1:0; }\n"
        "          if (!_sr && (uint32_t)ctx->gpr[0]==0u && _io && ps3_fios_sticky_peek(_io)) {\n"
        "            static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
        "              const char* _e=getenv(\"PS3_TRACE_FIOSSCHED\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
        "            if(_on){ static int _n=0; if(_n++<16){\n"
        "              fprintf(stderr,\"[FIOSSCHED] STICKY-RESTORE-SKIPPED #%d io=0x%08X (stale entry, not restored)\\n\",_n,_io);\n"
        "              fflush(stderr); } } }\n"
        "          if (_sr && (uint32_t)ctx->gpr[0]==0u && _io && ps3_fios_sticky_peek(_io)) {\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e413_sticky_restore_gate.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E413: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    texts = {p: open(p, errors="surrogateescape").read() for p in chunks}
    if any(MARK in s for s in texts.values()):
        print("E413: ALREADY")
        return 0
    n = sum(s.count(NEEDLE) for s in texts.values())
    if n != 1:
        print(f"E413: needle found {n}x (expected 1)", file=sys.stderr)
        return 3
    for p, s in texts.items():
        if NEEDLE in s:
            open(p, "w", errors="surrogateescape").write(s.replace(NEEDLE, REPL, 1))
            print(f"E413: {os.path.basename(p)}: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
