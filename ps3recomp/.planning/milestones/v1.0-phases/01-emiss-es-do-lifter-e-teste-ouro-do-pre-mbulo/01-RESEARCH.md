# Phase 1: Emissões do lifter e teste-ouro do preâmbulo - Research

**Researched:** 2026-07-26
**Domain:** Python code-generation (a self-hosted C/C++ lifter emitting text), golden/mutation
testing without a test framework, upstream-fork merge-surface analysis
**Confidence:** HIGH — every claim below was verified by reading the actual files in this
checkout, running the production decoder/lifter against synthetic instruction words, or diffing
against `upstream/master` (fetched live in this session). No web research was needed; this phase
has zero external dependencies and zero new packages.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** Instrumentar **inline** os quatro helpers `ppu_res_lwarx/stwcx/ldarx/stdcx` no
  `SOURCE_PREAMBLE` do lifter (`tools/ppu_lifter.py:307-341`), em vez de declarar a perda de
  instrumentação por escrito ou fundir com o par de `ppu_memory.h`. — Reversibility: reversible.
- **D-02:** A instrumentação chama **`ps3_mem_oob_report`**, não `vm_oob`. `vm_oob` é `static int`
  em `runtime/ppu/ppu_loader.cpp:399` — file-local, inacessível de outra unidade de tradução.
  `ps3_mem_oob_report` é declarado no header público `include/ps3_mem_trace.h` e definido em
  `runtime/trace/ps3_mem_trace.c`, já linkado. O preâmbulo faz o seu próprio bounds check inline.
- **D-03:** Seguir a assinatura já estabelecida pelos atômicos do runtime: `len` estático — 4 para
  `lwarx`/`stwcx`, 8 para `ldarx`/`stdcx`; `op="read"` para `lwarx`/`ldarx`, `op="write"` para
  `stwcx`/`stdcx`.
- **D-04:** Comportamento em OOB é **report-only, nunca abortar** — já decidido pelo contrato
  existente de `ps3_mem_oob_report` (report-only, gated, com cap).
- **D-05:** O teste-ouro ancora em **`games/gow2/build_macos.sh`** (presente em CI e em clone
  limpo), mais um gate de sincronização que compara com `../gow2-recomp/build_macos.sh` e emite
  `SKIP:` quando o irmão não existe. — Reversibility: reversible.
- **D-06:** Enquanto a divergência conhecida existir, o gate de sync **avisa (WARN) e não falha**;
  vira FAIL numa fase posterior (Fase 2).
- **D-07:** Mutação **automatizada em todo run**. O próprio teste remove, em memória, cada uma das
  emissões do preâmbulo, reroda o assert e exige que ele falhe. Descartadas: fixtures fixadas e
  alvo `--self-check` sob demanda. Custo aceito: passagens extras de string, não compilação.
- **D-08:** Ambos (teste-ouro e gate G4) entram em **`runtime/ppu/tests/test_ppu_lifter_emit.py`**,
  estendendo o arquivo existente em vez de criar irmãos.
- **D-09:** O gate G4 varre o **`SOURCE_PREAMBLE` e as strings de emissão de `tools/ppu_lifter.py`**
  — tudo que o lifter *pode* emitir. Roda em CI sem exigir EBOOT nem lift. Limitação declarada: não
  pega token injetado por um `patch_*.py`.
- **D-10:** O teste-ouro fixa **quatro** emissões, não três. A quarta é o contrato
  `ppu_timebase_now`: a declaração no preâmbulo (`tools/ppu_lifter.py:230,232`) + emissão de
  `mftb`/`mftbu` (`:2127`) têm de casar com um definidor versionado no runtime
  (`runtime/syscalls/sys_timer.c`). **Não renomear no lifter** — `ppu_*` é o contrato do lifter
  upstream (`sp00nznet`); renomear criaria conflito a cada merge. A cola é o wrapper de uma linha
  no runtime. — Reversibility: reversible.

### Claude's Discretion

- Forma exata do bounds check inline no preâmbulo (comparação contra o tamanho do espaço de
  endereçamento guest) — desde que não dependa de `vm_oob`.
- Estrutura interna da rotina de mutação dentro do teste (helper, decorator ou loop).
- Nomes de funções e organização das novas seções dentro de `test_ppu_lifter_emit.py`.

### Deferred Ideas (OUT OF SCOPE)

- Promover o gate de sync do `build_macos.sh` para FAIL — Fase 2 (domínio: `host_gow2_factory`).
- Varredura G4 sobre a saída de um lift real, cobrindo tokens injetados por `patch_*.py` — fora da
  Fase 1 (exige EBOOT e lift); candidata a Fase 3 ou 4.
- Fechar as três cifras do inventário de host órfão (1603 / 1662 / 1793) e cobrir `preamble_pure.cpp`
  no `verify_lift_baseline.sh` — Fase 2.
- `git subtree pull` do gow2-recomp para eliminar a divergência — manutenção, fora do domínio da
  Fase 1.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| REQ-RDY0-1 | Emissões do lifter e teste-ouro do preâmbulo: `#include <stdlib.h>`, ramo `__APPLE__` do TLS de `g_trampoline_fn`, geração de `ppu_stubs.cpp`, e o contrato `ppu_timebase_now` — todos os quatro fixados por um teste-ouro que falha por mutação; instrumentação OOB dos quatro atômicos `ppu_res_*`; gate G4 de tokens proibidos | Emissions 1 and 2 confirmed already present (no code change, only test coverage needed); Emission 3 (`ppu_stubs.cpp`) has a complete, field-validated prior implementation recoverable from commit `e52da4a` (see Architecture Patterns); Emission 4 (`ppu_timebase_now`) golden pair fully derived and verified this session (exact instruction words `0x7C6C42E6`/`0x7C6D42E6`, exact expected output strings — see Code Examples); OOB instrumentation contract fully specified via the `ppu_lwarx`/`ppu_stwcx`/`ppu_ldarx`/`ppu_stdcx` precedent at `ppu_loader.cpp:1691-1752` and the `ps3_mem_oob_report` header contract; G4 gate scope and the `_MSC_VER`-vs-`_WIN32` pitfall documented in Common Pitfalls |
