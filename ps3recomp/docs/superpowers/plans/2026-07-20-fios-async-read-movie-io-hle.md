# FIOS async-read → movie_io HLE (R_PermA stream) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

## Status 2026-07-20 late — NÃO REABRIR (refutação mantida)

| Tasks | Estado |
|-------|--------|
| 1–3, 7 | ✅ |
| 4–6 (HLE 002B3D1C in-boot) | ⛔ **bloqueadas por refutação** — `func_002B3D1C` nunca entra no boot medido; open m2v é completado por `func_00306534` na thread fios scheduler |
| Open/poll intro | tratado no plano **4.0** (sticky/play-already) — **não** por este HLE |

**Actualização correlacionada (4.0, não este plano):** o consumo de `[op+0x90]` no poll do estado 1 foi destravado com sticky done + Play already-active. Isso **não** reabre Tasks 4–6 daqui: o path de *bulk async-read* de R_PermA continua não exercitado via `002B3D1C`.

Só reabrir T4–T6 se nova medição in-boot mostrar `[AREAD]` / entrada em `002B3D1C` com controlo positivo.

---

**Goal:** Get the guest's bulk async-read path (`func_002B3D1C`) to actually deliver bytes from the host-backed pre-extracted psarc members (`movie_cache/R_PermA.wad_ps3`, 20169344 bytes) into the guest ring, so the asset/shader pipeline stops being starved for data.

**Architecture:** The Windows-proven path never relied on FIOS submitting ANY2ANY inflate commands for `R_PermA`. That WAD is **raw** once extracted; `push2=0` / `HOSTINFL=0` for it is **correct**. The missing piece is an **idempotent lift patch** that rewrites the entry of `func_002B3D1C` so, when the stream is backed by a `movie_io` fd (or by a path resolvable in `PS3_MOVIE_CACHE`), the host fulfills the read via `movie_io_pread`, updates the op fields the natural poll (`func_002B45D0`) already understands, and returns. Probe first (layout from live op dumps), then HLE. Natural FIOS/psarc block inflate (DIR_ANY2ANY) is a **separate, later** track — not the R_PermA fix.

**Tech Stack:** `libs/video/movie_hle.c` (`movie_io_*`), lift patches in `../gow2-recomp/recomp_mid_v2/patch_*.py`, `apply_all_patches.sh --check`, `boot_gow2` + env from `env_gow2.sh`, offline C unit tests under `libs/video/tests/` and/or `runtime/ppu/tests/`.

**Repos:** macOS/arm64 native, sibling checkout `../gow2-recomp` (lift at `recomp_macos_v2/`, patches at `recomp_mid_v2/`). Motor at this repo (`ps3recomp`).

## Global Constraints

- Lift output (`ppu_recomp_*.cpp`) is gitignored and regenerated → every fix is an **idempotent** `patch_*.py` under `../gow2-recomp/recomp_mid_v2/`, re-applied by `../gow2-recomp/apply_all_patches.sh` (has `--check` with markers).
- HLE allowed when the real path is unreachable; **prefer completing the guest's own op protocol** (same fields `func_002B45D0` reads) over inventing a parallel FSM.
- **Forbidden as fixes:** CRC bypass, stubbing combinations, hand-stamping magic, cosmetic null-checks, masking guards. Probes env-gated, OFF by default, no-op on baseline.
- Faithful to CELL/PPC behaviour at the **contract** of the op (done flags, pos advance, bytes into ring). Timing may be synchronous-eager (Windows path was too).
- Acceptance always labels: **in-boot** vs **offline/unit** vs **not exercised**. Never claim "R_PermA streams" without a log line.
- No game data in git. Commits in Portuguese, no Co-Authored-By. Push only personal remotes.

---

## Design answers (a–e) — read before Task 1

### (a) Where should the hook live?

**Decision: rewrite the body entry of `func_002B3D1C` via `patch_2b3d1c_movie_io.py`, not call-site rewrites, not ANY2ANY force-feed.**

| Seam | Verdict | Why |
|------|---------|-----|
| **Entry of `func_002B3D1C`** (chosen) | **Yes** | Single choke point. Exactly 2 direct call sites in `ppu_recomp_001.cpp` (~35668, ~35709), both already call this function. Windows evidence named this function. Op layout is local (`r3` = op). |
| Rewrite call sites | No | Duplicates logic, breaks if a 3rd site appears, harder idempotent needles. |
| Hook `func_0030FC00` (I/O submit inside 002B3D1C) | Secondary only | More callers across chunks; harder to know which submits are movie-backed. Use only if probe proves dest/fd live only there. |
| Force DIR_ANY2ANY / inflate | **Wrong primary fix for R_PermA** | `R_PermA.wad_ps3` is raw (`WAD_R_Perm…`). Asset map already proved `HOSTINFL=0` is expected. Inflate channel silence is not the R_PermA stream bug. |
| cellFs-only (`ppu_fs.cpp`) | Insufficient alone | Bulk WAD traffic goes through guest FIOS async ops, not `cellFsRead` loops. `movie_io_*` already has cellFs callers; the gap is the **async op**. |

Gate the HLE on `PS3_MOVIE_IO=1` (already the product gate). When the HLE cannot identify a host file, **fall through to the natural lifted body** — never mask failure.

### (b) How to discover op layout without guessing?

Two sources, in order:

1. **Static (already available, offline):** the lift of `func_002B3D1C` / `func_002B45D0` / `func_002B3E0C` in `recomp_macos_v2/ppu_recomp_001.cpp` shows:
   - `op+0x4` read as arg into `func_0030FC00` (likely handle / media object)
   - `op+0x8` written with status after submit (`func_002B3E0C`); poll `func_002B45D0` reads `op+0x8` and `op+0xC`
   - `op+0xC` flags: bit set with `\| 0x4000` (in-flight); `rlwinm … 22,22` tests a flag; poll clears bits and sets done path
   - `op+0x10` end/limit; `op+0x14` cursor (advanced by `n` before/during submit)
   - Call ABI: `r3=op`, `r4=dest EA`, `r5=want bytes`
