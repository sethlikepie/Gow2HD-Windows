# Wall D verdict (natural post-R_Perm) — 2026-07-22 goal run

## Recipe
See `intro_wad_env.txt` + traces: `PS3_TRACE_A1CHAIN=1 PS3_TRACE_TYMAP=1
PS3_TRACE_LDRSH=1 PS3_TRACE_TYMAP_DUMP=1`. No `PS3_GATE_FORCE` synthetic stream,
no CRC bypass, no `PS3_SHADER_DEMO` as pass. Log: `registry_natural.log`.
Counts: `registry_counts.txt`.

## In-boot counts (this run)

| Probe | N | Meaning |
|-------|--:|---------|
| R_Perm full | ≥1 | bytes_read=20169344 |
| WADLD-BODY | 1 | first body member (SBP_) |
| **A1-468C3C** | **0** | `func_00468C3C` never entered |
| A1-30D54 | 0 | downstream of 468C3C |
| **CMP-ENTER** | **0** | `func_0032E200` never |
| **TYMAP-171** | **0** | walk `func_00171244` never |
| **LDRSH** | **0** | ICGLdr natural never |
| TYMAP-DUMP | 1 | object dump once |
| Default.ps3fx | ≥1 | empty-registry residual (not acceptance) |
| GATE synthetic | 0 | natural path |

## TYMAP-DUMP (natural, no forge)

```
[TYMAP-DUMP] base=0x00700DF8 slot=0x00835778
  comp=0x4306ADF0 tm=0x4306B160 vt=0x005130B8 vt+8=0x00522E70
```

Typemap object **built** with correct walk OPD `0x522E70` (= `func_00171244`).
**Built-never-walked (H3) reconfirmed.**

## Static (lift, faithful)

In `func_00468C3C` (ppu_recomp_001.cpp):
- `ctx->gpr[30] = (int64_t)(int32_t)(0);`
- `vm_write32(ctx->gpr[1] + 0x7C, ctx->gpr[30]);`
Even if 00468C3C ran, the insert path stores null at sp+0x7C so the
`0032DF98`/`0032E200` replace/vcall path stays dead for new entries
(prior diagnosis; not a mis-lift of a live branch).

## Criterion 2 branch used

Walk natural = 0. **Diagnostic branch of goal criterion 2** (not registry green).

### Reconfirm / refute closed walls
| Claim | Status this run |
|-------|-----------------|
| dead `li r30,0` in 00468C3C | **Reconfirmed** (source) |
| 0032E200 never | **Reconfirmed** (CMP-ENTER=0) |
| walk never | **Reconfirmed** (TYMAP-171=0) |
| typemap missing | **Refuted** (TYMAP-DUMP live) |

### Next single concrete gate (with anchors)

**Gate:** natural *schedule* of the micro-ctor / hashmap-set family that would
enter `00468C3C` (or any other site that vcalls `vt+8` / OPD `0x522E70`).

| Evidence | Anchor |
|----------|--------|
| Entry root 0 | `[A1-468C3C]=0` in `registry_natural.log` after R_Perm full |
| Micro-ctor OPD dead (prior) | notes `2026-07-22-opd-534cb8-dead.md` (OPD 534CB8→32854C never) |
| Object ready | TYMAP-DUMP vt+8=0x522E70 |

**Not** the next experiment: CRC table host fill, combination stub, synthetic
GATE-FORCE stream as acceptance.

**Related but separate:** post-SBP WADLD hang (`need=0x687DD790` / freelist
guard) blocks later menu phase (H1) but **does not explain** walk=0 during
TOC/SHGX expand already completed before that hang — A1 chain was already 0
with R_Perm full + SHGX expand (notes `2026-07-22-a1-chain-inboot.md`).

## Criterion 3
N/A this iteration (walk still 0; diagnostic branch of criterion 2 used).
Must not claim registry fixed.

## Honesty
No forge of walk/CRC. Probes env-gated. This does **not** claim Wall D green.
