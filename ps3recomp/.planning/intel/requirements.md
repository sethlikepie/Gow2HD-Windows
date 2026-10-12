# Requirements (synthesized from PRDs)

Source type: PRD (16 documents). Most are **already-executed implementation plans with
checkboxes**, not pending scope. Every entry below is therefore state-tagged in its
`- description:` field with one of:

- `[OPEN]` — still-pending scope. This is the actionable output.
- `[PARTIAL]` — partially landed; the residual is named in the description.
- `[DONE]` — historical record. Do **not** re-emit as work to build.
- `[SUPERSEDED]` — a later plan owns this scope; kept for provenance only.
- `[CONTESTED]` — sources disagree on completion state; see `INGEST-CONFLICTS.md`.

Checkbox census used for state tagging (mechanical, per source):
wall-chain-INDEX 0/0, metal-stack-INDEX 0/0, metal-pending-backlog 0/0,
gow2-playable-master-INDEX 0 done / 8 open, m3plus-postwad-present 19 done / 1 open,
menu-type15-frame-loop 27 done / 0 open, xenonrecomp 5 done / 55 open,
shader-registry-typemap-walk 0 done / 34 open, rdy0 0 done / 63 open,
fase1-desmascarar-memoria 0 done / 42 open (but its executing plan is recorded as
executed by `IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md` §Fase 1),
bringup-instrumentacao-fase0 0/0 (recorded as CONCLUÍDA by the same source),
and the four prose plans (`IMPLEMENTATION_PLAN_*`, `MACOS_PORT_PLAN`,
`PLAN_READY_FOR_GAME_RECOMPILATION`, `SPURS_BRINGUP`) carry no checkboxes.

---

# OPEN — pending scope

## REQ-gow2-playable
- source: docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md
- description: [OPEN] God of War II HD (NPUA80491) 100% usable on static recomp: boot → intro → WAD → main menu → first playable interaction, with a natural content path (shaders/textures/draws) on macOS Metal, without an emulator and without theater. Two repositories: engine `ps3recomp` (runtime/libs/tools) and port `gow2-recomp`/`../gow2_work`. Critical path is guest product construction plus the frame loop after AUTO_LOAD; Metal/shader tooling and Xenon-style recomp tooling run in parallel but never redefine "menu reached".
- acceptance: Gate B (thr path) green AND Gate A (post-thr life) green AND main menu UI observed AND first playable interaction. Status recorded 2026-07-23: Gate B GREEN, Gate A **RED**, Main menu UI **NO**, Playable **NO**.
- scope: GoW2 end-to-end, tracks M/S/G/U/T/P

## REQ-gate-a-post-thr-life
- source: docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md
- description: [OPEN] Gate A — post-thr life. The dual-gate acceptance model must never be collapsed: Gate B covers the thr path, Gate A covers life after thr. Gate B is already green; Track M targets Gate A without breaking B.
- acceptance: `st620` leaves 0→0, **or** SetFlip/pad after B71, **or** menu FSM non-idle with proof. Currently: `SetFlip_after_R_Perm=0` even at TIMEOUT=120; `Pad_after_R_Perm=0`; post-thr `st620` only 0→0.
- scope: Gate A, present, pad, menu FSM

## REQ-type15-f4-writers
- source: docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md
- description: [OPEN] The primary active wall. TYPE15 f4 writers / product work (residual of M2: nodes=12 freelist ≠ UI) inside the parent `2B2DD0→B951C`; present re-arm depends on this. Explicitly **not** a bug in the gate `2B2DD0`/`B951C` (R12) and **not** OPD `B94EC` (R11 DEAD). Refuted as sole cause across R1–R12: empty-list (M2), ICALL-BAD (R1–R2), HEAP40 (R2), content-hold/handler/late-flip (R3), 17ACC (R5), circuit-breaker in 2EFD60/CDBA4 (R7–R8), early-return in the menu present paths (R9 H2), CE03C wait-idle as sole block (R10 H3), thr re-entry (R10 H2), static OPD schedule B94EC/B6150 (R11 H1–H2), early-return/gate bug in 2B2DD0/B951C (R12 H3).
- acceptance: f4 becomes non-zero via a real guest writer and the component tick leaves the CC9D0 spin, with Gate B still green.
- scope: TYPE15 products, func_000CC9D0, func_000B951C, func_002B2DD0, present re-arm

## REQ-present-reentry-after-rperm
- source: docs/superpowers/plans/2026-07-23-m3plus-postwad-present.md
- description: [OPEN] Restore guest re-entry into `cellGcmSetFlip*` after `R_Perm` completes, without theater and without regressing Gate B. Root class (R3): the only pre-WAD flip emitter is the intro band `func_00017ACC`; opening `R_LglScA` ends that path and there is no re-arm after thr. This is the single unchecked acceptance box in a plan whose 19 other boxes are done — R1/R2/R4 DONE, R3 DIAG DONE / FIX FAIL.
- acceptance: `SetFlip_after_R_Perm >= 1` on the menu-fast smoke (`TIMEOUT=90 LOG=/tmp/r_m3plus.log PS3_NO_RSX=1 ./rodar_gow2_menu_fast.sh` then `python3 count_menu_gate.py`). Currently FAIL at TIMEOUT=90 and 120. Named follow-ups: static RE of the callers of `func_00017ACC` (`1E1A8`/`1E34C`/`1EAD8`/`25064`/`25614`/`1E3DC`) after thr, and TYPE15 f4 writers in the natural flow. Explicitly forbidden: reopening H3 HEAP40 as a fix; forging a flip HLE.
- scope: cellGcmSetFlip, Gate A, present re-arm, R_Perm

## REQ-postb71-setflip
- source: docs/superpowers/plans/2026-07-23-menu-type15-frame-loop.md
- description: [OPEN] Task M3 — after thr + R_Perm + B71 the main thread must leave the two-object CC9D0 spin so the host observes a flip. All five M3 steps are checked as executed, but the planned acceptance failed: outcome recorded as "**FAIL** aceite flip; **PASS** diag honesto (`90829b6`). Gate A **RED**." Root class established: the guest abandons the present path at WAD open. Step 4 ("minimal unblock") is annotated "não desbloqueado — Gate A continua RED; sem fix cosmético".
- acceptance: `SetFlip_after_R_Perm >= 1`, counted by the footer counters added to `rodar_gow2_menu_fast.sh` / `count_menu_gate.py`. Fail if only intro flips are seen.
- scope: frame loop, post-B71, SetFlip, Gate A

## REQ-pad-menu-fsm-arm
- source: docs/superpowers/plans/2026-07-23-menu-type15-frame-loop.md
- description: [OPEN] Task M4 — pad and menu FSM signal after thr. All four steps executed; acceptance failed. Outcome: "**FAIL** aceite; **PASS** diag `e7cf689` — pad never polled; no menu; no theater." `PS3_PAD_AUTOSTART=1` confirmed reaching the HLE; `Pad_total=0`, `cellPadGetData` never called by the guest.
- acceptance: `cellPadGetData` count > 0 after thr, **or** a clear menu state log (`[MENU]`, FlashUI correlated with a flip, or New Game arm). FlashUI alone is an explicit fail.
- scope: cellPad, menu FSM, input

## REQ-track-s-shader-registry-natural
- source: docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md
- description: [OPEN] Track S — natural shaders plus guest draw. Entry condition: post-B71 `SetFlip_after_R_Perm>=1` (or an equivalent present path), or a typemap walk proven independent of flip without theater. Blocked as of 2026-07-23 because Gate A is still RED.
- acceptance: `[LDRSH]` N>0 natural; SHADERSRC non-empty; `set_shader` hash ≠ demo. CRC bypass as "done" is forbidden.
- scope: shaders, SHADERSRC, LDRSH, guest draw