2. **Dynamic (required before locking HLE offsets that are not proven by (1)):** gated probe `PS3_TRACE_AREAD=1` at entry of `func_002B3D1C` dumping `r3..r7` and `vm_read32(op+0x00..+0x1C)`, plus first successful `movie_io` open's fd token if any. Corroborate file `pos` against Windows log shape (`pos=… want=… dst=…`).

Do **not** hardcode `op=0x008695D4` — that EA is per-run heap. Discover at runtime from `r3`.

### (c) Minimal red test cheaper than a 40s boot?

Layered:

| Layer | Cost | What fails today |
|-------|------|------------------|
| **L0 offline unit** | <1s | Host helper `ps3_fios_aread_hle(op,dst,n)` missing / returns 0 on synthetic op + temp cache file |
| **L1 patch check** | <1s | `apply_all_patches.sh --check` fails missing marker `AREAD-HLE` in `func_002B3D1C` |
| **L2 short boot** | ~15–25s | With intro-skip recipe: no `[movieio] open 'R_PermA'` **or** open exists but `bytes_read` stays ~0 / only header |

L0+L1 are the day-to-day red/green. L2 is the in-boot acceptance. Full 40s asset smoke stays the regression net, not the edit loop.

### (d) TOC inflate vs R_PermA — ordering

**Measured facts:**

- `gow2.psarc` magic `PSAR`, compression tag `zlib`, `toc_length=297560` (matches your TOC read).
- Host tool `tools/psarc_list.py` parses the **TOC as raw bytes** (zlib applies to **file data blocks**, not the TOC table itself for this archive layout).
- `movie_cache/R_PermA.wad_ps3` is already the **fully decompressed** member (`WAD_R_Perm…`, 20169344 B). Serving it via `movie_io` **does not** require ANY2ANY.
- Windows chain that produced working evidence: `[movieio] open 'R_PermA'` → HLE `func_002B3D1C` → ring fill. **Not** psarc block inflate.

**Conclusion:**

- **Precondition for R_PermA on the proven path:** intro/FSM reaches a guest open of the bare name `R_PermA` (or path ending in it) while `PS3_MOVIE_IO=1` and the cache file exists. **Not** TOC inflate.
- **If** the boot only ever opens `gow2.psarc` and never `R_PermA` by name, you are on the **natural dearchive** track: then member **block** inflate (DIR_ANY2ANY pushes of 32B cmds) becomes the next wall — track B in Task 7. That is **not** what Windows used for the first full stream.
- Your observation `push2 count = 0` is therefore **expected** on the movie_io path and is **not** the red test for this plan's green.

### (e) Divergence risk and bounds

| Risk | Bound |
|------|-------|
| Completes sync while console is async | Same as Windows HLE; poll `func_002B45D0` still runs natural code and must see "done". Prefer writing the **same** status transitions the natural body uses (`op+0x8`, `op+0xC` bit clears). |
| Wrong ring dest / wrap | Probe must print `dst` and `n`; compare first windows to Windows (`~0x40080000`, ~64–147KB). HLE copies only `min(n, remaining)` into `vm_base+dst`. |
| Hijacking non-movie streams | Activate only when `PS3_MOVIE_IO=1` **and** the backing identity is a movie_io slot / cache-resolved name. Else fall through. |
| Skipping guest media thread side-effects | Accept only if L2 shows consumer progress (bytes_read climbs, no new spin). If side-effects missing, narrow HLE to "fill + done" after natural flag set, or call guest completion helper if probe names one. |
| Claiming inflate fixed | Do **not**. Log `[AREAD-HLE]` and `[movieio] stats`; `HOSTINFL` may stay 0. |

---

## File map

| File | Role |
|------|------|
| `libs/video/movie_hle.c` | Existing `movie_io_*` (POSIX OK). Possibly add a thin lookup helper if fd is not in the op. |
| `libs/video/tests/test_fios_aread_hle.c` (**create**) | L0 offline unit for the HLE helper. |
| `runtime/ppu/fios_aread_hle.c` (**create**) or section in `movie_hle.c` | Host helper called from patched lift: given op EA + dest + n, pread + update op. |
| `../gow2-recomp/recomp_mid_v2/patch_2b3d1c_movie_io.py` (**create**) | Idempotent entry rewrite of `func_002B3D1C` + optional probe in `func_002B45D0`. |
| `../gow2-recomp/apply_all_patches.sh` | Add check marker `AREAD-HLE` for `func_002B3D1C`. |
| `../gow2-recomp/smoke_aread_mac.sh` (**create**) | L2 short boot acceptance. |
| `../gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp` | **Patched only via script** (gitignored). |
| `docs/gow2-asset-pipeline-map.md` | After green: note that the 002B3D1C HLE is now committed as `patch_2b3d1c_movie_io.py`. |

---

### Task 1: Discriminating baseline — which path is live today?

> ✅ **FEITO** — baseline classe B: R_PermA nunca aberto

**Files:**
- Read only: `../gow2-recomp/env_gow2.sh`, `../gow2-recomp/movie_cache/R_PermA.wad_ps3`, `../gow2-recomp/smoke_boot_mac.sh`
- Create: `../gow2-recomp/smoke_aread_baseline.sh` (temporary baseline logger; may be folded into Task 6)

**Purpose:** Separate "R_PermA never opens" from "opens but async read never delivers" from "natural psarc-only path". Do not implement HLE yet.

- [x] **Step 1: Confirm cache + env**

```bash
cd ../gow2-recomp
test -f movie_cache/R_PermA.wad_ps3
test "$(stat -f%z movie_cache/R_PermA.wad_ps3)" = "20169344"
xxd movie_cache/R_PermA.wad_ps3 | head -1   # expect WAD_R_Perm
. ./env_gow2.sh
echo "MOVIE_IO=$PS3_MOVIE_IO CACHE=$PS3_MOVIE_CACHE VFS=$PS3_VFS_ROOT"
```

Expected: size 20169344, magic contains `WAD_R_Perm`, `PS3_MOVIE_IO=1`.

