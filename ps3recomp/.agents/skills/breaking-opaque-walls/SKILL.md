---
name: breaking-opaque-walls
description: Use when a bug sits behind a wall you cannot step through — a hang, freeze, or silent stall in an emulator, recompiler, firmware, long pipeline, or any system where each run is expensive and you cannot attach a debugger to the failing frame
---

# Breaking Opaque Walls

## Overview

A wall is a defect you cannot look at directly. The process hangs, the pipeline stalls, the
frame never renders — and each attempt to observe costs a full run. Sessions burn without a
single measurement because the loop is: *guess a cause → build something → hope it works.*

**Core principle: the only cheap thing is refutation.** A run that eliminates three hypotheses
is worth more than a fix that might work, because eliminations accumulate and guesses don't.

Measured: four sessions reached one wall with **zero in-boot measurements**. The fifth wrote
predictions first, ran one probe, and named the mechanism to the instruction.

## When to Use

- A hang/freeze/stall with no stack you can read
- Each observation costs a full expensive run (boot, build, deploy, batch)
- Two or more competing theories and no way to tell them apart by reading code
- Sessions have gone by without a number

**Not for:** bugs with a reproducible unit test (write the test), or crashes with a usable
stack trace (read it).

## The Loop

```
1. one question, one sentence      — what single fact splits the hypotheses?
2. predictions, WRITTEN and COMMITTED before the probe exists
3. control probes                 — prove you are where you think you are
4. run                            — 3+ times; determinism is a finding
5. verdict, including refutations — what is now impossible
6. next question
```

Step 2 is what makes a probe discriminating instead of confirmatory. Commit the predictions
**before writing the probe**, and never rewrite anything above that line.

## Predictions Before the Probe

State what each outcome would mean, and what would count as neither:

```markdown
## Question
For the children the walk visits, are tab[low16] and tab[subtag] the SAME manager?

## P1 — they are the same
Then push/pop is coherent and indexing asymmetry cannot cause the 27M iterations.

## P2 — some child differs
Then push credits one manager's cursor and pop debits another's.

## Neither
If diff_mgr is 0 but iterations stay high, both are wrong and the target moves.
<!-- CUT LINE — nothing above is rewritten after running -->
```

Without this, every result confirms whatever you already believed. **P1 won the criterion and
lost the conclusion** in a real session — provable only because the conclusion had been written
down in advance.

## Control Probes: If the Control Fails, the Numbers Are Void

Every probe needs at least one assertion that proves it is reading the right thing.

```c
// control: this must be the type table, or nothing below means anything
if (r28 != 0x00868D48) { tab_bad++; }
```

A control that fails is not a detail to mention later. In one session a control read a
*different field* than a prior probe had (`+0x88` vs `+4`), and the honest consequence was
recorded: object identity rested on address correlation, **not** on a confirmed read. Two
waves later a probe read the right field and closed it.

## Distrust Your Own Fix

**Add a step whose only job is to disbelieve the previous step.**

A fix followed a plan exactly, reported `CONVERTED`, applied cleanly, passed its tests — and
in-boot measured **zero effect**. Numbers identical to baseline. The cause: it read a value
that was garbage (`0x3F800000` — a float from a matrix), which was precisely the uncertainty an
earlier failed control had flagged.

| | baseline | fix v1 | fix v2 |
|---|---|---|---|
| runaway iterations | 179 459 342 | 27 362 359 | **0** |
| junk keys | 2 | 2 | **0** |

Without a verification step, v1 ships as done. **A fix that reports success is a claim.**

## Counting Beats Arguing

When candidate fixes differ in blast radius, **count** instead of debating.

Three candidates for one wall: the root cause (touches every record of a class), a surgical
filter (touches the enumeration), and a defensive null-check (forbidden — see below). The
argument was unresolvable. One probe counted how many objects a subtag filter would prune:

**`n = 1`, identical across 3 runs.** Exactly one pathological record, its address matching
byte-for-byte a measurement from two days earlier. The surgical fix was correct, and it was a
number that said so.

Design the count so each outcome routes to a different fix. If every outcome leads to the same
place, you are not measuring — you are justifying.

## Negative Proof Closes Doors Permanently

Absence, when the search space is bounded, is stronger than presence.

