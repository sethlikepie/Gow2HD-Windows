## Conflict Detection Report

Mode: new. Precedence: ADR > SPEC > PRD > DOC (no per-doc override present in any of
the 44 classifications). No document in this ingest carries `locked: true`, so the
LOCKED-vs-LOCKED check is vacuous and no blocker was manufactured on that basis.

### BLOCKERS (0)

No blockers. All 44 classifications are `confidence: high` with no `UNKNOWN` type, so
the type-tag blocker does not apply. Cross-reference cycle detection ran and did find
cycles, but they are benign bidirectional index/child navigation links rather than
content dependency loops — see the first WARNING for the full reasoning and for the
rule deviation that was made deliberately rather than silently.

### WARNINGS (5)

[WARNING] Cross-reference cycles in the ingest graph (4 cycles, all 2-node)
  Found: DFS with three-colour marking over the 44-node / 34-edge `cross_refs` graph
    reported 4 cycles, all of them a master index and one of its children referring to
    each other —
    (1) 2026-07-21-00-macos-metal-stack-INDEX.md <-> 2026-07-21-00-macos-metal-pending-backlog.md
    (2) 2026-07-23-00-gow2-playable-master-INDEX.md <-> 2026-07-23-m3plus-postwad-present.md
    (3) 2026-07-23-00-gow2-playable-master-INDEX.md <-> 2026-07-23-menu-type15-frame-loop.md
    (4) 2026-07-23-xenonrecomp-lessons-recomp-tooling.md <-> docs/RECOMP_CONFIG.md
    Traversal depth stayed far below the 50 cap (max out-degree 7, on
    2026-07-20-00-wall-chain-INDEX.md), so the graph-size abort was not triggered.
  Impact: The synthesizer contract nominally maps any cycle to a BLOCKER on the grounds
    that "synthesis loops produce garbage". These four are not content dependency loops:
    each is a parent INDEX listing a satellite plan while the satellite links back to its
    parent (and, for cycle 4, a plan documenting a config schema while the schema
    reference cites the plan that produced it). Synthesis used single-visit traversal —
    each source document was read exactly once, with no recursive expansion — so no loop
    occurred and no content was double-counted. Escalating these to BLOCKER would gate the
    whole ingest over ordinary navigation backlinks and would push the user to delete
    useful cross-references. Classified WARNING rather than BLOCKER for that reason; this
    is a deliberate, declared deviation from the letter of the rule, not an omission.
  → If you disagree with the downgrade, treat this entry as a blocker and re-run after
    removing the child→parent backlinks; otherwise approve and proceed. No intel content
    was dropped on account of the cycles.

[WARNING] Competing completion state for the macOS Metal stack M2–M10
  Found: docs/superpowers/plans/2026-07-21-00-macos-metal-stack-INDEX.md marks all eleven
    entries M0–M10 as "✅ **GREEN** 2026-07-21", and its companion
    docs/superpowers/plans/2026-07-21-00-macos-metal-pending-backlog.md marks M2, M3, M5,
    M6, M7, M8, M10 GREEN in the vtable and extras matrices.
  Found: docs/PLAN_READY_FOR_GAME_RECOMPILATION.md (2026-07-24) states as a global
    constraint "Metal é o backend **default** no Darwin (stack M0–M10 todo GREEN)" and in
    §6 "o stack M0–M10 está GREEN. Este plano consome-o".
  Found: docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md Track G states
    "M0/M1 already GREEN. Next: M2 MSL → M3 bind_texture → M5/M6 state → M7 RT present",
    i.e. M2–M10 pending.
  Found: docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md satellite table lists
    M2 through M10 as "⬜" with M0 and M1 as "✅ GREEN".
  Impact: All four sources are type PRD, so precedence cannot resolve this, and resolving
    by document date would be an arbitrary tiebreaker the contract forbids. The
    disagreement decides whether Track G (Metal content stack) is closed scope or roughly
    nine open work items — a material difference for any roadmap built on this intel.
    Both variants are preserved: REQ-track-g-metal-content-stack is tagged [CONTESTED],
    and the sub-items the backlog does *not* claim GREEN (`set_alpha_test` 🟡 log-only,
    `set_viewport` 🟡 partial, FP/VP validate tests with no `metallib` CI, `rsx_live_draw`
    capture-replay explicitly "adiado") are carried separately as
    REQ-metal-alpha-test-and-fp-vp-tests.
  → Confirm the true state of M2–M10 (the nine per-task outcome notes under
    docs/notes/2026-07-21-macos-metal-* were deliberately excluded from this ingest and
    would settle it), then update the two stale indexes before routing Track G.

