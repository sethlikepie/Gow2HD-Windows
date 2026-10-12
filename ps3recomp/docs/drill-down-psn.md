# PSN drill-down — 7 titles characterized (June 2026)

A deeper pass on a hand-picked slice of the PSN catalog: extract → decrypt →
full recomp triage (profile, function detection, import/NID profile), then
compare *across* games. The point isn't any one title — it's the **clustering**:
which games share a shell, what every title needs, and which are good recomp
targets.

## How we got decrypted EBOOTs (no per-title keys)

PSN content is NPDRM-encrypted and we hold one RAP (Tokyo Jungle). But the
archives in this set bundle a small **`*_Crack-x.yz.pkg`** next to the full
`*_Full.pkg` — a DRM-stripped (fake-signed) EBOOT. So the pipeline is:

```
outer.rar → inner.rar → {Full.pkg, Crack.pkg}
rpcs3 --installpkg Full.pkg   # game files + PARAM.SFO to dev_hdd0
rpcs3 --installpkg Crack.pkg  # overlays the DRM-free EBOOT
rpcs3 --decrypt  .../EBOOT.BIN # -> EBOOT.elf  (fake-signed, no klicensee needed)
```

7 of 8 attempted decrypted this way (PixelJunk Eden Encore's pkg didn't install
an EBOOT; titles whose crack doesn't cover the SELF still want a RAP). Scripted
in `_drill/drill.ps1`.

## The matrix

| Game | engine / type | funcs | imports | libs | NP* | top libraries |
|---|---|---:|---:|---:|---:|---|
| Everyday Shooter | native (music-reactive) | 14,353 | 123 | 12 | 0 | cellGcmSys, cellFont, **cellAtrac**, cellResc |
| Gravity Crash | native shooter | 12,713 | 318 | 27 | 90 | sceNp, sysPrxForUser, cellGcmSys, **sceNpCommerce2** |
| Echochrome | native puzzle (**SPU**) | 10,236 | 205 | 17 | 53 | sceNp, cellSysutil, cellGcmSys, **cellSpurs** |
| Snakeball | native online MP | 7,291 | 230 | 20 | 77 | **sceNp(77)**, cellGcmSys, sys_net |
| Simpsons Arcade | Konami arcade hub | 5,170 | 256 | 20 | 79 | sceNp, **cellSail**, sceNp2, cellGcmSys |
| Sonic 1 | SEGA Genesis hub | 5,160 | 257 | 19 | 75 | sceNp, **cellSail**, cellSysutil, sceNp2 |
| Gunstar Heroes | SEGA Genesis hub | 4,955 | 240 | 17 | 77 | sceNp, **cellSail**, sceNp2, cellSysutil |

_NP\* = total sceNp\* imports. All are PPC64 ET_EXEC at image base 0x10000._

## What the cross-game comparison shows

**1. The arcade-emulator collections are nearly one program.** Import-NID overlap:

| pair | shared / union | overlap |
|---|---|---:|
| Sonic 1 vs **Simpsons Arcade** | 250 / 263 | **95%** |
| Sonic 1 vs Gunstar Heroes | 233 / 264 | **88%** |
| Echochrome vs Gravity Crash | 139 / 384 | 36% |
| Gunstar vs Snakeball | 93 / 377 | 25% |
| Everyday Shooter vs Gravity Crash | 71 / 370 | 19% |

SEGA's Genesis Classics (Sonic, Gunstar, and by extension Golden Axe, Streets of
Rage, Altered Beast, …) all ride the **same hub shell** (88% identical imports),
and it's **95% identical to Konami's Simpsons Arcade hub** — both are the same
class of "emulator + PSN front-end (`cellSail` AV menu, `sceNp`/`sceNp2`
trophies/store)". **Recomp one, and you're ~90% of the way to the whole class** —
and we already have the Simpsons hub booting. This is the highest-leverage target
in the set: many shipped titles, one shared runtime.

By contrast the **native indies are each their own engine** (19–36% overlap) —
Everyday Shooter (14k funcs, a custom music/visual engine, `cellAtrac`-driven,
*zero* NP), Gravity Crash (12.7k, in-game store via `sceNpCommerce2`), Echochrome
(10k, offloads to **SPU via `cellSpurs`**). Each is a separate, larger port.

**2. There is a real universal core.** 41 import-NIDs appear in **all 7** games;
`cellGcmSys`, `sysPrxForUser`, `cellSysutil` are in every title. These are the
must-have stubs — exactly the ones the harness's cross-title ranking already
flagged, now confirmed on an independent sample.