- [x] **Step 2: 25s boot with asset + FS traces (RED baseline)**

```bash
cd ../gow2-recomp
. ./env_gow2.sh
export PS3_NO_RSX=1
export PS3_TRACE_ASSET=1
export PS3_VDEC_ASYNC=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000   # force past intro if needed
export PS3_PAD_AUTOSTART=1
LOG=/tmp/aread_base.log
./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 &
PID=$!; sleep 25; kill -9 $PID 2>/dev/null; wait $PID 2>/dev/null || true

echo "=== movieio opens ==="; grep -a "\[movieio\] open" "$LOG" | head -20
echo "=== R_PermA ==="; grep -a "R_PermA\|r_perma" "$LOG" | head -30
echo "=== psarc ==="; grep -a "gow2.psarc\|psarc" "$LOG" | head -20
echo "=== stats ==="; grep -a "stats.*R_PermA\|stats.*r_perma" "$LOG" || true
echo "=== HOSTINFL/LFQ push2 ==="; grep -ac "\[HOSTINFL\]" "$LOG"; grep -ac "push2" "$LOG"
```

**Classify (pick one, write into the log header as a comment in your notes):**

| Class | Evidence | Next |
|-------|----------|------|
| **A** | `[movieio] open 'R_PermA'` and `bytes_read` ≪ size (or no stats) | HLE of 002B3D1C is the right wall (expected) |
| **B** | No R_PermA open; only `gow2.psarc` header/TOC | Intro/FSM or open-path wall first (still do Tasks 2–3 probes; HLE waits on open) |
| **C** | R_PermA open and `bytes_read` near 20169344 | Hook already somehow live — stop; re-measure push2 claim |

- [x] **Step 3: Commit only the baseline script if you keep it**

```bash
# only if smoke_aread_baseline.sh is worth keeping
cd ../gow2-recomp
git add smoke_aread_baseline.sh
git commit -m "chore: baseline smoke do caminho movie_io / R_PermA (sem HLE)"
```

**Acceptance (Task 1):** in-boot classification A/B/C recorded with log path. No code change required for green of later tasks if class A; class B adds "must see open" as an L2 precondition.

---

### Task 2: Gated probe patch — dump op layout at runtime

> ✅ **FEITO** — abc299f + e624e02 — sonda com controlo positivo: func_002B3D1C NUNCA entra

**Files:**
- Create: `../gow2-recomp/recomp_mid_v2/patch_2b3d1c_probe.py`
- Modify: `../gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp` (via script only)
- Modify: `../gow2-recomp/apply_all_patches.sh` (optional check for probe marker; probe-only may stay out of hard checks)

**Interfaces:**
- Produces: log lines `[AREAD] #N op=0x… r4=dst r5=n +00=… +04=… +08=… +0C=… +10=… +14=… +18=…`
- Consumes: `PS3_TRACE_AREAD=1` (OFF default)

- [x] **Step 1: Write the probe patch (model: `patch_1856a8_stream_opd.py`)**

```python
#!/usr/bin/env python3
"""Gated probe at entry of func_002B3D1C — dump op words + ABI.

Env: PS3_TRACE_AREAD=1 (OFF default). Idempotent. Lift dir = sys.argv[1].
"""
from pathlib import Path
import sys

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
# Prefer chunk that actually contains the function (001 on current lift).
candidates = sorted(ROOT.glob("ppu_recomp_*.cpp"))
target = None
for p in candidates:
    if "void func_002B3D1C" in p.read_text(encoding="utf-8", errors="replace"):
        target = p
        break
if not target:
    raise SystemExit("func_002B3D1C missing in lift")

s = target.read_text(encoding="utf-8", errors="replace")
i = s.find("void func_002B3D1C")
if i < 0:
    raise SystemExit("002B3D1C missing")
j = s.find("void func_", i + 10)
region = s[i:j]

if "AREAD-PROBE" in region:
    print("002B3D1C probe already applied")
else:
    # Insert immediately after the opening brace of the function.
    brace = region.find("{")
    if brace < 0:
        raise SystemExit("no opening brace")
    probe = r'''
        /* AREAD-PROBE: gated dump of async-read op (PS3_TRACE_AREAD=1). */
        { static int on=-1; if(on<0){extern char* getenv(const char*);
            const char*e=getenv("PS3_TRACE_AREAD"); on=(e&&*e&&*e!='0')?1:0;}
          if(on){ static int n=0; if(n++<64){
            uint32_t op=(uint32_t)ctx->gpr[3];
            fprintf(stderr,
              "[AREAD] #%d op=0x%08X r4=0x%08X r5=%u "
              "+00=%08X +04=%08X +08=%08X +0C=%08X +10=%08X +14=%08X +18=%08X +1C=%08X\n",
              n, op, (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5],
              op?vm_read32(op+0x00):0, op?vm_read32(op+0x04):0,
              op?vm_read32(op+0x08):0, op?vm_read32(op+0x0C):0,
              op?vm_read32(op+0x10):0, op?vm_read32(op+0x14):0,
              op?vm_read32(op+0x18):0, op?vm_read32(op+0x1C):0);
            fflush(stderr);} } }
'''
    region = region[: brace + 1] + probe + region[brace + 1 :]
    s = s[:i] + region + s[j:]
    target.write_text(s, encoding="utf-8", newline="\n")
    print(f"patched probe into {target.name}")
print("OK patch_2b3d1c_probe")
```

- [x] **Step 2: RED — marker absent before apply**

```bash
cd ../gow2-recomp
grep -n "AREAD-PROBE" recomp_macos_v2/ppu_recomp_001.cpp || echo "RED ok: no probe yet"
```

Expected: no matches (or only if re-running).

- [x] **Step 3: Apply + rebuild chunk 001 only (faster than full boot rebuild if your build script supports it)**

```bash
cd ../gow2-recomp
python3.12 recomp_mid_v2/patch_2b3d1c_probe.py recomp_macos_v2
# second run must be no-op:
python3.12 recomp_mid_v2/patch_2b3d1c_probe.py recomp_macos_v2
# rebuild (use project script; full build_macos.sh if no chunk target)
./build_macos.sh
```