</phase_requirements>

## Summary

This phase touches exactly one generator (`tools/ppu_lifter.py`) and one golden test
(`runtime/ppu/tests/test_ppu_lifter_emit.py`), both fully readable in this checkout, plus one new
sync-gate script the phase must create. Nothing here requires library research: `tools/` is
stdlib-only Python 3.11+, and the runtime headers involved (`ppu_context.h`, `ppu_memory.h`,
`ppu_ops.h`, `ps3_mem_trace.h`) already exist and are already linked. The research effort instead
went into three things the CONTEXT.md explicitly flagged as unknown, and one thing it didn't:

1. **Upstream merge risk is low and precisely characterized.** `SOURCE_PREAMBLE` is one big
   triple-quoted Python string, in both our fork and `upstream/master` (fetched and diffed live).
   The diff between the two is exactly two hunks: the `#include <stdlib.h>` line, and the
   `g_trampoline_fn` TLS declaration (upstream has one line, `__declspec(thread)` only; ours has a
   3-way `#if _MSC_VER / #elif __APPLE__ / #else` block). A future `git merge upstream/master` on
   this file will only conflict if upstream itself edits these exact lines — a plain line-based
   text merge, not a structural one. The `ppu_res_*` four atomics are byte-identical between
   upstream and our fork today (no OOB instrumentation in either), so this phase's planned
   instrumentation is genuinely new, additive text with no existing upstream competitor on those
   lines.

2. **`ppu_stubs.cpp` is not new work — it is a restoration with the original implementation still
   in git history.** Commit `e52da4a` (2026-06-22, this fork, pre-upstream-merge) added
   `PPULifter.undefined_targets()` and `PPULifter.emit_fallback_stubs()` plus `--fallback-stubs`/
   `--max-mid-passes` CLI flags and `main()` wiring that writes `ppu_stubs.cpp`. Commit `11a1c3c`
   (2026-07-21) replaced the entire `ppu_lifter.py` file with upstream's version (which never had
   this feature — confirmed absent in `upstream/master` today) and silently dropped it. The
   runtime-side counterpart, `ppu_unlifted_stub(uint64_t addr, ppu_context* ctx)`, **still exists**
   at `runtime/ppu/ppu_loader.cpp:2519` — untouched, because it lived in a different file than the
   one that got replaced. This means the fourth emission (`ppu_stubs.cpp`) can be restored by
   porting the ~65-line diff from `e52da4a` forward onto the current file structure, not designed
   from scratch.

