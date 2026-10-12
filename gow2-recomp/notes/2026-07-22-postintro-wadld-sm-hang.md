# Post-intro hang: 00041D5C / WADLD-SM → 262610 (2026-07-22)

## Chain (sample)

```
boot → B71B8 → 00041D5C → (wait) 002BA76C
  after WADLD-BODY SBP_general:
  → 002BACE8 → 00262610  **spin forever**
```

## Measured (PS3_TRACE_TYMAP + POSTINTRO)

| Sinal | Valor |
|-------|-------|
| WADLD-SM | #1 state=1 rem=0xC00 (Lgl); #2 rem=0; #3 state=3 rem=0x133C280 (R_Perm full) |
| WADLD-BODY | #1 type16=1 name `SBP_` / `SBP_general` size≈1 162 380 |
| F2B-STREAM-FILL | Lgl full; R_Perm **window 262144 / 20169344** only at open |
| pool | idx=127 count=128 (last slot) at BACE8 |
| BACE8 | arena=0x40004020 head=0x40637CC0 **need=0x687DD790** (~1.7 GiB) |

`need` does **not** appear in `r_perma.wad_ps3` (not a real size). Next real
member after SBP is `SBP_general2` with LE size 0x0BB4 at file off ~0x11C82C.
Size field on disk for SBP is **LE** at header+4 (`8C BC 11 00` = 0x11BC8C).

## Discriminator results

1. **Not** outer wait-only: sample shows deep hang in `func_00262610` freelist walk.
2. **Not** missing 2550C8 fallthrough (already Task 3f FIX on Mac).
3. TAG-GUARD on **263178** alone does not cover **262610** alloc walk.
4. With entry guard `need > 64MiB → r3=0`:
   - log: `[FREELIST-TAG-GUARD] 262610 entry … need=0x687DD790 → abort r3=0`
   - hang in 262610 **gone**
   - rem: 0x133C280 → 0x1220280 (≈SBP) → then **rem=0 state=3** forever (SM #5+)
   - stack moves to 41D5C → B367C → yield path (spin waiting, not freelist)

## Root wall (next)

Corrupt **next-member size** after first multi‑MB body (SBP). Likely:

- F2B ring **cap=0x40000** + incomplete refill across 1.1 MB body → next header
  bytes are not `SBP_general2`; size field becomes garbage → BACE8 asks 1.7 GiB.
- And/or header endian path on LE size fields after stream desync.

## Guards shipped (lift-local + script)

- `games/gow2/recomp_mid_v2/patch_freelist_guard_262610.py` (idempotent)
- Applied on `gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp`
- Probe `[WADLD-ALLOC]` on BACE8 (`ppu_recomp_002.cpp`, TYMAP)

These are **safety / unstick**, not acceptance of menu. Menu still blocked
until body stream + size parse produce real next headers (BODY #2+).

## Next experiments

1. Log `F2B-STREAM-FILL` every refill (raise cap) + stream cursor/avail/file_pos
   at end of SBP body and at BACE8.
2. Dump 0x20 bytes at stream read ptr when state→2 after SBP; compare to file
   at 0x11C82C (`01 00 00 00 B4 0B 00 00 SBP_general2`).
3. If cursor short: force `f2b_stream_fill(st, member_size)` before body, or
   grow ring / multi-window pull until `file_pos` covers body+next hdr.
4. pool_idx=127/128: may need grow path when last chunk exhausted (secondary).

## Honesty

- **Proved in-boot:** hang site, insane need, rem after SBP, guard abort.
- **Not proved:** full R_Perm type walk, menu binds, B71B8 exit.
- Guard is circuit-breaker (declared); root is stream/header after multi-MB body.
