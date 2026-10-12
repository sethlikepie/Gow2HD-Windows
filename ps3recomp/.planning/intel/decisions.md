# Decisions (synthesized from ADRs)

Source type: ADR (3 documents). **No ADR in this ingest carries `locked: true`** — none
of the three has an explicit `Status: Accepted` field, so every entry below is
`status: proposed`. They are treated as strong architectural framing, not as
immovable locked decisions.

Precedence applied: ADR > SPEC > PRD > DOC (no per-doc override present).

---

## ADR-ARCH-1: Static AOT recompilation, not runtime emulation
- source: docs/ARCHITECTURE.md
- status: proposed
- decision: Transform a PS3 ELF binary into native C/C++ source ahead of time through a five-stage pipeline (ELF loading and analysis → PPU disassembly → control flow analysis → code generation → compilation by a standard host compiler), linked against the ps3recomp runtime library. There is no runtime CPU interpretation of PPU code.
- scope: recompilation pipeline, PPU, code generation, host build

## ADR-ARCH-2: Flat guest→host address translation over a mapped 4 GB space
- source: docs/ARCHITECTURE.md
- status: proposed
- decision: The runtime maps the entire 4 GB guest address space into host memory and translates addresses as `host_ptr = vm::g_base + guest_addr`; `vm::g_base` is set at initialization to a large `mmap`/`VirtualAlloc` region. Games use 32-bit effective addresses despite the 64-bit PPU.
- scope: virtual memory manager, guest address space, memory model

## ADR-ARCH-3: Endianness conversion only at the memory boundary
- source: docs/ARCHITECTURE.md
- status: proposed
- decision: PS3 is big-endian and hosts are little-endian; every memory access in recompiled code passes through endian conversion (`be_t<T>`, `vm::read<T>`, `vm::write<T>`). Register-local computation stays in host-native byte order; conversion happens only at memory boundaries.
- scope: endianness layer, memory accessors, lifted code

## ADR-ARCH-4: Fixed guest memory map regions
- source: docs/ARCHITECTURE.md
- status: proposed
- decision: Guest address ranges are assigned as: `0x00010000-0x0FFFFFFF` user ELF text+data; `0x10000000-0x1FFFFFFF` PRX module space; `0x20000000-0x2FFFFFFF` stack/TLS; `0x30000000-0x3FFFFFFF` heap (user malloc); `0xC0000000-0xCFFFFFFF` RSX local memory (VRAM, mapped); `0xD0000000-0xDFFFFFFF` RSX IO-mapped main memory.
- scope: memory map, VM regions

## ADR-ARCH-5: NID-based import resolution with CELL_ENOSYS fallback
- source: docs/ARCHITECTURE.md
- status: proposed
- decision: Parse the ELF import stubs for `(module, NID)` pairs, look each NID up in a database (`tools/nid_db.json`) to obtain the human-readable function name, and replace the import stub with a call to the HLE implementation. Unresolved NIDs log a warning and return `CELL_ENOSYS`. The NID database derives from RPCS3 function tables and Sony SDK headers.
- scope: NID system, imports, HLE bridge

## ADR-ARCH-6: Three-layer RSX translation architecture
- source: docs/ARCHITECTURE.md
- status: proposed
- decision: RSX is handled by three layers — `cellGcmSys` (HLE: command buffer control, display buffers, memory mapping, flip/VBlank, tile/zcull) → RSX Command Processor (`rsx_commands.c`, NV47xx FIFO parsing and GPU state tracking for surfaces, viewport, blend, depth/stencil, 16 texture units, 16 vertex attributes, shader programs, draw commands) → Graphics Backend behind the `rsx_backend` callback interface.
- scope: RSX graphics pipeline, cellGcmSys, rsx_backend vtable