[WARNING] Stale ADR backend inventory versus Metal-first reality
  Found: docs/ARCHITECTURE.md (type ADR, highest precedence) lists the graphics backends
    as "Null" and "D3D12", with "Vulkan: Planned", and names no Metal backend anywhere.
  Found: docs/PLATFORM_ABSTRACTION.md (type ADR) gives macOS audio as "Primary Backend
    SDL2 / Fallback CoreAudio (potential future)" and macOS input as SDL2 GameController.
  Found: docs/superpowers/plans/2026-07-21-00-macos-metal-stack-INDEX.md (type PRD) makes
    "Metal nativo como caminho default Mac (`PS3_RSX_BACKEND=metal`)" rule 1 for every M*
    plan and prefers "VideoToolbox (decode), Metal (GPU), AudioQueue/AudioUnit (áudio)".
  Found: the read-only evidence map at .planning/codebase/ records these as facts about
    the current code — STACK.md lists Objective-C enabled only `if(APPLE)` for
    `libs/video/rsx_metal_backend.m`, `libs/video/movie_vt_metal.m` and
    `libs/input/cellpad_gamecontroller.m`; ARCHITECTURE.md:83 lists
    `rsx_metal_backend.m` as the macOS `rsx_backend` implementation; INTEGRATIONS.md:41
    records CoreAudio/AudioToolbox linked on Apple with ring-buffer glue in
    `libs/audio/coreaudio_ring.h`, unit-tested by `libs/audio/tests/test_coreaudio_ring.c`;
    and Vulkan is optional (`find_package(Vulkan QUIET)`), MoltenVK-backed on macOS and
    described as a non-preferred fallback.
  Impact: Mechanically applying precedence here (ADR beats PRD) would let two stale
    reference documents delete the Metal and CoreAudio backends from the synthesized
    intent, even though the codebase map shows both exist and are wired. That is exactly
    the silent corruption the precedence layer is supposed to prevent, so it was not
    auto-resolved. Both ADR entries were emitted verbatim in decisions.md
    (ADR-ARCH-7, ADR-PLAT-4) each carrying an explicit `- note:` pointing here.
  → Decide whether to update docs/ARCHITECTURE.md and docs/PLATFORM_ABSTRACTION.md to
    record Metal/VideoToolbox/CoreAudio, or to supersede those ADR sections explicitly.
    Until then, do not let ADR precedence override the Metal-first policy.

[WARNING] Competing macOS RSX API recommendation: Vulkan-first versus Metal-first
  Found: docs/MACOS_PORT_PLAN.md Fase 5 weighs Metal against Vulkan+MoltenVK and
    concludes "**Recomendação:** **Vulkan first** se o roadmap já aponta Vulkan; **Metal**
    se o único alvo Apple e quiser menos camadas", with its §7 snapshot recording "RSX GPU
    ❌ só D3D12" and "Runtime static lib ❌ compile errors Darwin".
  Found: docs/superpowers/plans/2026-07-21-00-macos-metal-stack-INDEX.md mandates Metal as
    the default Mac path and explicitly forbids OpenGL and ffmpeg-on-PATH as "Apple
    solutions"; docs/PLAN_READY_FOR_GAME_RECOMPILATION.md restates "Apple-first no macOS:
    Metal é o backend **default** no Darwin … não um requisito opcional".
  Impact: Both are type PRD on the same scope (macOS GPU backend choice), so precedence
    cannot pick a winner and picking by date would be an arbitrary tiebreaker. Left
    unresolved rather than merged; REQ-macos-port-layers is tagged [SUPERSEDED] with the
    divergence named in its description, and REQ-track-g-metal-content-stack carries the
    Metal-first rules verbatim.
  → Retire or annotate docs/MACOS_PORT_PLAN.md Fase 5 (and its §7 snapshot, which the
    codebase map contradicts) so a single macOS GPU direction is stated once.

