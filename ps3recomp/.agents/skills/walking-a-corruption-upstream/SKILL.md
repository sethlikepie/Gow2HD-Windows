---
name: walking-a-corruption-upstream
description: Use when a crash, bad pointer, garbage value, or memory corruption surfaces far from its cause — the failing function looks broken but is only consuming something wrong — and each level you inspect looks like the answer
---

# Walking a Corruption Upstream

## Overview

A bad value surfaces where it is *used*, not where it is *produced*. Between the
two there may be many frames, each of which will look like the bug when you
arrive at it.

Measured: a symptom — 16 writes into uncommitted memory — traced up **nine
levels** to its origin. Every intermediate level presented a plausible defect, and
**five of them were written up as the root before being refuted**.

**Core principle: the level you are looking at is a victim until you have shown
it produces the bad value rather than passing it on.**

## When to Use

- A pointer, handle, or index is garbage at the point of use
- The failing function looks defensible once you read it
- You keep finding "the cause" and then finding another one behind it
- The value has structure (ASCII, a small integer, a near-miss address) — structure
  means provenance

**Not for:** a fault with a clean stack trace to a caller you can read, or a
reproducible unit-level failure.

## The Loop

At each level, one question: **does this frame produce the bad value, or receive
it?**

```
1. capture the bad value AND its input, at the same site, live
2. produce -> this is the level; stop and dig here
3. receive -> the level above is the target; repeat
```

Instrument **one call site**, not the shared callee. A callee has many callers and
often many probe instances; a call site is unique and lets you capture the argument
*and* the return of the same invocation on one line.

## Structure in the Value Is a Free Clue

Garbage that is uniform is uninformative. Garbage with **shape** names its source.

| observed | shape | what it meant |
|---|---|---|
| `0x2F725F70` | ASCII `/r_p` | a path string being read as a pointer |
| `0x726D612E` | ASCII `rma.` | the **same** string, 5 bytes further in |
| `5` | small integer | uninitialized memory, not a count |
| `≡ 14 mod 16` | misaligned | never a base for 16-byte copies |

Two values that are windows into one string are the same bug seen twice. Decoding
`0x2F725F70` as four characters cost one command and collapsed two investigations
into one.

**Check every encoding**: hex, decimal, ASCII, and as a split immediate pair.

## Count the Population at Every Level

Always ask *how many* — the ratio localizes the defect better than any single
observation.

| level | population | pathological |
|---|---|---|
| free-list pops | 8625 | 2 |
| allocations at one site | 237 | 1 |
| calls to one function | 13 | 1 |
| manager lookups | 238 | 1 |

A one-in-N defect with N large means the surrounding machinery is **fine** — do not
rewrite it. And comparing the healthy majority against the single outlier, field by
field, is what isolates the differing input:

```
237x  key=0x407…  tab=0x400C6210  target=0x0039D3C4  -> mgr=0x406387E0
  1x  key=0x40780850  tab=0x400C6210  target=0x0039D3C4  -> mgr=0x400C6B50
```

Same table, same target, different key. That single row eliminated "wrong dispatch"
and "wrong table" in one measurement.

## The Victim Looks Guilty — Check Whether It Decides Anything

Repeatedly, a level turned out to be a pass-through:

- a function that only calls an allocator and returns its result
- a loop whose termination was correct; the bad value came from the *other* branch
- a lookup that **did** handle its miss — so there was no miss

Before blaming a frame, ask: *if its input were right, would its output be right?*
If yes, it is a victim. Reading 20–70 lines answers this and is cheaper than a run.

## Read the Code, But Only After Measurement Names the Function

Two of the biggest jumps in the chain came from **reading 23 and 66 lines** — after
sixteen rounds of measurement. That order is not an accident:

- measurement said *which* function to read (from a captured return address)
- measurement said *what to look for* (a stride, a count, a field offset)

Reading earlier would have produced the same text with no idea which part mattered.
**Measurement does not replace reading; it aims it.**

And expect reading to refute the hypothesis that measurement suggested — it did,
twice.

## Do Not Fix the Victim

Every level offers a tempting local fix: reject the implausible pointer, guard the
null, bound the index at the point of use.

Those are **palliatives**. The object stays misclassified and the symptom moves.
Worse, they hide the levels above: a filter at the consumption site would have
masked both the missing registration *and* the string in low memory, and the
measured evidence for either would have disappeared.

**Fix only where the value is produced.** If you must ship a guard first, gate it
OFF and record that it is a diagnostic, not a fix.

## Quick Reference

| Situation | Action |
|---|---|
| Bad value at point of use | Capture input and output at the same call site, live |
| The frame looks broken | Ask if correct input would give correct output; if yes, it's a victim |
| Garbage value | Decode as ASCII, decimal, split immediate — structure names the source |
| Two different garbage values | Test whether they are windows into one buffer |
| Found "the cause" | Count the population; 1-in-N means look above, not around |
| Tempted to guard at the use site | That hides the levels above. Gate it OFF or skip it |
| About to read the source | Good — if a measurement told you which function and what to look for |

## Common Mistakes

**Instrumenting the shared callee.** Many callers, many probe instances, ambiguous
field labels. Instrument the one call site.

**Treating "never initialized" as the root.** It may be that the object was never
*supposed* to be that type. A field that is zero because nobody wrote it looks
identical to a field that is zero because initialization failed — the distinguisher
is whether *anything* ever wrote it, including a zeroing pass.

**Believing a guard's semantics.** Code testing `field != 0` as "already
initialized" will lie when the field is a pointer set by something unrelated. Read
what actually writes it.

**Stopping at the first coherent story.** Five coherent stories were written and
refuted here. Coherence is cheap; a population count and a same-site capture are
not.

## Real-World Impact

A PS3→C++ static recompiler. Symptom: 16 writes through a garbage pointer, one run
in one. Nine levels up: an object whose class index selects an out-of-range slot in
a type table with no bounds check, returning allocator bookkeeping that the caller
consumes as a memory manager.

Five intermediate conclusions were published and retracted along the way. None was
caught by re-reading code — each fell to a population count, a same-site capture, or
a control that exercised the same regime as the measurement.
