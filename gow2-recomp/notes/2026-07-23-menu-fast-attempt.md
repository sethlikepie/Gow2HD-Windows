# Menu-fast (skip intro → menu) attempt 2026-07-23

## Recipe

```bash
./rodar_gow2_menu_fast.sh
# or:
PS3_VDEC_FORCE_SEQDONE_MS=1500 PS3_BOOT_LOGO_MS=200 \
PS3_AUTO_LOAD_RUN=1 PS3_PAD_AUTOSTART=1 PS3_TYPE15_UNSTICK=1 \
PS3_SPU1=1 PS3_SPU4=1 PS3_SPU5=1 \
PS3_RSX_BACKEND=metal PS3_FULLSCREEN=0 TIMEOUT=90 \
./rodar_gow2_menu_fast.sh
```

## Mutations shipped

| Change | Where | Purpose |
|--------|-------|---------|
| FORCE SEQDONE 1.5s | env | skip intro movies fast |
| BOOT_LOGO_MS 200 | env | short SCEA/Bluepoint hold |
| REPLAY-NOPIC | cellVdec (prior) | re-Play GetPicture hang bypass |
| TYPE15 UNSTICK +4=1 | CB56C | seed f4 (alone insufficient) |
| **CC9D0-SKIP** + giant-lock yield | `func_000CC9D0` | skip incomplete TYPE15 product tick |
| SPU4/5 opt-in | register | frontend workloads |

## In-boot (Metal, 90s) — `menu_fast_metal.log`

| Signal | Result |
|--------|--------|
| StartSeq | **2** (FORCE×2, ~1.5s each) |
| thr_auto_load end | **1** @ ~34s |
| R_Perm full 20MB | **1** |
| logo DONE | **2** |
| SetFlip during intro | **~2200** |
| **SetFlip after R_Perm/B71** | **0** |
| cellPadGetData | **0** |
| st620 post thr | only **0→0** |
| Menu / NewGame UI | **not observed** |
| FATAL | 0 |

## Wall blocking menu (unchanged root)

After WAD + B71, main never returns to flip/pad:

1. CB56C installs TYPE15 products via **shell/reuse + skip_icall2**.
2. Scene ticks them in `func_000CC9D0` forever (objs `0x4066D798` / `0x4066D804`, product `0x42F85AE4`).
3. CC9D0-SKIP makes each tick cheap + yields, but the **parent list walk still re-visits only these two** — no path to `SetFlip` / `cellPadGetData`.

Full natural `icall2` (`PS3_TYPE15_FORCE_ICALL2=1`) **hangs** before thr (prior note).

## Honest status

| Goal | Status |
|------|--------|
| Skip intro fast | **PASS** (FORCE 1.5s) |
| Reach AUTO_LOAD + R_Perm | **PASS** |
| Reach **main menu UI** | **FAIL** — TYPE15 incomplete attach / CC9D0 list spin |
| Playable | **not claimed** |

## Next (single gate)

Fix TYPE15 **product construction** so icall2+2A4FE4 succeed (non-shell product),  
**or** remove/disable the two broken components from the scene tick list so main  
returns to the frame loop (flip + pad) after B71.