## REQ-typemap-nested-walk-diagnosis
- source: docs/superpowers/plans/2026-07-21-shader-registry-typemap-walk.md
- description: [OPEN] Diagnosis-first: determine, with in-boot measurement and a cheap discriminating experiment (1–2 boots), **why** `func_00171244` (the typemap nested walk) is never invoked on the natural path, and where the chain breaks. The chain that should populate the registry: type `0xF85F9B1E` registered → typemap walk via vtable `@0x5130B8+8` (OPD `0x522E70` = `func_00171244`) → lookup/dispatch → `ICGLdrShader` (`func_0032109C`) → SHADERSRC N>0 → precalculated CRC table. Broken link observed between "type registered" and "`func_00171244` runs". Hypotheses ordered in three classes: A (component/typemap never built or walk never scheduled — A1 caller never runs, A2 wrong vt/OPD at the vcall, A3 typemap never installed at `component+0xDC`), B (walk scheduled but predicate fails — B1 null/empty stream, B2 tags never `0xF85F9B1E`, B3 lookup uses the wrong map), C (walk and dispatch run but ICGLdr receives an empty source — C1 payload size 0 / N=-1, C2 stream with bytes but zero definition records). All 34 steps are unchecked.
- acceptance: PASS on any one of, measured in-boot on the natural path: (1) `[TYMAP-171] >= 1` without `PS3_GATE_FORCE`; (2) SHADERSRC N>0 by the natural path; (3) `[LDRSH] >= 1` natural without GATE-FORCE or injected tag; (4) a written A/B/C class verdict with the N1 (and N2 if run) table plus a faithful fix route — even if the fix does not exist yet. A "parede aberta" verdict (only GATE-FORCE produces the walk, documented as such, wall [D] not declared won) is also a valid PASS. REJECT as dishonest: acceptance based on `PS3_GATE_FORCE`, synthetic stream, CRC bypass or `PS3_SHADER_DEMO`; declaring "registry populated" from `[SHREG]` alone (type registration ≠ variant/CRC population); counting a drop in `Invalid shader combination` without a natural walk/SHADERSRC N.
- scope: shader registry, typemap nested walk, ICGLdrShader, SHADERSRC, CGOWShader

## REQ-track-u-spu-jobs-deep
- source: docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md
- description: [PARTIAL] Track U — SPU jobs deep plus residual ISA. Tables spu0–5 registered; roughly 90 residual TODO opcodes. Deep work: SPURS job DMA/LFQueue TDD; promote spu2/spu3 to default only with a smoke.
- acceptance: a real job completes with proof, not merely a register. The wall-chain INDEX records this chain link as "🟡 parcial (wiring OK; jobs deep TBD)" with the target "Jobs SPU reais (DMA/LFQueue) com TDD".
- scope: SPU jobs, SPURS, DMA, LFQueue, residual opcodes

## REQ-spurs-m5-gow2-protocol
- source: docs/SPURS_BRINGUP.md
- description: [OPEN] Marco 5 — bring-up with the real GoW2. The generic HLE SPURS kernel engine is implemented and tested; what remains is title-specific: map GoW2's SPURS protocol (the structure at `0x86E118`, the bitmap at `[TOC-0x142C]`, the dispatch vtable at `0x5728EC`) and make the kernel produce the signals/structures that `func_0030600C` and the next links in the chain await; lift GoW2's SPU set and register it via `spu_workload_register` so the game's policy modules have a lifted entry (today: MISS → inert); wire the `q=1` event queue feed (`EnableSpuPrintf`) to `_SPU_printf_server`. Concrete path recorded: (b) run `spu_0000` (the real libspurs kernel image, present at `spu_images/spu_0000_at_004C8780.elf`) on the SPU engine — lifter phase done 2026-06-25, kernel 421→448 funcs (98%), full self-init runs, now stops at a computed target of uninitialized state; (a) make `cellSpursInitialize` build the real libspurs instance in guest memory — **current phase, this is the libspurs ABI (RE / RPCS3 reference)**; (c) refine MFC atomics (`getllar/putllc`) if the kernel work queue requires a real reservation.
- acceptance: the GoW2 boot advances **past** `func_0030600C` because the real SPURS kernel wrote `[0x86E118]` — this does not mean rendering; the next wall is RSX.
- scope: SPURS, libspurs ABI, SPU kernel, GoW2 boot

## REQ-bringup-c1-boot-memory-ok
- source: docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
- description: [PARTIAL] Fase 1 — guest memory and heap integrity. Explicitly recorded state (2026-07-25): PARCIAL, C1 NOT reached. What landed: canonicalizing the two ad-hoc guards into `MEMORY|SWALLOWED`, correcting the `vm.h` region map (`lv2_heap`/`sys_vm`/`rsx_local`/`tls` stop reading `unmapped`), registering committed ranges in `sys_memory.c`, and measuring E1/E2. What is explicitly still missing for C1: allocator header/footer validation; double-free, overlap and invalid-split detection; the `strict`/`diagnostic` memory modes; closing the ~2110 direct `vm_base` writes that bypass the hooked funnel (so `SWALLOWED`/`OOB` counts remain a lower bound); instrumenting the two `type15_*` masks in `gow2-recomp/host_gow2_factory.cpp`; resolving the re-lift mine that would erase the atomics instrumentation.
- acceptance: checkpoint `BOOT_MEMORY_OK` — no known header corruption in the boot, no silent invalid guest write, and the invalid address can be attributed to an origin. Currently the first error remains an undiagnosed `MEMORY|OOB ea=0xFB788082 len=4 region=unmapped op=read` in `_Z13func_00263554P11ppu_context+0x1A8`, identical to Fase 0.
- scope: memory/heap, allocator forensics, strict/diagnostic modes

## REQ-bringup-c2-ppu-state-ok
- source: docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
- description: [OPEN] Fase 2 — PPU state, TLS and ABI. Validate GPR, FPR, VMX, CR, LR, CTR, XER, FPSCR, VSCR, VRSAVE, CIA and TOC; validate `lwarx/stwcx` per thread context; validate stack frame, back-chain and alignment; create typed guest-pointer wrappers; compare HLE arguments and returns against the guest ABI; test `bl`, `blr`, `blrl`, `bctrl`, `bctr`, tail calls and trampolines. Tests: golden lifter tests, one test per indirect call form, CTR/LR/TOC preservation, big-endian struct, host→guest→host callback, syntactic compilation of all chunks.
- acceptance: checkpoint `PPU_STATE_OK` — execution passes through the same set of calls with no context corruption, missing labels, or return to an incorrect address.
- scope: PPU context, ABI, TLS, lifter fidelity

## REQ-bringup-c3-threads-progress
- source: docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
- description: [OPEN] Fase 3 — PPU threads and scheduler. Record per-thread state (running/ready/blocked/sleeping/finished) plus wait reason and object; guarantee release/reacquire on every blocking syscall; add a deterministic scheduler mode; add starvation and deadlock detection; keep the giant lock only as a comparison option and compare execution with it against per-subsystem locks.
- acceptance: checkpoint `THREADS_PROGRESS` — all critical threads alternate progress, no thread waits without a recorded reason, and the event order is reproducible. Failure discipline: if the giant lock works but concurrent mode fails, the cause is a race and must not be solved by increasing sleeps.
- scope: threads, scheduler, giant lock, deadlock detection

## REQ-bringup-c4-spu-effects-ok
- source: docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
- description: [OPEN] Fase 4 — SPURS, SPU and DMA. Register each SPU image and its `image_id`; validate code/data load into local store; integrate fallback and lift by configuration; implement a per-context MFC queue; record GET/PUT, EA, LSA, size, tag and completion; fix atomic commands and reservations; model blocking mailbox and signals; distinguish completed / blocked / halt / crash jobs; validate `stop_code` and return to SPURS.
- acceptance: checkpoint `SPU_EFFECTS_OK` — every critical job has an entry record, guest effects, return and termination; no pending job without a progress event.
- scope: SPU, SPURS, DMA, MFC, mailbox

## REQ-bringup-c5-wad-pipeline-ok
- source: docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
- description: [OPEN] Fase 5 — FIOS, filesystem and WADs. Reproduce the asynchronous loading protocol rather than merely making the file available on the host: model a FIOS operation object with explicit state; preserve partial reads, alignment, buffer and callback; implement completion ordering and cancellation; validate path mapping and case differences; validate the decompressor and output size; validate WAD offsets, headers, tables and guest destination; associate FIOS completion with the responsible thread/event flag/job.
- acceptance: checkpoint `WAD_PIPELINE_OK` — the expected WADs are opened, read, decompressed, written to the correct destination and signalled in the natural order; the FSM advances without a synthetic flag.
- scope: FIOS, filesystem, WADs, async I/O

## REQ-bringup-c6-intro-natural-eos
- source: docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
- description: [OPEN] Fase 6 — media, video and EOS. Implement real decode for the format the game uses; model AU, sequence, PICOUT, PCMOUT, AUDONE, SEQDONE and EOS; preserve timestamps and audio/video ordering; integrate guest buffers and callbacks; remove EOS forcers from the default path; keep the host overlay strictly as a backend, never as a substitute for the guest FSM.
- acceptance: checkpoint `INTRO_NATURAL_EOS` — the intro state advances with EOS produced by the real pipeline and all expected callbacks observed. Failure discipline: never change the FSM value directly; locate the first missing event (decode, buffer, callback, timestamp, EOS or consumer).
- scope: cellVdec, media, EOS, intro FSM

## REQ-bringup-c7-first-frame-presented
- source: docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
- description: [OPEN] Fase 7 — RSX, render and presentation. Instrument FIFO and per-frame commands; validate labels, fences, timestamps and flip; validate render targets, textures, swizzle and vertex formats; validate shaders and parameter combinations; implement command-buffer replay; guarantee present does not alter guest structures without a corresponding event; separate `flip requested`, `flip complete` and `frame presented`.
- acceptance: checkpoint `FIRST_FRAME_PRESENTED` — the first expected frame is presented without a synthetic FSM hook and with the complete FIFO → draw → fence/flip → present sequence.
- scope: RSX, FIFO, present, flip