- [x] **Step 4: Boot with probe (in-boot measurement)**

```bash
. ./env_gow2.sh
export PS3_NO_RSX=1 PS3_TRACE_AREAD=1 PS3_TRACE_ASSET=1
export PS3_VDEC_ASYNC=1 PS3_VDEC_FORCE_SEQDONE_MS=8000
LOG=/tmp/aread_probe.log
./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 &
PID=$!; sleep 25; kill -9 $PID 2>/dev/null; wait $PID 2>/dev/null || true
grep -a "\[AREAD\]" "$LOG" | head -20
grep -a "\[movieio\]" "$LOG" | head -20
```

**If zero `[AREAD]` lines:** `func_002B3D1C` is never entered in this window → upstream open/FSM (class B). Still implement HLE (Tasks 3–5) but L2 acceptance needs the open first; add a note, do not invent calls.

**If lines appear:** extract the stable field meanings into a short table in your working notes (file pos candidate = which of +04/+14/+18 tracks the Windows `pos=` column).

- [x] **Step 5: Commit probe script**

```bash
cd ../gow2-recomp
git add recomp_mid_v2/patch_2b3d1c_probe.py
git commit -m "feat: probe gated PS3_TRACE_AREAD em func_002B3D1C"
```

**Acceptance (Task 2):** offline — script idempotent, marker present. in-boot — either ≥1 `[AREAD]` line **or** documented zero with class B evidence. Layout table written (even if partial from static analysis only).

---

### Task 3: Offline HLE helper (TDD L0) — no boot

> ✅ **FEITO** — ca2c303 — helper offline, 10 casos, 7 mutacoes

**Files:**
- Create: `libs/video/fios_aread_hle.c` (or `runtime/ppu/fios_aread_hle.c` if you prefer pure runtime — pick **one**; recommended next to movie_io: `libs/video/fios_aread_hle.c`)
- Create: `libs/video/fios_aread_hle.h`
- Create: `libs/video/tests/test_fios_aread_hle.c`
- Modify: `scripts/smoke_macos.sh` to compile+run the new test (section next to movie io)
- Modify: `CMakeLists.txt` only if the boot link pulls objects from the archive by explicit list — otherwise the test links the `.c` directly like `test_movie_io.c`

**Interfaces:**
- Produces:
  ```c
  /* Returns bytes written into dst, or 0 if not handled (caller falls through). */
  unsigned ps3_fios_aread_hle(uint32_t op_ea, uint32_t dst_ea, uint32_t n,
                              unsigned movie_fd /* 0 = unknown */);
  ```
- Consumes: `movie_io_pread`, `movie_io_is`, `vm_base` / `vm_read32` / `vm_write32` (declare extern like other HLE)

**Op contract to implement (lock after Task 2; defaults from static lift + Windows notes):**

```
// Defaults — override only if Task 2 dumps disagree:
//   r3=op, r4=dst, r5=n
//   file_pos  = op+0x14  (Windows: after HLE becomes pos+n)
//   OR file_pos tracked separately — if +14 is a window cursor, use the
//   field Task 2 correlated with Windows pos= column.
//   On success:
//     copy n' bytes to vm_base+dst
//     op+0x14 = old_pos + n'   (if +14 is file pos)  OR update measured field
//     op+0x8  = 0              (done — Windows)
//     clear in-flight bit on op+0xC (mask off the 0x4000 bit the natural body sets)
//     return n' (guest r3)
```

- [x] **Step 1: Write the failing unit test**

```c
/* libs/video/tests/test_fios_aread_hle.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

/* Minimal vm stubs for the unit — same idea as other host tests. */
unsigned char* vm_base;
static unsigned char arena[1 << 20];

uint32_t vm_read32(uint32_t ea) {
    return ((uint32_t)arena[ea] << 24) | ((uint32_t)arena[ea+1] << 16)
         | ((uint32_t)arena[ea+2] << 8) | (uint32_t)arena[ea+3];
}
void vm_write32(uint32_t ea, uint32_t v) {
    arena[ea] = (v >> 24) & 0xFF; arena[ea+1] = (v >> 16) & 0xFF;
    arena[ea+2] = (v >> 8) & 0xFF; arena[ea+3] = v & 0xFF;
}

/* From movie_hle.c */
unsigned movie_io_open(const char* guest_path, unsigned* out_size);
unsigned movie_io_pread(unsigned fd, void* dst, unsigned n, unsigned pos);
void     movie_io_close(unsigned fd);
int      movie_io_is(unsigned fd);

unsigned ps3_fios_aread_hle(uint32_t op_ea, uint32_t dst_ea, uint32_t n,
                            unsigned movie_fd);

static void be_write_file(const char* path, const char* data, size_t n) {
    FILE* f = fopen(path, "wb");
    fwrite(data, 1, n, f);
    fclose(f);
}

int main(void) {
    vm_base = arena;
    memset(arena, 0, sizeof arena);

    char dir[] = "/tmp/aread_cache_XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    char path[512];
    snprintf(path, sizeof path, "%s/R_PermA.wad_ps3", dir);
    const char payload[] = "WAD_R_Perm\0\0\0\0" "HELLO-AREAD-PAYLOAD-0123456789";
    be_write_file(path, payload, sizeof payload);

    setenv("PS3_MOVIE_IO", "1", 1);
    setenv("PS3_MOVIE_CACHE", dir, 1);

    unsigned sz = 0;
    unsigned fd = movie_io_open("R_PermA", &sz);
    if (!fd || !movie_io_is(fd)) {
        fprintf(stderr, "FAIL open\n"); return 1;
    }

    const uint32_t op = 0x1000;
    const uint32_t dst = 0x8000;
    /* Synthesize op: +08=busy, +0C=in-flight, +10=limit, +14=pos */
    vm_write32(op + 0x08, 1);
    vm_write32(op + 0x0C, 0x4000);
    vm_write32(op + 0x10, 0x100000);
    vm_write32(op + 0x14, 0); /* pos 0 */

    unsigned got = ps3_fios_aread_hle(op, dst, 16, fd);
    if (got != 16) {
        fprintf(stderr, "FAIL got=%u want=16\n", got); return 1;
    }
    if (memcmp(arena + dst, payload, 16) != 0) {
        fprintf(stderr, "FAIL payload mismatch\n"); return 1;
    }
    if (vm_read32(op + 0x08) != 0) {
        fprintf(stderr, "FAIL op+8 not done\n"); return 1;
    }
    if (vm_read32(op + 0x14) != 16) {
        fprintf(stderr, "FAIL op+14 pos=%u\n", vm_read32(op + 0x14)); return 1;
    }
    /* fallthrough: unknown fd must return 0 */
    if (ps3_fios_aread_hle(op, dst, 4, 0) != 0) {
        fprintf(stderr, "FAIL should decline fd=0 without mapping\n"); return 1;
    }

    movie_io_close(fd);
    printf("PASS test_fios_aread_hle\n");
    return 0;
}
```