3. **The `ppu_timebase_now` golden pair (D-10's fourth emission) is fully derivable and was
   verified end-to-end in this session**, including exact PowerPC instruction word encodings for
   `mftb r3` (`0x7C6C42E6`) and `mftbu r3` (`0x7C6D42E6`), decoded by the production decoder and
   translated by the production `_translate_op` to the exact strings
   `ctx->gpr[3] = ppu_timebase_now();` and `ctx->gpr[3] = (ppu_timebase_now() >> 32);`.

4. **The G4 forbidden-token gate has a subtlety CONTEXT.md's wording doesn't capture**: none of the
   three forbidden tokens present in `ppu_lifter.py` today (`<intrin.h>`, `__declspec`) are guarded
   by `#ifdef _WIN32` — they're guarded by `#ifdef _MSC_VER`, a related but distinct macro. A gate
   that literally scans for `_WIN32` as the only acceptable guard will misfire against currently-
   correct code. `GetModuleHandleA` and `Sleep(` do not appear anywhere in `ppu_lifter.py` today
   (both clean).

**Primary recommendation:** Treat all four emissions as small, targeted, independently-testable
diffs against known line ranges; port the `ppu_stubs.cpp` logic forward from commit `e52da4a`
rather than redesigning it; write the mutation harness as pure-Python string surgery on the
already-imported `SOURCE_PREAMBLE` constant (no extra compilation needed for 3 of 4 emissions);
and treat `_MSC_VER` as an equally valid G4 guard alongside `_WIN32`.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Lifter text emission (preamble, stubs, timebase decl) | Build tool (`tools/ppu_lifter.py`, Python) | — | Pure code generator; runs offline, no runtime component |
| OOB instrumentation call-through | Generated C++ (lift output) → Runtime (`runtime/trace/ps3_mem_trace.c`) | — | Preamble emits the call; the reporter/gate/cap logic already lives in the runtime static lib and is out of scope to modify |
| Golden/mutation test | Test tooling (`runtime/ppu/tests/test_ppu_lifter_emit.py`, Python + compiled C++ harness) | CTest / `scripts/test_python.sh` (CI registration) | Offline, no ELF/EBOOT/boot required |
| Build-consumption gate (D-05 sync) | Test tooling (new Python script under the existing test) | `games/gow2/build_macos.sh` (read-only anchor, not modified) | Compares two files on disk; no runtime component |
| G4 forbidden-token gate | Test tooling (static text scan of `tools/ppu_lifter.py`) | — | Purely lexical; runs without EBOOT, lift, or compiler |

This phase has no browser/frontend/API/database tiers — it is entirely build-tooling and test
infrastructure. No misassignment risk of the kind this map usually catches.

## Standard Stack

No third-party packages apply. This phase is 100% stdlib Python 3.11+ (already the project floor
per `pyproject.toml`) plus the C++20 compiler already required by the rest of the test suite.

| Tool | Verified version present | Purpose | Note |
|------|---------------------------|---------|------|
| Python | `.venv/bin/python3.14` (symlink, Homebrew) | Runs `ppu_lifter.py` and the test | System `python3` is 3.9.6 and does **not** satisfy the `>=3.11` floor — `scripts/test_python.sh`'s discovery order (`$PYTHON` → `.venv/bin/python3` → versioned `python3.1x` → bare `python3`) already accounts for this; nothing to fix |
| clang++ | Apple clang 21.0.0 (Xcode CLT) | Compiles the golden test's synthetic harness | `-std=c++17 -O0 -ffp-contract=off`, matches `test_ppu_lifter_emit.py:561` |
| CMake | 4.4.0 | CTest registration | Project requires `>=3.20`; satisfied |
| git | 2.50.1 | `_lifter_rev()` provenance stamp, sync-gate diffing | Already a hard dependency of the lifter itself |

**Installation:** none — nothing to install for this phase.

## Package Legitimacy Audit

**Not applicable.** This phase installs zero external packages. `tools/` is verified (per
`.planning/codebase/CONVENTIONS.md`) to import zero third-party packages anywhere; the
`requirements.txt`/`pyproject.toml` optional-deps list is dead/unused backward-compat cruft. The
Package Legitimacy Gate protocol is skipped entirely — there is nothing to check.

## Architecture Patterns

### System Architecture Diagram

```
tools/ppu_lifter.py (Python, offline)
  │
  ├─ SOURCE_PREAMBLE (one string constant)         ── the 4 pinned emissions live here (3 of 4)
  │    ├─ #include <stdlib.h>                       [EXISTS — verified :146]
  │    ├─ extern uint64_t ppu_timebase_now(void);    [EXISTS — verified :230/:232]
  │    ├─ #if _MSC_VER/#elif __APPLE__/#else TLS     [EXISTS — verified :377-383]
  │    └─ ppu_res_lwarx/stwcx/ldarx/stdcx (inline)   [OOB instrumentation MISSING — this phase adds it]
  │
  ├─ PPULifter._translate_op()                      ── mftb/mftbu → "ppu_timebase_now()" calls
  │                                                      [EXISTS — verified this session]
  │
  ├─ PPULifter.undefined_targets() /
  │  PPULifter.emit_fallback_stubs()                ── writes ppu_stubs.cpp
  │                                                      [MISSING — deleted by 11a1c3c, this
  │                                                       phase restores from e52da4a]
  │
  └─ main() → write_source_files() → ppu_recomp_NNN.cpp (+ ppu_stubs.cpp if undefined targets exist)
       │
       ▼
games/gow2/build_macos.sh   (consumer — unmodified by this phase)
  ├─ compiles ppu_recomp_*.cpp + ppu_stubs.cpp (already tolerant of stub's absence: `[ -f "$f" ] || continue`)
  └─ links against runtime/ppu/ppu_loader.o, which defines:
       ├─ ppu_unlifted_stub(uint64_t, ppu_context*)   [EXISTS — runtime/ppu/ppu_loader.cpp:2519]
       └─ ps3_mem_oob_report (via runtime/trace/ps3_mem_trace.c — public, already linked)

runtime/ppu/tests/test_ppu_lifter_emit.py (golden + mutation, this phase extends it)
  │
  ├─ imports SOURCE_PREAMBLE, PPULifter, LiftedFunction directly (module import, no subprocess)
  ├─ CASES dict: (instruction word, decode addr) → exact golden C string
  │    └─ mftb/mftbu entries: NEW, words verified this session (0x7C6C42E6 / 0x7C6D42E6)
  ├─ check_goldens(): decode → _translate_op → exact string compare
  ├─ build_and_run(): drops golden strings into a synthetic C++ harness, compiles + runs it
  │    (mocks ps3_indirect_call, vm_base, g_trampoline_fn — does NOT link the real runtime .a)
  └─ [NEW this phase] mutation helpers: string-surgery on SOURCE_PREAMBLE / delattr on PPULifter
       methods, re-run presence/behavior assertion, require it to fail

New sync-gate script (D-05, this phase, location is Claude's discretion within
test_ppu_lifter_emit.py per D-08)
  ├─ reads games/gow2/build_macos.sh (anchor, always present)
  ├─ reads ../gow2-recomp/build_macos.sh IF PRESENT, else prints "SKIP: ..." + exit 0
  └─ WARN (not FAIL) on diff, per D-06 — this phase does not promote to FAIL
```

### Recommended Approach per Emission

**Emission 1 — `#include <stdlib.h>` (already present, `:146`).** No code change. Golden test
adds a presence assertion (`"#include <stdlib.h>" in SOURCE_PREAMBLE`) plus a mutation case.

**Emission 2 — `__APPLE__` TLS branch (already present, `:377-383`).** No code change. Golden
test adds a presence assertion for the `#elif defined(__APPLE__)` line and the
`thread_local void (*g_trampoline_fn)(void*);` declaration, plus a mutation case.

**Emission 3 — `ppu_stubs.cpp` generation (genuinely absent).** Port forward from `e52da4a`:
```python
# Source: git show e52da4a -- tools/ppu_lifter.py (this fork's own prior implementation,
# verified present at HEAD~N and absent at HEAD via `git log --all -S"ppu_stubs"`)
def undefined_targets(self) -> list[int]:
    defined = {f.start_addr for f in self.functions}
    return sorted((self.call_targets | self.branch_targets) - defined)

def emit_fallback_stubs(self, header_name: str = "ppu_recomp.h") -> str | None:
    undefined = self.undefined_targets()
    if not undefined:
        return None
    lines = [
        "/* Auto-generated fallback stubs ... */",
        f'#include "{header_name}"', "",
        "#if defined(_WIN32) || defined(_MSC_VER)",
        "#define PPU_WEAK",
        "#else",
        "#define PPU_WEAK __attribute__((weak))",
        "#endif", "",
        "#ifdef __cplusplus", 'extern "C"', "#endif",
        "void ppu_unlifted_stub(uint64_t addr, ppu_context* ctx);", "",
    ]
    for target in undefined:
        name = f"{self.prefix}func_{target:08X}"
        lines += [f"void PPU_WEAK {name}(ppu_context* ctx) {{",
                   f"    ppu_unlifted_stub(0x{target:08X}ULL, ctx);", "}", ""]
    return "\n".join(lines)
```
Reinsertion points on the CURRENT file (line numbers verified this session, will drift as the
plan's own edits land — re-verify at implementation time):
- `self.call_targets`/`self.branch_targets` — already tracked (`:514-515`, populated throughout
  `lift_function`); no new state needed.
- New methods go in the same class as `emit_header()` (`:3049`) — natural home, since
  `emit_header()` already does the near-identical `(self.call_targets | self.branch_targets) -
  defined` computation at `:3064` for forward declarations.
- CLI wiring goes in `main()` after the existing "remove stale output" block (`:4284-4292`, which
  should also gain `ppu_stubs.cpp`/`ppu_stubs.c` to its stale-cleanup glob — `e52da4a` did this too)
  and after the chunk-writing block (`:4304-4310`), mirroring the original's placement exactly.
- The runtime counterpart `ppu_unlifted_stub(uint64_t addr, ppu_context* ctx)` needs **no
  changes** — confirmed present and untouched at `runtime/ppu/ppu_loader.cpp:2519`.
- **Scope discipline:** `e52da4a` also restored `--max-mid-passes` (currently hardcoded to `25` at
  `:4268`). That flag is **not** one of the four locked emissions in CONTEXT.md/D-10 and is not
  required by any of the four success criteria — restoring it is optional scope creep, not part of
  this phase's aceite. Recommend leaving `max_passes = 25` hardcoded and only porting the stub
  logic, unless the planner has a specific reason tied to a success criterion.

**Emission 4 — `ppu_timebase_now` contract (D-10, declaration already present, mftb/mftbu
emission already correct — the gap is only the golden/lock-in test).**
```python
# Verified this session: production decoder + production _translate_op, no hand transcription.
"mftb":  (0x7C6C42E6, 0x1000, "ctx->gpr[3] = ppu_timebase_now();"),
"mftbu": (0x7C6D42E6, 0x1000, "ctx->gpr[3] = (ppu_timebase_now() >> 32);"),
```
These slot directly into the existing `CASES` dict in `test_ppu_lifter_emit.py` using the exact
same shape as every other entry — no new test machinery needed for the "emission" half. For the
"definer exists" half of D-10 ("casar com um definidor versionado no runtime"), the test should
additionally read `runtime/syscalls/sys_timer.c` (already a repo-relative path reachable via the
test's existing `REPO` variable) and assert a `uint64_t ppu_timebase_now(void)` definition line is
present — this directly encodes the link-break class of bug (commit `e441f37`) as a text
assertion, without needing to actually link. Whether to *also* attempt a real link-time proof (compile
the mftb/mftbu golden C against `sys_timer.c` and check the symbol resolves) is Claude's discretion —
the existing harness already compiles C++, so this is mechanically possible, but note the harness
today deliberately mocks everything (`ps3_indirect_call`, `vm_base`) rather than linking real
runtime `.c` files; linking `sys_timer.c` would be the first departure from that pattern and pulls
in `ps3_timebase_now()`'s own dependencies (check `runtime/syscalls/sys_timer.c` for what else it
needs before doing this — not verified in this session beyond the wrapper's existence).

**The OOB instrumentation (D-01..D-04, tied to the SAME preamble but not itself a "golden C
string" success criterion — it's success criterion 3, "a mina do re-lift").** Add inline to the
four `ppu_res_*` functions in `SOURCE_PREAMBLE` (`:307-358`). The runtime precedent to mirror
(verified this session at its ACTUAL current location, not the stale line numbers in CONTEXT.md
which point at old code):
```c
// Source: runtime/ppu/ppu_loader.cpp:1691-1752 (ppu_lwarx/stwcx/ldarx/stdcx — the value-correct,
// non-preamble pair this phase's inline instrumentation must mirror in shape, per D-03)
uint32_t ppu_lwarx(ppu_context* ctx, uint32_t addr) {
    if (vm_oob(addr, 4)) {
        ps3_mem_oob_report(addr, 4, "read", __builtin_return_address(0));
        ctx->reserve_valid = 0; return 0;
    }
    ...
}
```
Since the preamble's `ppu_res_*` functions cannot call the file-local `vm_oob()` (`static`,
`runtime/ppu/ppu_loader.cpp:399`), the self-contained bounds check (Claude's discretion per
CONTEXT) should mirror `vm_oob`'s own logic against the ALREADY-forward-declared-equivalent public
symbol `ppu_vm_size` (`extern "C" uint32_t ppu_vm_size = 0;` at `ppu_loader.cpp:376` — public,
not static, unlike `vm_oob` itself):
```c
/* Self-contained bounds check mirroring vm_oob()'s logic (runtime/ppu/ppu_loader.cpp:399-401);
 * cannot call vm_oob directly (static, file-local) so this duplicates its ONE comparison against
 * the public ppu_vm_size extern, consistent with vm_base already being forward-declared above. */
extern "C" uint32_t ppu_vm_size;
extern "C" void ps3_mem_oob_report(uint32_t ea, uint32_t len, const char* op, void* caller_ra);
static inline int ppu_res_oob(uint32_t ea, uint32_t len) {
    return ppu_vm_size && (uint64_t)ea + len > ppu_vm_size;
}
```
`ps3_mem_oob_report` should be **forward-declared inline** (as shown), not `#include
"ps3_mem_trace.h"` — this matches the existing preamble's philosophy (documented at `:292-300`:
"self-contained ... so no game-project runtime file is required") and keeps the merge-risk surface
identical to the `ps3_indirect_call`/`ps3_hle_call` forward declarations already present a few
lines below. `build_macos.sh`'s `INC` array does include `$PS3/include` (where `ps3_mem_trace.h`
lives), so `#include` would also technically compile — but a forward declaration is more
consistent with the file's own stated design principle and doesn't add a new upstream-diff line
inside an `#include` block that upstream might also touch.

### Anti-Patterns to Avoid

- **Don't fix the string-literal boundary of `SOURCE_PREAMBLE` in a way that changes unrelated
  lines.** Every line NOT part of one of the 4 emissions or the OOB instrumentation is
  byte-identical to upstream (verified by diff this session) — touching whitespace or reflowing
  comments elsewhere in the preamble needlessly widens the future merge conflict surface for no
  benefit.
- **Don't redesign `ppu_stubs.cpp` generation from first principles.** A working, tested,
  field-validated implementation (proven on the real GoW2 lift per `e52da4a`'s own commit message:
  "0 undefined refs, was 16113") already exists in git history. Re-deriving it risks
  reintroducing bugs that commit already fixed (e.g., C++ linkage matching, `PPU_WEAK` fallback
  for non-GCC/Clang).
- **Don't literally grep for `#ifdef _WIN32` as the only acceptable G4 guard.** The two forbidden
  tokens actually present today (`<intrin.h>`, `__declspec`) are correctly guarded by `_MSC_VER`,
  not `_WIN32`. A gate that doesn't accept `_MSC_VER` will FAIL on currently-correct, already-
  shipped code — a false positive on day one.
- **Don't let the mutation test import `ppu_lifter` fresh per mutation case via `importlib.reload`
  if avoidable.** The module is already imported once at file scope
  (`from ppu_lifter import PPULifter, LiftedFunction, SOURCE_PREAMBLE`) and `SOURCE_PREAMBLE` is a
  plain immutable string — mutating a local copy (`SOURCE_PREAMBLE.replace(needle, "")`) and
  re-running a presence check against the copy is sufficient and avoids Python import-caching
  pitfalls entirely for 2 of the 4 emissions (stdlib.h, `__APPLE__` branch, timebase declaration
  are all pure substring checks). Only the `ppu_stubs.cpp` emission needs behavioral mutation
  (see below), because its "removal" is a method/logic change, not a string a golden expects to
  see restated verbatim.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Fallback-stub generation for unlifted call targets | A new stub-emission design | Port `PPULifter.undefined_targets()` / `emit_fallback_stubs()` from commit `e52da4a` | Already field-validated on the real GoW2 lift; re-deriving risks losing the C++-linkage-matching and `PPU_WEAK` details that made it work |
| Mutation testing without a framework | A generic "mutation testing engine" | Plain Python: string-slice the already-imported `SOURCE_PREAMBLE` constant, or `delattr`/monkeypatch a `PPULifter` method and expect `AttributeError`/behavior change | The project has zero mutation-testing infra and zero third-party deps by design (`CONVENTIONS.md`); a bespoke `pytest-mutmut`-style dependency would be the first third-party import in `tools/` ever |
| OOB bounds checking in the preamble | A new memory-safety abstraction | Mirror `vm_oob()`'s single comparison inline (`ppu_vm_size && (uint64_t)ea+len > ppu_vm_size`) | `vm_oob` itself is a 3-line function; the "don't hand-roll" risk here is over-engineering a guard for a check this simple, not under-building one |
| Sync-gate diffing of two `build_macos.sh` copies | A generic file-diff library | Python stdlib `difflib` or a plain line-set comparison | No third-party deps allowed per project convention; stdlib `difflib.unified_diff` is sufficient for a WARN-only gate |

**Key insight:** every piece of this phase already has either (a) a proven prior implementation in
git history, (b) an existing runtime-side counterpart already linked and untouched, or (c) a
trivial stdlib-only implementation. There is no part of this phase that benefits from a new
abstraction or a new dependency.

## Runtime State Inventory

Not applicable — this is a code-generation/testing phase, not a rename/refactor/migration. No
stored data, live service config, OS-registered state, secrets, or build artifacts change identity
or naming as a result of this phase's work. (The one thing that superficially resembles this
category — `ppu_stubs.cpp` files sitting in gitignored lift output directories — is a generated
build artifact regenerated by the lifter each run, not persisted state that needs migration.)