## REQ-bringup-c8-menu-input-ok
- source: docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
- description: [OPEN] Fase 8 — input, menu and gameplay. Map host pad to stable guest state; preserve pressed/released/held/repeat; integrate input with the scheduler and frame loop; validate sysutil/menu callbacks; remove dependency on synthetic input from normal tests.
- acceptance: checkpoint `MENU_INPUT_OK` — the game reaches the menu, accepts a known input sequence and changes state reproducibly.
- scope: input, cellPad, menu, gameplay

## REQ-bringup-c9-save-and-exit-ok
- source: docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
- description: [OPEN] Fase 9 — save/load and shutdown. Validate paths, PARAM.SFO, callbacks and save-data buffers; test create, read, modify and controlled delete; implement ordered shutdown of threads, SPURS, audio, video and RSX; validate function returns and handle destruction.
- acceptance: checkpoint `SAVE_AND_EXIT_OK` — the game saves and returns to menu or exits with no pending threads, no pending DMA, no invalid handle and no guest corruption.
- scope: cellSaveData, shutdown, lifecycle

## REQ-bringup-c10-final-regression
- source: docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
- description: [OPEN] Fase 10 — final validation and regression. For each approved checkpoint: run three times in deterministic mode and three times in normal mode, compare event logs and state snapshots, run with sanitizers, run with and without the giant lock, verify the absence of experimental hooks, and update the compatibility matrix.
- acceptance: final criterion `GAME_RECOMPILED_RUNTIME_OK` — the game reaches every defined checkpoint, results are reproducible, no barrier depends on a force flag, and remaining workarounds are explicitly classified as game-specific.
- scope: regression, determinism, sanitizers, compatibility matrix

## REQ-reliability-recompiled-quality-gate
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Global quality criteria: a game may only be marked `recompiled` when (1) there is no unknown critical instruction, (2) no mandatory import lacks explicit resolution, (3) every chunk compiles and links, (4) the loader validates segments, BSS, OPD, TOC and relocations, (5) state and concurrency tests pass, (6) the boot reaches the game-defined checkpoint with no unauthorized invalid guest access, (7) the result is reproducible from the manifest.
- acceptance: `ps3recomp build --elf EBOOT.ELF --config game.toml --out build/game` either produces a complete build or fails naming exactly the stage and cause, emitting a manifest with hashes/versions/options, the analyzed and relocated ELF/PRX image, a report of functions/imports/NIDs and PPU/SPU coverage, generated sources and compiled objects, executed ABI/runtime/integration tests, and a report of pending items that prevent declaring the build reliable.
- scope: build pipeline, quality gate, manifest, reproducibility

## REQ-reliability-phase1-orchestrator
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Fase 1 — single recompilation pipeline. Create `tools/ps3recomp_build.py` as the official orchestrator integrating twelve stages (ELF/SELF/PRX identification, optional decryption, ELF parse/validate, PRX/ET_DYN relocation, PPU function discovery, SPU extraction and discovery, imports/exports/NID analysis, PPU/SPU lift, auxiliary sources and HLE table generation, incremental compilation, host link, tests and report). Make `recomp_config.py` actually control every pipeline option. Fix a stable build directory layout (`build/<game>/{manifest.json,input,analysis,relocated,lifted/{ppu,spu},generated,objects,reports,bin,logs}`). Replace the GoW2 scripts with wrappers over the orchestrator. Make each stage fail with a distinct code and structured message. Implement hash-based caching over input, config, tooling and dependencies.
- acceptance: a game can be rebuilt with no intermediate manual commands; re-running the same build reuses unchanged stages; a relocation, function, NID, coverage or compilation error prevents the link; the manifest allows reproducing the build exactly.
- scope: build orchestrator, manifest, caching

## REQ-reliability-phase2-elf-validation
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Fase 2 — ELF, PRX, OPD, TOC and relocation validation. Unify `elf_parser.py`, `ppu_loader.py` and `prx_relocate.py` behind a common API; validate all header/segment/offset/size bounds before reading; validate segment overlap and invalid regions; make PRX/ET_DYN relocation an official stage rather than a manual operation; record each applied/ignored/rejected relocation in the manifest; treat unsupported relocation types as an explicit error; validate OPD (alignment, code address inside an executable segment, TOC in a valid region, duplicate/inconsistent descriptors); validate `e_entry` and `OPD -> code/toc` resolution; add synthetic tests for EXEC ELF, PRX, BSS, TLS, OPD and relocations.
- acceptance: fixed EBOOTs and relocatable PRXs pass through the same pipeline; a truncated ELF, invalid OPD and unknown relocation fail before the lift; the report shows coverage and the reason for each unapplied relocation.
- scope: ELF/PRX validation, OPD, TOC, relocations

## REQ-reliability-phase3-lifter-coverage-contract
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [PARTIAL] Fase 3 — lifter with a coverage contract. Make both PPU and SPU lifters record every instruction as translated, approximate or unknown; remove silent `TODO` as default behaviour; introduce TOML coverage policy (`[coverage] unknown_ppu`, `unknown_spu`, `approximate`, `allow_noop`); for each approximation record opcode, guest address, function, omitted effect, expected impact and permission rationale; make lifter output deterministic including in parallel mode; validate labels, gotos, trampolines and function entries per chunk; add automatic syntactic compilation tests for all chunks; separate data embedded in `.text` from SPU instructions; add golden tests for PPU/SPU instructions and hard CFGs. Partial: the TOML parser has landed (`tools/recomp_config.py`), and `PLAN_READY` records that the coverage policy is to be consumed from the TOML rather than CLI flags.
- acceptance: no unknown instruction goes unnoticed; the same input produces equivalent sources regardless of `-j`; the build reports exactly which approximations were accepted; label/target/trampoline failures are detected before the host compiler.
- scope: PPU lifter, SPU lifter, coverage policy, determinism

## REQ-reliability-phase4-memory-safety-modes
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Fase 4 — memory runtime with safety modes. Create a single guest-memory API used by PPU, HLE, syscalls, SPU and RSX; eliminate divergent copies of `vm_read*`/`vm_write*`; implement `strict` (invalid access terminates with full diagnostics), `diagnostic` (log and return a controlled error) and `compat` (tolerant legacy behaviour) modes; record guest thread, function, address, size, access type and call chain; implement a dynamic committed-region table without an artificial small-region limit; validate alignment where the instruction requires it; distinguish a recoverable guest error from runtime corruption; add guard pages where the host allows; create tests for endianness, BSS, TLS, stack, RSX memory and cross accesses.
- acceptance: an invalid pointer never causes silent corruption in `strict` mode; the report identifies the first known origin of the invalid address; HLE and recompiled code observe the same memory rules.
- scope: guest memory API, strict/diagnostic/compat modes, committed regions

## REQ-reliability-phase5-ppu-state-abi
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Fase 5 — PPU state and ABI. Review `ppu_context` against the architectural state used by generated code; guarantee preservation of GPR, FPR, VMX, CR, LR, CTR, XER, FPSCR, VSCR, VRSAVE, CIA, TLS, atomic reservation and thread identifier; make `lwarx/stwcx` depend on guest thread context rather than global state; create explicit save/restore APIs for syscall, callback, HLE, thread switch, trampoline and controlled exception; implement typed guest/host pointer wrappers; forbid the generic dispatcher from passing guest pointers directly to host C; create ABI tests for arguments, returns, pointers, big-endian structs, floats, vectors and callbacks.
- acceptance: every HLE handler explicitly declares its argument and return contract; tests detect corruption of TOC, LR, CTR, CR or the atomic reservation; host↔guest callbacks preserve the full context.
- scope: ppu_context, ABI, atomics, HLE contracts

## REQ-reliability-phase6-scheduler-locks
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Fase 6 — scheduler, threads and state machine. Define a guest thread object holding PPU context, TLS, block state, priority, deadline and wait cause; implement a cooperative scheduler for known blocking points; progressively replace the global giant lock with per-subsystem locks (memory, HLE, filesystem, audio, RSX, SPURS, thread table); keep the giant lock as a temporary instrumented compatibility mode; create an explicit release/reacquire policy for blocking syscalls; implement deadlock, starvation and no-progress detection; record state transitions with a monotonic sequence and virtual timestamp; add snapshots of threads, locks, events and relevant regions.
- acceptance: deterministic mode reproduces the same event sequence across repeated runs; a deadlock produces a dump with threads, locks, wait sites and last progress; the game does not depend on `Sleep(0)` or accidental timing to progress; the giant lock can be disabled in tests without immediate corruption.
- scope: scheduler, giant lock, per-subsystem locks, determinism

