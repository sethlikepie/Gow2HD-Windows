# Task R10 report — sched arm (who arms menu present)

**Gate A RED** (SetFlip_after=0) · **Gate B GREEN** · no fix (diag only)

## Deliverables
- Probe: `gow2-recomp/recomp_mid_v2/patch_sched_arm_probe.py` (`PS3_TRACE_SCHEDARM=1`)
- Note: `gow2-recomp/notes/2026-07-23-r10-sched-arm.md`
- Updated: `gate-a-verdict.md`, master INDEX, wall-chain INDEX

## RE (static)
- **B61F4** ← B6150 only if `*(TOC-0x62B0)+0x1CC==0` → BB424 JT → BB4E0 → 194FFC
- **B94EC→B6150** no live static caller (dead fall from B94D4; B94D4 live→B7888)
- **25C838** ← 10354 (oneshot boot); **36A598** after return (never seen)
- **2B25AC** dead fall from 2B25A4 (live→2B24E0); OPD only

## Smoke (TIMEOUT=90 NO_RSX SCHEDARM) `/tmp/r10_schedarm.log`
| site | tot | post |
|------|----:|-----:|
| B61F4/BB424/BB4E0/B6150/B94* | 0 | 0 |
| 25C838 / 10354 | 1 | 0 |
| **36A598** | **0** | 0 |
| BB414/BB37C | 1 | 0 |
| CE03C | 1 | 0 (wait-idle exit st620=0) |
| CDFDC | 11008 | 0 |
| 2B25AC/2B24E0 | 0 | 0 |
| SetFlip_after_R_Perm | **0** | |

## Discriminators
1. **B61F4 never** (pre/post) — arm parents also never
2. **25C838 only once**; main **does not** reach 36A598
3. **CE03C wait not sole block** — exits; thr OK; menu arm zero *pre* thr too
4. No CE03C wait fix (would be theater)

## Wall next (R11)
OPD/vtable → B94EC/B6150; residual B71 after thr; writers of +0x1CC; why 36A598=0