[WARNING] Competing acceptance definitions for build reproducibility
  Found: docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md global criterion 7 requires that
    "o resultado for reproduzível a partir do manifesto", and its Fase 1 acceptance says
    "O manifesto permite reproduzir exatamente o build".
  Found: docs/PLAN_READY_FOR_GAME_RECOMPILATION.md RDY-5 acceptance redefines it: "'O
    mesmo executável' significa **funcionalmente equivalente medido pelos gates**, não
    bit-idêntico (paths absolutos, mtimes e build ids tornam bit-identidade inatingível).
    A v1 não distinguia; esta versão distingue."
  Impact: Two PRDs define acceptance for the same scope (reproducible rebuild of a title)
    with non-identical criteria — exact reproduction from a manifest versus functional
    equivalence measured by named gates. Choosing silently would either set an
    unreachable bar or quietly weaken the reliability plan's stated criterion 7. Both
    variants are preserved: REQ-reliability-recompiled-quality-gate carries the
    manifest-exact wording and REQ-rdy5-gow2-reproducible-build carries the
    gate-equivalence wording, each with its own source.
  → Choose one definition (or split into two requirements: bit-level manifest
    reproducibility as a separate, later goal) before routing the build-pipeline work.

### INFO (16)

[INFO] No locked decisions anywhere in the ingest
  Note: All 44 classifications carry `locked: false`; none of the three ADRs
    (docs/ARCHITECTURE.md, docs/PLATFORM_ABSTRACTION.md, docs/RPCS3_AS_REFERENCE.md) has
    an explicit `Status: Accepted` field, and two classifier notes say so directly
    ("No explicit status field (Accepted/Proposed) found in scanned content, so locked
    left false"). Every entry in decisions.md is therefore `status: proposed`, and ADR
    content is treated as strong framing rather than an immovable decision.

[INFO] Supersession chain applied between the three master indexes
  Note: docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md defers in its own header
    to docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md ("Índice
    mestre"), which owns the GoW2 tracks M/S/G/U/T/P. On the engine side,
    docs/PLAN_READY_FOR_GAME_RECOMPILATION.md (2026-07-24) declares itself
    non-self-sufficient by design, owning only what no existing plan owns and delegating
    the rest by explicit reference — including "Wall activo do GoW2 (Gate A, TYPE15 f4,
    present re-arm) → dono `2026-07-23-00-gow2-playable-master-INDEX.md:44`, **fora de
    escopo**". The two ownership boundaries are compatible, so this was recorded rather
    than treated as a conflict: the wall-chain INDEX is tagged [SUPERSEDED] and the two
    surviving masters keep disjoint scope.

[INFO] docs/MACOS_PORT_PLAN.md superseded by later measured state
  Note: Its §7 snapshot (2026-07-19) records "Runtime static lib ❌ compile errors Darwin",
    "RSX window ❌ stub", "RSX GPU ❌ só D3D12" and "GoW2 binary ❌ Windows-only pipeline".
    The codebase map records macOS/arm64 as the only platform actually exercised and
    CI-blocking, with a native Metal backend present. Layers A–E of the plan are treated
    as delivered by later work; only layer F survives as open scope.

[INFO] Auto-resolved: PRD beats DOC on whether a playable GoW2 is the goal
  Note: docs/gow2-recomp-notes.md (DOC) concludes "Honest verdict: the path to a
    *playable* GoW2 is RPCS3. ps3recomp here is research", while
    docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md (PRD) states the
    goal as "God of War II HD (NPUA80491) 100% usable on static recomp: boot → intro →
    WAD → main menu → first playable interaction". PRD outranks DOC, so the master
    INDEX's goal wins and is carried as REQ-gow2-playable. Reinforcing evidence: the same
    DOC is named in docs/PLAN_READY_FOR_GAME_RECOMPILATION.md §7 under "Obsoletas — não
    usar como diagnóstico".

[INFO] Auto-resolved: title identifier NPUA80491, not BCUS-98164
  Note: docs/gow2-recomp-notes.md (DOC) identifies the title as "BCUS-98164 / EUR equiv".
    docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md (PRD) says
    "God of War II HD (NPUA80491)", and docs/bringup/gow2/FASE0/attempt-1.md titles the
    measured baseline "gow2, NPUA80491". PRD beats DOC, so NPUA80491 is used throughout
    the synthesized intel.

[INFO] Auto-resolved: minimum Python version is 3.11, not 3.9
  Note: docs/GETTING_STARTED.md, docs/TOOLS.md:611 and docs/GAME_PORTING_GUIDE.md:35 (all
    DOC) state "Python 3.9+". docs/PLAN_READY_FOR_GAME_RECOMPILATION.md (PRD) records
    `find_package(Python3 3.11 REQUIRED)` at `CMakeLists.txt:42` and makes correcting the
    two doc sites item 7 of RDY-1; the codebase map records `requires-python = ">=3.11"`
    in `pyproject.toml`. PRD beats DOC; the drift is already owned by
    REQ-rdy1-phase0-residue.

[INFO] Auto-resolved: the generic SPURS HLE kernel already exists
  Note: docs/MODULE_STATUS.md (DOC) lists as "Next Priority 2" — "cellSpurs — Actual SPU
    program execution on host threads". docs/SPURS_BRINGUP.md (PRD) records
    `libs/spurs/spurs_kernel.c` as implemented and tested (2026-06-25) with a 50-workload
    unit test and an e2e test running a real lifted SPU job. PRD beats DOC; the remaining
    SPURS work is title-specific (Marco 5), carried as REQ-spurs-m5-gow2-protocol.

[INFO] Auto-resolved: FFmpeg is not the media direction on macOS
  Note: docs/MODULE_STATUS.md (DOC) proposes "Integrate FFmpeg for actual video/audio
    decoding and demuxing" as a next priority for cellVdec/cellAdec/cellDmux. The Metal
    stack INDEX (PRD) explicitly forbids "depender de `ffmpeg` no PATH" as an Apple
    solution and assigns decode to VideoToolbox. PRD beats DOC. The Windows path is
    unaffected by this resolution.

[INFO] Superseded measurement: the R_PermA stream stall
  Note: docs/gow2-asset-pipeline-map.md traces a linear stream stopping at 5,619,712 of
    20,169,344 bytes (27.9%) with the root cause a guest free-list allocator
    (`func_00263178`) dereferencing a `0x80000000`-tagged word as a raw pointer. The
    2026-07-23 master INDEX records `R_Perm` reaching the full 20,169,344 bytes as part of
    Gate B being green. The document's structural findings (that `R_PermA.wad_ps3` is an
    uncompressed WAD, so `push2=0`/`HOSTINFL=0` is correct behaviour and the
    inflate-deadlock thesis is a red herring) remain valid and are kept in context.md;
    only the stall figure is superseded.

