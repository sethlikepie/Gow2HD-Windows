# TYPE15 CB56C skip_icall2 → CC9D0 spin: attach forced, spin persists (refuted)

## Goal
Classify item 4 of `2026-07-22-2nd-movie-st620-wall.md` ("TYPE15 CB56C skip_icall2=1
— shell reuse, not natural icall2") and its claimed causal link to item 2 ("post-thr
st620 idle") via the `CC9D0` spin (`f4=f54=0` forever, see
`2026-07-22-autoload-savedata-guestptr-crash.md` tail and
`2026-07-22-b71-body-residual.md`).

**Proof:** three in-boot runs, `env_gow2.sh` + `PS3_AUTO_LOAD_RUN=1 PS3_NO_RSX=1
PS3_TRACE_POSTINTRO=1 PS3_B71_FULL_393E0=1 PS3_TRACE_FACTORY=1 PS3_TRACE_CC9D0=1`
(baseline), plus `PS3_TYPE15_FORCE_ICALL2=1` and the new `PS3_TRACE_2A4FE4=1`
(diagnostic-only, default OFF, added this session).

## Baseline (existing probes only, no code change)
Both `func_000CB56C` calls in the post-B71 loop (`this=0x4066D798`, `this=0x4066D804`,
stride `0x6C`, from `func_000CB790`) hit the TYPE15 reuse shortcut and share **the
same** shell product:

```
[TYPE15] CB56C reuse product hdr=0x42F85AE0 prod=0x42F85AE4 (skip icall2; was_shell=1)
[TYPE15] CB56C skip icall2 (shell/reuse product=0x42F85AE4)
[TYPE15] CB56C skip 2A4FE4 (shell product)
[POSTINTRO] enter func_002B7188 #1 (post-B71 chain)
[CC9D0] iter=0 this=0x4066D798 f54=0 f4=0 (prev_this=0xFFFFFFFF)
[CC9D0] iter=1 this=0x4066D804 f54=0 f4=0 (prev_this=0x4066D798)
```
`CC9D0` alternates between the two objects forever (`f54=0` is written
unconditionally by `CB56C` itself at `+0x54` before any construct/attach — not
evidence of a broken attach). `func_002B7188` (the caller) was entered **22380×** in
a 45s window in a later run — this is the "main thread pegged at 100%" symptom the
existing `PS3_TRACE_CC9D0` doc-comment already predicted.

Side note (new, unrelated, not chased further): the **same** construct function
(`func_0039E794`) throws `[ppu] unresolved indirect call -> 0x4E800421` (0x4E800421
= the `bctrl` opcode itself — code read as a function pointer) once per run, always
for a **different** factory object (`this=0x400D6808`, type `d0=0x00000004`, the B71
`icallA` object, not TYPE15/`d0=0x80000015`). Not seen for TYPE15. Flag for whoever
picks up `b71-body-residual`/`factory15-393e0-desync-walld` — not investigated here.

## Experiment 1 — force real icall2 (`PS3_TYPE15_FORCE_ICALL2=1`)
`func_0039D428` (the real icall2 target, resolved via `tab[0x54]`) runs to completion
cleanly on the reused product, no hang:
```
[FACTORY] 39D428 enter factory=0x47D00000 product=0x42F85AE4 vt=0x00516D70
[FACTORY] 39D428 leave r3=0x47D00800
[POSTINTRO] CB56C obj+8=product=0x42F85AE4 skip_icall2=0
```
But **`func_002A4FE4` (the second, non-virtual post-icall2 call) never returns** — no
`[POSTINTRO] CB56C after 2A4FE4` line, no second `func_000CB56C` invocation, whole
process silently wedged until killed at 45s. This matches the pre-existing code
comment ("002A4FE4 hangs on pin-shell products") — now localized to the *second*
call, not the vtable icall2.

## Experiment 2 — instrument the hang site (new probe, gated `PS3_TRACE_2A4FE4`)
Added `patch_2a4fe4_trace.py` (idempotent, default OFF, byte-identical when unset):
entry trace of `product`/`head_word`/`sentinel`, first 16 node visits, and a
200000-iteration bailout (diagnostic escape valve, explicitly **not** a fix — logged
as such).

```
[2A4FE4] enter product=0x42F85AE4 head_word=0x42F85F10 (sentinel=0x42F85B54)
[2A4FE4] node[0]=0x42F85F10 next=0x42F85FE8
[2A4FE4] node[1]=0x42F85FE8 next=0x42F860C0
...                                              (stride 0xD8, 12 real nodes)
[2A4FE4] node[11]=0x42F86858 next=0x00000000
[2A4FE4] node[12]=0x00000000 next=0x27182818
[2A4FE4] node[13]=0x27182818 next=0x00000000    (2-cycle: NULL <-> poison, forever)
[2A4FE4] LOOP-CAP hit at 200000 nodes (diagnostic bailout, NOT a fix) product=0x42F85AE4
```

