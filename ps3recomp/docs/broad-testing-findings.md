# Broad-testing findings (harness run, June 2026)

First run of `tools/harness/ps3_recomp_harness.py` across the PSN library and a
decrypted-ELF corpus. This is the "turn one-off runs into a compatibility
matrix" pass — the point is to find recurring patterns that tell us what to
harden next. (Full machine-readable output lives under `_harness/`, which is
git-ignored; this file is the human summary.)

## 1. PSN library catalog (filename pass, no extraction)

- **1,345 archives** under `Z:\Roms\PS3\PSN` (PSN_1: 672, PSN_2: 673), **~1.13 TB** total.
- **1,339 / 1,345** carry a parseable title-id in the filename (`… [NPxx-NNNNN]`).
- **Region:** EU (SCEE) 661 · US (SCEA) 635 · JP (SCEJ) 28 · Asia 6 · unknown 15.
- **Content class** (PSN prefix 4th letter, coarse):
  - PSN full game (`B`): **710**
  - Demo / minis (`Z`): 273
  - PS1 / minis (`J`): 112
  - App / utility (`A`): 142
  - Other / disc-linked / unknown: ~108

**Takeaway:** the realistic recomp target space here is the **~710 full PSN
games** plus a long tail of minis. Region is overwhelmingly EU/US, so PPU ABI +
the western SDK versions dominate — good for prioritization.

## 2. Decrypted-ELF recomp triage (the real pipeline)

Corpus: Tekken 6, Marvel Ultimate Alliance, Simpsons Arcade, Tokyo Jungle, flOw
(plus `tornado.self`). All are PS3 PPU executables.

_Updated after the `.opd` detection fix (see §3.1) and the import-NID stage._

| Binary | machine | image base | functions | `.opd` seeded | imports (libs) | notes |
|---|---|---|---:|---:|---:|---|
| scout/tekken6.elf | PPC64 | 0x00010000 | 36,213 | 18,281 | 399 (27) | |
| scout/simpsons.elf | PPC64 | 0x00010000 | 56,640 | 24,184 | 180 (15) | |
| scout/mua.elf | PPC64 | 0x00010000 | **51,388** | **23,410** | 126 (11) | was 28,088 / 1 before `.opd` fix |
| tokyojungle/EBOOT.elf | PPC64 | 0x00010000 | 11,231 | 3,816 | 318 (24) | |
| simpsons/EBOOT.elf | PPC64 | 0x00010000 | 5,170 | 889 | 256 (20) | lifted tier-5 OK |
| flow/EBOOT.elf | PPC64 | 0x00010000 | **51,547** | **20,613** | 140 (12) | was 31,008 / 1 before `.opd` fix |
| scout/tornado.self | — | — | — | — | — | **blocked: no klicensee** |