## Common Pitfalls

### Pitfall 1: Confusing "restore" language with "build from scratch"
**What goes wrong:** REQUIREMENTS.md's `REQ-RDY0-1` description says the commit `11a1c3c` "apagou"
(erased) the three original emissions, which reads as if they need to be re-invented.
**Why it happens:** The requirement was written before this session's git-archaeology; two of the
three are actually present (verified, see Specifics in CONTEXT.md), and the third
(`ppu_stubs.cpp`) has a complete prior implementation sitting in `e52da4a`, not lost.
**How to avoid:** Treat this phase as "verify 2, restore 1 from history, add 1 new (timebase golden)
+ 1 new (OOB instrumentation) + 1 new (G4 gate) + 1 new (sync gate)" rather than "reimplement 3
emissions from a description."
**Warning signs:** A plan task that says "implement stdlib.h include" or "implement __APPLE__ TLS
branch" without first re-verifying they're already absent is about to do wasted/redundant work.

### Pitfall 2: G4 gate treating `_MSC_VER` as a violation
**What goes wrong:** A naive regex like `r'#ifdef\s+_WIN32'` as the ONLY acceptable guard around
`<intrin.h>` / `__declspec` will flag the two currently-correct occurrences in `SOURCE_PREAMBLE`
(guarded by `#ifdef _MSC_VER`) as G4 violations, making a new, correct gate fail immediately on
merge.
**Why it happens:** CLAUDE.md's own convention table phrases the rule as "`GetModuleHandleA` só em
`_WIN32`" — a simplification that doesn't distinguish "the Windows platform" (`_WIN32`) from "the
MSVC/clang-cl compiler" (`_MSC_VER`), which are different, only-partially-overlapping macros (clang
targeting Windows with `-target x86_64-pc-windows-gnu` defines `_WIN32` but not `_MSC_VER`; MSVC
always defines both).
**How to avoid:** Accept both `_WIN32` and `_MSC_VER` as valid guards for the G4 scan, since both
are used correctly in the current codebase (`_MSC_VER` for compiler-specific intrinsics like
`_BitScanReverse`/`__declspec(thread)`, `_WIN32` elsewhere for OS-specific APIs).
**Warning signs:** The gate fails on first run against unmodified `tools/ppu_lifter.py`, before any
new code is even added.

