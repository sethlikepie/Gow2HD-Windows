# TYPE15 fix: product+0x70 list sanitize + full attach (2026-07-23)

## Root cause (verified)

`func_002A4FE4` walks `product+0x70` as a **circular** intrusive list whose
sentinel is `product+0x70` itself (`func_002A5024` inits both links to self).

Reused TYPE15 product `0x42F85AE4` kept a **NULL-terminated** pool (12 nodes ×
`0xD8`). Walk: last real → NULL → poison `0x27182818` → NULL … forever.
That is why `PS3_TYPE15_FORCE_ICALL2=1` hung before thr.

## Fix (shipped)

| Piece | Change |
|-------|--------|
| `ps3_type15_product_list_reset(prod)` | Reset `+0x70/+0x74` to empty circular |
| Freelist shell copy | Reset list after template copy |
| `func_002A4FE4` | SANITIZE bad head; CLOSE-TAIL on NULL/poison; hard LOOP-CAP 64k |
| `func_000CB56C` | **Default full attach** (icall2 + 2A4FE4) after list reset; legacy skip via `PS3_TYPE15_SKIP_ATTACH=1` |

## In-boot proof (`type15_fix.log`, menu-fast recipe)

```
[TYPE15] product list RESET prod=0x42F85AE4 was_head=0x42F85F10 -> circular empty
[POSTINTRO] CB56C icall2 … code=0x0039D428
[POSTINTRO] CB56C obj+8=product=0x42F85AE4 attach=full reused=1
[POSTINTRO] CB56C after 2A4FE4          ← ×2, no hang
thr_auto_load() end
R_Perm full 20169344
```

| Metric | Before (skip attach) | After (this fix) |
|--------|----------------------|------------------|
| 2A4FE4 on reuse | hang if forced | **completes** |
| attach=full | 0 | **2** |
| thr_end | 1 (only with skip) | **1** (with full attach) |
| FATAL | 0 | 0 |

## Still open (not this fix)

- Main menu UI / post-thr SetFlip still blocked (CC9D0 tick of TYPE15 components
  with f4=0 may be correct idle; flip dies after B71 — next wall).
- Flaky runs that never reach WAD (post-movie st620 0→0 only) remain separate.

## Opt-out

- `PS3_TYPE15_SKIP_ATTACH=1` — old skip icall2/2A4FE4 on reuse/shell.