[INFO] Self-declared obsolete: docs/GOW2_BOOT_STATE.md
  Note: The document's own header (2026-06-25) states "`func_0030600C` NÃO é mais o wall",
    with `sys_lwmutex_lock` measured at 102 rather than ~96k, and attributes the
    resolution to later PPU lifter fidelity fixes (probably `rlwinm` zero-extension). It
    is retained "para histórico". Recorded so a downstream consumer does not read its body
    as a current diagnosis.

[INFO] docs/PPU_RECOMP.md documents a different test title
  Note: It states "the work here is game-agnostic — Uncharted: Drake's Fortune (BCUS98103)
    is only the test target (we have its decrypted `EBOOT.elf` + an RSX shader corpus)".
    This ingest is scoped to the GoW2 port, and the ingest manifest deliberately excluded
    three documents about unrelated PS3 titles. The PPU_RECOMP status table is still used
    as context for the PPU pipeline; only the title reference is out of scope.

[INFO] Stale test count in docs/SPU_LIFTER.md
  Note: Its integration-test section says "Current state (4 tests)" (`sum`, `shufb`,
    `brsl_return`, `dma`). The measured suite is 21 cases — quoted verbatim as
    `Results: 21 passed, 0 failed` in docs/bringup/gow2/FASE0/attempt-1.md, required at
    `21 passed, 0 failed` by the Fase 1 SPEC's acceptance criteria, and recorded as 21
    cases in the codebase map.