- [x] **Step 2: Run test — RED**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
clang -std=c11 -O1 -I include \
  libs/video/tests/test_fios_aread_hle.c libs/video/movie_hle.c \
  -o /tmp/test_fios_aread_hle 2>&1 | tail -20
```

Expected: **link failure** `Undefined symbols: _ps3_fios_aread_hle` (or compile error if the header is included but missing).

- [x] **Step 3: Implement minimal helper**

```c
/* libs/video/fios_aread_hle.c — sketch; match header + test */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned char* vm_base;
extern uint32_t vm_read32(uint32_t ea);
extern void     vm_write32(uint32_t ea, uint32_t v);
extern unsigned movie_io_pread(unsigned fd, void* dst, unsigned n, unsigned pos);
extern int      movie_io_is(unsigned fd);

static int aread_enabled(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_MOVIE_IO");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}

unsigned ps3_fios_aread_hle(uint32_t op_ea, uint32_t dst_ea, uint32_t n,
                            unsigned movie_fd)
{
    if (!aread_enabled() || !op_ea || !dst_ea || !n) return 0;
    if (!movie_fd || !movie_io_is(movie_fd)) return 0;
    if (!vm_base) return 0;

    uint32_t pos = vm_read32(op_ea + 0x14);
    uint32_t lim = vm_read32(op_ea + 0x10);
    if (lim && pos >= lim) return 0;
    if (lim && n > lim - pos) n = lim - pos;

    unsigned got = movie_io_pread(movie_fd, vm_base + dst_ea, n, pos);
    if (!got) return 0;

    vm_write32(op_ea + 0x14, pos + got);
    vm_write32(op_ea + 0x08, 0); /* done */
    uint32_t fl = vm_read32(op_ea + 0x0C);
    vm_write32(op_ea + 0x0C, fl & ~0x4000u); /* clear in-flight if present */

    { static int tr = -1; if (tr < 0) {
        const char* e = getenv("PS3_TRACE_AREAD");
        tr = (e && *e && *e != '0') ? 1 : 0; }
      if (tr) { static int c=0; if (c++ < 48)
        fprintf(stderr, "[AREAD-HLE] op=0x%08X dst=0x%08X pos=%u n=%u got=%u\n",
                op_ea, dst_ea, pos, n, got); } }
    return got;
}
```

Adjust `pos` field offset if Task 2 proved a different word.

- [x] **Step 4: Run test — GREEN**

```bash
clang -std=c11 -O1 -I include \
  libs/video/tests/test_fios_aread_hle.c \
  libs/video/movie_hle.c libs/video/fios_aread_hle.c \
  -o /tmp/test_fios_aread_hle && /tmp/test_fios_aread_hle
```

Expected: `PASS test_fios_aread_hle`.

- [x] **Step 5: Wire into `scripts/smoke_macos.sh` (section 3c)**

Mirror section 3 / 3b: compile with `movie_hle.c` + `fios_aread_hle.c`, require `PASS test_fios_aread_hle`.

- [x] **Step 6: Commit (motor repo)**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
git add libs/video/fios_aread_hle.c libs/video/fios_aread_hle.h \
        libs/video/tests/test_fios_aread_hle.c scripts/smoke_macos.sh
# CMakeLists only if you had to list the new .c for the archive
git commit -m "feat: HLE host de async-read FIOS via movie_io (unit L0)"
```

**Acceptance (Task 3):** **offline/unit** green. Not yet in-boot.

---

### Task 4: Idempotent lift patch — early-out in `func_002B3D1C`

> ⛔ **BLOQUEADA POR REFUTACAO** — a Task 2 provou, com controlo positivo
> (`nm` mostra os estaticos da sonda no simbolo certo; `objdump` mostra o gate
> como as PRIMEIRAS instrucoes; os 7 call sites e a entrada da tabela entram
> todos pelo topo), que **`func_002B3D1C` nunca e chamado**. Estas tasks
> existem para lhe pendurar um early-out e ligar o helper — nao ha onde
> pendurar. Alem disso a Task 4 do plano intro-audio-open-wall mediu que a op
> de open E completada, por `func_00306534` a partir da thread "fios
> scheduler" do jogo, e que o handler async-read serve **reads** (opcode
> diferente). Nao reabrir sem primeiro refutar essas duas medicoes.


**Files:**
- Create: `../gow2-recomp/recomp_mid_v2/patch_2b3d1c_movie_io.py`
- Modify: `../gow2-recomp/apply_all_patches.sh` (`run_checks` marker)
- Patches lift: `recomp_macos_v2/ppu_recomp_001.cpp`

**Interfaces:**
- Consumes: `unsigned ps3_fios_aread_hle(uint32_t,uint32_t,uint32_t,unsigned);`
- Produces: marker `AREAD-HLE` inside `func_002B3D1C`
- FD discovery (implement the first option that Task 2 supports):
  1. If `op+0x4` is a `movie_io_is` fd → use it.
  2. Else if a side table was filled at `movie_io_open` of R_PermA (add `movie_io_last_fd_for("R_PermA")` only if needed).
  3. Else return 0 → natural body.