### Pitfall 3: Compiling the mutation-test harness for emissions that don't need it
**What goes wrong:** Reusing `build_and_run()`'s full compile-and-run harness machinery
(`test_ppu_lifter_emit.py:534-575`) for all four mutation cases adds ~4 extra C++ compilations per
test run for checks that are pure string presence — unnecessary cost and unnecessary new C++ mock
surface (the OOB instrumentation calls `ps3_mem_oob_report`, which the harness doesn't currently
mock; adding that mock is only needed if the plan chooses to compile-verify the OOB emission, which
is Claude's discretion per CONTEXT, not a stated requirement).
**Why it happens:** The existing test's dominant pattern (decode → translate → compile → run) is
attractive to imitate uniformly for consistency.
**How to avoid:** D-07 explicitly accepts "três passagens extras... operações de string, não
compilação" as the intended cost model. Keep emissions 1/2/4's presence checks and mutations as
pure string operations; only the `ppu_stubs.cpp` restoration needs to actually call
`PPULifter.emit_fallback_stubs()` (Python-level, no C++ compilation) to prove it produces the
expected stub body.
**Warning signs:** Test run time balloons noticeably past the existing harness compile+run cost
that's already documented as acceptable.

### Pitfall 4: Mirroring stale line numbers from CONTEXT.md's canonical refs into the plan verbatim
**What goes wrong:** CONTEXT.md's own instructions say to re-verify line anchors before dispatching
work (a general GSD rule), and this research already found one instance where CONTEXT.md's line
citation for the atomics precedent (`ppu_loader.cpp:452-465`) is stale — the actual
`ppu_lwarx`/`ppu_stwcx`/`ppu_ldarx`/`ppu_stdcx` functions with the `ps3_mem_oob_report` calls live
at `:1691-1752` in the current checkout, not `:452-465` (`:452-465` is inside the earlier
`vm_uncommitted_guard`/committed-range machinery, a different subsystem).
**Why it happens:** Line numbers drift between the session that wrote CONTEXT.md and later greps;
the file is large (2500+ lines) and actively edited.
**How to avoid:** The plan should re-grep for `ps3_mem_oob_report` and `ppu_res_lwarx` immediately
before writing task briefs, not trust either this document's or CONTEXT.md's line numbers past
that point.
**Warning signs:** A task brief citing a line number that, when read, shows unrelated code.

## Code Examples

### Verified mftb/mftbu golden pair (production decoder + production translator, this session)
```python
# Source: this session, tools/ppu_disasm.py::decode + tools/ppu_lifter.py::PPULifter._translate_op
# Encoding derivation: XFX-form, opcode=31, RT=bits[6:10], SPR field = swap(tbr) per
# ppu_disasm.py:709-719 (tbr=268 for mftb, tbr=269 for mftbu), XO=371, Rc=0.
mftb_r3  = 0x7C6C42E6   # decodes to: mftb r3   -> "ctx->gpr[3] = ppu_timebase_now();"
mftbu_r3 = 0x7C6D42E6   # decodes to: mftbu r3  -> "ctx->gpr[3] = (ppu_timebase_now() >> 32);"
```

### Exact preamble diff vs. upstream (this session, `git diff upstream/master -- tools/ppu_lifter.py` on the SOURCE_PREAMBLE region only)
```diff
--- upstream/master (SOURCE_PREAMBLE)
+++ this fork (SOURCE_PREAMBLE)
@@ -2,6 +2,7 @@
 /* Auto-generated by ppu_lifter.py -- do not edit by hand. */
 #include "ppu_recomp.h"
 #include <stdio.h>
+#include <stdlib.h>
 #include <string.h>
 #include <math.h>
@@ -227,8 +228,18 @@
 /* Trampoline function pointer for cross-fragment branches (TLS).
- * Must match the __declspec(thread) definition in indirect_dispatch. */
+ * Must match the definition in runtime/ppu/ppu_loader.cpp.
+ * (Apple thread_local rationale comment...)
+#if defined(_MSC_VER)
 extern "C" __declspec(thread) void (*g_trampoline_fn)(void*);
+#elif defined(__APPLE__)
+extern "C" thread_local void (*g_trampoline_fn)(void*);
+#else
+extern "C" __thread void (*g_trampoline_fn)(void*);
+#endif
```
This is the ENTIRE diff between our `SOURCE_PREAMBLE` and upstream's, confirming emissions 1 and 2
are isolated, minimal, and merge-safe.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `ppu_stubs.cpp` fallback-stub generation present in fork | Silently dropped by wholesale-replace-with-upstream merge | commit `11a1c3c`, 2026-07-21 | Any re-lift with unresolved call targets now fails to link instead of degrading gracefully — this phase's entire reason to exist for that emission |
| `ppu_timebase_now` undefined at link | Wrapper defined in `runtime/syscalls/sys_timer.c` | commit `e441f37`, 2026-07-26 (same day as this research) | Already fixed; this phase only needs to lock it in with a test, not fix it again |

**Deprecated/outdated:** None — this is greenfield-within-an-existing-tool work, not a migration
away from a deprecated pattern.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Reinsertion line numbers cited for `ppu_stubs.cpp` restoration (`:3049`, `:4284-4292`, `:4304-4310`) will still be accurate when the plan's implementer opens the file, given no other change lands first | Architecture Patterns / Emission 3 | Low — these are read-only anchors verified this session; only risk is drift if another phase's work lands on `tools/ppu_lifter.py` first (unlikely, no other phase touches this file per ROADMAP) |
| A2 | Forward-declaring `ps3_mem_oob_report` inline in the preamble (rather than `#include "ps3_mem_trace.h"`) is the right call for merge-risk minimization | Architecture Patterns / OOB instrumentation | Low-Medium — this is a recommendation grounded in the preamble's own stated design philosophy (verified quote at `:292-300`), not a locked decision; CONTEXT.md explicitly leaves "forma exata do bounds check inline" to Claude's discretion, so either choice is within scope, but the plan should pick one explicitly |
| A3 | Compiling/linking `sys_timer.c` into the golden test's harness (to link-verify `ppu_timebase_now`, beyond the text-presence check) is optional, not required, for D-10's "casar com um definidor versionado" | Architecture Patterns / Emission 4 | Low — D-10's own wording says "tem de casar com um definidor versionado no runtime," which a text-presence assertion against `sys_timer.c` satisfies; a full link-proof is stronger but not what was asked, and this session did not verify what `sys_timer.c` itself needs to compile standalone (untested) |

## Open Questions

1. **Should the G4 gate scan the whole `tools/ppu_lifter.py` file or only `SOURCE_PREAMBLE`/
   `HEADER_PREAMBLE`?**
   - What we know: D-09 says "o `SOURCE_PREAMBLE` e as strings de emissão de
     `tools/ppu_lifter.py` — tudo que o lifter *pode* emitir," which is broader than just the two
     named string constants; per-instruction `_translate_op` format strings are also technically
     "emission." A full-file grep was run this session and found the three forbidden-token
     occurrences are ALL inside the two preamble constants (none in per-instruction translate
     logic) — so scanning the whole file vs. just the two constants produces the same result today.
   - What's unclear: whether a future instruction handler could introduce a forbidden token outside
     the preamble, which only a whole-file scan would catch.
   - Recommendation: scan the whole `tools/ppu_lifter.py` source text (cheap, Python `open().read()`
     + regex), not just the two named constants — matches D-09's broader wording at no extra cost,
     and is more future-proof.

2. **Does `runtime/syscalls/sys_timer.c` compile standalone enough to be pulled into the golden
   test's harness for a real link-proof of `ppu_timebase_now`?**
   - What we know: the wrapper function itself is 3 lines (`runtime/syscalls/sys_timer.c:99-101`)
     calling `ps3_timebase_now()`, which is a "REAL wall-clock" implementation likely touching
     platform time APIs.
   - What's unclear: whether `ps3_timebase_now()`'s own dependencies (clock APIs, any global init)
     would compile cleanly in the harness's minimal include environment, or need additional mocks.
   - Recommendation: not investigated further this session (out of budget priority per the research
     focus); the text-presence check (Assumption A3) is sufficient to satisfy D-10 without this. If
     the planner wants the stronger link-proof, budget a small spike task to check
     `sys_timer.c`'s own includes/deps first.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Python ≥3.11 | Running the lifter and the test | ✓ (not via bare `python3`) | `.venv/bin/python3.14` (Homebrew symlink) | `scripts/test_python.sh`'s existing discovery order already handles this — no action needed |
| clang++ (C++20/17) | Compiling the golden test's harness | ✓ | Apple clang 21.0.0 | — |
| CMake ≥3.20 | CTest registration | ✓ | 4.4.0 | — |
| git | Sync-gate diffing, `_lifter_rev()` stamp | ✓ | 2.50.1 | — |
| `../gow2-recomp` sibling checkout | D-05's sync gate (non-blocking input) | ✓ locally (this dev machine) / ✗ in CI (single-repo checkout, confirmed in `.github/workflows/ci.yml`) | — | Sync gate prints `SKIP:` + exit 0 when absent — this is the DESIGNED behavior (D-05), not a gap to fill |

**Missing dependencies with no fallback:** none.

**Missing dependencies with fallback:** the `../gow2-recomp` sibling is absent in CI by
construction (single-repo `actions/checkout@v4`) — this is exactly the scenario D-05's `SKIP:`
convention exists to handle, confirmed by reading `.github/workflows/ci.yml` this session (macOS
job checks out only `ps3recomp`, no sibling).

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Plain Python `assert` scripts (no pytest) + CTest aggregation; see `.planning/codebase/TESTING.md` |
| Config file | `CMakeLists.txt` (root), `option(PS3RECOMP_BUILD_TESTS "Build unit tests" OFF)` at line 24 |
| Quick run command | `python3 runtime/ppu/tests/test_ppu_lifter_emit.py` (no cmake configure needed — imports `ppu_lifter` directly) |
| Full suite command | `./scripts/test_python.sh` (Python-only, fast) or `./scripts/test_all.sh` (Python + native CTest) |

### Phase Requirement → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| REQ-RDY0-1 (criterion 1: 4 emissions present) | `#include <stdlib.h>`, `__APPLE__` TLS branch, `ppu_stubs.cpp` generation, `ppu_timebase_now` contract all present in a fresh lift | unit (string presence + Python-level call of `emit_fallback_stubs`) | `python3 runtime/ppu/tests/test_ppu_lifter_emit.py` | ✅ file exists, extend with new assertions |
| REQ-RDY0-1 (criterion 2: mutation proof) | Each of the 4 emissions, removed in isolation, fails the test | unit (string mutation + delattr/monkeypatch) | same as above | ❌ Wave 0 — mutation helpers don't exist yet in this file |
| REQ-RDY0-1 (criterion 3: OOB instrumentation) | `ppu_res_lwarx/stwcx/ldarx/stdcx` call `ps3_mem_oob_report` on OOB | unit (string presence in `SOURCE_PREAMBLE`; optionally compile-verify) | same as above | ❌ Wave 0 — instrumentation code doesn't exist yet |
| REQ-RDY0-1 (criterion 4: G4 gate) | No forbidden token outside `#ifdef _WIN32`/`_MSC_VER` in lifter emission | unit (regex/text scan with guard-context tracking) | same as above (new function in the same file, or `scripts/test_python.sh` entry — Claude's discretion per D-08's "estender em vez de criar irmãos") | ❌ Wave 0 — gate doesn't exist yet |
| REQ-RDY0-1 (D-05/D-06: sync gate) | Compare `games/gow2/build_macos.sh` vs `../gow2-recomp/build_macos.sh`, WARN on diff, SKIP if sibling absent | unit/integration (file read + diff) | same as above | ❌ Wave 0 — sync gate doesn't exist yet |

### Sampling Rate
- **Per task commit:** `python3 runtime/ppu/tests/test_ppu_lifter_emit.py` (fast — no cmake
  configure, existing harness compile is the only slow part, already acceptable per D-07's own
  cost analysis)
- **Per wave merge:** `./scripts/test_python.sh` (full Python suite, includes this test plus the
  seven others already registered)
- **Phase gate:** `./scripts/test_all.sh` (adds native CTest) before `/gsd-verify-work`, per
  project convention; CI's macOS job (blocking) already runs both scripts

### Wave 0 Gaps
- [ ] Mutation-testing helper functions in `runtime/ppu/tests/test_ppu_lifter_emit.py` — none exist
      today; the file's only "mutation-adjacent" logic is the golden-vs-emission diff in
      `check_goldens()`, which compares CURRENT emission to a fixed golden, not a removed-emission
      to an expect-failure assertion. New code, not an extension of existing helpers.
- [ ] G4 forbidden-token scan function — does not exist anywhere in the repo (verified: no hits for
      "forbidden" or "G4" as identifiers in `tools/` or `runtime/`).
- [ ] D-05 sync-gate script/function — does not exist anywhere in the repo (verified: no existing
      Python code references `../gow2-recomp` or `gow2-recomp/build_macos.sh` by relative sibling
      path).
- [ ] `mftb`/`mftbu` CASES entries in `test_ppu_lifter_emit.py` — verified this session (see Code
      Examples), ready to paste in, no further investigation needed.

*(Framework install: none needed — `python3.11+` already satisfied via `.venv`.)*

## Security Domain

**`security_enforcement`:** not found set to `false` in `.planning/config.json` (config file not
present in this checkout at the standard path — treating as absent/default-enabled per the
protocol's instruction, but flagging that no explicit config toggle was found to confirm either
way).

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-------------------|
| V2 Authentication | No | Build tooling, no auth surface |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A |
| V5 Input Validation | Marginal | The lifter parses attacker-adjacent input (a PS3 ELF/BIN), but this phase touches only the fixed preamble text and stub-generation logic, not the ELF parser itself — out of scope |
| V6 Cryptography | No | N/A |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|----------------------|
| Fallback stubs silently masking a real missing-function bug as a graceful "return 0" | Tampering (data integrity, not security in the traditional sense) | Already mitigated by design: `ppu_unlifted_stub` logs the first 20 occurrences (`ppu_loader.cpp:2522-2526`) rather than failing silently forever; this phase does not change that behavior, only restores the stub-emission path that feeds it |
| OOB memory access in generated atomics reaching unmapped guest memory | Denial of Service (crash) / Tampering | `ps3_mem_oob_report` is report-only per D-04 (explicit project decision, matches CLAUDE.md rule 6 — probes gated, never abort); this phase's instrumentation adds observability, not a new mitigation boundary — a genuinely OOB `lwarx`/`stwcx` would still proceed to read/write `vm_base + ea` today exactly as it does now (this phase does not add a bounds-enforced abort, only a report call before the existing behavior) |

This phase does not introduce new attack surface — it adds text emission and test coverage to an
existing, already-shipping code generator. No new external input parsing, no new network/file I/O
beyond what the lifter and test scripts already do.

## Sources

### Primary (HIGH confidence — read/executed directly in this session)
- `tools/ppu_lifter.py` (current checkout) — `SOURCE_PREAMBLE`, `HEADER_PREAMBLE`, `_translate_op`,
  `emit_header`, `write_source_files`, `main()` — read in full for the relevant sections
- `upstream/master:tools/ppu_lifter.py` (fetched live via `git fetch upstream`,
  `github.com/sp00nznet/ps3recomp`) — diffed against the current fork's `SOURCE_PREAMBLE`
- `git show e52da4a -- tools/ppu_lifter.py` — the original `ppu_stubs.cpp` implementation, read in
  full
- `git show e441f37` — the `ppu_timebase_now` link-break fix and its commit message
- `runtime/ppu/ppu_loader.cpp` — `vm_oob`, `ppu_lwarx`/`ppu_stwcx`/`ppu_ldarx`/`ppu_stdcx`,
  `ppu_unlifted_stub`, `ppu_vm_size` — read directly, line numbers verified against CONTEXT.md's
  citations (found one stale citation, documented in Pitfall 4)
- `include/ps3_mem_trace.h`, `runtime/trace/ps3_mem_trace.c` — read in full for the
  `ps3_mem_oob_report` contract
- `runtime/syscalls/sys_timer.c` (partial, via grep) — confirmed `ppu_timebase_now` wrapper exists
- `runtime/ppu/tests/test_ppu_lifter_emit.py` — read in full (harness template, `CASES` dict,
  `check_goldens`, `build_and_run`)
- `scripts/test_python.sh`, `CMakeLists.txt` (lines 280-349) — read in full for the SKIP convention
  and dual test-registration
- `games/gow2/build_macos.sh` (this repo) vs. `/Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/build_macos.sh`
  (sibling checkout, present on this machine) — diffed directly, confirmed the exact 18-line
  `host_gow2_factory` divergence CONTEXT.md described
- `.github/workflows/ci.yml` — read in full, confirmed macOS-blocking / Linux+Windows
  non-blocking, confirmed single-repo checkout (no `gow2-recomp` sibling in CI)
- Live Python execution this session: `ppu_disasm.decode()` and `PPULifter._translate_op()` against
  synthetically-constructed `mftb r3`/`mftbu r3` instruction words, confirming exact golden strings

### Secondary (MEDIUM confidence)
- None — no web research was performed; this phase's domain is entirely internal to the checkout
  and its own git history.

### Tertiary (LOW confidence)
- None.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no external packages, verified stdlib-only via `CONVENTIONS.md` and direct
  grep
- Architecture: HIGH — every file/line cited was read directly in this session, not inferred
- Pitfalls: HIGH — all four pitfalls are grounded in specific, verified facts (git history,
  upstream diff, live decode/translate execution, CI config), not speculation

**Research date:** 2026-07-26
**Valid until:** Line-number citations should be re-verified at plan/implementation time (files
change fast in this repo — CONTEXT.md's own citations were already stale by ~1200 lines in one
case, per Pitfall 4). Structural/architectural findings (upstream diff shape, `e52da4a`'s
implementation, the runtime-side symbols being present) are stable and unlikely to change before
this phase executes.
