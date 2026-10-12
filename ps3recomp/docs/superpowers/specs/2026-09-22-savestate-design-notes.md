# Savestates for the static recompiler — design notes (not scheduled)

Status: **idea / feasibility**, written 2026-09-22. Nothing here is implemented.
Motivation: RPCS3 offers `Ctrl+S` (create savestate + stop), `Ctrl+R` / "Boot with
savestate" (restore) and savestate buttons in its Home (PS button) overlay. During
the Colossus bone-8 investigation every console measurement cost a full intro
replay; the same is true for our own runs (`PS3_PAD_AUTOSTART` needs ~2 min to reach
the Colossus). A savestate would cut both to seconds.

Related, already planned and much smaller: native CellSaveData autosave
(`docs/superpowers/specs/2026-09-21-gow2-native-autosave-design.md`). That restores
*game progress* (start of a level), not *emulation state* (mid-fight).

## Why this is harder for us than for RPCS3

RPCS3 (interpreter or LLVM) keeps every guest thread's full state in explicit
structures (`ppu_thread` registers, guest stack in guest memory). It can stop all
threads at any instruction boundary and serialise them.

In a static recompiler the guest call chain lives on the **host** stack: each
`func_XXXXXXXX(ppu_context*)` is a native C++ call, with native return addresses,
spilled callee-saved registers and `DRAIN_TRAMPOLINE` bookkeeping. The `ppu_context`
alone is not enough to resume a thread that is in the middle of 30 nested guest
functions. So "serialise registers + guest RAM" (the RPCS3 model) does not restore
a running game here.

Guest-visible state that must survive, by owner:

| State | Where it lives today | Restorable? |
|-------|----------------------|-------------|
| Guest RAM (main, VRAM/local mem, SPU LS) | `vm_base` (`runtime/memory/vm.h`, `mmap(NULL, …)`: address NOT fixed) | Yes, raw copy |
| PPU registers | `ppu_context` per thread | Yes |
| PPU host call chain | host pthread stacks (`sys_ppu_thread.c`, default `pthread_create` attrs) | Only if stacks + code are at identical addresses on restore |
| SPU threads / SPURS / lifted jobs | host threads + `spu_*` structs | Same as PPU |
| HLE kernel objects (lwmutex, lwcond, event queues/flags, semaphores, timers, rwlock) | runtime tables in host heap | Yes, if serialised explicitly (IDs + guest-visible fields) |
| Filesystem handles (cellFs, FIOS, movie IO) | host fds + tables | Reopen by path + seek |
| cellVdec / VideoToolbox, cellAudio / CoreAudio | host OS objects | No: rebuild on restore |
| RSX backend (Metal / D3D12): textures, PSOs, shader cache | host GPU objects | No: rebuild from guest memory + replay of RSX state registers |

## Candidate approaches

### A. Park-and-dump with fixed addresses (the "unexec" route), recommended if we do it

1. **Quiesce:** a global request makes every guest thread park at a runtime-owned
   park point (any blocking HLE syscall already is one; add a check to the frame-flip
   path and to the giant-lock/preemption tick for threads that never block). Each
   parked thread `setjmp`s into a per-thread buffer.
2. **Fixed layout:** make everything a lifted frame can point to live at a
   deterministic address on the next boot:
   - `vm_base` with a fixed hint + `MAP_FIXED_NOREPLACE`-style check (macOS: fixed
     hint, verify the result);
   - PPU/SPU host stacks allocated by us at fixed addresses (`pthread_attr_setstack`);
   - the executable loaded without ASLR (macOS: re-exec via `posix_spawn` with
     `_POSIX_SPAWN_DISABLE_ASLR`; Windows: fixed image base, which `build_crc.sh`
     already uses), so return addresses and `.data/.bss` pointers stay valid;
   - runtime tables that lifted code or park frames point into must come from a
     fixed-address arena, not `malloc`.
3. **Dump:** guest RAM + stacks + arena + `.data/.bss` + per-thread jmp_bufs + a
   serialised table of HLE objects + a build fingerprint (hash of the executable).
4. **Restore:** boot the same binary without ASLR, map the regions back, recreate
   host OS objects (mutexes, condvars, fds, Metal/VT/CoreAudio), create each thread
   on its original stack and `longjmp` it back into its park point.

Hard parts / risks:
- Any host pointer stored in guest-reachable or dumped memory that points into
  system frameworks (Metal objects, dyld shared cache) becomes invalid. Backends must
  keep those outside the dumped arena and rebuild them from guest state.
- A savestate only works with the **identical executable**; any re-lift or rebuild
  invalidates it (check the fingerprint, refuse otherwise).
- Threads spinning in lifted code (no syscall) need a cooperative check point; the
  E229 preemption tick is a natural host.
- Correctness tests: save → restore → compare a guest-RAM hash and frame counter
  after N frames against an uninterrupted run (deterministic modulo timing).

### B. Whole-process snapshot (fork/CRIU-like)

No CRIU on macOS; Mach task snapshots of a process holding Metal/IOSurface resources
are not restorable. Rejected.

### C. Cheaper alternatives that cover most of the need

- **Native autosave** (planned, see above): resume at the last in-game save.
- **Warp gate (diagnostic):** a gated env var (`PS3_WARP=<level>`) that drives the
  game's own level-load path (write the requested level/area into the global the
  front-end reads, like the RPCS3 patch approach) instead of playing through.
  Needs RE of the GoW2 level-load request; probably cheap once found.
- **Faster autostart:** `PS3_PAD_AUTOSTART` + skip-video (`patch.yml` "Skip any
  videos with X": `be32 0x2C1A88 -> li r0,1`, re-lift of one function).

## Menu / UX (for when A exists)

Mirror RPCS3 so muscle memory carries over:
- `Ctrl+S`: create savestate, then keep running (RPCS3 stops; an option can match).
- `Ctrl+R` / launcher button "Continuar do savestate": restore.
- Metal overlay (and D3D12 equivalent) with Save / Load buttons, drawn by the host
  backend over the guest frame; it must never touch guest RSX state.
- Store under `savestates/<TITLE_ID>/<timestamp>.p3s` next to the game data, with
  N-file rotation like RPCS3 (`Maximum SaveState Files`).

## Suggested order if picked up

1. Warp gate or autosave first (small, immediately useful for debugging).
2. Fixed-address layout (VM, stacks, arena, no-ASLR re-exec) with **no** savestate
   yet: measurable in isolation (boot unchanged, addresses identical across runs).
3. Quiesce/park protocol with a "park then resume" test (no dump).
4. Dump/restore of guest RAM + HLE tables, then backends rebuild, then the menu.