## ADR-ARCH-7: Backend inventory as stated by the ADR
- source: docs/ARCHITECTURE.md
- status: proposed
- decision: Current graphics backends are Null (Win32 window showing clear color, for debugging) and D3D12 (real GPU rendering — device, swap chain, clear, present, "Phase 1"); Vulkan is listed as Planned.
- scope: graphics backends, D3D12, Vulkan, Null
- note: contradicted by PRD-level intent and by the evidence-backed codebase map, which record a native macOS Metal backend (`libs/video/rsx_metal_backend.m`) as existing and default on Darwin. See `INGEST-CONFLICTS.md` WARNING "Stale ADR backend inventory vs Metal-first reality".

## ADR-ARCH-8: SPU strategy ordering — HLE preferred, then interpreter, then static recomp
- source: docs/ARCHITECTURE.md
- status: proposed
- decision: Three SPU strategies in priority order: (1) HLE of SPU tasks (preferred) — identify SPURS/cellSync task types and replace audio mixing, physics, vertex processing and decompression with host implementations; (2) SPU interpreter — cycle-inaccurate switch-case execution, slow but compatible; (3) SPU recompilation — static recompilation of SPU programs, hybrid with interpreter fallback for dynamically loaded code.
- scope: SPU handling strategy, SPURS, cellSync

## ADR-ARCH-9: RPCS3 as read-only oracle in implementation practice
- source: docs/ARCHITECTURE.md
- status: proposed
- decision: When implementing HLE, syscalls, SPURS or RSX behaviour, use RPCS3 as a read-only oracle (source layout under `Emu/Cell/lv2`, `Emu/Cell/Modules`, `Emu/RSX`, plus runtime logs against the same EBOOT). Do not link RPCS3 into the recomp host.
- scope: RPCS3, implementation practice, HLE, syscalls, SPURS, RSX

## ADR-PLAT-1: Inline `#ifdef` platform switches instead of an abstraction layer
- source: docs/PLATFORM_ABSTRACTION.md
- status: proposed
- decision: ps3recomp uses inline platform switches (`#ifdef _WIN32`) rather than a separate abstraction layer, keeping the code direct and avoiding unnecessary indirection. This pattern is used consistently across all runtime and HLE module code.
- scope: platform abstraction, Windows, POSIX, Linux, macOS

## ADR-PLAT-2: Per-subsystem Windows↔POSIX primitive mapping
- source: docs/PLATFORM_ABSTRACTION.md
- status: proposed
- decision: Each subsystem has a fixed Win32↔POSIX mapping — threads (`CreateThread`/`pthread_create`, TLS via `__declspec(thread)`/`TlsAlloc` vs `__thread`/`pthread_key_create`, PS3 priority 0..3071 mapped to host priority bands); sockets (Winsock2 with `WSAStartup` vs POSIX, Winsock errors translated to PS3 errno); timers (`QueryPerformanceCounter` vs `clock_gettime(CLOCK_MONOTONIC)`, PS3 tick base 79,800,000); virtual memory (`VirtualAlloc` reserve/commit vs `mmap(PROT_NONE)` + `mprotect`); crypto RNG (`BCryptGenRandom` vs `/dev/urandom`); filesystem (`/` internally, PS3 open-flag table).
- scope: threading, synchronization, networking, timers, memory management, RNG, filesystem

## ADR-PLAT-3: `CRITICAL_SECTION` over `CreateMutex` on Windows
- source: docs/PLATFORM_ABSTRACTION.md
- status: proposed
- decision: Use `CRITICAL_SECTION` rather than `CreateMutex` for mutexes because it is much faster (user-space fast path, no kernel transition), is sufficient for intra-process synchronization, and has no named-object overhead. Condition variables use `CONDITION_VARIABLE`/`pthread_cond_t`; semaphores use Win32 `HANDLE`/`sem_t`; rwlocks use `SRWLOCK`/`pthread_rwlock_t`.
- scope: synchronization primitives, mutex, condvar, semaphore, rwlock

