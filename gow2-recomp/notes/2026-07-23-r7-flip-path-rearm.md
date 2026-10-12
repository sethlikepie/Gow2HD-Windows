# R7 — present re-arm: enters no path real de flip (2EFD60 / 14FE18 / B71)

**Data:** 2026-07-23  
**Tipo:** diagnóstico (enter pre/post R_Perm no issuer R6). **Sem** flip HLE forge.  
**Gate B:** GREEN · **Gate A:** RED (`SetFlip_after_R_Perm=0`)  
**Log canónico:** `/tmp/r7_flippath.log`  
**Recipe:**
```bash
TIMEOUT=90 LOG=/tmp/r7_flippath.log PS3_NO_RSX=1 PS3_TRACE_FLIPPATH=1 ./rodar_gow2_menu_fast.sh
python3 count_menu_gate.py /tmp/r7_flippath.log
# SUMMARY: grep FLIPPATH /tmp/r7_flippath.log | tail
```

Prior: R6 — issuer real = NID `0x21397818` `_cellGcmSetFlipCommand` via
`func_002EFD60` / frag `func_002EFDA4`, parent `func_0014FE18` ← … ←
`func_002B2E74` (B71). R5 refutou `func_00017ACC` (0 enters).

---

## Probe shipped

| Artefacto | Função |
|-----------|--------|
| `recomp_mid_v2/patch_flip_path_enter_probe.py` | Idempotente; inject entry + helpers |
| Gate | `PS3_TRACE_FLIPPATH=1` (first char `'1'`, default OFF) |
| post window | `g_ps3_rperma_full` (movie_hle) — sem change em ps3recomp |
| Log | `[FLIPPATH] probe armed` · enter cap · `SUMMARY` atexit/SIGTERM |

Sites (`recomp_macos_v2`):

| id | fn | chunk | cap |
|---:|----|-------|----:|
| 0 | `func_0014FE18` | 000 | 8 |
| 1 | `func_00156680` | 000 | 4 |
| 2 | `func_000B71B8` | 000 | 4 |
| 3 | `func_002B2E74` | 001 | 8 |
| 4 | `func_002EFD60` | 001 | 8 |
| 5 | `func_002EFDA4` | 002 | 8 |

---

## Smoke metrics (menu-fast, TIMEOUT=90, NO_RSX)

| Métrica | Valor |
|---------|------:|
| R_Perm full (20169344) | 1 |
| thr_auto_load end | 1 |
| PARK / FATAL | 0 / 0 |
| SetFlip_total | 3812 |
| **SetFlip_after_R_Perm** | **0** |
| Pad_after_R_Perm | 0 |
| ICALL_BAD_after | 12 |
| Gate B | **GREEN** |
| Gate A | **RED** |

Último `SetFlipCommand` na linha **antes** de `movieio open /wad/r_lglsca.wad_ps3`
(confirmado timeline no log).

---

## Contagens enter (SUMMARY final, `rperma_full=1`)

| fn | tot | post | pre | Fase |
|----|----:|-----:|----:|------|
| **2EFD60** | **28938** | **0** | 28938 | pre-WAD only (hot intro flip) |
| **14FE18** | **28938** | **0** | 28938 | 1:1 com 2EFD60 |
| **156680** | **28938** | **0** | 28938 | 1:1 parent de 14FE18 |
| **2EFDA4** | **1794** | **0** | 1794 | só path grow → import |
| **2B2E74** | **1** | **0** | 1 | uma vez no boot (não “hot” re-entry) |
| **B71B8** | **1** | **0** | 1 | uma vez; loop present aninhado em CE0A0… |

`grand=88610` (≈ 3×28938 + 1794 + 2). **Zero** enter pós R_Perm em qualquer site.

### Tabela pre-WAD vs post thr / R_Perm

| fn | pre-WAD (tot, post=0) | post R_Perm | post thr |
|----|----------------------:|------------:|---------:|
| 2EFD60 | 28938 | 0 | 0 |
| 14FE18 | 28938 | 0 | 0 |
| 156680 | 28938 | 0 | 0 |
| 2EFDA4 | 1794 | 0 | 0 |
| 2B2E74 | 1 | 0 | 0 |
| B71B8 | 1 | 0 | 0 |

---

## Interpretação (gate step 6)

1. **Path real de flip prova R6 in-boot:** `156680 → 14FE18 → 2EFD60` (e ocasionalmente
   `2EFDA4` após grow) com tot ≫ 0 e correlação 1:1 com SetFlip pré-WAD.
2. **post=0 em 2EFD60** com `rperma_full=1` ⇒ o path **não** reentra após R_Perm.
   Não é falha dentro de `2EFD60` (buffer id / grow) no pós-load: **nunca entra**.
3. **2B2E74 / B71B8 tot=1** ⇒ o “mainloop B71” **não** é um spin reentrante de
   `func_002B2E74`. É **uma** chamada de boot; o present hot fica no loop aninhado
   (`func_000CE0A0` → … → `func_00156680`) durante a intro. Pós thr o main segue
   vivo em TYPE15/CC9D0 (R3/M), **sem** re-chamar B71 nem 156680/14FE18.
4. **Não há circuit-breaker nosso** em 2EFD60/14FE18/156680 (nenhum patch de skip
   de CF nesses sites; HLE só responde a calls guest). Corte = guest **para de
   chamar** no open `R_LglScA` (modo intro-present → asset-load) e **não re-arma**
   present pós thr — root class R3/R6 **confirmada** no path real.
5. Gate A continua RED. **Sem fix de re-arm nesta task** (não forjar flip).

### RE estática (≤20 linhas de leitura)

- `func_00156680`: lê ptr TOC-0x3CA4 → `func_0014FE18`.
- `func_0014FE18`: `func_0014A478` + `func_002F0EEC` + **`func_002EFD60(r3=gcm, r4=bufid u8@obj+0x1A3)`**.
- `func_002EFD60`: se `r4≥7` → erro `0x802100FF` (`2EFDF8`); se grow precisa →
  `2EFDD4` → icall → `2EFDA4` → import `004B9818` → HLE `_cellGcmSetFlipCommand`.
- `func_002B2E74`: sequência linear de subcalls incl. **uma** `func_000B71B8`; sem loop.
- `func_000B71B8`: boot sequence; `func_000CE0A0` (intro FSM loop com guard
  `u8[obj+4]` / idx filme) é o ninho onde o present roda milhares de vezes.

**Gate que “pára” flip:** saída do loop de intro/present **antes** do re-agendar
de frame/menu — **não** um branch errado nosso dentro de 14FE18/2EFD60.

---

## R8 (próximo)

Quem deveria **re-chamar** `156680`/`14FE18` (ou path menu equivalente) **após**
thr / product TYPE15 completo? RE callers de `func_00156680` fora do ninho CE0A0
+ disc: product f4 / schedule pós-AUTO_LOAD chama algum present?

**Não** reabrir 17ACC. **Não** forjar SetFlip HLE.

---

## Commits

- gow2-recomp: `diag(gow2): R7 flip-path enter pre/post R_Perm (2EFD60/14FE18)`
- ps3recomp: report task-R7 apenas (sem change runtime)
