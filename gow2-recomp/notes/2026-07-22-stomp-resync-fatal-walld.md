# Goal: factory stomp root, residual RESYNC=0, post-B71 FATAL, Wall D

**Date:** 2026-07-22 (skeptic re-pass)  
**Proof:** `{SCRATCH}/combined.log` / `post_b71_fatal.log` / criterion copies

## Recipe

```bash
. ./env_gow2.sh
export PS3_NO_RSX=1 PS3_PERF_FSM=1
export PS3_MOVIE_EOS=1 PS3_VDEC_ASYNC=1 PS3_VDEC_FORCE_SEQDONE_MS=5000
export PS3_TRACE_POSTINTRO=1 PS3_B71_FULL_393E0=1
export PS3_TRACE_TYMAP=1 PS3_TRACE_LDRSH=1 PS3_TRACE_TYMAP_DUMP=1
./boot_gow2 EBOOT.ELF
```

## Pass/fail (gating)

| # | Criterion | Result | Evidence |
|---|-----------|--------|----------|
| 1 | Factory without REPAIR | **PASS** | `TYPE15 REPAIR=0`; `REHOME pin=0x47D00000`; CB56C `ent=0x47D00000 vt=0x516D70 opd=0x51B300 code=0x39E794`; SKIP=0 |
| 2 | Residual RESYNC=0 | **PASS** | `F2B-STREAM-RESYNC=0`; `F2B-STREAM-ALIGN residual from=… → 20038352 (MDL_PUMeterDrain1_0 chain)`; DESYNC-STOP=0; FULL; B71 exit |
| 3 | No FATAL 0x27182818 ≥30s post-exit | **PASS** | `exit +0x64=1`; log has 8× `[POSTEXIT] post-exit alive=5s…40s process_alive=1 fatal=0` (wall 21:01:02Z–21:01:42Z); FATAL=0; ICALL-HEAP + AUTO_LOAD stub |
| 4 | Wall D schedule diagnostic | **PASS (diag)** | CMP=0 TYMAP-171=0 LDRSH=0 DUMP=1; next gate `00468C3C` / OPD `0x522E70` |

## Residual ALIGN (criterion 2)

Offline FO `R_PermA.wad_ps3` (20169344): solid type1 chain from **MDL_PUMeterDrain1_0@20038352** (after MAT_EnergyConstant@20038320) through platefire/DC_WAD to FO end with hdr=0x20+align16.

In-boot residual desync lands mid-payload (~20031552). Fix:

1. First residual absurd → **ALIGN** rewind `file_pos=20038352`, fill header, state=2 (**no** `F2B-STREAM-RESYNC` log).
2. Later residual absurd → FO FULL + SM idle (`EOF-QUIET residual full`).

Measured: RESYNC×16 band-aid removed; RESYNC=0; B71 still exits.

## Post-B71 FATAL (criterion 3)

| Piece | Role |
|-------|------|
| `ICALL-HEAP` in `ps3_indirect_call` | Skip CTR in 0x20–0x3F heap band (was stuck FATAL 0x27182818) |
| `AUTO_LOAD` stub in `sys_ppu_thread_create` | Guest entry 0x521768 SIGSEGV’d host; create succeeds, no host thread |

Post-exit window: process **alive ≥35s** after B71 with FATAL=0 (log continues MOVIEFSM/etc.).

## Factory REHOME (criterion 1)

Bulk/memcpy stomps original `0x401002F0` (not `vm_write32`). On SNAP: copy object to pin `0x47D00000`, `tab[0x54]=pin`. REPAIR remains defensive, unused.

## Code sites

| Fix | Path |
|-----|------|
| TYPE15 REHOME / ALIGN residual | `gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp` |
| PIN-FREE on free | `ppu_recomp_000.cpp` `func_00263318` |
| STOMP-BLOCK + ICALL-HEAP | `ps3recomp/runtime/ppu/ppu_loader.cpp` |
| AUTO_LOAD stub | `ps3recomp/runtime/syscalls/sys_ppu_thread.c` |
| EOF-DONE large FO | `ppu_recomp_001.cpp` `f2b_stream_eof_try_complete` |