## REQ-reliability-phase7-time-events-callbacks
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Fase 7 — time, events and callbacks. Create a virtual guest clock with conversion to the PS3 timebase; allow advancing the clock by event in tests; separate `sys_time_get_current_time` from `mftb` and document both; implement deadlines, timers and timeouts without depending directly on the host clock; centralize event queues, event flags, semaphores, mutexes and conditions; test spurious wakeups, timeout, cancellation, destroy-during-wait and handle reuse; instrument callbacks with origin, destination, context and delivery order.
- acceptance: time-dependent FSMs can be executed instantly in tests; timeout and wakeup are reproducible; delayed or duplicated callbacks are detected automatically.
- scope: virtual clock, timers, events, callbacks

## REQ-reliability-phase8-spu-spurs-dma
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Fase 8 — SPU, SPURS, DMA and synchronization. Define a real per-context MFC queue even if the backend initially executes synchronously; model tag states (pending, executing, completed, error); implement tests for GET/PUT, lists, barriers, sync and atomic commands; fix and test GETLLAR/PUTLLC/PUTLLUC and reservation semantics; validate LS/EA/DMA-list endianness and alignment; integrate `image_id` with kernel, policy module and job loading; define the job return contract, `stop_code`, halt and unresolved branch; integrate SPU fallback and lifted code under a declarative per-image policy; test blocking mailbox, signals, event status, decrementer and preemption; add multi-worker and ThreadSanitizer tests.
- acceptance: SPU jobs produce the same guest effects under fallback or an equivalent lift; no job is indistinguishable between "finished", "blocked" and "crashed"; SPURS queues do not depend on accidental host thread ordering.
- scope: MFC, DMA, SPURS queues, SPU job contract

## REQ-reliability-phase9-hle-contracts
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Fase 9 — HLE and module contracts. Change `MODULE_STATUS.md` to separate ABI, partial behaviour and stub; generate a matrix of NIDs required by each game; make a mandatory import without a handler fail the build unless explicitly overridden; create a typed registry of HLE signatures; implement guest/host conversion for pointers and structures in each API; prioritize modules by the game's real usage rather than function count; complete the critical paths first (filesystem, threads and synchronization, SPURS, video/media, input, audio, save data); for media modules integrate a real backend and keep the fallback clearly identified.
- acceptance: the report shows NID, name, signature, handler and fidelity level; a stub used on a critical path produces a configurable warning or failure; integration tests exercise real handlers, not just mocks.
- scope: HLE modules, NID matrix, typed signatures

## REQ-reliability-phase10-rsx-presentation
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Fase 10 — RSX, presentation and multimedia resources. Separate the RSX command processor, state tracker and host backend; create deterministic replay tests for FIFO, shaders, textures, render targets, flips and synchronization; guarantee the backend does not alter guest state invisibly; implement a per-frame presentation policy with no game-specific hooks in the generic runtime; complete or explicitly declare the limitations of VDEC/ADEC, VPOST and codecs; integrate video and audio files through a replaceable host layer. `PLAN_READY` records that RDY-2 delivers the FIFO core as a prerequisite and reorders it ahead of this phase.
- acceptance: a replayed frame produces a stable result across supported backends; the game needs no specific injections to advance its normal FSM; shader, texture or presentation failures point back to the original guest command.
- scope: RSX architecture, replay tests, presentation policy, codecs

## REQ-reliability-phase11-gow2-migration
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Fase 11 — GoW2 migration. Convert `games/gow2/*.sh` into declarative configuration and orchestrator commands; classify every current hook as a generic runtime fix, a module fallback, a game-specific workaround, or a temporary diagnostic; progressively remove "force FSM", EOS and synthetic-state hooks once the natural path is implemented; keep diagnostic probes but off the normal execution path; create acceptance checkpoints (boot, intro, WAD open, first frame presented, menu, input, gameplay, save/load, shutdown) and store logs and snapshots per checkpoint.
- acceptance: GoW2 recompiles and starts through the common pipeline; remaining workarounds are documented and isolated; each checkpoint has an automated test or reproducible replay.
- scope: GoW2 migration, hook classification, checkpoints

## REQ-reliability-phase12-perf-debt
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [OPEN] Fase 12 — performance and technical-debt removal. Measure parsing, discovery, lift, compilation and link time; optimize only after the correctness gates; improve lift and compilation parallelism without changing semantic output; replace linear dispatch tables with scalable structures where needed; remove hardcoded game-address diagnostics from the generic runtime; remove dead code, duplicated scripts and outdated documentation; publish a per-game, per-platform compatibility matrix.
- acceptance: the incremental build is measurably faster than a clean build; sanitizers pass on the runtime tests; the generic runtime contains no game-specific addresses or FSMs.
- scope: performance, technical debt, compatibility matrix

## REQ-rdy0-unblock-relift
- source: docs/superpowers/plans/2026-07-25-00-rdy0-desbloquear-relift.md
- description: [OPEN] RDY-0, the hard prerequisite of the engine-side master plan: make a clean re-lift of the EBOOT produce a functionally equivalent `boot_gow2` with no manual edits. Three converging fronts across ten tasks (all 63 checkboxes unchecked): (1) Engine — restore the lifter emissions that commit `11a1c3c` deleted (`#include <stdlib.h>`, the `__APPLE__` branch of the `g_trampoline_fn` TLS, and `ppu_stubs.cpp` generation), with a golden preamble test that fails if they leave again; (2) Orphan host code — 1603 lines of our own host code exist only inside the gitignored lift (698 in chunk 000, 897 in 001 including 197 lines of an F2B subsystem with no versioned home, 8 in 003), and 21 behaviour markers have no script capable of writing them; extract them to versioned files compiled separately and wire `host_gow2_factory.cpp` into the build; (3) Catalogue — turn 71 `patch_*.py` scripts into a gate that actually fails, closing `PATCH_CATALOG.tsv` at 71 with no "indeterminado". Add an optional `RELIFT=1` step to `build_macos.sh` writing to a new directory (`recomp_macos_v3`) with promotion only after the smoke passes. Fixed decisions not to reopen: no `--code-end` in the canonical lift; acceptance recipe is the headless M0 arm; 6 runs per side; the 19 PROBE patches stay outside the gate; new directory plus promotion; canonical engine root is this checkout.
- acceptance: `RELIFT=1 ./build_macos.sh` writes `recomp_macos_v3` and builds, `recomp_macos_v2` untouched; `./verify_lift_baseline.sh recomp_macos_v3` exits 0 (none of the 21 orphan behaviours lost); `./apply_all_patches.sh recomp_macos_v3 --status <tsv>` shows **zero** `FAILED` outside class `PROBE` (pre-plan baseline: `APPLIED 13 / ALREADY 9 / FAILED 49`); `./smoke_relift_equiv.sh recomp_macos_v3 6 <tsv>` exits 0 with `st620 max >= 3` in **>=4 of 6** runs, `function_table_count == 56072`, `56223 lifted functions registered`, `151 imports / 13 modules`, zero orphans; `PATCH_CATALOG.tsv` closes at 71; `git status` clean in both repos. Explicitly declared: `R_PermA` does not enter the acceptance (not observable in the M0 recipe), the acceptance does not cover the present path (headless recipe — a re-lift that breaks flip would pass this gate), and Gate A stays RED as an expected value rather than a failure.
- scope: PPU lifter, re-lift pipeline, patch catalogue, F2B subsystem, build_macos.sh, smoke gate

## REQ-rdy1-phase0-residue
- source: docs/PLAN_READY_FOR_GAME_RECOMPILATION.md
- description: [OPEN] RDY-1 — close only what Fase 0 of the reliability plan left open. Parameterize the SPU suite output directory (`SPU_TEST_OUTDIR`, default `mktemp -d`) and explicitly decide whether `runtime/spu/tests/out_*/` are comparison goldens or leave version control — not both versioned and overwritten. Make `scripts/test_runtime.sh` **fail** when `git status --short` shows modified tracked files instead of merely warning. Register `test_rsx_fifo` and `test_fp_decompiler` in CTest as `WILL_FAIL` so the exclusion stops living in a CMake comment. Fix the ledger entry `.superpowers/sdd/progress.md:373`, which classifies the two `test_fp_decompiler` failures as "BUGS REAIS DE PRODUTO" when dated evidence (`f2363ce`) says otherwise. Register `test_msl_decompiler` after converting it to the SKIP convention. Guard `-ffp-contract=off` per compiler so the Windows CI job stops being green-by-`continue-on-error`. Finish the doc correction begun in `2c5fef2` (`docs/TOOLS.md:611` and `docs/GAME_PORTING_GUIDE.md:35` still say "Python 3.9+" against `find_package(Python3 3.11 REQUIRED)`), and mark `docs/gow2-recomp-notes.md` and `games/gow2/SPURS_M2_FINDINGS.md` obsolete in their headers. Repair `scripts/smoke_macos.sh` (step 3 does not link: `_cellGcmMainloopNote`, `_movie_vt_*`) or admit it with a declared `SKIP:`.
- acceptance: raw `ctest --test-dir <build> --output-on-failure` (no wrapper) followed by `git status --short` shows only expected untracked files and zero ` M `; `ctest -N` lists `test_rsx_fifo` and `test_fp_decompiler` as `WILL_FAIL` plus `test_msl_decompiler`; a real CI run exists and is green on macOS (today `total_count: 2`, both Dependabot).
- scope: CTest, CI, test hygiene, working-tree gate, docs drift

