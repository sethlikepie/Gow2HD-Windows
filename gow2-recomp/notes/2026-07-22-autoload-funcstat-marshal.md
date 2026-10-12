# AUTO_LOAD / funcStat marshal — first-run NODATA → CELL_OK (2026-07-22)

## Goal
Unblock post-B71 `AUTO_LOAD` past `cellSaveDataAutoLoad2` so the guest
callback runs, the API does **not** return `0x8002B40B`, and the prior
`[vm] OOB access 0xE0000000` spin is gone. Observe FSM only (no menu green).

**Proof:** `{SCRATCH}/autoload_funcstat.log`, `{SCRATCH}/autoload_counts.txt`  
**Recipe:** `env_gow2.sh` + `PS3_AUTO_LOAD_RUN=1` + `PS3_TRACE_SAVEDATA=1` +
`PS3_NO_RSX=1` (~60s). No GATE synthetic walk / CRC / SHADER_DEMO as pass.

---

## Root causes fixed

### 1) CBResult layout was wrong (pointer ABI)
PS3 / RPCS3 layout (not host `char invalidMsg[256]`):

| off | field |
|-----|--------|
| +0 | `result` |
| +4 | `progressBarInc` |
| +8 | `errNeedSizeKB` |
| +0xC | `invalidMsg` **ptr** |
| +0x10 | `userdata` **ptr** |

GoW2 `func_00147BB8` does `lwz r9, 0x10(r3)` and uses that as a struct base
(writes `+0x28`, `+0xC`). With userdata left 0, it poked low guest memory →
corruption / OOB after AutoLoad.

**Fix:** marshal `invalidMsg` + `userdata` as guest EAs; pass AutoLoad2
`userdata` (raw EA, e.g. `0x57EBD8`) into `CBResult+0x10`.

### 2) First-run `ERR_NODATA` mapped to API error
With no save dir, `isNewData=1`. Guest funcStat **intentionally** writes
`cbResult.result = -4` (`CELL_SAVEDATA_CBRESULT_ERR_NODATA`). Old HLE mapped
that to `CELL_SAVEDATA_ERROR_NODATA` (`0x8002B40B`). `thr_auto_load` then
stored status=4 and the post-path spun.

**Fix:** `autoload_map_cb_result`: if `is_new && cb == ERR_NODATA` → **CELL_OK**
(new-game / no-save success). Existing-save NODATA still returns ERROR_NODATA.

### 3) Probes (env-gated OFF by default)
`PS3_TRACE_SAVEDATA=1` → `[SAVEDATA-PROBE]` pre/post funcStat (`isNew`,
hddFreeKB, bind, sizeKB, fileNum, dir, userdata, cbResult).

---

## In-boot evidence (this run)

```
[SYS] sys_ppu_thread_create name="AUTO_LOAD" entry=0x00521768 arg=0x57EBD8
thr_auto_load() start
[cellSaveData] AutoLoad2(version=0, dir='BCUS98229_GOW2' userdata=0x57EBD8)
[SAVEDATA-PROBE] pre funcStat isNew=1 hddFreeKB=1048576 bind=0x0 … userdata=0x0057EBD8
[cellSaveData] funcStat returned cbResult.result=-4
[SAVEDATA-PROBE] post … userdata+0xC=0x1
[cellSaveData] AutoLoad first-run: funcStat ERR_NODATA + isNewData=1 → CELL_OK (new game)
[SAVEDATA-PROBE] AutoLoad2 api_ret=0x00000000 cb=-4 isNew=1
cellSaveDataAutoLoad2() : 0x0
thr_auto_load() end
```

| Criterion | Result |
|-----------|--------|
| AUTO_LOAD no host crash / real dir string | **PASS** |
| API not 0x8002B40B (api_ret=0) | **PASS** |
| No OOB `0xE0000000` spin | **PASS** (count 0) |
| Probes show isNew/cbResult/StatGet | **PASS** |
| R_Perm FULL non-regression | **PASS** (`20169344`) |

---

## Observation only (60s) — not acceptance

| Signal | N / note |
|--------|----------|
| st620 | intro `0→1→3→11→0`; **post-AutoLoad still `0→0`** |
| 2nd `cellVdec` StartSeq | **N=1** (intro only) |
| A1-468C3C / TYMAP-171 / LDRSH | **0** |
| Menu / registry / pixels | **not claimed green** |

After `thr_auto_load() end`:
```
[hle] unresolved NID 0x4E153E3E  (×2)
```

---

## Single next gate

**Identify and HLE (or stub faithfully) NID `0x4E153E3E`**, which the game calls
immediately after a successful AutoLoad2. Until that import resolves (or the
caller path is understood), st620 stays at 0 and there is no 2nd movie /
menu FSM advance.

Do **not** treat SHADERSRC / HOSTRES / GATE-FORCE as menu green.

---

## Code sites

| Piece | Where |
|-------|--------|
| guest→host dir string | `libs/system/cellSaveData.c` `savedata_dir_host` |
| CBResult pointer layout + userdata | `marshal_cbresult_init` / `dispatch_func_stat` |
| first-run map | `autoload_map_cb_result` |
| probes | `PS3_TRACE_SAVEDATA` |
| AUTO_LOAD gate | `runtime/syscalls/sys_ppu_thread.c` `PS3_AUTO_LOAD_RUN=1` |

## Recipe

```bash
cd gow2-recomp
. ./env_gow2.sh
export PS3_AUTO_LOAD_RUN=1 PS3_TRACE_SAVEDATA=1 PS3_NO_RSX=1
./boot_gow2 EBOOT.ELF
# expect: AutoLoad2 dir=BCUS…, first-run → CELL_OK, thr_auto_load() end, no 0x8002B40B
```
