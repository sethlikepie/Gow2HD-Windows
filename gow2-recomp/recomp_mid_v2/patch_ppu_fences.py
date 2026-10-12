#!/usr/bin/env python3
"""patch_ppu_fences.py -- lower guest memory barriers to host fences in an EXISTING lift.

Upstream sp00nznet/ps3recomp 981930fd (2026-09-03) changed the lifter so sync/lwsync/eieio/
isync/ptesync emit PPU_FENCE(...) instead of a no-op comment. On arm64 (weakly ordered) a
dropped barrier lets a guest publish a pointer before the data it guards. This script applies
the same lowering to a lift produced by the OLD lifter, so an A/B does not need a re-lift.

Differences from a re-lift with the ported lifter, stated:
  * the old disassembler named XO 598 "sync" for sync, lwsync AND ptesync (it did not read the
    L field). Here every "sync" becomes PPU_FENCE(seq_cst): a superset of what lwsync promises
    (acq_rel). Correct, slightly stronger than necessary. A re-lift restores the exact mapping.
  * isync -> acquire, eieio -> release, lwsync -> acq_rel, ptesync -> seq_cst (as upstream).

Always-on (it is a correctness fix, faithful to the PowerISA; not a probe).
Idempotent: markers are the needles themselves; a second run reports ALREADY.
Usage: patch_ppu_fences.py <lift_dir>     rc 0 ok / 2 no lift / 3 header needle missing
"""
from __future__ import annotations

import glob
import os
import sys

FENCE_MARK = "PPU_FENCE"
FENCE_PREAMBLE = """\
/* Guest memory barriers: the lowering of sync/lwsync/eieio/isync/ptesync
 * (port of upstream 981930fd; applied by patch_ppu_fences.py to a pre-port lift).
 * On a weakly ordered host (arm64) a dropped barrier lets a guest publish a
 * pointer before the words it guards. */
#ifndef PPU_FENCE
#  ifdef __cplusplus
#    include <atomic>
#    define PPU_FENCE(o) std::atomic_thread_fence(std::memory_order_##o)
#  else
#    include <stdatomic.h>
#    define PPU_FENCE(o) atomic_thread_fence(memory_order_##o)
#  endif
#endif
"""
HDR_NEEDLE = "#include <stdint.h>\n"

REPL = {
    "/* sync: cache/sync — no-op */;":
        "PPU_FENCE(seq_cst);   /* sync (old disasm: may be lwsync/ptesync): full barrier */",
    "/* ptesync: cache/sync — no-op */;":
        "PPU_FENCE(seq_cst);   /* ptesync: full barrier */",
    "/* lwsync: cache/sync — no-op */;":
        "PPU_FENCE(acq_rel);   /* lwsync: everything but StoreLoad */",
    "/* eieio: cache/sync — no-op */;":
        "PPU_FENCE(release);   /* eieio: orders stores ahead of it */",
    "/* isync: cache/sync — no-op */;":
        "PPU_FENCE(acquire);   /* isync: discards speculation past it */",
}


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    d = sys.argv[1]
    hdr = os.path.join(d, "ppu_recomp.h")
    chunks = sorted(glob.glob(os.path.join(d, "ppu_recomp_*.cpp")))
    if not os.path.isfile(hdr) or not chunks:
        print(f"fences: no lift in {d}", file=sys.stderr)
        return 2

    h = open(hdr).read()
    if FENCE_MARK in h:
        print("fences: header ALREADY")
    else:
        if h.count(HDR_NEEDLE) < 1:
            print("fences: header needle missing", file=sys.stderr)
            return 3
        h = h.replace(HDR_NEEDLE, HDR_NEEDLE + "\n" + FENCE_PREAMBLE, 1)
        open(hdr, "w").write(h)
        print("fences: header APPLIED")

    totals = {k: 0 for k in REPL}
    already = 0
    for p in chunks:
        s = open(p, errors="surrogateescape").read()
        already += s.count("PPU_FENCE(")
        ns = s
        for old, new in REPL.items():
            n = ns.count(old)
            if n:
                totals[old] += n
                ns = ns.replace(old, new)
        if ns != s:
            open(p, "w", errors="surrogateescape").write(ns)
    for old, n in totals.items():
        print(f"fences: {old.split(':')[0][3:]:8s} converted={n}")
    print(f"fences: pre-existing PPU_FENCE sites={already} (nonzero only on a re-run)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
