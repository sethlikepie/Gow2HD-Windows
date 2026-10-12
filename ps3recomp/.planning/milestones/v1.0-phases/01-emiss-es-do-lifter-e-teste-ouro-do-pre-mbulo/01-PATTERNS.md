# Phase 1: Emissões do lifter e teste-ouro do preâmbulo - Pattern Map

**Mapped:** 2026-07-26
**Files analyzed:** 2 (both existing files, modified in place — no new files this phase)
**Analogs found:** 2 / 2 (both are "extend self" — the closest analog to each file IS the file
itself; the phase adds sections following its own established internal conventions)

This phase has an unusual pattern-mapping shape by explicit decision (D-08): there are no new
files. Both targets are extended in place. "Closest analog" therefore means: which *existing
section within the same file(s)* establishes the shape new code must copy. Three additional
cross-file analogs (runtime atomics, deleted `ppu_stubs` code, CMake/test_python.sh registration)
supply the concrete excerpts for the parts that genuinely have no local precedent.

## File Classification

| File | Role | Data Flow | Closest Analog | Match Quality |
|------|------|-----------|-----------------|---------------|
| `tools/ppu_lifter.py` (`SOURCE_PREAMBLE`, `ppu_res_lwarx/stwcx/ldarx/stdcx`) | code-generator (text emission) | transform (Python string → C++ source) | itself: `runtime/ppu/ppu_loader.cpp:1691-1752` (`ppu_lwarx`/`ppu_stwcx`/`ppu_ldarx`/`ppu_stdcx`) | role-match (same operation, different translation unit — runtime is the value-correct pair this preamble pair must mirror in *instrumentation shape*, not in CAS logic, per the design-philosophy comment already at `:280-300`) |
| `tools/ppu_lifter.py` (`emit_fallback_stubs`/`undefined_targets`, restored) | code-generator (batch — writes `ppu_stubs.cpp`) | batch / file-I/O | git history: `git show e52da4a -- tools/ppu_lifter.py` (this fork's own prior implementation) | exact (literal restoration, not a new design) |
| `runtime/ppu/tests/test_ppu_lifter_emit.py` (golden + mutation extension) | test (golden/property test, no framework) | request-response (decode→translate→assert) + batch (compile+run harness) | itself: `CASES` dict (`:68-160`) + `check_goldens()` (`:171-192`) + `main()` (`:588-598`) | exact (D-08: extend, do not create sibling) |
| Sync-gate + G4 gate (new functions, living inside `test_ppu_lifter_emit.py` per D-08) | test / static-analysis gate | file-I/O (read two `.sh` files) / transform (regex scan of one `.py` file) | none in-repo (see "No Analog Found") | no analog |
| Registration (no new file — must NOT drift) | config | — | `scripts/test_python.sh:102-111` `TESTS` block; `CMakeLists.txt:298-329` `py.ppu_lifter_emit` + `SKIP_REGULAR_EXPRESSION` block | exact (already registers this exact file; nothing to add here since D-08 avoids a sibling file) |

## Pattern Assignments

### `tools/ppu_lifter.py` — OOB instrumentation of `ppu_res_lwarx/stwcx/ldarx/stdcx` (D-01..D-04)

**Analog:** `runtime/ppu/ppu_loader.cpp:1691-1752` (the value-correct, non-preamble atomics pair)

**Current un-instrumented preamble code to be extended** (`tools/ppu_lifter.py:307-358`):
```python
static inline uint32_t ppu_res_lwarx(ppu_context* ctx, uint64_t ea) {
    uint32_t raw;
    memcpy(&raw, vm_base + (uint32_t)ea, 4);
    ctx->reserve_addr  = (uint32_t)ea;
    ctx->reserve_value = raw;              /* raw guest (big-endian) bytes */
    ctx->reserve_valid = 1;
    return ppu_res_bswap32(raw);
}
static inline void ppu_res_stwcx(ppu_context* ctx, uint64_t ea, uint32_t val) {
    int ok = 0;
    if (ctx->reserve_valid && ctx->reserve_addr == (uint32_t)ea) {
        uint32_t expected = (uint32_t)ctx->reserve_value;
        uint32_t desired  = ppu_res_bswap32(val);
#ifdef _MSC_VER
        ok = (_InterlockedCompareExchange((long*)(vm_base + (uint32_t)ea),
                                           (long)desired, (long)expected) == (long)expected);
#else
        ok = __atomic_compare_exchange_n((uint32_t*)(vm_base + (uint32_t)ea), &expected, desired,
                                          0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#endif
    }
    ctx->reserve_valid = 0;
    ctx->reserve_addr  = 0;
    ctx->cr = ok ? ((ctx->cr & ~(0xFu << 28)) | (2u << 28))
                 :  (ctx->cr & ~(0xFu << 28));
}
/* ldarx / stdcx mirror lwarx/stwcx at 8 bytes -- same shape */
```
(`ppu_res_ldarx`/`ppu_res_stdcx` are byte-identical in shape at 8 bytes, `:333-358`.)

**Runtime precedent to mirror — the EXACT shape the inline instrumentation must copy**
(`runtime/ppu/ppu_loader.cpp:1691-1725`, comment block `:1680-1690` explains the "why"):
```cpp
uint32_t ppu_lwarx(ppu_context* ctx, uint32_t addr) {
    if (vm_oob(addr, 4)) {
        ps3_mem_oob_report(addr, 4, "read", __builtin_return_address(0));
        ctx->reserve_valid = 0; return 0;
    }
    uint32_t raw; memcpy(&raw, vm_base + addr, 4);
    uint32_t val = __builtin_bswap32(raw);
    ctx->reserve_addr = addr; ctx->reserve_value = val; ctx->reserve_valid = 1;
    return val;
}
int ppu_stwcx(ppu_context* ctx, uint32_t addr, uint32_t val) {
    int ok = 0;
    if (ctx->reserve_valid && (uint32_t)ctx->reserve_addr == addr) {
        if (vm_oob(addr, 4)) {
            ps3_mem_oob_report(addr, 4, "write", __builtin_return_address(0));
        } else {
            uint32_t expected = __builtin_bswap32((uint32_t)ctx->reserve_value);
            uint32_t desired  = __builtin_bswap32(val);
            ok = __atomic_compare_exchange_n((uint32_t*)(vm_base + addr), &expected, desired, 0,
                                             __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? 1 : 0;
            if (ok) ppu_rsv_on_store(addr, 4);
        }
    }
    ctx->reserve_valid = 0;
    ctx->cr = (ctx->cr & ~(0xFu << 28)) | (ok ? (2u << 28) : 0u);
    return ok;
}
```
Note `len` is static per instruction (4 for lwarx/stwcx, 8 for ldarx/stdcx); `op="read"` for the
loads, `op="write"` for the conditional stores — exactly D-03's assignment.

**Bounds check the preamble must self-provide (D-02 forbids calling `vm_oob`, `static` at
`ppu_loader.cpp:399`)** — verified this session still current:
```cpp
static int vm_oob(uint32_t a, uint32_t n)
{
    if (ppu_vm_size && (uint64_t)a + n > ppu_vm_size) {
        ...
    }
}
```
`ppu_vm_size` is the public extern already forward-declared for reuse
(`runtime/ppu/ppu_loader.cpp:376`: `extern "C" uint32_t ppu_vm_size = 0;`). The preamble should
forward-declare it too (it does not currently — this is new text), plus forward-declare
`ps3_mem_oob_report`, matching the existing forward-declaration style just below the atomics block
for `ps3_indirect_call`/`ps3_hle_call` (`tools/ppu_lifter.py:360-369`):
```python
extern "C" void ps3_indirect_call(ppu_context* ctx);
extern "C" void ps3_hle_call(unsigned int nid, ppu_context* ctx);
```
i.e. plain `extern "C"` declarations, no `#include`, consistent with the preamble's own stated
self-containment principle (comment at `tools/ppu_lifter.py:292-300`: "so no game-project runtime
file is required").

**Public contract of the reporter to declare/call** (`include/ps3_mem_trace.h:54`,
`runtime/trace/ps3_mem_trace.c:36-46` — already linked, report-only, gated, capped; do not
reproduce the cap/gate logic in the preamble, just call it):
```c
void ps3_mem_oob_report(uint32_t ea, uint32_t len, const char* op, void* caller_ra);
```

---

### `tools/ppu_lifter.py` — restore `ppu_stubs.cpp` generation (Emission 3)

**Analog:** git history, this fork's own deleted implementation — `git show e52da4a -- tools/ppu_lifter.py`

```python
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
Runtime counterpart already present and untouched — `runtime/ppu/ppu_loader.cpp:2519`
(`ppu_unlifted_stub(uint64_t addr, ppu_context* ctx)`); no runtime change needed.
`self.call_targets`/`self.branch_targets` are already tracked state (populated throughout
`lift_function`); the near-identical set-difference computation already exists in `emit_header()`
for forward declarations — that is where the sibling method should live for consistency.

---

### `runtime/ppu/tests/test_ppu_lifter_emit.py` — golden pair for `ppu_timebase_now` (Emission 4)

**Analog:** the file's own `CASES` dict, same shape as every other entry (`:68-160`)

```python
CASES = {
    "lmw": (
        0xBB810010, 0x1000,
        "{ uint64_t _ea = ctx->gpr[1] + 0x10; "
        "ctx->gpr[28] = vm_read32(_ea + 0); ctx->gpr[29] = vm_read32(_ea + 4); "
        "ctx->gpr[30] = vm_read32(_ea + 8); ctx->gpr[31] = vm_read32(_ea + 12); }",
    ),
    # ... (word, decode_addr, golden_c) tuples, one per instruction under audit
}
```

New entries to add (verified this session via live decode/translate, per RESEARCH.md Code
Examples — no hand transcription needed):
```python
"mftb":  (0x7C6C42E6, 0x1000, "ctx->gpr[3] = ppu_timebase_now();"),
"mftbu": (0x7C6D42E6, 0x1000, "ctx->gpr[3] = (ppu_timebase_now() >> 32);"),
```

**Consumption pattern (`check_goldens()`, `:171-192`)** — the exact decode → translate → assert
loop that will pick up the new CASES entries automatically, no separate wiring needed:
```python
def check_goldens() -> dict:
    lift = PPULifter()
    func = LiftedFunction(name="func", start_addr=0x0, end_addr=0x0100_0000)
    emitted = {}
    ok = True
    for name, (word, addr, golden) in CASES.items():
        insn = decode(word, addr)
        got = lift._translate_op(insn, func)
        emitted[name] = got
        if got != golden:
            ok = False
            print(f"[FAIL] golden {name}: emission diverged from checked-in golden")
            ...
    if not ok:
        raise SystemExit("\nEmission goldens diverged. ...")
    return emitted
```

**"Definer exists" half of D-10** (text-presence check against the runtime, no link needed per
Assumption A3 in RESEARCH.md): read `runtime/syscalls/sys_timer.c` via the same `REPO`-relative
path pattern already used for `sys.path.insert(0, TOOLS)` at the top of the file:
```python
HERE = os.path.dirname(os.path.abspath(__file__))
RUNTIME_PPU = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(RUNTIME_PPU))
TOOLS = os.path.join(REPO, "tools")
```
Assert a `uint64_t ppu_timebase_now(void)` (or equivalent wrapper) definition line is present in
`os.path.join(REPO, "runtime", "syscalls", "sys_timer.c")`.

---

### `runtime/ppu/tests/test_ppu_lifter_emit.py` — mutation harness (D-07, new)

**No exact analog exists** (RESEARCH.md Wave-0 gap: "none exist today"). Closest available shape
to imitate for the *presence* half is `check_goldens()`'s pure-string comparison above (no
compilation needed for 3 of 4 emissions — stdlib.h, `__APPLE__` branch, timebase declaration are
substring checks against the already-imported `SOURCE_PREAMBLE` constant). Per D-07/Pitfall 3,
avoid `build_and_run()`'s compile step for these:
```python
# Pattern to follow (not existing code — Claude's discretion per CONTEXT.md):
assert "#include <stdlib.h>" in SOURCE_PREAMBLE
mutated = SOURCE_PREAMBLE.replace("#include <stdlib.h>\n", "")
assert "#include <stdlib.h>" not in mutated   # proves the mutation actually removed it
# then re-run whatever the real assertion depends on against `mutated`, expect it to fail
```
The `ppu_stubs.cpp` mutation is the one case needing *behavioral* (not string) mutation, since its
absence is a method/logic change — call `PPULifter().emit_fallback_stubs()` directly (pure Python,
no C++ compilation, per Pitfall 3) and assert it returns the stub body when `undefined_targets()`
is non-empty, then monkeypatch/delattr the method and assert the caller path fails or degrades as
expected.

**Compile+run harness template (`HARNESS_TEMPLATE`, `:198-240+`)** — only needed if the OOB
instrumentation emission is compile-verified (optional, per D-07/Pitfall 3, not required):
```cpp
#pragma STDC FP_CONTRACT OFF
#include "{ppu_context_h}"
#include "{ppu_memory_h}"
#include "{ppu_ops_h}"
...
static uint8_t g_guest_mem[4096];
extern "C" uint8_t* vm_base = g_guest_mem;
extern "C" int ppu_guest_range_committed(uint32_t, uint32_t) { return 1; }
extern "C" __thread void (*g_trampoline_fn)(void*) = nullptr;
extern "C" void ps3_indirect_call(ppu_context* ctx) { ... }
```
Note: this mock surface does **not** currently mock `ps3_mem_oob_report` — adding that mock is
only needed if the plan chooses to compile-verify the OOB emission (Claude's discretion, not a
stated requirement per RESEARCH.md Pitfall 3).

**`main()` entry point pattern (`:588-598`)** — unchanged shape, new gate calls should slot into
this sequence:
```python
def main() -> int:
    print("=== ppu_lifter emission goldens ===")
    emitted = check_goldens()
    print("\n=== compiled emission harness ===")
    rc = build_and_run(emitted, pick_cxx(sys.argv[1:]))
    print(f"\n[{'OK' if rc == 0 else 'FAIL'}] test_ppu_lifter_emit ({rc})")
    return rc
```

---

### G4 forbidden-token gate (new — no analog anywhere in the repo)

**No analog found.** Verified: no hits for "forbidden" or "G4" as identifiers in `tools/` or
`runtime/`. Must be written from the pitfall constraints documented in RESEARCH.md, not copied
from precedent:

- Scan the whole `tools/ppu_lifter.py` source text (`open().read()` + regex), not just
  `SOURCE_PREAMBLE`/`HEADER_PREAMBLE`, per D-09's broader wording ("tudo que o lifter *pode*
  emitir") — a full-file scan costs nothing extra and is more future-proof (Open Question 1 in
  RESEARCH.md).
- Forbidden tokens: `<intrin.h>`, `Sleep(`, `__declspec`, `GetModuleHandleA`.
- **Critical correction vs. CLAUDE.md's own wording:** accept **both** `_MSC_VER` and `_WIN32` as
  valid guards. The two forbidden tokens actually present today are guarded by `_MSC_VER`, not
  `_WIN32`:
```python
# tools/ppu_lifter.py:377 (verified current)
#if defined(_MSC_VER)
extern "C" __declspec(thread) void (*g_trampoline_fn)(void*);
#elif defined(__APPLE__)
```
A gate requiring only `_WIN32` as guard fails on day one against this already-correct code
(RESEARCH.md Pitfall 2).

---

### D-05 sync-gate script (new — no analog anywhere in the repo)

**No analog found.** Verified: no existing Python code references `../gow2-recomp` or
`gow2-recomp/build_macos.sh` by relative sibling path. Must be built from the SKIP/WARN
conventions below (which DO have analogs) rather than from a prior sync-gate implementation.

**SKIP convention to reuse exactly** (`CMakeLists.txt:276-298`, comment explains the mechanism):
```
# um self-test que não pode correr aqui (falta um compilador/SDK) imprime
# "SKIP: <motivo>" em stdout e sai com código 0 -- não é falha do produto.
set(PS3RECOMP_SKIP_REGEX "SKIP:")
```
Precedence rule stated in CONTEXT.md/RESEARCH.md: exit code is evaluated FIRST — an exit≠0 is
always FAIL even with `SKIP:` in stdout. The sync gate must `print("SKIP: ...")` then
`return 0`/`sys.exit(0)` when `../gow2-recomp/build_macos.sh` is absent, never a nonzero exit.

**Diff mechanism** (Don't Hand-Roll table in RESEARCH.md): use stdlib `difflib.unified_diff` or a
plain line-set comparison — no third-party diff library. WARN (print a message), never raise/fail,
per D-06.

---

## Shared Patterns

### `_MSC_VER`/`__APPLE__`/`#else` three-way portability split
**Source:** `tools/ppu_lifter.py:377-383` (the `g_trampoline_fn` TLS declaration, already present)
**Apply to:** the OOB instrumentation's `#ifdef _MSC_VER` CAS branch already follows this pattern
one level up in the same functions (`ppu_res_stwcx`/`ppu_res_stdcx`, `:320-326`/`:346-352`); no new
portability split needed for the OOB report call itself (`ps3_mem_oob_report` is portable C).

### Env-gate cached `PS3_*` pattern
**Source:** `runtime/ppu/ppu_loader.cpp:1702-1706` (`PS3_TRACE_LWARX`, inside `ppu_lwarx` itself)
```c
{ static int on = -1; if (on < 0) on = getenv("PS3_TRACE_LWARX") ? 1 : 0;
  if (on) { ... } }
```
**Apply to:** only if the preamble instrumentation needs its own gate (CONTEXT.md's "Established
Patterns" flags this form as the one to follow if needed) — but per D-04, `ps3_mem_oob_report`
itself is already gated/capped downstream, so the preamble instrumentation likely needs **no**
gate of its own, just the unconditional call.

### Dual test-registration mirror (must stay in sync, but nothing to ADD here per D-08)
**Source A:** `scripts/test_python.sh:102-111`
```
TESTS="
tools/test_audit_fallthrough.py
...
runtime/ppu/tests/test_ppu_lifter_emit.py
"
```
**Source B:** `CMakeLists.txt:325-329`
```cmake
add_test(NAME py.ppu_lifter_emit
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime/ppu/tests/test_ppu_lifter_emit.py")
set_tests_properties(py.ppu_lifter_emit PROPERTIES
    SKIP_REGULAR_EXPRESSION "${PS3RECOMP_SKIP_REGEX}")
```
**Apply to:** nothing — both already register `test_ppu_lifter_emit.py`. D-08 exists precisely to
avoid needing a third registration site for a sibling file. Confirm at implementation time that no
new file was accidentally created that would need this mirroring.

## No Analog Found

| File/Feature | Role | Data Flow | Reason |
|---------------|------|-----------|--------|
| G4 forbidden-token gate | static-analysis gate | transform (regex scan) | Verified: zero hits for "forbidden"/"G4" as identifiers anywhere in `tools/` or `runtime/`. Built from CLAUDE.md's own convention table plus the `_MSC_VER`-vs-`_WIN32` correction in RESEARCH.md Pitfall 2, not copied from precedent. |
| D-05 sync-gate (build_macos.sh comparison) | test tooling | file-I/O (diff) | Verified: no existing code references `../gow2-recomp` by relative sibling path anywhere in this repo. Assembled from the SKIP convention (`CMakeLists.txt:276-298`) + stdlib `difflib`, not copied. |
| Mutation-testing helpers | test tooling | transform (string surgery / monkeypatch) | Verified: `check_goldens()` compares CURRENT emission to a FIXED golden, not a removed-emission to an expect-failure assertion — a different shape. New code per D-07, structure left to Claude's discretion. |

## Metadata

**Analog search scope:** `tools/ppu_lifter.py` (full read of relevant sections), `runtime/ppu/ppu_loader.cpp`
(targeted grep + read of atomics/`vm_oob`/`ppu_vm_size` regions), `runtime/ppu/tests/test_ppu_lifter_emit.py`
(full structural read via grep + targeted offsets), `CMakeLists.txt` (lines 275-330), `scripts/test_python.sh`
(lines 95-120), `git show e52da4a -- tools/ppu_lifter.py` (git history), `include/ps3_mem_trace.h` +
`runtime/trace/ps3_mem_trace.c` (contract only, per RESEARCH.md — not re-read here since RESEARCH.md
already extracted the relevant lines with HIGH confidence).
**Files scanned:** 5 read directly this session + 1 git-history show, cross-referenced against
CONTEXT.md/RESEARCH.md's own verified excerpts (RESEARCH.md already did the heavy verification;
this pass re-confirmed the specific line ranges needed for concrete plan excerpts and caught no
further drift beyond what RESEARCH.md's Pitfall 4 already flagged).
**Pattern extraction date:** 2026-07-26
