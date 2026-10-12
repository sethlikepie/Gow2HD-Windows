# NID `0x4E153E3E` = `cellSpursGetWorkloadInfo` (2026-07-22)

## Identification

| Item | Value |
|------|--------|
| NID | `0x4E153E3E` |
| Symbol | **`cellSpursGetWorkloadInfo`** |
| Module | `cellSpurs` |
| Proof | `compute_nid("cellSpursGetWorkloadInfo") == 0x4E153E3E` (SHA-1 LE first 4, project `nid_database.py`) |
| EBOOT import | `file+0x4A9EE8` in **cellSpurs workload** cluster (between `cellSpursWorkloadAttributeSetName` / `cellSpursRemoveWorkload`) |

Scratch: `{SCRATCH}/nid_4e153e3e_id.txt`.

## HLE

| Piece | Where |
|-------|--------|
| API | `s32 cellSpursGetWorkloadInfo(CellSpurs*, WorkloadId, CellSpursWorkloadInfo*)` |
| Struct | `CellSpursWorkloadInfo` (0x30, RPCS3-compatible BE fields) in `cellSpurs.h` |
| Impl | `libs/spurs/cellSpurs.c` — zero-fill guest info; if wid in use, fill data/priority/pm/size/ready from `s_workloads[]` |
| Register | auto via `gen_hle_nids.py` → `ppu_hle_nids.cpp` `ps3_hle_register(0x4E153E3Eu, ...)` |

Returns `CELL_OK`. Does not forge menu/registry.

## In-boot (`PS3_AUTO_LOAD_RUN=1`, POSTINTRO/B71 recipe)

```
thr_auto_load() start
AutoLoad2 dir='BCUS98229_GOW2' → first-run CELL_OK (api_ret=0)
thr_auto_load() end
[cellSpurs] GetWorkloadInfo(wid=0 ready=1 sizePm=0)   ×16 (rate-limited log)
```

| Check | Result |
|-------|--------|
| `unresolved NID 0x4E153E3E` | **0** |
| `GetWorkloadInfo` called | **≥16** |
| AutoLoad still OK (no 0x8002B40B) | **PASS** |
| thr_auto_load end | **PASS** |

Log: `{SCRATCH}/nid_4e153e3e_boot.log`.

## FSM observe (not acceptance)

| Signal | Note |
|--------|------|
| st620 pós-AutoLoad | still `0→0` |
| 2nd StartSeq | still 1 (intro only) |
| Menu / registry / pixels | **not** claimed green |

Other unresolved NIDs still early in boot (out of scope): `0x4A5EAB63` (`cellSpursWorkloadAttributeSetName`), `0xEFEB2679` (`_cellSpursWorkloadAttributeInitialize`), `0x63FF6FF9`, `0xDB869F20`, `0x4692AB35`.

## Single next gate

After GetWorkloadInfo returns OK, the game still sits at **st620=0**. Next: either
1. identify what the post-GetWorkloadInfo loop waits on (readyCount / signal / other Spurs API), or  
2. 2nd movie / vdec path that never opens StartSeq #2.

Do **not** treat this HLE alone as menu green.

## Recipe

```bash
. ./env_gow2.sh
export PS3_AUTO_LOAD_RUN=1 PS3_NO_RSX=1 PS3_TRACE_POSTINTRO=1 PS3_B71_FULL_393E0=1
./boot_gow2 EBOOT.ELF
# expect: no "unresolved NID 0x4E153E3E"; GetWorkloadInfo after thr_auto_load end
```
