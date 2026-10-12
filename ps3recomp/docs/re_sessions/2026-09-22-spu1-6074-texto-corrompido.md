# 2026-09-22 — post-Colossus break: spu1 writes into the PPU code segment

## Symptom (user report, `claude_runs/jogar.log`)

After the cutscene that ends the Colossus fight, the game stops at:

```
[ICALL-BAD] ctr=0x80029F47 lr=0x002A1E40 r2=0x00541178 r3=0x0057BF04 r11=0x00029B5C
      host_ra: ra0=ps3_indirect_call+0x7A8 ra1=func_002A1E18+0x1F4 ...
```

## What the registers actually say

`ra0 = ps3_indirect_call+0x7A8` is the return address right after `blr fn` inside
`ps3_indirect_call` (checked in `g2play` with otool). So the virtual call from
`func_002A1E18` through the type-1 descriptor 0x51A2F0 **did** reach 0x00029AF0 (the
descriptor was never corrupt, which is why it read back intact). The bad `ctr` comes
from inside `func_00029AF0`, the script VM's opcode switch, which tail-calls through
its jump table:

* `r11 = [TOC-0x7C04] = 0x29B5C` is the table base, `r9 = 0x14` is opcode 5 × 4.
* The table entry at 0x29B70 is `0x000003EC` in the ELF (target 0x29F48, a real case).
* The code read `0x800003EB`, which gives `0x29B5C + 0x800003EB = 0x80029F47`.

## Reproduction without a controller (≈60 s)

New diagnostic `PS3_MOVIE_AUTOSKIP_FRAMES=N` (movie_vt_metal.m, OFF) presses "skip" by
itself after N frames of a non-logo movie. With `env_gow2.sh` plus the pad autostart,
New Game starts `introhud.m2v`, and the skip is taken there. The skip turned out not to
matter: letting the movie end on its own corrupts too.

## Who writes into the code segment

`PS3_WATCH_OPD` got two options (ppu_loader.cpp):

* `PS3_WATCH_OPD_RANGE=lo:hi` watches any range. Segment 0 of the EBOOT
  (`0x10000..0x50C7E0`, R-X on the console) is the interesting one.
* `PS3_WATCH_OPD=2` only diffs, with no mprotect. The protected mode hangs the boot,
  because the writer is a host-side DMA inside an SPU job.

The diff shows one burst of about 3 ms, early at boot. It writes one word every 0x60
bytes from 0x10250 upward (0x29B70 is on that lattice: 0x443 × 0x60), always as
`new = old + 0x7FFFFFFF`:

```
0x60000000 -> 0xDFFFFFFF   0x38210080 -> 0xB821007F   0x000003EC -> 0x800003EB (0x29B70)
```

`PS3_WATCH_STORE=0x29B70` named the writer: `spu-dma PUTLL*` (GETLLAR/PUTLLC line). The
`[SPUDMA]` trace, which now prints `img` and `pc`, shows the source: image 2 = **spu1**,
pc 0xA750/0xA810, with EAs 0x3FE00, 0x3FD00, 0x3FC00, … Those are LS addresses (the top
of the 256 KB local store) used as main-memory effective addresses.

## Bisection over the 2026-09-20 spu1 promotions

On 2026-09-20 (16:30–17:37), 20 "observed" indirect targets were promoted to lifted
entries in spu1 (`spu1_observed_indirect_targets.lst`). Rebuilding spu1 with each
subset (kit/make_spu_lifts.sh replay), and counting code-segment writes in the
reproduction:

| spu1 promoted targets | code writes |
|---|---|
| 391C (before 2026-09-20) | 0 |
| 391C 6088 64E0 56AC | 0 (110 s) |
| … + 6074 3318 | 400 at 2 s |
| … + 6074 | 400 at 2 s |
| all code targets except 6074 (391C 6088 64E0 56AC 3318 3410 ABA0) | 0 (150 s) |
| production (all 20) | 400 at 2 s |

0x6074 is `il r84,-3; br 0x5100`, the −3 exit of a return-code table. Before the
promotion, the branch there hit "unknown LS address" and the job stopped. With it
promoted, the job runs its −3 cleanup, and the GETLLAR/PUTLLC loop in that cleanup
takes LS addresses as EAs.

Ten other promoted targets are not code at all:

* 0xB54, 0x1BB0 and 0x2B84 are below the image, in the SPURS kernel area.
* 0xEF80, 0x15BB0, 0x26168, 0x30000, 0x34288, 0x3C0D0 and 0x3FDA8 are above the code
  segment (0x3000..0xBB20), in data, bss and stack.

## Fix

* `spu1_observed_indirect_targets.lst` keeps the 7 code destinations and leaves out
  0x6074.
* `patch_spu1_observed_entries.py` drops any destination outside the image's
  executable PT_LOAD.
* `kit/make_spu_lifts.sh` replays that list.

Validated with the fixed build: 305 s of New Game → introhud skip → gameplay, **0**
code-segment writes, 0 ICALL-BAD, 33–60 fps.

## Still open

1. **Why spu1 reaches −3 with LS addresses in its lock list.** This is the real SPU/LFQ
   defect (edgezlib inflate queue; the host fulfils inflates in `lfq_host_inflate.c`, so
   the SPU task sees a queue state the console never produces). Until that is fixed,
   0x6074 must not be lifted.
2. **The `[vm] UNCOMMITTED read32 access 0x80000004` pool read.** This is independent;
   it is present with the pre-2026-09-20 spu1 too. `PS3_TRACE_POOLCTOR`: func_002A43DC
   runs with r30 = 0x407DB31C, which is not a manager (word0 0xC0150015, floats around
   it). The pool is [r30+0xD4] = 0, and later 0x80000000. The manager constructor
   func_002A4120 never runs. Chain: func_00029AF0 → func_000CD7B4 → func_000CC094 →
   0x2A43DC, first seen in the menu diorama around 30–40 s, deterministic address. The
   read now returns 0 and the allocation fails (b6abb7c5), so it no longer kills the
   process. Refuted as its cause: the fast memory paths (`PS3_VM_NOCHECK_W=0` and
   `PS3_VM_NOCHECK=0` both show the same two POOL-MAU) and the movie EOS hook
   (disarm-on-store A/B, no effect). The oracle probe `tools/rpcs3_probe/oracle_mgr_d4.py`
   is ready, but headless RPCS3 has no pad, so it never gets past "press start" to the
   diorama.
