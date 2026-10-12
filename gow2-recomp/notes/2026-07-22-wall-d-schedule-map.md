# Wall D schedule map — 00468C3C / vt+8 walk / OPD 534CB8 (2026-07-22)

## Goal
1. Who *should* call `00468C3C` / load walk OPD `0x522E70` (vt+8) after R_Perm  
2. Micro-ctor OPD `0x534CB8` → `0x32854C` still never?  
3. ICGLdr/SHADERSRC natural only if schedule moves (not hard pass here)

**Proof:** `{SCRATCH}/static_callers.txt`, `{SCRATCH}/wall_d_schedule_callers.log`, `{SCRATCH}/opd_534cb8.log`  
**Recipe:** natural post-R_Perm (FORCE/EOS for intro only); `PS3_TRACE_A1CHAIN=1 PS3_TRACE_TYMAP=1 PS3_TRACE_TYMAP_DUMP=1 PS3_TRACE_LDRSH=1 PS3_TRACE_MC=1`; **no** `PS3_GATE_FORCE` synthetic WAD stream, no CRC bypass, no SHADER_DEMO as pass.

---

## 1. Static schedule map

### Edges into `func_00468C3C` (guest `0x00468C3C`)

| Edge | Found? | Anchor |
|------|--------|--------|
| Direct `bl`/`func_` call | **yes** | only from micro-ctor family (below) |
| `func_00329490` → `00468C3C` | **yes** | `ppu_recomp_001.cpp` ~143682 |
| `func_00329658` → `00468C3C` | **yes** | `ppu_recomp_001.cpp` ~143800 |
| `func_003294F8` → `00468C3C` | **yes** | `ppu_recomp_005.cpp` ~135116 |
| `func_003296C0` → `00468C3C` | **yes** | `ppu_recomp_005.cpp` ~135208 |
| Other lift callers | **no** | `rg func_00468C3C` → only registry + these 4 sites |

### Upstream of those four (who *would* schedule 468C3C)

| Edge | Found? | Anchor |
|------|--------|--------|
| `func_0032854C` → `00329490` | **yes** | `001.cpp` ~143007; **root of A1 micro-ctor** |
| `func_00328B18` → `00329490` | **yes** | `001.cpp` ~143217 |
| `func_00328678` / `00328C40` → `00329490` | **yes** | `013.cpp` |
| `func_0032862C` / `00328BF8` → `00329490` | **yes** | `005.cpp` |
| `func_00329650` trampoline → `003294F8`/`00329658` | **yes** | `003.cpp` ~419294 (switch tail) |
| `func_00329818` trampoline → `003296C0` | **yes** | `003.cpp` ~419300 |
| **Callers of `func_0032854C`** | **none** | only `ppu_recomp_030` registry — **orphan** |

Search that proved orphan: `rg func_0032854C recomp_macos_v2 --glob '*.cpp'` → definition + registry only.

### Walk OPD `0x522E70` / `func_00171244` (typemap `vt+8`)

| Edge | Found? | Anchor |
|------|--------|--------|
| Direct lift call to `func_00171244` | **no** | only registry in `ppu_recomp_030` |
| `ps3_call_opd(..., 0x522E70)` in guest lift | **no** | only **GATE-FORCE** host helper in `ppu_loader.cpp` |
| BE immediate `0x00522E70` in EBOOT | **1×** | `file+0x5030c0` (OPD/vtable slot cluster, not a `lis` site) |
| Live typemap vtable | **yes** | `vt=0x5130B8`, **`vt+8=0x522E70`** (in-boot TYMAP-DUMP) |
| Alternate OPD for walk code `0x32E200` | **no** as OPD word | EBOOT BE `0x0032E200` count 0 |

**Conclusion static:** the *only* lift edges into `00468C3C` are the micro-ctor family rooted at **orphan** `0032854C` (OPD `0x534CB8`). The walk is only reachable via **indirect** OPD `0x522E70` (vtable slot); no second static entry to `171244`.

---

## 2. In-boot post-R_Perm (natural)

Log: `wall_d_schedule_callers.log` (probes in binary confirmed via `strings`).

| Probe | N | Meaning |
|-------|--:|---------|
| R_Perm FULL `20169344` | ≥1 | WAD delivered |
| **A1-468C3C** | **0** | hashmap set never entered |
| A1-30D54 / CMP-ENTER | 0 | replace/vcall path dead |
| **TYMAP-171** | **0** | walk never |
| **LDRSH** | **0** | ICGLdr natural never |
| **TYMAP-DUMP** | **1** | object **live** `vt+8=0x522E70` |
| B71 exit `+0x64` | 1 | non-regression |

```
[TYMAP-DUMP] base=0x00700DF8 slot=0x00835778
  comp=0x4306ADF0 tm=0x4306B160 vt=0x005130B8 vt+8=0x00522E70
```

**Criterion 2 branch used:** schedule N=0 for all mapped sites; typemap still built (**built-never-walked** reconfirmed).

---

## 3. Micro-ctor OPD `0x534CB8` → `0x32854C`

| Check | Result |
|-------|--------|
| Static: OPD content in EBOOT | **present** `file+0x524CB8` code=`0x32854C` toc=`0x541178` |
| Static: BE literal `0x00534CB8` | **0** (nobody loads OPD addr as imm) |
| Static: lift callers of `0032854C` | **0** |
| Runtime `[MC-32854C]` | **0** |
| Runtime `[MC-328B18]` / `[MC-329490]` | **0** |
| Runtime `[MC-SCAN]` heap/data for OPD/code | **hits=0** |
| Runtime `[OPD-ICG]` for 534CB8/32854C | **0** |

**Still never.** Orphan OPD + zero runtime registration outside `.opd`.

---

## 4. SHADERSRC / ICGLdr (not hard pass)

| Signal | N | Notes |
|--------|--:|-------|
| SHADERSRC log lines | 18 | Early/HOSTRES-era objects (e.g. N=2,16,24…); **not** walk/ICGLdr natural |
| LDRSH | 0 | No natural ICGLdr entry |
| GATE synthetic stream | 0 | Only log tags “GATE-FORCE R_PermA full flagged” / movie audio gate — **not** PS3_GATE_FORCE stream |

**Deferred:** natural SHADERSRC N>0 / LDRSH as Wall D green until schedule (2a) moves.

---

## Pass/fail table

| Criterion | Result |
|-----------|--------|
| 1 Static map with anchors | **PASS** |
| 2 In-boot schedule N≥1 **or** all 0 + live typemap | **PASS** (branch b: all 0 + TYMAP-DUMP) |
| 3 OPD 534CB8 still 0 | **PASS** |
| 4 No SHADERSRC-via-GATE/CRC/DEMO as acceptance | **PASS** |

---

## Single next gate

**Not** “find a missing `bl` to 0032854C” (static orphan).

**Gate:** a *different* natural path that either:

1. **Registers / calls OPD `0x522E70`** without going through 468C3C (e.g. component stream after menu FSM / second movie / scene load), **or**  
2. **Brings the micro-ctor family into the live graph** only if a later phase actually loads OPD `0x534CB8` into a callable slot (MC-SCAN would then hit ≠0).

Discriminate with: longer boot + pad/menu FSM if known; probe any new `ps3_call_opd` / vtable write of `0x522E70` post-B71 — **not** GATE-FORCE / CRC fill as acceptance.

---

## Non-regression

- R_Perm FULL 20169344  
- B71 `exit func_000B71B8 … +0x64=1`  
- TYPE15 REHOME path intact  
- Probes env-gated OFF by default  
