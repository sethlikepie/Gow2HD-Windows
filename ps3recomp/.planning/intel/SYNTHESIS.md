# Synthesis — ps3recomp doc ingest

Entry point for downstream consumers (`gsd-roadmapper`). Mode: `new`.
Precedence applied: ADR > SPEC > PRD > DOC. No per-doc precedence override was present.

## Documents consumed

44 of 44 classifications in `.planning/intel/classifications/`, all
`confidence: high`, all `locked: false`, zero `UNKNOWN`.

| Type | Count | Files |
|---|---:|---|
| ADR | 3 | ARCHITECTURE.md, PLATFORM_ABSTRACTION.md, RPCS3_AS_REFERENCE.md |
| SPEC | 1 | superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md |
| PRD | 16 | 3 master indexes + Metal backlog + 6 dated execution plans + 5 engine-scope plans + SPURS_BRINGUP.md |
| DOC | 24 | subsystem references, onboarding guides, and measured GoW2 evidence |

## Decisions

**Locked: 0.** No ADR carries an `Accepted` status field, so all 19 extracted decisions
are `status: proposed`. Sources:

- `docs/ARCHITECTURE.md` → 9 decisions (ADR-ARCH-1..9): static AOT recompilation, flat
  `vm::g_base + guest_addr` translation, endianness only at the memory boundary, the
  fixed guest memory map, NID resolution with `CELL_ENOSYS` fallback, the three-layer RSX
  architecture, the backend inventory, the SPU strategy ordering, RPCS3 as a read-only
  oracle.
- `docs/PLATFORM_ABSTRACTION.md` → 5 decisions (ADR-PLAT-1..5): inline `#ifdef` switches
  over an abstraction layer, per-subsystem Win↔POSIX mappings, `CRITICAL_SECTION` over
  `CreateMutex`, audio/input backend selection, fibers and the macOS `ucontext` caveat.
- `docs/RPCS3_AS_REFERENCE.md` → 5 decisions (ADR-RPCS3-1..5): oracle-not-dependency, the
  GPLv2 boundary, the M2 measure-before-HLE lesson, RE session acceptance, and the
  explicit anti-pattern list.

Two entries (ADR-ARCH-7 backend inventory, ADR-PLAT-4 audio backends) carry a `- note:`
flagging that they are contradicted by both PRD-level intent and the codebase map.

## Requirements

**63 requirements extracted, state-tagged** — the central output, because most PRDs in
this set are already-executed plans rather than pending scope.

| State | Count | Meaning |
|---|---:|---|
| OPEN | 46 | actionable pending scope |
| PARTIAL | 4 | partly landed; the residual is named in the description |
| DONE | 10 | historical record — do not re-emit as work |
| SUPERSEDED | 2 | a later plan owns the scope |
| CONTESTED | 1 | sources disagree on completion (Metal M0–M10) |

**The active wall**, per the newest GoW2 master index: `REQ-type15-f4-writers` and
`REQ-present-reentry-after-rperm`. Gate B is GREEN; Gate A is RED
(`SetFlip_after_R_Perm = 0`, `Pad_after_R_Perm = 0`); menu UI NO; playable NO. Twelve
rounds of diagnosis (R1–R12) are closed with named refutations, so the remaining work is
a fix, not a re-diagnosis.

**Open scope by cluster:**

- GoW2 gameplay path — REQ-gow2-playable, REQ-gate-a-post-thr-life,
  REQ-type15-f4-writers, REQ-present-reentry-after-rperm, REQ-postb71-setflip,
  REQ-pad-menu-fsm-arm, REQ-track-s-shader-registry-natural,
  REQ-typemap-nested-walk-diagnosis, REQ-track-u-spu-jobs-deep,
  REQ-spurs-m5-gow2-protocol, REQ-track-p-playable-polish
- Engine bring-up checkpoints C1–C10 — REQ-bringup-c1..c10 (C1 PARTIAL, rest OPEN)
- Reliability phases 1–12 — REQ-reliability-phase1..12 plus the global quality gate
  (phase 3 PARTIAL)
