# Constraints (synthesized from SPECs)

Source type: SPEC (1 document):
`docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md`
("Fase 1, 1º entregável — Desmascarar a memória guest e corrigir o mapa de regiões";
header states "design aprovado pelo autor do projecto em 2026-07-25", `locked: false`).

The spec's global constraints G1–G8 are declared binding on **all** tasks of the
deliverable ("Violação é defeito, mesmo que a tarefa individual não as repita").
Several of them (G4, G5, G6, G7) restate repo-wide rules and are reused verbatim by
the PRD that executes this spec.

---

## G1 — Canonical trace line format
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: schema
- content: Every new line uses the canonical format in force since Fase 0: `[PS3T] <tb> | <thr> | <fn> | <subsys> | <event> | <data>`. Separator is exactly ` | `; no field contains `|`; `<data>` is `k=v` pairs separated by a single space; one line equals exactly one write call; destination is `stderr`. Sanitization is centralized in `ps3_trace_emit` and must not be reimplemented.

## G2 — Gating and defaults (what is a probe vs what is a bug fix)
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: nfr
- content: `MEMORY/SWALLOWED` emission defaults OFF (gated by `PS3_TRACE_MEMORY`) because it is observation (CLAUDE.md rule 6). `PS3_MASK_OFF_*` default = mask ON, as today; disabling is the experiment, not the norm. The `vm_region_name` correction and the committed-range registration default ON because they are bugs, not experiments. With `PS3_TRACE_MEMORY` unset and no `PS3_MASK_OFF_*` set: zero bytes emitted by the new layer and zero behaviour change versus the pre-deliverable code, with the sole declared exception of those two bug fixes, whose effect is measured by the baseline re-measurement.

## G3 — The observation layer must not invert control
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: api-contract
- content: The code that decides to swallow continues to decide. The new layer observes; it does not change the decision except when the corresponding `PS3_MASK_OFF_*` is enabled.

## G4 — Platform: macOS/arm64 primary, Windows must not regress
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: nfr
- content: macOS/arm64 is the primary target and where everything is measured. Windows cannot regress. Forbidden outside `#ifdef _WIN32`: `<intrin.h>`, `Sleep(`, `__declspec`, `GetModuleHandleA`. Nothing POSIX-only unguarded in code that enters `libps3recomp_runtime.a`.

## G5 — Honesty of acceptance
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: nfr
- content: Every acceptance statement declares its evidence level: **in-boot**, **offline-unit**, or **não-exercitado**. Over-declaring the evidence level is a defect. Never forge results.

## G6 — Run protocol for measurements
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: protocol
- content: Runs use `PS3_NO_RSX=1` **and** `PS3_RSX_BACKEND=trace` (Fase 0 proved the first alone is not headless — `cellGcmSys.c` has a second independent backend registration). Kill by PID (TERM, then `-9`); never `pkill -f boot_gow2`. Logs live outside both repos and are never committed. `PS3_ENGINE_ROOT` is mandatory when building `boot_gow2`, otherwise the engine from the other checkout is linked in.

## G7 — Git protocol
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: protocol
- content: Commits in Portuguese, no `Co-Authored-By`. `git add` with explicit paths — never `git add -A`. Never game data, boot logs, or the regenerated artefacts `runtime/spu/tests/out_*/spu_recomp.{c,h}`.

## G8 — Repository boundary for this deliverable
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: nfr
- content: This deliverable touches only the engine repo (`ps3recomp`). The two `type15_*` masks live in `gow2-recomp/host_gow2_factory.cpp` (game repo) and remain declared but not instrumented. No commits in `gow2-recomp`.

## SWALLOWED event contract
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: schema
- content: Emitted at the point where a mask swallows an access: `[PS3T] <tb> | <thr> | <fn> | MEMORY | SWALLOWED | ea=0x%08X len=%u mask=<nome> op=<read|write> region=<nome>`. `mask=` is a closed token set { `uncommitted_guard`, `tblsize_guard` }, no spaces. `<fn>` comes from `dladdr` over `caller_ra`, the same mechanism that already resolves `_Z13func_00263554P11ppu_context+0x1A8` in `MEMORY/OOB`. It is emitted under the existing `MEMORY` subsystem — no eleventh subsystem is created; the source plan fixes ten.