## REQ-rdy2-fifo-core-be
- source: docs/PLAN_READY_FOR_GAME_RECOMPILATION.md
- description: [OPEN] RDY-2 — a single, big-endian-correct NV47xx FIFO core. Two decoders exist and diverge in measured ways: `cellGcmSys.c:522-635` (live) uses `vm_read32` with byteswap, method mask `cmd & 0x3FFCu`, follows JUMP, jumps on CALL **without pushing**, does `get += 4` on RETURN **without popping**, and bounds only by `guard < 200000` with `ioSize` never used; `rsx_commands.c:613-646` (tested) reads `buf[pos++]` raw with no byteswap, masks `& 0x1FFC`, and `break`s on JUMP/CALL/RETURN, bounded by `pos < count`. CALL and RETURN are correct in neither — migrating them adds semantics, not just moved code. Build the core over an abstract source (`read_be32(offset)`, ring bounds/size, `get`/`put`, jump/call/return with a real stack, per-method callback, structured result); decide and document the canonical method mask (no NV constant in `rsx_commands.h` exceeds `0x1FF0`, but the live path uses `0x3FFC` — choosing wrong silently changes in-boot behaviour); have `cellGcmSys` supply the source and the sync callbacks while preserving the sync-without-backend mode; add a gated shadow-diff (`PS3_RSX_SHADOW_DIFF=1`, OFF by default); keep the trace backend as an init safety net; decide the fate of the third RSX state model (`libs/video/rsx_dispatch.c:159`, a 16K register file consumed by `rsx_live_draw.c:1297` and the replays, which knows methods no walker can produce, e.g. `M_GCM_DRIVER_FLIP 0xE944`, while `rsx_live_draw_method` has zero callers outside its own file and is a no-op off Windows); add the missing integration test `cellGcmInit → guest memory → FIFO → SetFlipCommand → backend recorder`; update `docs/RSX_GRAPHICS.md:138` and `README.md:409` (the latter describes `gcm_flush_guest_cmdbuf()`, a function that does not exist).
- acceptance: `test_rsx_fifo` 4/4 in CTest (today 0/4) with the RDY-1 `WILL_FAIL` removed in the same commit; new green tests for non-increment, truncated packet and `count` past the end (no out-of-bounds, proven with ASan), jump, **call+return with a stack**, ring wrap, and reference/semaphore/label callbacks; **shadow-diff: 0 divergences** of `(method, value, get)` over a full intro run with `PS3_RSX_SHADOW_DIFF=1`; 25s intro smoke with probes OFF showing `st620` max >= 3, sticky pub/hit, no orphans.
- scope: RSX FIFO, NV47xx decode, endianness, cellGcmSys, shadow-diff

## REQ-rdy3-spu-strict-decode
- source: docs/PLAN_READY_FOR_GAME_RECOMPILATION.md
- description: [OPEN] RDY-3 — revert the SPU decode regression and enable strict mode. The scope is not 99 unknowns: it is 97 `heqi` + 2 data words + a policy that does not exist. Restore `heqi/hgti/hlgti` in the `RI10_TABLE` of `spu_disasm.py` (added by `7554685`, deleted by `11a1c3c`) with a decode test that would fail today. Resurrect `spu_halt_if` (`runtime/spu/spu_channels.c:482`, currently zero callers) so the lifter emits `spu_halt_if(ctx, <cond>, 0x<addr>)` instead of a comment, with a semantic test. Declare `spu2_v2` `0x10500..0x10510` as **data** via `[[invalid_instructions]]` in the config rather than an unimplemented opcode, and add detection of overlapping functions and holes in the map (`spu1_v2`: `spu_func_000030E8` covers 0x30E8..0x3174 but `spu_func_00003128` starts inside it, so the same `heqi` at 0x3164 is lifted twice). Put the policy in the TOML (`[coverage] unknown_spu = "error"|"warn"|"emit"`), not in CLI flags; in `error` mode write no artefact and exit non-zero. Emit a per-image JSON report (address, raw word, mnemonic, function, section, rejection reason) plus the LS address as a per-instruction comment in the generated C. Redefine "reachable instruction" as "inside `[start_addr, end_addr)` of a mapped function and not declared as data in the config" — computable today, since `grep 'reach|cfg|visited|worklist' tools/spu_lifter.py` returns zero results and the v1 criterion is not computable; real CFG/worklist is a later improvement, not a prerequisite. Define the acceptance universe as `spu_lifted/spu?_v2` (what `build_macos.sh:160` actually compiles), importing the missing `spu4_v2`/`spu5_v2` or explicitly declaring their exclusion.
- acceptance: every image in `spu_lifted/spu?_v2` lifts with `unknown_spu = "error"` and **0 `.word`**; `grep -c 'TODO spu'` in the compiled images → 0; `grep -c 'spu_halt_if' <image>` → > 0; a regression test that fails if `heqi` leaves `RI10_TABLE` again; a synthetic SPU ELF with a reachable unknown word makes `error` mode fail, point at the address, and **not** write usable source.
- scope: SPU disassembler, spu_lifter, coverage policy, data-vs-code separation

## REQ-rdy4-ppu-e2e-fixture
- source: docs/PLAN_READY_FOR_GAME_RECOMPILATION.md
- description: [OPEN] RDY-4 — a legal, portable end-to-end PPU fixture; described as the only genuinely new, non-overlapping track. Feasibility already measured: a synthetic 488-byte ELF64 BE PPC64 traversed `elf_parser.py → find_functions.py → ppu_lifter.py → clang` successfully, including `.opd` seeding with `shoff=0`. Build `tools/wrap_ppu_elf.py` (~300–400 lines: ELF64 BE, two `PT_LOAD`, `.text`/`.opd`/BSS, valid entry OPD and TOC, >=2 functions, direct call, return, branch, BE load/store), a dummy HLE import (a `bl` to a dummy stub plus a one-entry JSON via `ppu_lifter.py --hle-stubs`, not a forged `PT_PROC_PRX_PARAM` + `.lib.stub`), `runtime/ppu/tests/test_ppu_pipeline_e2e.py` orchestrating the chain in a tmpdir, and a portable C++ runner. Known blocker: `runtime/ppu/ppu_loader.cpp:1114-1116` calls `ps3_type15_block_stomp()`, a symbol that does not exist in the monorepo, so the runner needs a weak stub (measured: `Undefined symbols: _g_trampoline_fn, _ps3_type15_block_stomp`). The lifter preamble emits `__declspec` (`tools/ppu_lifter.py:66,371`), so the test must pass `-fms-extensions` on POSIX or the preamble must become conditional. Also move `tools/test_ppu_lift.py:946`, which writes to `<repo>/scratch/`, into a tmpdir.
- acceptance: in a clean clone with no EBOOT and no PS3 SDK, `ctest -R py.test_ppu_pipeline_e2e` passes on all three platforms; **and mutation proves value** — the test must fail when the ELF parser byteswap is inverted, when a function boundary is corrupted, and when the dispatcher fails to register a function. An E2E that passes with a broken pipeline is not regression-proof; that is the criterion, not "traverses the chain".
- scope: PPU pipeline E2E test, synthetic ELF fixture, CTest, mutation testing

## REQ-rdy5-gow2-reproducible-build
- source: docs/PLAN_READY_FOR_GAME_RECOMPILATION.md
- description: [OPEN] RDY-5 — reproducibility of the GoW2 build as a delta over reliability Fase 1, not a new orchestrator. Existing: `decrypt_self.py`, `extract_pkg.py`, `apply_all_patches.sh` (319 lines, single catalogue, `--check`), `build_macos.sh` (233 lines), 18+ smokes with named gates. Missing: the single-command chaining, the re-lift step (RDY-0), the hash manifest, and the check that no proprietary data is in the index. Do not introduce `GOW2_ROOT` — that role already belongs to `PS3_VFS_ROOT`/`PS3_MOVIE_CACHE`/`PS3_ENGINE_ROOT`; a fourth name for the same thing is debt. Add a local gitignored hash manifest over the game inputs and a pre-build check that fails if proprietary data is in the Git index, plus a wrapper chaining extract → re-lift → patches → strict SPU lift → compile → link → smoke as a client of the Fase 1 orchestrator, not a competitor. Depends on RDY-0, RDY-3 and reliability Fase 1.
- acceptance: from a clean checkout plus a private copy, **one command** produces a `boot_gow2` that passes the intro smoke with the same named counters as the baseline; "the same executable" means **functionally equivalent as measured by the gates**, not bit-identical (absolute paths, mtimes and build ids make bit identity unreachable); `git status` clean and no game file in any commit.
- scope: reproducible build, hash manifest, proprietary-data gate, single-command wrapper