- [ ] **Step 1: RED — check marker missing**

```bash
cd ../gow2-recomp
./apply_all_patches.sh --check recomp_macos_v2
# after adding the check, expect FAIL on AREAD-HLE until patch applied
```

Add to `run_checks` in `apply_all_patches.sh`:

```python
# N) AREAD-HLE: func_002B3D1C early-out to movie_io host pread
try:
    text = ""
    for p in sorted(lift.glob("ppu_recomp_*.cpp")):
        t = p.read_text(encoding="utf-8", errors="replace")
        if "void func_002B3D1C" in t:
            text = t; break
    i = text.find("void func_002B3D1C")
    ok = i >= 0 and "AREAD-HLE" in text[i:i+2500]
    report("002B3D1C AREAD-HLE movie_io", ok,
           "marcador AREAD-HLE ausente em func_002B3D1C")
except Exception as e:
    report("002B3D1C AREAD-HLE movie_io", False, "erro: %r" % (e,))
```

- [ ] **Step 2: Write `patch_2b3d1c_movie_io.py`**

Structure (exact needle must match **your** current lift prologue — copy the first 8–12 real lines of `func_002B3D1C` from `ppu_recomp_001.cpp` into `old`; do not invent whitespace):

```python
#!/usr/bin/env python3
"""HLE early-out for func_002B3D1C: fulfill async reads from movie_io.

Idempotent. Lift dir = sys.argv[1]. Marker: AREAD-HLE.
When PS3_MOVIE_IO is on and the helper accepts the op, skip the natural
FIOS submit path. Otherwise fall through unchanged.
"""
from pathlib import Path
import sys

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
target = None
for p in sorted(ROOT.glob("ppu_recomp_*.cpp")):
    if "void func_002B3D1C" in p.read_text(encoding="utf-8", errors="replace"):
        target = p
        break
if not target:
    raise SystemExit("func_002B3D1C missing")

s = target.read_text(encoding="utf-8", errors="replace")

# Ensure extern decl once near top-of-file helpers (after first extern "C" block).
DECL = 'extern "C" unsigned ps3_fios_aread_hle(uint32_t op_ea, uint32_t dst_ea, uint32_t n, unsigned movie_fd);'
if DECL not in s[:80000]:
    anchor = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'
    if anchor not in s[:80000]:
        # fallback: insert after first include of headers region
        raise SystemExit("no anchor for ps3_fios_aread_hle decl — adjust anchor")
    s = s.replace(anchor, anchor + "\n" + DECL, 1)
    print(f"{target.name}: declared ps3_fios_aread_hle")

i = s.find("void func_002B3D1C")
if i < 0:
    raise SystemExit("002B3D1C missing")
j = s.find("void func_", i + 10)
region = s[i:j]

if "AREAD-HLE" in region:
    print("002B3D1C already HLE-patched")
else:
    brace = region.find("{")
    if brace < 0:
        raise SystemExit("no brace")
    # Early-out: try host fulfill; on success set r3=got and return via natural epilogue pattern.
    hle = r'''
        /* AREAD-HLE: host movie_io fulfill (PS3_MOVIE_IO). Fall through if 0. */
        {
          uint32_t _op = (uint32_t)ctx->gpr[3];
          uint32_t _dst = (uint32_t)ctx->gpr[4];
          uint32_t _n = (uint32_t)ctx->gpr[5];
          unsigned _fd = _op ? (unsigned)vm_read32(_op + 0x4) : 0;
          unsigned _got = ps3_fios_aread_hle(_op, _dst, _n, _fd);
          if (_got) {
            ctx->gpr[3] = _got;
            return;
          }
        }
'''
    region = region[: brace + 1] + hle + region[brace + 1 :]
    s = s[:i] + region + s[j:]
    target.write_text(s, encoding="utf-8", newline="\n")
    print(f"patched AREAD-HLE into {target.name}")
print("OK patch_2b3d1c_movie_io")
```

**If Task 2 showed `op+0x4` is not the movie fd:** change `_fd` source before declaring green — e.g. call `movie_io_current_fd()` added in Task 3, or map from a name stored when the parent open ran. Document the measured choice in the patch header comment.

- [ ] **Step 3: Apply twice (idempotency)**

```bash
cd ../gow2-recomp
python3.12 recomp_mid_v2/patch_2b3d1c_movie_io.py recomp_macos_v2
python3.12 recomp_mid_v2/patch_2b3d1c_movie_io.py recomp_macos_v2
./apply_all_patches.sh --check recomp_macos_v2
```

Expected: second apply prints already-patched; checks include PASS `002B3D1C AREAD-HLE movie_io`.

- [ ] **Step 4: Link helper into `boot_gow2`**

Ensure `fios_aread_hle.c` is in `libps3recomp_runtime.a` (CMake) **or** listed in `../gow2-recomp/build_macos.sh` object list. RED if link fails with undefined `ps3_fios_aread_hle`.

```bash
cd ../gow2-recomp
./build_macos.sh 2>&1 | tail -40
# expect boot_gow2 links clean
nm boot_gow2 | grep ps3_fios_aread_hle
```

- [ ] **Step 5: Commit (gow2-recomp for patch; ps3recomp for CMake if needed)**

```bash
cd ../gow2-recomp
git add recomp_mid_v2/patch_2b3d1c_movie_io.py apply_all_patches.sh
git commit -m "feat: patch AREAD-HLE em func_002B3D1C (movie_io)"
```

**Acceptance (Task 4):** offline — marker present, idempotent apply, binary exports symbol. **not exercised** in-boot yet.

---

### Task 5: FD binding — make HLE actually accept ops (if Task 4 fallthrough stays cold)

> ⛔ **BLOQUEADA POR REFUTACAO** — a Task 2 provou, com controlo positivo
> (`nm` mostra os estaticos da sonda no simbolo certo; `objdump` mostra o gate
> como as PRIMEIRAS instrucoes; os 7 call sites e a entrada da tabela entram
> todos pelo topo), que **`func_002B3D1C` nunca e chamado**. Estas tasks
> existem para lhe pendurar um early-out e ligar o helper — nao ha onde
> pendurar. Alem disso a Task 4 do plano intro-audio-open-wall mediu que a op
> de open E completada, por `func_00306534` a partir da thread "fios
> scheduler" do jogo, e que o handler async-read serve **reads** (opcode
> diferente). Nao reabrir sem primeiro refutar essas duas medicoes.


