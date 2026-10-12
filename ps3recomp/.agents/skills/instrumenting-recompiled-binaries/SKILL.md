---
name: instrumenting-recompiled-binaries
description: Use when adding a probe, trace, or watch to machine-generated code — a static recompiler's lifted output, transpiled sources, codegen — or when reading a probe's output and deciding whether to believe it, especially if a counter came back zero or two fields of one line disagree
---

# Instrumenting Recompiled Binaries

## Overview

Generated code has no debugger story: you cannot breakpoint a guest function that
exists only as one of 52 000 emitted C++ symbols. You instrument by **patching the
generated source** with gated probes, rebuilding, and reading logs.

Every failure below was measured in one session on a PS3→C++ recompiler. Four of
them produced **wrong conclusions that survived several rounds** before something
else contradicted them.

**Core principle: a probe is a program you also have to debug.** Its output is
evidence about the probe until you have shown otherwise.

## The Probe Contract

Every probe patch is an idempotent script, committed, with a tri-state exit:

```python
MARKER = "MYPROBE-<SITE>"
NEEDLE = (...)                      # exact generated lines
REPL   = NEEDLE + "<gated block>"

def patch_text(t):
    if MARKER in t:              return t, "ja"          # rc 0
    if t.count(NEEDLE) != 1:     return t, "nao-casou"   # rc 2  <- NOT silent
    return t.replace(NEEDLE, REPL, 1), "ok"              # rc 0
```

- **Gated by env var, OFF by default.** `getenv` once into a `static int`, plus a
  `_CAP` var. No-op on the baseline.
- **`rc != 0` when nothing happened.** A patch that matched nothing must say so, or
  the catalog records it as "already applied" and the gate stays green over a build
  that never got the change.
- **Idempotent, proven:** run twice on a clean copy, assert 1 marker and an
  identical hash.

## Count the Needle Before You Trust It

Measured, same session, three separate patches:

| intended needle | actual occurrences |
|---|---|
| lines around `ps3_indirect_call` | **5** |
| the lazy-init flag test | **4** |
| flag test + preceding indirect call | **3** |
| after anchoring on a unique label/`lr` constant | **1** |

Generated code is repetitive by construction. A 3-line needle that reads
distinctive is routinely not. **Count across every chunk before applying**; anchor
on something the generator emits once — a label name, a return-address constant, an
unusual offset.

Applying a 5-match needle instruments four unrelated sites and every conclusion
drawn afterwards is about the wrong code.

## Capture, Never Recalculate

The single most expensive bug of the session. A probe printed an "anchor" like this:

```c
uint32_t _A = vm_read32((uint32_t)ctx->gpr[2] - 0x2B18u);   /* re-read */
```

The register the program actually used had been **overwritten 20 lines earlier**.
The probe was measuring its own arithmetic — deterministic — so the value came out
"constant across all 13 calls", which read as a finding. It was not.

**If the register is dead at your insertion point, move the probe; do not
reconstruct the value.** A recomputation is a hypothesis rendered as a number.

## Inherited Probes Carry Every Site's Ambiguity

A pre-existing probe printed `pool=…` and `head=…`. Its source, read at one site,
justified a whole model. There were **16 instances of that marker** in the lift, at
sites with different register meanings.

A field label is a promise about **one** site, not about the line you are reading.

> Before using someone else's probe: `grep -c` the marker. If it is more than 1,
> either find which instance emitted your line, or write your own single-site probe.

Writing your own is usually cheaper than proving which of 16 fired.

## Caps Hide Answers

Three times in one session a cap truncated the run before the interesting event:

| probe | cap | outcome |
|---|---|---|
| free-list head | 400 | last line at 3987; the event was at 4866 |
| allocator | 200 | 200 healthy allocations shown; **the bad one was #237** |
| chain | 60 | stopped before the divergence |

**A result that lands exactly on the cap is not a result, it is a warning.** On a
reconnaissance probe start the cap absurdly high and tighten only after you know
the density.

## Count the Definitions Before You Instrument the Primitive

The most expensive failure of the session, and the cheapest to prevent.

A watch was added to `vm_write32` in the runtime header. It never fired for guest
writes. Over four rounds that silence was read as evidence and produced four
conclusions — three of them wrong, all later retracted.

The cause: **there were two `vm_write32`.**

