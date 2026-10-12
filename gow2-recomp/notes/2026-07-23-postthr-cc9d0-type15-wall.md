# Wall: post-thr CC9D0 / TYPE15 incomplete attach (2026-07-23)

## Status: **primary next wall** after plan criterion 2b (2nd StartSeq + thr)

Prior wall (2nd StartSeq real + thr + R_Perm) holds with REPLAY-NOPIC  
(not PARK). See `2026-07-22-2nd-movie-st620-wall.md`.

### Next wall criterion

After thr, main never leaves idle:

| Signal | Measured |
|--------|----------|
| st620 post thr | **only 0→0** |
| Play after thr | **0** (no title movie) |
| cellPadGetData | **0** entire boot |
| SetFlip post thr | **0** (with `PS3_NO_RSX=1`) |
| LDRSH / walk | **0** |

### Root fingerprint (`PS3_TRACE_CC9D0=1`)

```
[CC9D0] this=0x4066D798 f54=0 f4=0
[CC9D0] this=0x4066D804 f54=0 f4=0
… ~1e6 iters alternating ONLY these two
```

Same EAs as CB56C factory objects:

```
CB56C #1 r3=0x4066D798  → product reuse 0x42F85AE4, skip_icall2=1
CB56C #2 r3=0x4066D804  → same
```

### Discriminators

| Experiment | Result |
|------------|--------|
| `PS3_TYPE15_CB56C=0` | still skip_icall2 via shell range; product stays pin-shell |
| `PS3_TYPE15_FORCE_ICALL2=1` | icall2 `code=0x0039D428` runs once → **hang** (UNCOMMITTED 0x91…), **no thr/B71** |

Confirms comment in lift: full attach on shell/reuse product hangs. Skip unblocks
B71/thr but leaves components with f4=f54=0 forever in CC9D0.

### Single next gate

Make TYPE15 **product construction real** so either:
1. icall1 returns a non-shell product that survives icall2+2A4FE4, **or**
2. freelist/REHOME produces a complete object (not pin-zone shell vt=0x00200000),

so CC9D0 sees f4/f54 progress and main can leave the 2-object spin toward menu.

Do **not** claim menu/playable. Do **not** re-introduce PARK.

### Logs
- dual-gate after: `{SCRATCH}/wall_after_nopic.log`
- CC9D0 spin: `{SCRATCH}/wall_cc9d0.log`
- FORCE hang: `{SCRATCH}/wall_force_icall2.log`