**Files:**
- Modify: `libs/video/movie_hle.c` / `fios_aread_hle.c` as needed
- Possibly extend patch if parent object holds the path

**When needed:** Boot with `PS3_TRACE_AREAD=1` shows `[AREAD] #N …` but **zero** `[AREAD-HLE]` lines → helper declined (bad fd).

- [ ] **Step 1: RED**

```bash
grep -ac "\[AREAD-HLE\]" /tmp/aread_probe2.log   # expect 0
grep -a "\[AREAD\]" /tmp/aread_probe2.log | head -5
```

- [ ] **Step 2: Measure who owns the file**

Dump `op+0x4` and any pointer chain from Task 2. Cross-check against `movie_io_open` return tokens (`MOVIE_IO_MAGIC | slot+1` — see `movie_hle.c`).

Options (pick minimal):

1. **Store last movie_io fd** for names matching `R_PermA` / `R_LglScA` / `*.wad_ps3` in a process-global; HLE uses that when `op+0x4` is not a movie fd.
2. **Teach cellFs / FIOS open path** to write the movie fd into the field the guest later reads at `op+0x4` (more faithful, more work — only if (1) diverges).

Prefer (1) as diagnostic-first, env-gated… **No:** (1) as permanent HLE is OK if documented as "host cache path identity", same class as Windows serving by basename. Keep it simple:

```c
/* in movie_hle.c */
static unsigned s_last_wad_fd;
unsigned movie_io_last_wad_fd(void) { return s_last_wad_fd; }
/* set s_last_wad_fd in movie_io_open when path ends with .wad_ps3 */
```

```c
/* in ps3_fios_aread_hle */
if (!movie_io_is(movie_fd)) {
    extern unsigned movie_io_last_wad_fd(void);
    movie_fd = movie_io_last_wad_fd();
}
if (!movie_io_is(movie_fd)) return 0;
```

- [ ] **Step 3: Unit test update**

Extend `test_fios_aread_hle.c` with a case: `movie_fd=0` after a successful `movie_io_open("R_PermA")` still fulfills via last-wad-fd.

- [ ] **Step 4: GREEN unit + short boot grepping `[AREAD-HLE]`**

```bash
/tmp/test_fios_aread_hle
# boot 25s with PS3_TRACE_AREAD=1
grep -ac "\[AREAD-HLE\]" "$LOG"   # expect >0 if 002B3D1C runs
```

- [ ] **Step 5: Commit**

```bash
git commit -m "fix: associa fd movie_io ao HLE de async-read quando op+4 nao e token"
```

**Acceptance (Task 5):** offline unit green; in-boot `[AREAD-HLE]` count > 0 **if** `[AREAD]` count > 0. If `[AREAD]==0`, stop and fix open/FSM (class B) before claiming HLE.

---

### Task 6: In-boot acceptance smoke (L2)

> ⛔ **BLOQUEADA POR REFUTACAO** — a Task 2 provou, com controlo positivo
> (`nm` mostra os estaticos da sonda no simbolo certo; `objdump` mostra o gate
> como as PRIMEIRAS instrucoes; os 7 call sites e a entrada da tabela entram
> todos pelo topo), que **`func_002B3D1C` nunca e chamado**. Estas tasks
> existem para lhe pendurar um early-out e ligar o helper — nao ha onde
> pendurar. Alem disso a Task 4 do plano intro-audio-open-wall mediu que a op
> de open E completada, por `func_00306534` a partir da thread "fios
> scheduler" do jogo, e que o handler async-read serve **reads** (opcode
> diferente). Nao reabrir sem primeiro refutar essas duas medicoes.


**Files:**
- Create: `../gow2-recomp/smoke_aread_mac.sh`
- Optional: extend `../gow2-recomp/smoke_asset_pipeline.sh` metrics

- [ ] **Step 1: Write smoke**

```bash
#!/usr/bin/env bash
# smoke_aread_mac.sh — R_PermA must be host-served with real bytes_read growth.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
SECS="${1:-30}"
. ./env_gow2.sh
export PS3_NO_RSX=1
export PS3_TRACE_ASSET=1
export PS3_TRACE_AREAD=1
export PS3_VDEC_ASYNC=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
export PS3_PAD_AUTOSTART=1

LOG=$(mktemp /tmp/aread_smoke.XXXXXX)
./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 &
PID=$!
sleep "$SECS"
kill -9 "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true

fail() { echo "FAIL: $1"; echo "  log: $LOG"; exit 1; }

grep -q "\[movieio\] open 'R_PermA'" "$LOG" \
  || grep -qi "\[movieio\] open '.*r_perma" "$LOG" \
  || fail "R_PermA nao abriu via movie_io (FSM/open — fora do HLE)"

# Prefer HLE lines; if TRACE off, still require stats growth.
HLE=$(grep -ac "\[AREAD-HLE\]" "$LOG" || true)
STATS=$(grep -a "stats.*[Rr]_PermA\|stats.*r_perma" "$LOG" | tail -1 || true)
echo "hle_lines=$HLE"
echo "last_stats=$STATS"

# bytes_read parse: require at least 1 MiB consumed (stream alive), not full 20MB yet
# (full stream still hits allocator wall C history — do not demand 20169344 here).
python3 - <<PY
import re,sys
stats=\"\"\"$STATS\"\"\"
m=re.search(r'bytes_read=(\\d+)', stats)
br=int(m.group(1)) if m else 0
hle=int(\"$HLE\")
print(f\"bytes_read={br} hle={hle}\")
if br < 1_000_000 and hle < 8:
    sys.exit(2)
PY
[ $? -eq 0 ] || fail "stream morto: bytes_read baixo e poucos AREAD-HLE"

echo "PASS smoke_aread_mac (R_PermA host stream alive)"
echo "  log: $LOG"
```