| definition | where | who calls it |
|---|---|---|
| `static inline void vm_write32(uint32_t, uint32_t)` | the runtime header | runtime, HLE |
| `void vm_write32(uint64_t, uint32_t)` | the loader `.cpp` | **the generated code** |

The generated sources include their *own* header, which declares the out-of-line
one — different signature, different file. Instrumenting the header saw every
write **except the ones under investigation**.

The tell was there and I missed it for four rounds: **every positive the watch
ever produced came from the same non-generated module.** A watch on a hot path
that only ever fires from one caller is not covering what you think.

```bash
grep -rn 'vm_write32' --include=*.h --include=*.cpp | grep -v 'extern'   # count real definitions
grep -n 'include' generated_chunk.cpp                                     # which header does IT use?
```

Before instrumenting any primitive in generated code: **count its definitions and
confirm which one the generated translation unit actually compiles against.**
Signature differences (`uint32_t` vs `uint64_t` address) are the giveaway.

## Verify the Code Reached the Binary

A patch reported `CONVERTIDO`. It had made two edits; the **second silently did not
match**, and the script printed success anyway. The probe then measured nothing and
the zero looked like a finding.

One command settles it:

```bash
strings ./boot_binary | grep -c '^PS3_TRACE_MYPROBE$'   # 0 = the code is not in there
```

A gated probe's env-var name and format string only survive into the binary if the
block compiled and is reachable. **Check the string, not the patch's own report.**

Also force the rebuild — a generator touched the sources, so mtime-based skipping
will happily reuse stale objects.

## A Zero Needs a Same-Regime Control

Four zeros in one session meant four different things:

| zero | meaning | what separated it |
|---|---|---|
| capped probe | inconclusive | comparing line numbers |
| write watch | **valid refutation** | control with a known positive |
| macro never compiled | inconclusive | `strings` on the binary |
| translate watch below a threshold | **false negative** | a later direct read contradicting it |

The last one is the trap, and it cost four ledgers. The control **passed** — the
watch fired 3 000 times with a high threshold. But the measurement asked about
addresses under `0x1000`, and the control had only ever exercised the common path.

> **The control must exercise the same regime as the measurement.** Proving an
> instrument works in general does not prove it works where you are about to accept
> a zero. Produce a known positive *inside the same range/branch/state* first.

## When Two Fields of One Line Disagree

`pool=0` and `head=<text>` cannot both be true if reading `*(pool+4)` at that
address returns zero. Do not pick the convenient one — **discard the line and
re-measure with a single-site probe you wrote.**

Two fields printed together are only as coupled as the code between them; probes
inserted after intervening writes report a state that never coexisted.

## Quick Reference

| Situation | Action |
|---|---|
| About to apply a probe patch | `grep -c` the needle across all chunks; require exactly 1 |
| Register you want is dead at the insertion point | Move the probe. Never re-read/recompute |
| Using an existing probe's output | Count its markers; >1 means the labels are unreliable |
| Result lands exactly on the cap | Raise the cap and rerun; that is not a measurement |
| Probe printed nothing | `strings` the binary for the env name before interpreting |
| About to accept a zero as refutation | Do you have a known positive in the *same regime*? |
| Two fields of one line contradict | Discard the line; write a single-site probe |
| Patch reports success | That is text changed. Compile, check the binary, then run |

## Common Mistakes

**Instrumenting the callee instead of the call site.** A shared function has many
callers and often many probe instances; the one call site you care about is unique,
and a probe there captures the argument *and* the return of the same call.

**Trusting a delegate's verdict.** Take the facts it measured; re-derive the
inference. A report can hold correct measurements and a conclusion its own numbers
contradict.

**Reading generated code linearly.** Register provenance in lifted output is not
what a straight read suggests — a "callee-save prologue" that loads from the stack
instead of saving the register marks a *fragment*, an entry point mid-function
whose registers are inherited. Measure provenance; do not deduce it.

## Real-World Impact

One session, six probe patches, a chain traced from a symptom — 16 writes into
uncommitted memory — up six levels to a root: a manager object whose pool table was
marked initialized while its slots were empty, so an allocator popped a
"free-list" from guest address 0, where a path string happened to live.

Every level was found by measurement. **Four intermediate conclusions were wrong**
and were caught by controls or by a number that had to appear and didn't — never by
re-reading the code more carefully.