## REQ-xenon-midasm-hooks
- source: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md
- description: [OPEN] Phase A, Tasks A2 and A3 — mid-asm hook emission in `tools/ppu_lifter.py`, then migrate one real GoW2 hook (the CE03C wait-idle) from `patch_*.py` to mid-asm. Stated goal of the migration: prove that a re-lift does not delete wait-idle. Host implementations live in `gow2-recomp/hooks/gow2_midasm_hooks.cpp` so `ppu_recomp_*.cpp` stays regenerable. `docs/RECOMP_CONFIG.md` records that `midasm_hook` is documented in the TOML schema but **not yet consumed** by the lifter.
- acceptance: A2 — `pytest tools/test_midasm_emit.py` passes and `boot_gow2` links; A3 — 30–40s headless smoke shows `CE03C wait` or the midasm log, with thr if the recipe includes AUTO_LOAD.
- scope: mid-asm hooks, ppu_lifter, re-lift safety, patch migration

## REQ-xenon-weak-function-overrides
- source: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md
- description: [OPEN] Phase A, Task A4 — weak function overrides (`PPC_FUNC` / `PPC_FUNC_IMPL` macros in `runtime/ppu/ppc_hooks.h`, host overrides in `gow2-recomp/hooks/gow2_func_overrides.cpp`). Clang 18+ recommended for weak aliases; MSVC/clang-cl limits must be documented. `docs/RECOMP_CONFIG.md` records `functions_override` as documented but not yet consumed.
- acceptance: `nm` shows the weak symbols and the GoW2 smoke reports no missing symbols.
- scope: weak overrides, ppc_hooks.h, re-lift safety

## REQ-xenon-jumptable-analysis
- source: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md
- description: [PARTIAL] Phase B — jump-table analysis and correct emission. B1 (`tools/ps3_analyse.py` jump-table detector writing TOML) is unstarted. B2 (lifter consumes switch tables as a real `switch(ctr)`) is partially landed: Step 1 "Load TOML into `jump_tables` dict (merge with discover_jump_tables; config wins on conflict)" and Step 3 "Audit known fallthrough bug class (guest EA target lower than site — document search script)" are checked; the remaining B2 steps are not. This connects to the systematic cross-fragment fallthrough audit named as an open follow-up elsewhere in the project.
- acceptance: B1 — `pytest tools/test_ps3_analyse_jtables.py` passes; B2 — the jtable emit test passes and a re-lift smoke shows no new FATAL OOB at boot.
- scope: jump tables, ps3_analyse.py, ppu_lifter, fallthrough audit

## REQ-xenon-indirect-dispatch-o1
- source: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md
- description: [OPEN] Phase C, Task C1 — O(1) guest→host function map (`runtime/ppu/ppc_indirect_map.{h,cpp}`), with `ps3_indirect_call` using the map once built and the lifter emitting a `ppc_func_entry[]` registration array. Strategy v1 is a sorted table plus bsearch; v2 an open-address hash only if profiling shows it hot. Existing ICALL-ASCII and HEAP guards must be kept.
- acceptance: the map unit test passes and the GoW2 smoke shows ICALL-ASCII residual not increasing.
- scope: indirect dispatch, ps3_indirect_call, lifter emission

## REQ-xenon-volatile-guest-access
- source: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md
- description: [OPEN] Phase C, Task C2 — volatile guest memory accesses. Policy: all guest load/store helpers used by lifted code treat the underlying access as not reorderable across calls (C `volatile` on the pointer cast, or a compiler barrier). Do **not** mark the entire VM array volatile (performance death) — only the access macros.
- acceptance: the runtime builds, GoW2 links, and the intro smoke shows no new OOB / no regression.
- scope: ppu_memory.h, vm.h, memory ordering

## REQ-xenon-abi-local-opts
- source: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md
- description: [OPEN] Phase D — ABI local-variable optimizations, mirroring the Xenon flag names (`skip_lr`, `ctr_as_local`, `xer_as_local`, `reserved_as_local`, `cr_as_local`, `non_argument_as_local`, `non_volatile_as_local`), all false by default in `gow2_recomp.toml`. Hard gate: **do not start D until A+B are complete and the GoW2 thr smoke is green on re-lifted chunks**. D2 (restgpr/savegpr auto-detect) only if D1 proves the optimizations useful; otherwise skip.
- acceptance: implement one flag end-to-end (`cr_as_local`, smallest blast radius) with `lift_selftest` passing flag-on and flag-off, a GoW2 smoke with the flag true on one chunk only, and the enable order documented in `RECOMP_CONFIG.md`.
- scope: lifter optimizations, ABI locals, RECOMP_CONFIG

## REQ-xenon-simde-and-opcode-suite
- source: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md
- description: [OPEN] Phase E — E1: optional SIMDe for VMX/AltiVec helpers behind a CMake option `PS3RECOMP_USE_SIMDE=ON`, vendored or fetched header-only, mapping one VMX op family through SIMDe and building on arm64 and x86_64. E2: expand the `tools/lift_selftest.py` corpus (shifts, rotates, CR fields, loads if the harness allows) and add `tools/run_lift_regression.sh`.
- acceptance: E1 — dual-arch build succeeds; E2 — at least 10 new corpus functions and a script that exits non-zero on mismatch.
- scope: SIMDe, VMX portability, lift_selftest corpus, regression script

## REQ-xenon-shader-recomp-design
- source: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md
- description: [OPEN] Phase F, Task F1 — shader recomp track, explicitly optional and only after natural SHADERSRC/LDRSH or an explicit decision to recompile HOSTRES shaders. Write `docs/SHADER_RECOMP_DESIGN.md` mapping the CGOWShader / combination / CRC wall, comparing with XenosRecomp (HLSL/MSL emission), and deciding between recompiling guest FS/VS versus keeping the Metal decompiler path. No code until the natural LDRSH path is understood.
- acceptance: the design note exists with acceptance metrics (FS hash ≠ demo, bind texture post-stream) and only the note is committed.
- scope: shader recompilation, CGOWShader, MSL/HLSL emission

## REQ-xenon-patch-migration
- source: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md
- description: [OPEN] Phase G — ongoing migration of the remaining GoW2 `patch_*.py` to the new mechanisms, prioritized: `patch_ce03c_wait_idle_f2b_movie.py` → midasm + hooks (P0, done as A3); `patch_2a4fe4_trace.py` → midasm or gated TRACE in the source template (P1); `patch_factory_snap_ty15_pin.py` → weak override or always-on runtime (P1); `patch_f2b_multimb_stream.py` → midasm on stream ops (P1); the OPD patches → `recomp.toml` `functions` plus analyse (P2). Rule after Phase A ships: no new permanent game fix may exist only as a `patch.py` without a TOML/hook path.
- acceptance: a maintained checklist in `gow2-recomp/notes/patch-migration-ledger.md`.
- scope: patch migration, re-lift safety, hook mechanisms

## REQ-metal-alpha-test-and-fp-vp-tests
- source: docs/superpowers/plans/2026-07-21-00-macos-metal-pending-backlog.md
- description: [OPEN] Residual items in the D3D12→Metal gap matrix that the backlog does **not** mark GREEN: `set_alpha_test` is "🟡 log only" on Metal with the plan noting "**M5** (FS subset later)"; FP/VP validate tests exist on Windows via `D3DCompile` but there is "sem `metallib` CI" on Metal, assigned to "M2 task testes Apple". Also declared: `rsx_live_draw` capture-replay is D3D12-only and "N/A Mac", explicitly **adiado** so as not to block the M-series. Note that `set_viewport` is listed as "🟡 parcial" against "M1/M5".
- acceptance: not stated numerically in the backlog beyond parity with the `rsx_backend` vtable defined in `libs/video/rsx_commands.h` (~315–346), using `libs/video/rsx_d3d12_backend.c` (~2539–2553 plus extras) as the Windows reference and `libs/video/rsx_metal_backend.m` (~240–254) as the current Mac state.
- scope: rsx_backend vtable parity, set_alpha_test, set_viewport, Metal shader validation CI

## REQ-track-p-playable-polish
- source: docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md
- description: [OPEN] Track P — playable polish. Explicitly **not entered**: gated behind a real menu UI (Gate A green plus a pad/menu signal). Contents: New Game path, first level load, pad mapping, audio continuity.
- acceptance: entered only after menu UI is real; the master INDEX records "M5 + M3+ R1–R3 closed with Gate A **RED** / playable=NO — no Track P until present+menu".
- scope: input, save, audio continuity, first level