**Note on acceptance bar:** Do **not** require `bytes_read == 20169344` in this plan. History: full consume previously hung at 5.6MB until fallthrough `2550C8` fix; that fix is already in the lift (`apply_all_patches` wall-C check). Demanding full 20MB is a **follow-up** (Task 7 optional). This plan's bar is: **async path delivers real bulk bytes**.

- [ ] **Step 2: RED before HLE (if you still have an unpatched binary)** — expect fail `stream morto`.

- [ ] **Step 3: GREEN after Tasks 3–5**

```bash
chmod +x ./smoke_aread_mac.sh
./smoke_aread_mac.sh 30
```

Expected: `PASS smoke_aread_mac`.

- [ ] **Step 4: Commit smoke**

```bash
git add smoke_aread_mac.sh
git commit -m "test: smoke macOS do stream R_PermA via AREAD-HLE"
```

**Acceptance (Task 6):** **in-boot** PASS with explicit metrics printed. Label in any summary: proved in-boot for stream liveness; full WAD completion **not claimed** unless stats show it.

---

### Task 7: Bound risk + document; optional track B (psarc inflate)

> ✅ **FEITO (documentado)** — o risco esta limitado e escrito: as Tasks 4-6 estao bloqueadas por refutacao (func_002B3D1C nunca e chamado; a op de open e completada por func_00306534 na thread fios scheduler). O track B (inflate do psarc) e outro caminho e nao e preciso -- o membro e servido nativamente pelo dearchiver do guest. Nao reabrir sem refutar essas medicoes.

**Files:**
- Modify: `docs/gow2-asset-pipeline-map.md` (short section "HLE 002B3D1C commitado")
- Modify: `docs/gow2-recomp-notes.md` or gow2-recomp notes if you keep a mac ledger
- Optional only: `libs/spurs/cellSpursLFQueue.c` probes — **no functional change** unless track B opens

- [x] **Step 1: Update asset map**

Add under the chain diagram:

```markdown
## HLE async-read (macOS 2026-07-20+)

O corpo de `func_002B3D1C` pode early-out para `ps3_fios_aread_hle` →
`movie_io_pread` quando `PS3_MOVIE_IO=1` e ha fd de cache.
Script: `gow2-recomp/recomp_mid_v2/patch_2b3d1c_movie_io.py` (marcador `AREAD-HLE`).
Isto **nao** e inflate; `HOSTINFL=0` para R_PermA continua esperado.
```

- [x] **Step 2: Risk checklist (run once, paste results into notes)**

| Check | Command / observation | Pass |
|-------|----------------------|------|
| Baseline no-op | Boot with `PS3_MOVIE_IO=0` → zero `[AREAD-HLE]`, boot still reaches SPURS wall as before | |
| Gate default | Unset `PS3_TRACE_AREAD` → no `[AREAD]` spam | |
| No inflate lie | `HOSTINFL` count still 0 on movie_io path | |
| Fallthrough | If cache missing, natural body runs (may stall — OK) | |
| Idempotent patches | `apply_all_patches.sh` twice clean | |

- [x] **Step 3: Track B decision gate (do not implement in this plan unless Task 6 is green and you still need psarc members without cache)**

Only if product goal shifts to "no pre-extracted cache":

1. Confirm TOC parse in guest (member table non-empty in guest RAM).
2. Find who should `LFQueuePush` to queue dir=3 (ANY2ANY) with 32B elems.
3. RED: `push2=0` while a compressed block is requested.
4. Fix **upstream submit**, not `lfq_host_inflate_try` (already correct for v2).

Stop here for the R_PermA movie_io plan.

- [x] **Step 4: Commit docs**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
git add docs/gow2-asset-pipeline-map.md docs/superpowers/plans/2026-07-20-fios-async-read-movie-io-hle.md
git commit -m "docs: plano e mapa do HLE async-read movie_io (002B3D1C)"
```

**Acceptance (Task 7):** docs match reality; risk table filled with **in-boot** or **not exercised** labels.

---

## Ordering summary

| Order | Task | Cheap red | Green | Proves |
|------:|------|-----------|-------|--------|
| 1 | Baseline path class A/B/C | 25s boot greps | Classification written | Which wall is live |
| 2 | Probe `PS3_TRACE_AREAD` | no marker | marker + dumps or zero-entry note | Op layout |
| 3 | `ps3_fios_aread_hle` unit | link/assert fail | `PASS test_fios_aread_hle` | offline helper |
| 4 | `patch_2b3d1c_movie_io.py` | `--check` FAIL | marker + link symbol | patch seam |
| 5 | FD binding (if needed) | HLE lines=0 while AREAD>0 | HLE lines>0 | helper accepts ops |
| 6 | `smoke_aread_mac.sh` | stream morto | PASS ≥1MiB or ≥8 HLE | **in-boot stream** |
| 7 | Docs + risk bounds | — | map updated | no forged claims |

## Explicit non-goals (this plan)

- Making DIR_ANY2ANY / `lfq_host_inflate_try` fire for R_PermA.
- Full 20 169 344 byte consume + ICGLdr nested walk (walls C2/D — already partially owned by other patches).
- CRC bypass, synthetic SHADERSRC, `PS3_SHADER_DEMO` as acceptance.
- Restoring the lost uncommitted Windows patch by hand-editing gitignored lift without a script.

## Self-review

| Spec question | Task |
|---------------|------|
| (a) hook seam | Design (a) + Task 4 |
| (b) layout probe | Design (b) + Task 2 |
| (c) cheap red | Design (c) + Tasks 3–4 |
| (d) TOC vs R_PermA | Design (d) + Task 1/7 track B |
| (e) divergence risk | Design (e) + Task 7 |
| Idempotent patch_*.py | Tasks 2, 4 |
| movie_io used from guest path | Tasks 4–6 |
| No inflate cargo-cult | Design (a)(d), Task 7 |

Placeholder scan: none intentional — FD field may be adjusted after Task 2 dumps; that is an explicit measurement gate, not TBD design.