**Root mechanism, verified:** `func_002A4FE4` walks `product+0x70` as a **circular**
intrusive list (loop terminates when `next == product+0x70`, the sentinel address
itself — that convention is confirmed by `patch_2a4fe4_trace.py`'s comment and by
`func_002A4FE4`'s own top-of-function early-return on an empty/self-pointing list).
The reused product's `+0x70` list is instead a **NULL-terminated, fixed-stride
(`0xD8`) array/pool with 12 real populated entries** — i.e. genuine leftover content
from whatever originally used product `0x42F85AE4` (it *did* construct naturally the
first time this session, see baseline `[TYPE15] post-construct ... product=0x42F85AE4`
before the shell/reuse path took over). Walking past the last real node hits `NULL`,
and dereferencing `NULL+0` in this VM returns a fixed poison word (`0x27182818` —
the same value seen elsewhere as `slot_val=0x27182818` for the *unrelated* `0x400D6808`
object, so it looks like a generic unmapped/guard-page read, not object-specific
garbage) whose own `+0` is `0`, so the walk 2-cycles `0 ↔ 0x27182818` forever without
ever reaching the sentinel.

## Experiment 3 — does forcing icall2 + capping 2A4FE4 unstick CC9D0? **No.**
With both `PS3_TYPE15_FORCE_ICALL2=1` and `PS3_TRACE_2A4FE4=1` (loop-cap lets the
process continue past the hang instead of wedging), **both** `func_000CB56C` calls
complete (icall1 → icall2 → 2A4FE4-capped) and the boot reaches `func_002B7188`/
`CC9D0` same as baseline — **`f4`/`f54` are still 0 forever**, same alternation
between `0x4066D798`/`0x4066D804`, same 100%-CPU spin. Neither `func_0039D428`
(icall2) nor `func_002A4FE4` was ever the writer of `this+0x4` — grep of `CB56C`'s
own body confirms no store to `+0x4` either (only `+0x54=0`, `+0x8=product`,
`+0x55=1`, and the 4×12-byte sub-array at `+0xC..+0x39` cleared).

## Verdict
**Item 4 (`skip_icall2`) does NOT explain item 2 (post-thr st620 idle via `CC9D0`
spin).** The causal chain codex proposed (`CB56C reuse → skip attach → CC9D0 f4=0
forever → st620 stuck`) is **refuted** by a discriminating experiment: forcing the
"missing" attach steps to run (one for real, one capped) does not change `CC9D0`'s
observed fields. `this+0x4` is written by some third function never reached in this
window, most plausibly gated by a **later** game-state transition (2nd movie / FSM
progress that item 2 already flags as separately blocked) rather than by construction
at all — i.e. `f4=0` may be the *correct* idle value for an object waiting on that
later trigger, not evidence of a broken attach. This reverses the priority order:
**item 2 (2nd movie / st620 arming) is upstream of the `CC9D0` symptom, not the other
way round.** Investing further in the TYPE15 reuse/attach path is not indicated by
this evidence; the `2026-07-22-wall-d-schedule-map.md` / 2nd-movie-arming angle is.

`func_002A4FE4`'s real hang (product `+0x70` NULL pool walked as circular) remains
real and reproducible, but is now known to be **orthogonal to the `CC9D0` idle
symptom** — it only manifests when `PS3_TYPE15_FORCE_ICALL2=1` is set (opt-in,
default OFF, not on the natural boot path).

## Next experiment (not this session)
Find what actually reads/writes `this+0x4` on the `0x4066D798`/`0x4066D804`-class
objects — grep OPD/vtable callers of the type at `tab[0x54]` beyond `CB56C`/`CC9D0`
(e.g. a per-frame tick reached only after a 2nd `cellVdec` `StartSeq`), and re-test
whether `f4` ever goes non-zero once item 2 (2nd movie arming, `movie_eos_arm.c`
WIP in this session's `git diff`) is confirmed working, before touching the
TYPE15 factory/freelist code again.

## Code sites
| Piece | Where |
|-------|-------|
| CB56C reuse/skip_icall2 decision | `recomp_macos_v2/ppu_recomp_000.cpp` `func_000CB56C` (~161600-161736) |
| CC9D0 idle spin + existing trace | `recomp_macos_v2/ppu_recomp_000.cpp` `func_000CC9D0` (~162861-163002), `PS3_TRACE_CC9D0` |
| icall2 real target | `recomp_macos_v2/ppu_recomp_001.cpp` `func_0039D428` (~256174) |
| 2A4FE4 hang site + new trace | `recomp_macos_v2/ppu_recomp_001.cpp` `func_002A4FE4` (~11490), `recomp_mid_v2/patch_2a4fe4_trace.py` (new, gated `PS3_TRACE_2A4FE4`) |
| Unrelated `bctrl`-as-pointer side finding | `func_0039E794`, `this=0x400D6808`, `d0=0x00000004` |