Tier-5 lift validated on the Simpsons EBOOT: **5,170 detected → 14,560 lifted**
functions (mid-function tail-entries + gap-resident targets expand the count),
**59 MB** of C in 2 chunks, ~137 s with parallel lifting (caner #11). This
exercised the whole freshly-merged lifter stack (VMX handlers #8 + parallel #11
+ gap-target WIP) on a real game with no crashes.

## 3. Things we learned (actionable)

### 3.1 `.opd` detection — found and fixed

The first run flagged that Marvel UA and flOw seeded only **1** `.opd` descriptor
while the others seeded thousands. Root cause (now fixed): the shape-detector
required the descriptor's TOC word to land inside a *file-backed* data range, but
the r2/TOC base every descriptor shares routinely sits in **BSS or the gap just
past a segment's end** (a TOC base is typically `.got + 0x8000`). So whole tables
were rejected and every address-taken (virtual/callback) function went
undetected — the exact setup that crashes on the first call through a function
pointer. The fix relaxes the TOC test to a plausible-pointer check against the
bounding span of loadable non-exec segments (`memsz`, so BSS/gaps count); the
`≥16` exact-repeat threshold still discriminates a real table. Results:

- **flOw: 31,008 → 51,547** functions (+20,613 address-taken)
- **Marvel UA: 28,088 → 51,388** (+23,410)
- Tokyo Jungle / Tekken 6 / Simpsons: unchanged or +tens (no regression)

`find_functions` now also warns loudly when a large text segment yields ≤1
descriptors. (Builds on caner's `.opd` seeding, #13.)

### 3.2 Stub-prioritization ranking — now built

The harness now extracts each EBOOT's firmware imports (via `ppu_loader.py`'s
`proc_prx_param → libstub` walk, from PR #3) and resolves NIDs to names, then
aggregates a cross-title ranking — the 360 harness's killer feature, for PS3.
Across the 6-binary corpus, imported by **all 6**:

- **Libraries:** `cellAudio`, `cellSysutil`, `sys_net`, `cellGcmSys`,
  `cellSysmodule`, `sys_io`, `cellNetCtl`, `sysPrxForUser`, `sys_fs`.
- **Functions (named):** `cellGcmGetConfiguration`, `cellGcmGetControlRegister`,
  `cellGcmMapMainMemory`, `cellPadGetData`/`cellPadInit`, `cellFsFstat`/
  `cellFsOpendir`, `cellAudioPortOpen`/`Close`, `cellVideoOutConfigure`,
  `cellSysutilRegisterCallback`/`CheckCallback`, `sys_ppu_thread_exit`,
  `sys_lwmutex_unlock`, `sys_time_get_system_time`.

That's a concrete, evidence-ranked stub work-list. Run over more titles it gets
sharper. (A handful of NIDs don't resolve yet — e.g. `cellNetCtl::0xbd5a59fc` —
candidates for growing the NID database.)

### 3.3 Ghidra cross-check — find_functions recall is ~72%

A new opt-in harness tier (6) runs Ghidra headless and compares its function set
to `find_functions`'. On the Simpsons EBOOT (Ghidra ~33 s):

| | count |
|---|---:|
| find_functions | 5,170 |
| Ghidra | 3,681 |
| agree | 2,644 |
| **missed** (Ghidra found, we didn't) | **1,037** → recall **71.8%** |
| extra (we found, Ghidra didn't) | 2,526 |

Two signals worth chasing:
- **We miss ~1,000 functions Ghidra finds** (e.g. `0x10338`, `0x107D8`,
  `0x10C0C`…). These are reached by Ghidra's recursive call-target analysis but
  aren't in `.opd` and lack a standard prologue — a real recall gap the lifter
  would inherit. *Next:* fold Ghidra's (or IDA's) confirmed starts back as
  additional seeds, or strengthen the call-target pass.
- **We also have ~2,500 "extra" starts** Ghidra doesn't list. Many are genuine
  `.opd`-listed address-taken functions Ghidra folded into callers; some may be
  false positives. The cross-check gives the exact addresses to audit.

**Update — second oracle (IDA) + the gap is now closeable.** IDA Pro 9.1 headless
(`tools/ida_analyze.py`, ~10 s/binary) is wired as a second oracle
(`--oracle ghidra|ida|both`). With both, the high-confidence signal is the set
**Ghidra and IDA agree on**: on the Simpsons EBOOT they agree on 2,974 functions
and `find_functions` misses **741** of those (recall 75.1%) — two independent
disassemblers agreeing makes those almost certainly real. And the gap is now
recoverable: `find_functions --seed-json <ghidra|ida functions.json>` folds those
confirmed starts in as seeds, taking Simpsons recall to **100%** and producing a
best-of-both function list for the lifter. (On tokyojungle, `.opd` detection is
already so strong — 11,231 vs IDA's 6,967 — that seeding adds only 17.)

### 3.4 Other observations

1. **Image base is uniformly `0x00010000`** across every retail PS3 EXEC sampled —
   far less variance than the 360 side's high-base titles. The toolkit can lean
   on that assumption (and flag anything that deviates, as the report does).
2. **Decryption coverage is the scaling bottleneck for PSN**, not analysis.
   `tornado.self` (and the entire NPDRM library) needs the per-title klicensee
   (RAP/rif) before `ps3sce` can produce an ELF. We have exactly one RAP
   (Tokyo Jungle). *Next:* wire `klics.txt` / a RAP store into the decrypt tier so
   the harness can fan out over titles we *do* have keys for.

## 4. Suggested next steps (in priority order)

1. ~~`.opd` detection robustness~~ — **done** (§3.1).
2. ~~EXEC import-NID extractor → cross-title stub-priority ranking~~ — **done** (§3.2).
3. **Implement the top-ranked stubs** the ranking surfaced, in title-count order
   (the `cellGcmSys` / `cellAudio` / `sys_io` / `cellSysutil` cluster), and grow
   the NID database to cover the unresolved high-frequency NIDs.
4. **RAP/klicensee store for the decrypt tier** → fan the harness out over the
   PSN titles we have keys for (start with the minis: smallest, simplest,
   fastest), which sharpens the stub ranking with real catalog breadth.
5. **Tier build/boot stages** (compile the lifted C, attempt a boot) once a
   target game's runtime is far enough along — mirrors the 360 harness tiers 3–4.