## Explicit volume cap with recoverable total
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: schema
- content: As with Fase 0's `MEMORY/OOB`, volume is capped, but the total must always be recoverable: on truncation, emit `SWALLOWED_TRUNCATED | mask=<nome> count=<total>` at a cadence that allows reconstructing the total (Fase 0 uses a linear cadence; inherit and document the same decision).

## `PS3_MASK_OFF_*` semantics — "do not intercept", not "the write succeeds"
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: api-contract
- content: `PS3_MASK_OFF_UNCOMMITTED=1` makes `vm_uncommitted_guard` always return `false`; `PS3_MASK_OFF_TBLSIZE=1` makes `tblsize_guard` always return `0`. Amendment of 2026-07-25 (project author's decision): for `UNCOMMITTED`, returning `false` means only "do not intercept" — the caller proceeds to `vm_base[a]`; since `vm_init` mmaps the whole space `PROT_NONE` and only `main` and `stack` gain `PROT_READ|PROT_WRITE`, access to uncommitted memory raises a real fault (SIGBUS on macOS). That is the diagnostic value of E2. Declared consequence: the gate cuts **before** the `vm_commit_high_on()` block, so combining this flag with `PS3_COMMIT_HIGH=1` does **not** produce a safe access — it disables the only path that would commit the page. Only for `TBLSIZE` does "do not intercept" actually mean "the write passes".

## `vm_region_name` honesty rule
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: api-contract
- content: `vm_region_name` (`runtime/memory/vm.h:332`) knows 4 regions (`main`, `rsx`, `spu`, `stack`) and returns `unmapped` for everything else, so the game's real heap (`0x40000000`–`0x60000000`) and the real RSX local (`0xC0000000`) read as `unmapped`, while the TLS image (`0x10F00000`) falls inside the nominal `rsx` interval and is reported as `rsx`. The fix adds the missing regions and disambiguates TLS. It is a label correction and changes no control decision (`vm_oob` compares against `ppu_vm_size`; the guard consults the registered range list; neither uses `vm_region_name`). Honesty rule: an interval whose boundary is not confirmed by code or measurement stays **out** of the correction and is declared in the report — inventing an interval to eliminate `unmapped` would trade one lie for another.

## Committed-range registration contract
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: api-contract
- content: Measured: 5 calls to `vm_commit` in `runtime/syscalls/sys_memory.c` (`:101`, `:174`, `:383`, `:491`, `:570`) and 0 calls to `ppu_register_committed_range`. Memory is committed but never registered, so `vm_uncommitted_guard` treats it as uncommitted and swallows writes to memory the game legitimately allocated. Each successful `vm_commit` must register its range. This is the correction that changes default behaviour, which is why the deliverable ends with a baseline re-measurement. The `vm_commit` at `:491` ignores its return value — verify before registering and declare the case.

## E1/E2 experiment protocol — one mask at a time
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: protocol
- content: E1 (`PS3_NO_RSX=1 PS3_RSX_BACKEND=trace PS3_TRACE_MEMORY=1`, masks ON) answers how many writes each mask swallows, from which guest functions, and to which regions, with the game running exactly as today. E2 is one run per mask plus a control run: control (no `PS3_MASK_OFF_*`), E2-a (`PS3_MASK_OFF_UNCOMMITTED=1`), E2-b (`PS3_MASK_OFF_TBLSIZE=1`). Never both at once: disabling everything at once gives a crash and zero information about which mask mattered. A boot that breaks earlier is a valid deliverable **if** the volume, the origin and the exact point are known.

## Acceptance criteria of the deliverable
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: nfr
- content: (1) Per-mask report: total count, top attributed guest functions, affected regions, and how far the boot gets without it. (2) `region=` correct for the game's real heap, proven **in-boot** with the real line pasted. (3) Baseline re-measured (3 runs via `scripts/baseline_run.sh --runs 3`) and compared against `docs/bringup/gow2/FASE0/baseline.json` with `diff_baseline`; any difference must be explained — recording that it changed is not enough; no change is also a valid result. (4) All suites green, including `runtime/spu/tests/run_tests.sh` at `21 passed, 0 failed` and a complete `scripts/smoke_macos.sh --no-gui`. (5) With everything disabled: **zero** `[PS3T]` lines.

## Offline-unit test contract
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: nfr
- content: Offline-unit tests follow the `runtime/trace/tests/` convention and must be wired into `scripts/smoke_macos.sh` section 2b (a suite that nothing runs rots silently — a lesson paid in Fase 0). Required cases: `SWALLOWED` emitted with the specified fields when the guard swallows; gate disabled ⇒ zero bytes; `PS3_MASK_OFF_*` makes the **gate** return "do not intercept" (verified at gate level, i.e. the function's return value, without real `vm_base` mapped — explicitly not the same as "the write succeeds"); `SWALLOWED_TRUNCATED` on the full path with recoverable total; `vm_region_name` returns the right name for one EA per region, including those that today return `unmapped`; `ppu_register_committed_range` called for every successful `vm_commit`.

## Declared gap — ~2110 direct writes to `vm_base` bypass the hooked funnel
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: nfr
- content: The lift writes guest memory through two paths: the 12 hooked accessors (`vm_read/write*`, atomics) **and** direct writes to `vm_base` emitted by the lifter — `stvx/stvebx/stvehx/stvewx` (1184), `stwbrx/sthbrx` (922), `dcbz` (4), measured on the built lift. These bypass OOB checks, the guard, watches and reservation cancellation; a single `dcbz` zeroes 128 bytes invisibly. Consequence: the `SWALLOWED` count is a **lower bound**, and a header corrupted by one of these writes is not observable through this path. Declared, not closed; closing it requires changing the lifter and re-lifting.

## Declared gap — two masks outside the engine repo
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: nfr
- content: `ps3_type15_block_stomp` and `type15_pin_free` live in `gow2-recomp/host_gow2_factory.cpp`. The first is called **inside** `vm_write32` (`ppu_loader.cpp:1176-1178`) and blocks stores to word0; the second blocks a guest allocator `free`. They are declared with file:line in the report and not instrumented in this round (G8). Instrumenting them is an explicit author decision and becomes its own task, with its own commit in that repo.

## Declared risk — the re-lift mine
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: nfr
- content: The current lifter emits `ppu_res_lwarx/ppu_res_stwcx/ppu_res_ldarx/ppu_res_stdcx` as `static inline` over `vm_base`, **without `vm_oob` and without `ps3_mem_oob_report`**. The lift built today still uses the hooked `ppu_lwarx` (5277 calls; `ppu_res_*` = 0), i.e. it was generated by an earlier lifter. A re-lift today would silently erase the instrumentation of the 4 atomics added in Fase 0 — and per the survey a fresh lift does not compile on macOS. This deliverable was designed to **not** require a re-lift, precisely to avoid stepping on the mine; the mine remains and blocks any future work that depends on a re-lift.

## Declared risk — stale measurements and a suspect classification
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: nfr
- content: The allocator numbers in the docs (`arr[126]=0x40686288`, 6 arenas, `alloc_w=25718`) come from Windows runs predating the 2550C8 fix and were never re-measured on macOS post-fix; they must not be used as current truth. Separately, `ppu_loader.cpp:1205-1206` classifies `func_00263554` as free/coalesce while the survey reports the lift shows a fixed-size pool alloc. Since that is exactly the function of Fase 0's first baseline error (`_Z13func_00263554P11ppu_context+0x1A8`, `ea=0xFB788082`), the reading of that error may be wrong. The contradiction was **not** confirmed by direct reading of the lift; it is to be resolved when header forensics begins.

## Declared risk — a second uninstrumented accessor surface
- source: docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md
- type: nfr
- content: `runtime/ppu/ppu_memory.h:60-172` holds a **second** `static inline` copy of the accessors that only checks `ppu_guest_range_committed()` and emits nothing; `vm_memcpy_from/to` and `vm_memset` (`:159-172`) have no guard at all. Measured consumer: `libs/system/cellSaveData.c`. Risk declared as low.