- Engine readiness tracks — REQ-rdy0-unblock-relift (hard prerequisite: the build never
  re-runs the lifter and a clean re-lift does not reproduce today's binary),
  REQ-rdy1-phase0-residue, REQ-rdy2-fifo-core-be, REQ-rdy3-spu-strict-decode,
  REQ-rdy4-ppu-e2e-fixture, REQ-rdy5-gow2-reproducible-build
- Recomp tooling (XenonRecomp lessons) — REQ-xenon-midasm-hooks,
  REQ-xenon-weak-function-overrides, REQ-xenon-jumptable-analysis (PARTIAL),
  REQ-xenon-indirect-dispatch-o1, REQ-xenon-volatile-guest-access,
  REQ-xenon-abi-local-opts, REQ-xenon-simde-and-opcode-suite,
  REQ-xenon-shader-recomp-design, REQ-xenon-patch-migration
- macOS Metal residue — REQ-metal-alpha-test-and-fp-vp-tests

**Already done (do not rebuild):** REQ-gate-b-thr-path,
REQ-type15-product-list-membership, REQ-gate-a-diagnosis-closed,
REQ-common-instrumentation, REQ-bringup-fase0-baseline,
REQ-mask-observability-and-region-map, REQ-recomp-config-toml,
REQ-xenon-invalid-instruction-skip, REQ-spurs-hle-kernel-engine,
REQ-reliability-phase0-test-infra.

**Superseded:** REQ-macos-port-layers and REQ-wall-chain-order, both retained for
provenance. **Contested:** REQ-track-g-metal-content-stack (see WARNING 2).

## Constraints

**21 constraints**, all from the single SPEC
(`docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md`), by type:

| Type | Count |
|---|---:|
| nfr | 11 |
| api-contract | 4 |
| protocol | 3 |
| schema | 3 |

Highest-leverage for downstream planning: G4 (macOS/arm64 primary, Windows must not
regress, with a named list of tokens forbidden outside `#ifdef _WIN32`), G5 (every
acceptance statement declares in-boot vs offline-unit vs not-exercised), G2 (probes
default OFF; bug fixes default ON, and the deliberate carve-out is declared), and the
three declared gaps that bound what any memory measurement can claim — ~2110 direct
`vm_base` writes bypassing the hooked funnel (making `SWALLOWED`/`OOB` counts a lower
bound), two uninstrumented `type15_*` masks in the game repo, and the re-lift mine that
blocks any re-lift without erasing Fase 0 instrumentation.

## Context

**24 topic-keyed entries** in `context.md`, spanning the PPU pipeline, Python tooling,
the TOML config schema, the SPU lifter and fallback registry, the NID system, LV2
syscalls, the core runtime, HLE module architecture and status, custom modules, RSX
graphics and the NV40 fragment decompiler, building/getting started/porting/FAQ, and six
GoW2 evidence documents (boot state, asset pipeline map, research log, repository
baseline audit, and the Fase 0/Fase 1 bring-up checkpoints with the E1/E2 mask
measurements).

**Twelve `- caveat:` lines across ten entries** mark documents as stale or self-declared
obsolete: PPU_RECOMP (different test title), TOOLS / GETTING_STARTED / GAME_PORTING_GUIDE
(Python 3.9), RECOMP_CONFIG (declared stub with unconsumed schema), SPU_LIFTER (4 tests
vs 21), SYSCALLS (event-flag ranges), RSX_GRAPHICS (line flagged for RDY-2 update),
MODULE_STATUS (two stale next-priorities), GOW2_BOOT_STATE (self-declared obsolete body),
gow2-asset-pipeline-map (superseded stall figure) and gow2-recomp-notes (explicitly
declared obsolete by a PRD, plus two internal contradictions). Do not use a caveated
entry as a current diagnosis.

## Conflicts

| Bucket | Count |
|---|---:|
| BLOCKERS | 0 |
| WARNINGS (competing variants) | 5 |
| INFO (auto-resolved) | 16 |

**Blockers: none.** No `UNKNOWN`/low-confidence document, and no locked decision exists
anywhere in the set, so the LOCKED-vs-LOCKED gate is vacuous — none was manufactured.

**Warnings requiring a human decision before routing:**
1. Four cross-reference cycles (all 2-node index↔child backlinks) — detected, declared,
   and deliberately downgraded from BLOCKER with the reasoning written out.
2. Metal stack M2–M10: GREEN in two PRDs, pending in two other PRDs.
3. Stale ADR backend inventory (no Metal, no CoreAudio) versus Metal-first PRD policy and
   the codebase map — not auto-resolved despite ADR precedence, because doing so would
   delete real subsystems from the intent.
4. macOS RSX API: Vulkan-first (MACOS_PORT_PLAN) versus Metal-first (Metal INDEX,
   PLAN_READY).
5. Build reproducibility acceptance: exact-from-manifest versus functionally-equivalent-
   by-gates. Both variants preserved under separate requirement IDs.

Full detail, with `Found:`/`Impact:`/`→` per entry:
`/Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp/.planning/INGEST-CONFLICTS.md`

## Cycle detection

Ran DFS with three-colour marking over a 44-node, 34-edge graph built from `cross_refs`,
resolving edges by unique basename within the ingest set. Maximum out-degree 7
(`2026-07-20-00-wall-chain-INDEX.md`); traversal depth stayed far below the 50 cap, so no
graph-size abort. Four cycles found, all two-node — see WARNING 1. Synthesis used
single-visit traversal (each source read exactly once, no recursive expansion), so no
synthesis loop occurred and no content was double-counted.

## Grounding against the codebase map

`.planning/codebase/` (STACK, ARCHITECTURE, STRUCTURE, CONVENTIONS, TESTING,
INTEGRATIONS, CONCERNS) was read read-only as verified fact about the current code. It
was used to keep doc aspiration separate from code reality, never merged into this
intel. Where a document's claim conflicted with the map, the conflict was recorded (see
WARNING 3 and the INFO entries on Python version, SPU test count and the macOS port
snapshot) rather than either side being silently preferred.

## Files produced

- `.planning/intel/decisions.md` — 19 decisions from 3 ADRs
- `.planning/intel/requirements.md` — 63 state-tagged requirements from 16 PRDs
- `.planning/intel/constraints.md` — 21 constraints from 1 SPEC
- `.planning/intel/context.md` — 24 topic entries from 24 DOCs
- `.planning/INGEST-CONFLICTS.md` — 0 blockers / 5 warnings / 16 info
