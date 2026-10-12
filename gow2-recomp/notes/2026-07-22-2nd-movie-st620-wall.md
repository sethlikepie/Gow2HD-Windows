# Wall: 2nd StartSeq / thr (honest 2026-07-23 post-skeptic)

## Plan hard bar (criterion 2)

| Criterion | Status | Evidence |
|-----------|--------|----------|
| **2b** 2nd `cellVdec` StartSeq real | **PASS** | StartSeq=2, DecodeAu=10, **PARK=0**, async thread |
| thr_auto_load end + AutoLoad OK | **PASS** | thr_end=1, AutoLoad2 0x0 |
| R_Perm FULL non-regression | **PASS** | 20169344 |
| GWI ready=1 livelock | **PASS** (absent) | ready=1 count 0 |
| **2a** post-thr st620 leave 0→0 | **FAIL** | only 0→0 after thr |
| Menu / LDRSH / playable | **not claimed** | |

## Theater stripped (source + structural test)

| Claim | Proof |
|-------|-------|
| No StartSeq PARK / forceMs=50 | `cellVdecStartSeq` always async when `PS3_VDEC_ASYNC`; `{SCRATCH}/test_no_theater.out` |
| No CE03C soft-park inject | wait-idle + natural `func_002C00DC`; st620=0 **only** on FO-abort longjmp recovery |
| No `PS3_CE03C_PLAY2` inject gate | absent from CE03C body |

## Root hang without REPLAY-NOPIC

`PS3_VDEC_REPLAY_PIC=1` (guest PICOUT on re-Play):

```
StartSeq#2 → DecodeAu → GetPicture#1
→ ICALL path-as-code ctr=0x005F6D6F r2=0x76696573 ("vies" from /_movies)
→ Play aborted / thr_end=0 / no R_Perm
```

## Fix (minimal HLE)

`ps3recomp/libs/codec/cellVdec.c`: when `g_vdec_startseq_count >= 2` and
`PS3_VDEC_REPLAY_PIC` unset, **skip guest PICOUT** (log `REPLAY-NOPIC`).  
Host VT still presents. **Does not forge StartSeq.** Opt-out: `PS3_VDEC_REPLAY_PIC=1`.

## Before / after (distinct md5)

| | baseline (`REPLAY_PIC=1`) | after (default NOPIC) |
|--|--------------------------|------------------------|
| md5 | `697809e614a46458…` | `b418c5e5bb86c5ec…` |
| StartSeq | 2 | 2 |
| PARK | 0 | 0 |
| GetPicture | 1 | 0 |
| REPLAY_NOPIC | 0 | ≥4 |
| thr_end | **0** | **1** |
| R_Perm full | 0 | **1** |
| AutoLoad | 0 | **1** |

Logs: `{SCRATCH}/wall_baseline.log`, `{SCRATCH}/wall_after.log`,  
counts: `wall_counts_baseline.txt`, `wall_counts_after.txt` / `wall_counts.txt`.

## Single next gate (criterion 3)

**Post-thr CC9D0 / TYPE15 incomplete attach** — see  
`notes/2026-07-23-postthr-cc9d0-type15-wall.md`.

Main alternates only `this=0x4066D798` / `0x4066D804` with f4=f54=0  
(CB56C skip_icall2 shell product). Blocks post-thr st620 / menu arm.  
`FORCE_ICALL2` hangs — product construction still open.