**3. NP is everywhere — even a Genesis port.** 6 of 7 import `sceNp` heavily
(35–77 functions); trophies/store/sign-in are pervasive. The offline NP stubs
added recently (`sceNpManager*` → OFFLINE, `cellNetCtl` net-start dialog) are
broadly applicable, and `sceNpTrophy` / `sceNpCommerce2` are the next high-value
NP stubs (Gravity Crash alone imports 27 `sceNpCommerce2` functions).

**4. SPU matters for some.** Echochrome's `cellSpurs(14)` means a faithful port
needs the SPU recompilation path, not just PPU.

## Recomp-suitability ranking (from this sample)

1. **SEGA/Konami arcade-collection hub** (Sonic, Gunstar, Simpsons, …) — smallest
   (~5k funcs), offline-friendly, and one shell covers a dozen+ shipped titles.
   Simpsons already boots; the rest are mostly the same binary.
2. **Snakeball / smaller SCE first-party** — moderate size, but heavy online MP
   (`sys_net` + `sceNp` 77) means real netcode to stub or emulate.
3. **Native indies** (Everyday Shooter, Gravity Crash, Echochrome) — each a
   unique 10–14k-function engine; Echochrome additionally needs SPU.

## Boot follow-up — Sonic & Gunstar now boot (and confirm the clustering)

Pointed the generic boot harness at the two SEGA hubs (best-recall IDA-seeded
lift → clang-cl, ~33 MB exes). Both **execute real PPC code to the CRT** — the
same milestone Tokyo Jungle and Simpsons reached:

| title | lifted funcs | entry OPD | CRT syscalls hit | wall |
|---|---:|---|---|---|
| Gunstar Heroes | 15,464 | 0x1856F0 | **94** (sys_semaphore_post) | abort() |
| Sonic 1 | ~16k | 0x185AE0 | **988 + 94** | abort() |
| Tokyo Jungle | 25,696 | 0x3428F0 | 988 | abort() |

The clustering shows up even in the boot trace: **Sonic calls both `988` (Tokyo
Jungle's syscall) and `94` (Gunstar's)** — the SEGA shell straddles both. All
three hit the *same wall*: the boot harness's `lv2_syscall` only hand-implements
a few syscalls (352, 330); the CRT's `sys_semaphore_post`(94) and `988` fall to
a logger-stub returning 0, which derails a CRT table walk into `abort()`.

**Unified next step (advances all three at once):** wire the boot harness's
`lv2_syscall` to the real `runtime/syscalls` table (`lv2_register`) instead of
the inline stub — the semaphore/memory/sync syscalls already exist there. Each
boot's trace + projects are under `D:\recomp\ps3games\{gunstar,sonic1}\project\`
(`BOOT_TRACE.txt`).

These were near-free to bring up: at 88–95% import overlap they reused the
Simpsons/Tokyo-Jungle stubs and toolkit fixes verbatim — exactly the payoff the
clustering predicted.

## Does a "driver" exist for Tokyo Jungle? (SCE first-party doesn't cluster)

The arcade hubs cluster because they're literally the same program (88–95%). The
question was whether SCE Japan Studio's 3D titles cluster the same way, so one
bring-up could *drive* Tokyo Jungle. They don't:

| pair | overlap |
|---|---:|
| Echochrome vs **Echochrome ii** (same series, same studio) | **54%** |
| Echochrome vs Tokyo Jungle | 39% |
| Echochrome ii vs Tokyo Jungle | 35% |
| Super Stardust HD vs Tokyo Jungle | 39% |

Even a direct sequel by the same studio is only 54% — and Echochrome ii drifts
*further* from TJ than the original, because it's a **PlayStation Move** title
(`libgem(17)` + `cellCamera(13)` + `sceNpTus(22)` that the first game lacks).

**Conclusion:** Tokyo Jungle has no engine-twin. SCE first-party games are
bespoke engines over a shared middleware *base* (~35–40%: `cellGcmSys`,
`cellSpurs`, `cellResc`, `sceNp`, `cellSysutil`, `sysPrxForUser`). So nothing
"drives" TJ the way Sonic rode on Simpsons — the leverage is instead (a) the
**generic CRT/OPD function-pointer fix** (which every title needs, see the boot
follow-up above) and (b) hardening those **shared middleware HLE modules**. Both
advance the whole SCE 3D class — Tokyo Jungle, Echochrome, Super Stardust —
together, just not via a single sibling.

## Caveats
- Function counts are `.opd`-hardened `find_functions` (pre-`--seed-json`); the
  Ghidra/IDA cross-check would refine them per title.
- "Crack" EBOOTs are community-modified; fine for analysis, but a shipped port
  should re-derive from a clean dump.
- PixelJunk Eden Encore didn't install an EBOOT (different pkg layout) — not
  triaged.