[INFO] docs/RECOMP_CONFIG.md is a declared stub with unconsumed schema surface
  Note: It documents `[[midasm_hook]]` and `[[functions_override]]` while stating those
    fields "ainda não são consumidos" by the lifter — that is Tasks A2/A4 of the
    XenonRecomp tooling plan. Recorded so the documented schema is not mistaken for
    implemented behaviour; the gap is carried as REQ-xenon-midasm-hooks and
    REQ-xenon-weak-function-overrides.

[INFO] Module count drift across documents
  Note: docs/ARCHITECTURE.md's comparison table says "95 complete modules, full runtime";
    docs/MODULE_STATUS.md's summary says Complete 93, Partial 3, Not Started ~3, total
    ~99. Small numeric drift, no scope impact; recorded for transparency rather than
    reconciled.

[INFO] Known-red tests currently outside the CTest gate
  Note: docs/PLAN_READY_FOR_GAME_RECOMPILATION.md records `test_rsx_fifo` running with
    "methods processed: 0, 4 FAIL, exit 1", `test_fp_decompiler` failing 2 cases whose
    ledger classification as "BUGS REAIS DE PRODUTO" it says is wrong (dated evidence
    `f2363ce`), `test_msl_decompiler` returning FAIL rather than SKIP when no Metal device
    is present, and `scripts/smoke_macos.sh` step 3 failing to link. It also records that
    the CI has never actually run (`total_count: 2`, both Dependabot) and that Linux and
    Windows jobs are `continue-on-error`. These are owned by REQ-rdy1-phase0-residue and
    REQ-rdy2-fifo-core-be; noted here so a green-looking test suite is not assumed.

[INFO] Cross-references pointing outside the ingest set
  Note: 41 distinct `.md` targets referenced by the ingested documents are not part of
    this ingest — the ~14 already-executed plans from earlier rounds, the 11 individual
    Metal M0–M10 task plans, the per-round `gow2-recomp/notes/*.md` evidence files
    (`gate-a-verdict`, `r1`…`r12`, `type15-list-sanitize-fix`, `postthr-cc9d0-type15-wall`,
    `combo-discriminate`, `wad-trigger-g4`), plus `CLAUDE.md`, `FEASIBILITY.md`,
    `.superpowers/sdd/progress.md` and the glob `2026-07-24-*-rdy<N>-*.md`. All of these
    are covered by the deliberate exclusions recorded in .planning/ingest-manifest.yaml or
    live in the sibling game repository. Their absence is expected and is not a coverage
    gap; where a claim depended on one of them, the claim was attributed to the ingested
    document that makes it rather than followed through the link.