## ADR-PLAT-4: Audio and input backend selection per platform
- source: docs/PLATFORM_ABSTRACTION.md
- status: proposed
- decision: Audio — Windows primary WASAPI with SDL2 fallback; Linux primary SDL2 with PulseAudio as potential future; macOS primary SDL2 with CoreAudio as potential future. Input — Windows primary XInput (DS3 via DS3 drivers); Linux/macOS SDL2 GameController.
- scope: audio backends, input backends, WASAPI, SDL2, CoreAudio, XInput
- note: contradicted by PRD-level Apple-first policy (AudioQueue/AudioUnit/CoreAudio preferred over SDL2 on macOS) and by the codebase map, which records CoreAudio/AudioToolbox linked on Apple with a ring buffer in `libs/audio/coreaudio_ring.h`. See `INGEST-CONFLICTS.md`.

## ADR-PLAT-5: Fiber/coroutine implementation and its macOS caveat
- source: docs/PLATFORM_ABSTRACTION.md
- status: proposed
- decision: Windows uses Win32 fibers (`ConvertThreadToFiber`/`CreateFiber`/`SwitchToFiber`); POSIX uses `ucontext` (`getcontext`/`makecontext`/`swapcontext`). `ucontext` is deprecated on macOS but still works; for production macOS builds, consider `setjmp`/`longjmp` with manual stack management or platform-specific alternatives.
- scope: fiber/coroutine support, cellFiber, macOS

## ADR-RPCS3-1: RPCS3 is a living spec and trace oracle, never a linked dependency
- source: docs/RPCS3_AS_REFERENCE.md
- status: proposed
- decision: RPCS3 is used for syscall numbers, NIDs, HLE ordering and traces of FS/SPURS/RSX. It must not be linked inside `boot_gow2`, its GPL code must not be copied without an explicit policy, and guest state must not be forged because the emulator advances. RPCS3 is not a submodule; an optional sparse read-only clone (`Emu/Cell/lv2`, `Emu/Cell/Modules`, `Emu/RSX`) sits outside the game monorepo and is never committed.
- scope: RPCS3, LV2/HLE, GCM/RSX, SPU, reference workflow

## ADR-RPCS3-2: GPLv2 boundary — read to understand, reimplement clean-room
- source: docs/RPCS3_AS_REFERENCE.md
- status: proposed
- decision: RPCS3 is GPLv2. Reading the code to understand behaviour is normal; copying code into ps3recomp requires an explicit legal decision by the team. Clean-room reimplementation guided by observation is preferred.
- scope: licensing, GPLv2, clean-room reimplementation

## ADR-RPCS3-3: The M2 lesson — measure the lift before inventing HLE
- source: docs/RPCS3_AS_REFERENCE.md
- status: proposed
- decision: Before months of HLE work, (1) confirm in RPCS3 or the PPU interpreter the real effective address of the store at the same PC, (2) confirm what `ppu_lifter` emitted in the lifted C, and only then invent SPURS/kernel behaviour. Precedent: the watchpoint "nobody writes `0x86E118`" was true while the conclusion "SPURS is missing" was false — the old lift wrote to a different EA (`rA=0` mis-modelled). Rule for gap tables: one gap at a time.
- scope: debugging methodology, PPU lifter, SPURS, oracle workflow

## ADR-RPCS3-4: Acceptance criteria for an RPCS3 reverse-engineering session
- source: docs/RPCS3_AS_REFERENCE.md
- status: proposed
- decision: An RE session counts as well-executed only if it produces all three of: (1) an RPCS3 log or extract with timestamps/ordering of the symptom's events, (2) a recomp log over the same boot segment, (3) a gap table with exactly one implementable action (HLE, lift, VFS, or probe). Without (3) it was "just playing on the emulator".
- scope: RE session acceptance, oracle workflow, gap table

## ADR-RPCS3-5: Explicit anti-patterns when using the emulator as reference
- source: docs/RPCS3_AS_REFERENCE.md
- status: proposed
- decision: Forbidden — forging FSM state (`st620`/EOS) because RPCS3 advances; assuming an RPCS3 HLE maps to a point patch in the recomp; copying GPL files into the runtime; using "it plays in RPCS3" as port acceptance; matching RPCS3's timing/thread schedule. Specifically forbidden as a "fix": writing `st620`, `obj+0x744` or EOS from the host without the game having a real done-producer.
- scope: anti-patterns, acceptance discipline, FSM fidelity
