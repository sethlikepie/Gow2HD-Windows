# E411 (2026-09-14) — upstream sp00nznet improvements A/B, and where [D] stops today

> **§4 INFERENCE REFINED by [E412–E415](2026-09-14-E412-E415-sticky-owner-and-giant-handoff.md).** The
> measurements stand (no size on the natural open, 0 F2B lines, E402 binary not reproducing). The inference
> "the 70 shells of E402 depended on the F2B fallback" is superseded: the measured mechanism is our
> STICKY-RESTORE handing a recycled FIOS op the previous owner's "done" (E413), whose root is giant-lock
> starvation of the FIOS threads (E415). Kept as the record.

Session output. Internal notes in English (CLAUDE.md § Idioma).

## 1. What was brought in (branch `integra/upstream-2026-09-14`)

Triage of ~30 upstream commits (cherry-pick dry runs in a worktree). Taken:

| ours | upstream | what |
|---|---|---|
| 6ae68f17 | 17aec56c | sys_ppu_thread: join without holding the table lock |
| 3437fd61 | 5a75c739 | sys_mutex: recursive trylock re-entry takes the host mutex on POSIX |
| 66d39e4d | d7f28198 + 5d41f82e | SPU GETLLAR snapshot from LS (ours read memory twice, `spu_dma.h`); list-DMA cursor in quadwords |
| 508859aa | 429d8a33 + 959af58e | sys_rwlock write-lock owner on POSIX (E139 giant-lock release kept) |
| 740377a8 | d77930aa | vm_commit/vm_protect mprotect in host pages (16 KB on Apple Silicon) |
| 83ec8bbb | 981930fd | lifter/disasm: guest barriers -> host fences (`PPU_FENCE`), L field of XO598 decoded |
| ad13e7f2 | af6913e1 | lifter: lve*x/stve*x select the element by EA (only after re-lift) |

Not taken (reasons in the triage): 6913c27f (sys_cond rewrite; our lwcond uses pthread_cond
directly, not sys_cond), aa5050af (cellSync ticket lock; GoW2 imports one cellSync function,
never seen spinning), b603ab0f (GoW2 does not import GetTimeStampLocation), GCM rewrites tied
to the Yakuza runner, SPU commits tied to the LLE/SMC interpreter we do not have.

For existing lifts: `gow2-recomp/recomp_mid_v2/patch_ppu_fences.py` (sync -> seq_cst since the
old disasm did not separate lwsync/ptesync; isync -> acquire). GoW2 lift: 4211 sync + 4499 isync.

## 2. Instrument

`scratchpad/ab_run2.sh` / `ab_rounds2.sh` (session scratch): headless, env_gow2.sh +
`PS3_GCM_CB=1 PS3_SCOPE_CLAMP=1 PS3_MOVIE_DONE_MS=3000`, traces TYPE15_SHELL/TYMAP/LDRSH/
KEYHASH/ENTERS; stop on exit, 30 s without loader progress, or intro stuck at st620=1 for
100 s; kill by PID; runs serialized and interleaved across arms; no compile during a series.

Arms (lift `recomp_macos_e162` = the exact E402 lift; binary `boot_gow2_arena` = E402 binary):
- A: `boot_gow2_arena`. A': same lift rebuilt today (all host objects byte-identical to A;
  binaries differ only in UUID + ~440 bytes + signature).
- F: A' + fences (patch_ppu_fences.py, forced chunk rebuild; 777 fence relocs in chunk 005).
- B: A' lift + worktree runtime (lib Release, 128 members = main; `_sys_rwlock_wunlock`
  84 -> 116 bytes proves provenance); SPU lifted objects rebuilt.
- BF: F lift + worktree runtime.

## 3. Results

| series | arm | runs | intro stuck (st620=1) | sync open r3=1 | shells | ICALL-BAD |
|---|---|---|---|---|---|---|
| 1 | A | 3 | 1 | 0/2 | 5 | 0 |
| 2 | A' | 4 | 4 | – | 5 | 0 |
| 2 | F | 4 | 0 | 0/4 | 5-6 | 0-2 |
| 3 | A' | 5 | 0 | 0/5 | 6 | 2 each |
| 3 | F | 5 | 2 | 0/3 | 6 | 2 each |
| 3 | B | 5 | 2 | 0/3 | 6 | 2 each |
| 3 | BF | 5 | 2 | 0/3 | 6 | 2 each |

- **Prediction failed: the exact E402 binary does not reproduce E402 today** (0/2 valid, 5 shells).
- **No arm changes the [D] stop point.** LDRSH=0 everywhere. No regression signature rose.
- Intro-stuck rate is environment noise (same program: 4/4 in series 2, 0/5 in series 3).
  **RETRACTED** in-session: the series-2 reading "fences end intro flakiness" (4/4 vs 0/4);
  series 3 has F at 2/5. The counts stand; the inference falls.

## 4. Where [D] stops today (log `Ap_round1`, lines 36005-36053)

```
[441C] tail ... -> r3=0x00000001                      (E401 fix works: op+4 non-zero at the tail)
[DONE-4274] #3 wait_ret=0 ... op+0x10=0xFFFFFFFF      (E398 had 0xC00 here)
[BAD78] after-open #1 r3=1 ... ts+0x74=0x00000000 state=1 rem=0
[DRV] after 1st func_002BAD10 (sync) returned r3=0x00000000
[WADLD-SM] #1 ts=0x00869570 state=1 rem=0x0 ...
[vm] UNCOMMITTED read32 access 0x84000017 ... func_002ACBE8   (lookups on a NULL table)
[ICALL-BAD] ctr=0x00000001 lr=0x00072398 ... host_ra func_00072394 <- func_00041D5C <- func_000B71B8
[ICALL-BAD] ctr=0x00000001 lr=0x00041528
```
After that: process alive (MOVIEFSM heartbeat, ~3200 `[SPUJOB]`), loader never ticks again.

**Measured:** the file size of R_LglScA is not delivered by the natural open (`op+0x10=-1`,
`ts+0x74=0`), and there are **zero** F2B host lines in every run of this session.

**Inferred (probable, not proven):** the 0xC00 of E398-E402 came from our host F2B open
(`F2B-FO-SIZE`, `ppu_recomp_001.cpp:133010-133136` of lift e162, gate `PS3_FIOS_F2B_MOVIEIO`,
default ON), which only runs when the game's dearchiver REJECTS the WAD path (`gpr[3]==0`).
E401's B25 trace shows F2B-RESTATUS/F2B-STREAM-PUMP inside the open; today there are none.
So the 70 shells of E402 depended on that fallback firing. What would make this wrong: an E402
good-run log without F2B lines — none was kept.

**Real defect:** the natural open does not fill `FO+0x48` (u64 file size) — on the console it is
filled. E408/E410 (uncommitted, other session) stamp it from the host = forbidden as a fix (rule 5).

## 5. Next

1. RPCS3 oracle: watch `FO+0x48` of the R_LglScA file object during the sync open; name the guest
   function that writes it; check why ours does not run (or runs on another FO).
2. Keep the upstream branch (correctness, no regression measured) — merge decision is the user's.
3. Quarantine E403-E410 (applied in `recomp_macos_v2`, uncommitted, stamping) — user's decision.