A constant was hunted through **13 096 decompiled functions** in three encodings — hex,
decimal, and the split `lis`/`ori` immediate pair. **Zero in all three.** No code writes that
value; it is computed. That killed a hypothesis which had consumed multiple sessions, and it
can never come back.

Search every encoding. `0x40030001` also lives as `1073938433`, and as `0x4003` + `1`.

## Classify Candidates Before Choosing

| Kind | Changes | Risk |
|---|---|---|
| **Root cause** | how the thing is classified/produced | touches everything of that class |
| **Surgical** | one decision point downstream | reversible, narrow |
| **Palliative** | validates before consuming | **stops the bleeding, leaves the bug** |

Palliatives — null-checks, bounds guards, masked asserts — are **diagnostic tools, never
fixes**. They may exist gated and OFF for comparison. A palliative accepted as a fix leaves the
object still classified wrong, and the next symptom appears somewhere unrelated.

## Give the Fix Teeth Before It Exists

Write the test that would fail if the fix is wrong, **before writing the fix**.

The strongest form is a predicate test with a **control set from measurement**:

```python
assert prune(0x40030001) is True      # the pathological record
assert prune(0xC0010001) is False     # the legitimate family
for tag in (3,4,9,15,16,17,18,20,21,23,25,32):   # 12 keys measured in-boot
    assert prune(tag_to_w0(tag)) is False        # must NOT prune healthy work
```

The negative set matters more than the positive one — it is what stops the fix from cutting
good code. Take it from measurement, and get the count right: a control list with one extra
member makes the test looser than the evidence allows.

## Maintain the Refuted List

Every session ends with **what is now impossible**. This is the compounding asset.

```markdown
## Refuted by in-boot measurement — do NOT chase again
1. Exponential recursion — depth max 2 (this_cur -1..0), 6 runs
2. Missing pop — enters=176, pops=175, empty_pops=0
3. Push/pop asymmetry — diff_mgr=0 across ~885M visits
4. hflags & 4 as the missing prune — no bit distinguishes the junk keys
5. 0x47D00000 as a valid reference — that was our own removed workaround
```

Without this list, session five re-investigates what session two eliminated. With it, the
funnel only narrows.

**Include your own scaffolding.** One "valid reference" turned out to be a pinned region
fabricated by a workaround *we* had added. Audit your own patches before blaming the system.

## Quick Reference

| Situation | Action |
|---|---|
| Several theories, no way to choose | Design the count that routes to different fixes |
| About to build a fix | Write the predicate test with a measured negative set first |
| Fix reports success | Add a step that disbelieves it and measures in-situ |
| Probe returns surprising numbers | Check the control first; a failed control voids everything |
| Hypothesis feels unfalsifiable | Search the constant in every encoding; absence closes it |
| Tempted by a null-check | That is a palliative. Gate it OFF, keep looking |
| Out of questions after several refutations | Delegate the wide search — use delegating-to-grok — and keep the deciding experiment |
| Session ending | Write what is now impossible |

## Common Mistakes

**Writing predictions after seeing results.** Then the probe was confirmatory and proves
nothing. Commit predictions before the probe exists.

**One run.** Determinism is itself a finding — three identical runs mean something a single run
cannot. And a run that differs tells you the state isn't what you assumed.

**Reporting the mechanism as the fix.** Naming the cause to the instruction is a real
deliverable, and it is not the same as fixing it. Say which one you have.

**Skipping the count because the answer seems obvious.** The obvious candidate was the root
cause; the count showed a one-line surgical filter sufficed and avoided touching every record
of that class.

**Letting a metric you cannot reach define success.** One acceptance criterion required a thread
that a code comment showed is only created *after* the main loop returns — likely unreachable
in a 90-second smoke. Read the acceptance path's own source before interpreting a red result,
and record a complementary honest signal.

## Real-World Impact

A PS3→C++ static recompiler, one wall, five sessions. The first four produced no measurement.
Then: predictions committed, one probe, and **99.96 % of the trace turned out to be a single
`for` loop that never terminates** — a walk reading `matrix[0][3]` (a float, `0.0f`) as a
linked-list head, because a deserializer forced a type field to `1` for a record whose real
subtag was `3`.

Static analysis and in-boot measurement agreed to the byte. The fix was one enumeration filter,
verified at **`no_rep = 0`** across 3 runs — after its first version was refuted by measurement
for having zero effect.