---

# DONE — historical record, do not re-emit as work

## REQ-gate-b-thr-path
- source: docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md
- description: [DONE] Gate B, the thr path, is green. Recorded evidence: Mac link plus intro `st620` 0→1→>=3 on both the FORCE and EOS paths; a **real** second StartSeq (async, PARK=0) via REPLAY-NOPIC; `thr_auto_load` plus AutoLoad plus `R_Perm` at 20,169,344 bytes; the TYPE15 `product+0x70` circular hang in `2A4FE4` resolved by list sanitize plus full attach; the TYPE15 empty list after RESET closed by M2 CLOSE-PRESERVE with nodes=12 (`1f5d61c`); a menu-fast skip-intro path (FORCE 1.5s) reaching thr and R_Perm.
- acceptance: `thr_auto_load() end` >= 1, AutoLoad OK, `R_Perm` full, StartSeq >= 2 real with PARK=0. Met.
- scope: Gate B, thr, AutoLoad, R_Perm, StartSeq

## REQ-type15-product-list-membership
- source: docs/superpowers/plans/2026-07-23-menu-type15-frame-loop.md
- description: [DONE] Task M2 — fix product construction / list membership, driven by the M1 survivor (H1 primary, plus H3). Shipped as `1f5d61c` meeting criterion 1 (CLOSE-PRESERVE, nodes=12); criteria 2 and 3 not met, so f4 and flip remain residual. The proven list-close pattern (product+0x70 and +0x74 pointing at product+0x70, never a NULL-terminated pool where 2A4FE4 assumes circular) must not be re-broken. M1 itself (discriminate the CC9D0 spin root across hypotheses H1–H5, capped at 20 diagnostic lines, three boots) closed with exactly one primary survivor (`967525e`, `b87e6a3`).
- acceptance: any one of — after attach the child list installs >0 real children and CC9D0 sees >2 distinct `this` EAs within 5s post-thr; **or** f4/f54 becomes non-zero via a guest writer; **or** the two objects are removed from the parent tick list with static RE proof. Criterion 1 met, with Gate B green.
- scope: TYPE15 product construction, func_000CB56C, func_002A4FE4, list sanitize

## REQ-gate-a-diagnosis-closed
- source: docs/superpowers/plans/2026-07-23-m3plus-postwad-present.md
- description: [DONE] The Gate A diagnosis is closed as a diagnosis (not as a fix): R1 characterized ICALL-BAD `0x40637488` as freelist-as-code in the main heap arena (`0x40004020`), not a TYPE15 vtable or guest text, with the last SetFlip occurring before `movieio open R_LglScA` and the 12 ICALL-BAD events occurring after the cut (`1a540c8`); R2 discriminated H3 dead — `PS3_ICALL_HEAP40=1` still gives after=0 (`55e702a` / `e7dce33`); R3 pivoted to the present root class and killed D1 content-hold, D2 handler/mode, D3 GetFlipStatus and D4 late-flip, establishing Class A primary: mode cut intro→load with no re-arm after thr (`541c37b` / `8eb5e00`); R4 documented the verdict. The master INDEX extends this to R5–R12 (issuer reclassified to `2EFD60`/`2EFDA4` NID `0x21397818`; hot loop `CDBA4→CDC08→CDD00→CDD88` ~41k with post=0; menu present schedule parents post=0; `B6150` an OPD orphan; post-thr PCs only `{CC9D0, 2B2DD0, B951C}` at ratio 2:1:1; `B951C` not present).
- acceptance: R1–R4 acceptance boxes checked with Gate B staying green and no theater. Diagnostic rigour is declared a valid deliverable; the flip acceptance itself remains open (see REQ-present-reentry-after-rperm).
- scope: Gate A diagnosis, ICALL-BAD, HEAP40, flip issuer, present root class

## REQ-common-instrumentation
- source: docs/superpowers/plans/2026-07-24-bringup-instrumentacao-fase0.md
- description: [DONE] Common instrumentation plus Fase 0, decomposed into Tasks 1–8 with global constraints G1–G8. Delivered: the `ps3_trace` core with a single canonical line format, gating and a sentinel; the ten `PS3_TRACE_*` subsystems (CHECKPOINTS, SNAPSHOT, MEMORY, HEAP, THREADS, SPURS, DMA, FIOS, MEDIA, RSX) each with its own gate — the plan fixes ten subsystems and forbids an eleventh; the log parser `tools/trace_parse.py`; and the baseline harness `scripts/baseline_run.sh` plus `tools/baseline_report.py`. Constraints carried: default-off behaviour, no regression of existing consumers, macOS/arm64 platform rules, honesty-of-acceptance labels, PID-kill run protocol, Portuguese commits with explicit paths, and no forcers by default.
- acceptance: recorded as CONCLUÍDA by `docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md` §Fase 0.
- scope: ps3_trace, PS3_TRACE_* subsystems, trace_parse, baseline harness

## REQ-bringup-fase0-baseline
- source: docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
- description: [DONE] Fase 0 — baseline of the recompiled game. Three real headless runs of `boot_gow2` (`PS3_NO_RSX=1`, 60s timeout, PID kill) executed 2026-07-24. `barreira.igual_nos_runs = true` across all three: every run stopped at `ultimo_checkpoint=FIRST_WAD_OPEN` with the same `primeiro_erro` (`MEMORY/OOB ea=0xFB788082 len=4 region=unmapped op=read`), so no `variacao_explicada` was needed. Zero retries of the known "vm commit (sys_memory window) failed" flake in this batch. `instrucoes_desconhecidas` is left `null` (a lift-time quantity, not observable at run time) and `frames_apresentados=0` is a real measurement of the forced `trace` backend, not a defect.
- acceptance: a `baseline.json` exists (`docs/bringup/gow2/FASE0/baseline.json`) and the three runs show the same barrier or an explained variation. Met.
- scope: Fase 0 baseline, boot_gow2, baseline.json

