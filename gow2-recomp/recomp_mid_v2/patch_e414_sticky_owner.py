#!/usr/bin/env python3
"""E414 -- STICKY-RESTORE only for the container that owned the op when its completion was published.

Background (E412/E413, 2026-09-14):
  * our July palliative STICKY-RESTORE (func_002B4224) re-materializes a FIOS op's done word from a host table
    keyed by the op ADDRESS (publish in func_00306534 on every completion, consume only in func_002B4274);
  * the intro movie needs it: with it OFF (E413) the movie FSM bounces 11->1 in 3/3 runs and stays stuck in 1/3
    (its poller reads the op after our FIOS thread already recycled it -- the E401-class ordering inversion);
  * but a recycled op inherits the movie's stale "done": the loader's sync open of R_LglScA is declared done
    before the FIOS scheduler processes it, case 7 (0x30CA88) never copies the size into FO+0x48, rem=0,
    [D] stops at 6 shells (E412). With the restore OFF the open completes naturally with 0xC00 (E413).

The table cannot tell the old-life poller from the new-life poller by address. The op itself can: FIOS keeps the
submitter's user slot at op+0xB8 (&container+4, E400). This patch records that owner when the completion is
published (func_00306534) and, at the restore site, honours the sticky entry only if the polling container
(r31 = container, slot = container+4) is that owner. Unknown owner (entry published by a path this patch does not
annotate) keeps the old behaviour.

Gated PS3_FIOS_STICKY_OWNER (default OFF = old behaviour). PS3_TRACE_FIOSSCHED=1 logs STICKY-OWNER-MISMATCH.
Apply on a lift WITHOUT patch_e413 (the restore site must be the original one). Idempotent (marker E414-STICKY-OWNER).
rc 0 ok / 2 no lift / 3 needle count mismatch.
Usage: patch_e414_sticky_owner.py <lift_dir>
"""
from __future__ import annotations

import glob
import os
import sys

MARK = "E414-STICKY-OWNER"

HELPERS = r'''
/* E414-STICKY-OWNER: owner (op+0xB8 user slot) recorded at publish time, checked at the restore site. */
static uint32_t e414_op[64], e414_owner[64];
static int e414_on(void){ static int v=-1; if(v<0){ extern char* getenv(const char*);
  const char* e=getenv("PS3_FIOS_STICKY_OWNER"); v=(e&&*e&&*e!='0')?1:0; } return v; }
static void e414_owner_note(uint32_t op, uint32_t owner){
  if(!op) return; unsigned h=(op>>4)&63u;
  for(int i=0;i<64;i++){ unsigned j=(h+(unsigned)i)&63u;
    if(e414_op[j]==0u||e414_op[j]==op){ e414_op[j]=op; e414_owner[j]=owner; return; } }
  e414_op[h]=op; e414_owner[h]=owner; }
static uint32_t e414_owner_get(uint32_t op){
  unsigned h=(op>>4)&63u;
  for(int i=0;i<64;i++){ unsigned j=(h+(unsigned)i)&63u;
    if(e414_op[j]==op) return e414_owner[j]; if(e414_op[j]==0u) return 0u; }
  return 0u; }
static int e414_owner_ok(uint32_t io, uint32_t slot){
  if(!e414_on()) return 1;
  uint32_t ow=e414_owner_get(io);
  if(ow==0u || ow==slot) return 1;
  { static int _t=-1; if(_t<0){ extern char* getenv(const char*);
      const char* _e=getenv("PS3_TRACE_FIOSSCHED"); _t=(_e&&*_e&&*_e!='0')?1:0; }
    if(_t){ static int _n=0; if(_n++<24){
      fprintf(stderr,"[FIOSSCHED] STICKY-OWNER-MISMATCH #%d io=0x%08X published-for=0x%08X polled-by=0x%08X (not restored)\n",_n,io,ow,slot);
      fflush(stderr); } } }
  return 0; }
'''

PEEK_OLD = "          if ((uint32_t)ctx->gpr[0]==0u && _io && ps3_fios_sticky_peek(_io)) {\n"
PEEK_NEW = ("          if ((uint32_t)ctx->gpr[0]==0u && _io && ps3_fios_sticky_peek(_io)\n"
            "              && e414_owner_ok(_io, (uint32_t)ctx->gpr[31] + 4u)) { /* " + MARK + " */\n")
PUB_OLD = ("        if (((uint32_t)ctx->gpr[11]) != 0u)\n"
           "            ps3_fios_sticky_publish((uint32_t)ctx->gpr[31]);\n")
PUB_NEW = ("        if (((uint32_t)ctx->gpr[11]) != 0u) {\n"
           "            ps3_fios_sticky_publish((uint32_t)ctx->gpr[31]);\n"
           "            e414_owner_note((uint32_t)ctx->gpr[31], vm_read32((uint32_t)ctx->gpr[31] + 0xB8u)); /* " + MARK + " */\n"
           "        }\n")
ANCHOR = "void func_002B4224(ppu_context* ctx) {\n"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_e414_sticky_owner.py <lift_dir>", file=sys.stderr)
        return 2
    chunks = sorted(glob.glob(os.path.join(sys.argv[1], "ppu_recomp_*.cpp")))
    if not chunks:
        print("E414: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    texts = {p: open(p, errors="surrogateescape").read() for p in chunks}
    if any(MARK in s for s in texts.values()):
        print("E414: ALREADY")
        return 0
    counts = {k: sum(s.count(v) for s in texts.values()) for k, v in (("peek", PEEK_OLD), ("pub", PUB_OLD), ("anchor", ANCHOR))}
    if counts != {"peek": 1, "pub": 1, "anchor": 1}:
        print(f"E414: needle counts {counts} (expected 1 each; a lift with patch_e413 is not a valid base)", file=sys.stderr)
        return 3
    homes = [p for p, s in texts.items() if ANCHOR in s]
    others = [p for p, s in texts.items() if (PEEK_OLD in s or PUB_OLD in s) and p not in homes]
    if others:
        print(f"E414: publish/peek not in the same TU as func_002B4224: {others}", file=sys.stderr)
        return 3
    p = homes[0]; s = texts[p]
    s = s.replace(ANCHOR, HELPERS + "\n" + ANCHOR, 1).replace(PEEK_OLD, PEEK_NEW, 1).replace(PUB_OLD, PUB_NEW, 1)
    open(p, "w", errors="surrogateescape").write(s)
    print(f"E414: {os.path.basename(p)}: APPLIED (helpers + publish owner note + restore owner check)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