## REQ-mask-observability-and-region-map
- source: docs/superpowers/plans/2026-07-25-fase1-desmascarar-memoria.md
- description: [DONE] Fase 1 first deliverable, Tasks 1–7: the `ps3_mask_trace` module; wiring `vm_uncommitted_guard` and `tblsize_guard` (the latter needing a signature change to receive `caller_ra`) into the canonical `MEMORY|SWALLOWED` event; making `vm.h` the single source of truth for the region map with `_Static_assert` guards against drift; registering the committed ranges each successful `vm_commit` produces in `sys_memory.c`; and running experiments E1 (masks on) and E2 (one mask off at a time) plus the baseline re-measurement. Three always-on behaviour changes shipped and are declared: the region-map fix, the committed-range registration, and `sys_mmapper_allocate_address` now returning `CELL_ENOMEM` without writing `addr_out` when `vm_commit` fails (previously `CELL_OK` with `addr_out` pointing at never-committed memory — a change to the syscall's success contract). Explicitly not done: allocator header validation, `strict`/`diagnostic` modes, the ~2110 direct `vm_base` writes, the two `type15_*` masks in the game repo, and any lifter change or re-lift.
- acceptance: per-mask report, `region=` correct for the real game heap proven in-boot, baseline re-measured over 3 runs and compared with any difference explained, all suites green, and zero `[PS3T]` lines with everything disabled. Met; the source plan's own closing section states it does **not** reach C1.
- scope: ps3_mask_trace, vm.h region map, sys_memory committed ranges, E1/E2

## REQ-recomp-config-toml
- source: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md
- description: [DONE] Phase A, Task A1 — declarative recomp config schema and parser (`tools/recomp_config.py`, `tools/test_recomp_config.py`, optional `tools/schemas/recomp_config.schema.json`), inspired by XenonRecomp's declarative config and adapted to this repo's PPU lifter. Shipped per the master INDEX as `aa0a652`. The author-facing reference is `docs/RECOMP_CONFIG.md`, which covers `[main]`, `[[midasm_hook]]`, `[[functions_override]]`, `[[invalid_instructions]]` and `[optimizations]`, and is explicitly a stub filled in as later phases land.
- acceptance: `pytest tools/test_recomp_config.py` all pass.
- scope: TOML recomp config, recomp_config.py, RECOMP_CONFIG.md

## REQ-xenon-invalid-instruction-skip
- source: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md
- description: [DONE] Phase B, Task B3 — invalid instruction / padding skip. All three steps checked: the config field from A1 is wired into the linear scan; when skipping, no TODO spam is emitted, only a `/* invalid skip N bytes */` comment; committed. `docs/RECOMP_CONFIG.md` confirms `ppu_lifter.py` consumes `[[invalid_instructions]]` via `--config`.
- acceptance: the invalid-skip wiring is exercised and produces the comment rather than TODO spam.
- scope: ppu_lifter, invalid_instructions, padding

## REQ-spurs-hle-kernel-engine
- source: docs/SPURS_BRINGUP.md
- description: [DONE] Marcos 0–4 of the SPURS bring-up, with the HLE-first approach chosen over LLE (tractable, needs no firmware binaries, matches RPCS3's default, and the handshake scaffolding already existed). M0 — SPU interpreter foundation green (2026-06-20): `test_interp`, `test_dma`, `test_spu_elf`, `test_spu_integration`, `test_interp_park`, `test_interp_stopn` all PASS, with cross-thread park/resume working. M1 — captured the real GoW2 SPURS protocol (2026-06-22): the game initializes SPURS three times but never reaches `cellSpursAddWorkload`; two orphan waits stop the boot; the root is that `cellSpursInitializeWithAttribute` was a stub. M2 — blocker diagnosis complete (2026-06-22): the `spurs=0x0` ABI question resolved as deliberate (handle-based), the MVP status-byte fix proven inert, and the real blocker characterized by gdb plus a hardware watchpoint proving nothing writes `[0x86E118]`. M2.5/M3/M4 — the generic engine implemented and tested (2026-06-25): `libs/spurs/spurs_kernel.c` as an async host-thread kernel with a workload queue and wake/park plus `start`/`submit`/`drain`/`stop`, wired into `cellSpurs.c`, with a 50-workload unit test and an e2e test driving `cellSpursAddWorkload` → kernel → `spu_workload_dispatch` → a real lifted SPU job → DMA of the result into memory.
- acceptance: M0 SPU test suite green; M1 an annotated trace with concrete EAs; M2 the blocker characterized; M3 a synthetic workload executed with its result visible in host memory; M4 a blocked PPU `sys_event_queue_receive` woken with the expected payload. Met for M0–M4; M5 remains open.
- scope: SPU interpreter, SPURS HLE kernel, workload dispatch, event signalling

## REQ-reliability-phase0-test-infra
- source: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md
- description: [DONE] Fase 0 — baseline and development infrastructure: separate generic runtime, tooling and `games/gow2` code; create `pyproject.toml`; resolve the Python version question; fix self-test imports; remove `cmd /c`, `taskkill` and absolute paths from generic tests; add CTest and make `PS3RECOMP_BUILD_TESTS` actually register tests; create portable `scripts/test_python.sh`, `test_runtime.sh`, `test_all.sh`; configure CI for macOS, Linux and Windows. `PLAN_READY_FOR_GAME_RECOMPILATION.md` records this phase as already merged (`0c6a01b`) with 15 tests in CTest (`CMakeLists.txt:274-416`), 3 CI jobs (`.github/workflows/ci.yml:40,80,124`) and the SKIP convention (`CMakeLists.txt:298`); the open residue is owned by RDY-1.
- acceptance: one command installs dependencies and runs every test possible on the platform; platform-incompatible tests are marked SKIP rather than failing mysteriously; `cmake -DPS3RECOMP_BUILD_TESTS=ON`, `cmake --build` and `ctest --output-on-failure` work on all supported platforms. Met, with residue.
- scope: CTest, CI, portable test scripts, pyproject

---

# SUPERSEDED — a later plan owns this scope

## REQ-macos-port-layers
- source: docs/MACOS_PORT_PLAN.md
- description: [SUPERSEDED] The 2026-07-19 layered macOS port plan (A runtime library builds on arm64 → B full SPU suite → C headless host boot → D SDL2 window plus input and audio → E real RSX render → F GoW2 playable on Mac), with phases F0–F6 covering toolchain and CI, Darwin POSIX portability (`st_*timespec`, a `pthread_mutex_timedlock` polyfill, replacing unnamed `sem_t` in `cellSync2`, `MAP_NORESERVE`, cellFiber/ucontext), the SPU suite, an `rsx_sdl_backend`, a POSIX GoW2 build, and RSX via Metal or Vulkan/MoltenVK. Its §7 snapshot records the runtime static lib as failing to compile on Darwin and RSX GPU as D3D12-only — both contradicted by the current codebase map and by later plans. Its Fase 5 recommendation ("Vulkan first if the roadmap already points to Vulkan; Metal if Apple is the only target") is contradicted by the Metal stack INDEX rule "Metal nativo como caminho default Mac" — see `INGEST-CONFLICTS.md`. Layers A–E are recorded as delivered by later work (the Metal stack and the macOS bring-up chain); layer F (GoW2 fully playable on Mac) survives as REQ-gow2-playable.
- acceptance: original per-layer acceptance — A `cmake --build build` green producing `libps3recomp_runtime.a` on arm64; B `Results: N passed, 0 failed`; C a headless host reaching a known hang rather than a platform crash; D a window created, cleared with the RSX color, closed cleanly; E the same D3D12 test geometry (clear plus a colored DrawInstanced) in a macOS window; F parity with the Windows bring-up.
- scope: macOS/arm64 port, build portability, SPU suite, RSX backend, GoW2 on Mac

## REQ-wall-chain-order
- source: docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md
- description: [SUPERSEDED] The 2026-07-20 ordered wall chain (W0–W28) with mandatory execution order and its rule "do not open plan N+1 while the acceptance of N is red": 1 host rebaseline Mac (W0, done), 2 wall M2 / SPURS boot (W1/W6/W7, M2 won), 3 SPU jobs protocol (W8–W12, partial), 4.0 intro audio open (unblocked), 4.EOS movie EOS FSM (done), 4.EOS+ intro vdec open/FORCE/WAD (Gate B), 4 content intro WAD (W13–W15, partial), 4.MENU TYPE15 frame loop (partial), 5 RSX shaders and pixels (W17–W23, pending), 6 A/V and polish (W24–W28). Also records the common TDD method (red smoke → minimal green → refactor → small named commit; force-writing guest flags without a legitimate writer is forbidden) and the RPCS3 oracle recommendation for I/O, SPURS and RSX walls. Its own header defers to the 2026-07-23 master INDEX; its satellite table's Metal M2–M10 statuses are stale — see `INGEST-CONFLICTS.md`.
- acceptance: per-link acceptance as tabulated in the source; the chain's own header records Gate B GREEN, Gate A RED, menu UI NO, playable NO.
- scope: GoW2 wall chain, execution order, TDD method

## REQ-track-g-metal-content-stack
- source: docs/superpowers/plans/2026-07-21-00-macos-metal-stack-INDEX.md
- description: [CONTESTED] The native macOS/arm64 Metal stack, M0–M10: M0 VideoToolbox+Metal overlay, M1 RSX draw path, M2 FP/VP → MSL → PSO cache, M3 bind_texture with deswizzle, M4 VT→IOSurface/CV zero-copy, M5 pipeline state (blend/depth/scissor/mask), M6 vertex attribs, M7 render targets, M8 content hold HOSTRES, M9 cellVdec YUV→BGRA present, M10 metal default plus smoke matrix. Common rules for all M* plans: Metal native as the default Mac path (`PS3_RSX_BACKEND=metal`), SDL null only for headless/debug; forbidden as an "Apple solution" — depending on `ffmpeg` on PATH, re-encoding the game cache to Theora/VP9, or OpenGL; prefer VideoToolbox for decode, Metal for GPU, AudioQueue/AudioUnit for audio, and `MTLSharedEvent`/fine locks over spinning; Windows D3D12 must not regress and contracts (`rsx_backend` vtable) are shared where possible; acceptance is always in-boot measured versus offline unit versus not-exercised. **Completion state is contested**: this INDEX marks all eleven entries ✅ GREEN 2026-07-21 and `PLAN_READY_FOR_GAME_RECOMPILATION.md` states "stack M0–M10 todo GREEN", while `2026-07-20-00-wall-chain-INDEX.md` lists M2–M10 as ⬜ and the 2026-07-23 master INDEX Track G says "M0/M1 already GREEN. Next: M2 MSL → M3 bind_texture → M5/M6 state → M7 RT present". See `INGEST-CONFLICTS.md`.
- acceptance: per-M acceptance as tabulated — M0 logo VT+Metal in-boot 330 frames plus EOS with no ffmpeg; M1 draw path PSO plus a multi-color demo triangle dump; M2 FP/VP → MSL → PSO cache in-boot; M3 bind_texture plus deswizzle plus TEX_DEMO dump; M4 VT→IOSurface/CV zero-copy; M5 blend/depth/scissor/mask hooks plus apply; M6 attrib snapshot plus multi-type upload; M7 RT cache plus offscreen draw plus blit; M8 content hold plus movie pause; M9 YUV→BGRA stage plus a DEMO multi-frame; M10 metal default plus the smoke matrix. Content acceptance is only valid with guest draws (Tracks S/M), never a demo triangle.
- scope: Metal, VideoToolbox, AudioQueue, macOS/arm64, rsx_backend vtable, D3D12 parity
