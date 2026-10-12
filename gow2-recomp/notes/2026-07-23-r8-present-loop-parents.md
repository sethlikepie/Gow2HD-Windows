# R8 — parents of `func_00156680` (intro present loop) pre vs post thr

**Data:** 2026-07-23  
**Tipo:** diagnóstico (enter pre/post R_Perm nos parents de 156680). **Sem** flip HLE forge.  
**Gate B:** GREEN · **Gate A:** RED (`SetFlip_after_R_Perm=0`)  
**Log canónico:** `/tmp/r8_presentloop.log`  
**Recipe:**
```bash
TIMEOUT=90 LOG=/tmp/r8_presentloop.log PS3_NO_RSX=1 PS3_TRACE_PRESENTLOOP=1 ./rodar_gow2_menu_fast.sh
python3 count_menu_gate.py /tmp/r8_presentloop.log
# SUMMARY: rg PRESENTLOOP /tmp/r8_presentloop.log | tail
```

Prior: R6 issuer = `2EFD60`/`2EFDA4` NID `0x21397818`; R7 path
`156680→14FE18→2EFD60` tot≈29k **post=0**; 2B2E74/B71B8 tot=1.

---

## Probe shipped

| Artefacto | Função |
|-----------|--------|
| `recomp_mid_v2/patch_present_loop_probe.py` | Idempotente; inject entry + helpers |
| Gate | `PS3_TRACE_PRESENTLOOP=1` (first char `'1'`, default OFF) |
| post window | `g_ps3_rperma_full` (movie_hle) |
| Log | `[PRESENTLOOP] probe armed` · enter cap · `SUMMARY` atexit/SIGTERM |

Sites (`recomp_macos_v2`):

| id | fn | chunk | cap |
|---:|----|-------|----:|
| 0 | `func_000CDBA4` | 000 | 4 |
| 1 | `func_000CE0A0` | 000 | 4 |
| 2 | `func_00194FFC` | 000 | 4 |
| 3 | `func_002B21C4` | 001 | 4 |
| 4 | `func_002C07F8` | 001 | 4 |
| 5 | `func_000CDC08` | 001 | 4 |
| 6 | `func_000CDCF8` | 001 | 4 |
| 7 | `func_000CDD00` | 001 | 4 |
| 8 | `func_000CDD88` | 001 | 8 |
| 9 | `func_002B21F8` | 002 | 4 |
| 10 | `func_002B2288` | 002 | 4 |

---

## Smoke metrics (menu-fast, TIMEOUT=90, NO_RSX)

| Métrica | Valor |
|---------|------:|
| R_Perm full (20169344) | 1 |
| thr_auto_load end | 1 |
| PARK / FATAL | 0 / 0 |
| SetFlip_total | 20125 |
| **SetFlip_after_R_Perm** | **0** |
| Pad_after_R_Perm | 0 |
| ICALL_BAD_after | 12 |
| Gate B | **GREEN** |
| Gate A | **RED** |

Último `SetFlipCommand` imediatamente **antes** de
`movieio open /wad/r_lglsca.wad_ps3` (linhas log ~22231 → 22234).

---

## Contagens enter (SUMMARY final, `rperma_full=1`)

| fn | tot | post | pre | Papel |
|----|----:|-----:|----:|------|
| **CDBA4** | **41555** | **0** | 41555 | body do loop intro (hot) |
| **CDC08** | **41555** | **0** | 41555 | frag tempo — 1:1 com CDBA4 |
| **CDD00** | **41555** | **0** | 41555 | frag movie_state==1 |
| **CDD88** | **41555** | **0** | 41555 | tail present → 156680 |
| **CE0A0** | **1** | **0** | 1 | nest do loop (1× via B71) |
| **2C07F8** | **2** | **0** | 2 | path cedo (smlogo); **não** re-arm |
| 194FFC | 0 | 0 | 0 | never |
| 2B21C4 | 0 | 0 | 0 | never |
| CDCF8 | 0 | 0 | 0 | never |
| 2B21F8 | 0 | 0 | 0 | never |
| 2B2288 | 0 | 0 | 0 | never |

`grand=166223` (≈ 4×41555 + CE0A0 + 2C07F8). **Zero** enter pós R_Perm em qualquer site.

### Tabela pre-WAD vs post thr / R_Perm

| fn | pre-WAD (≈tot, post=0) | post R_Perm | post thr |
|----|-----------------------:|------------:|---------:|
| CDBA4 / CDC08 / CDD00 / CDD88 | ~41555 | **0** | **0** |
| CE0A0 | 1 | 0 | 0 |
| 2C07F8 | 2 | 0 | 0 |
| outros parents | 0 | 0 | 0 |

Hot intro loop **confirmado** ≈ `CDBA4` chain (não CE0A0 re-entry; CE0A0=1).
Tot ≫ SetFlip (41555 vs 20125) — nem todo tick de body emite flip (rate / ramo).

---

## Interpretação

1. **Hot path intro (1:1 chain):**  
   `B71B8 → CE0A0 (1×) → loop { CDBA4 → CDC08 → CDD00 → CDD88 → 156680 → 14FE18 → 2EFD60 }`.
2. **post=0 em todos os parents** com `rperma_full=1` ⇒ nenhum parent reentra após
   R_Perm / thr. O loop **morre** no open Lgl e **não** é re-agendado.
3. **CE0A0 tot=1** confirma R7: o nest corre **uma** vez (dentro de B71 boot);
   o spin de ~40k é **interno** ao nest, não re-call de CE0A0.
4. **2C07F8 tot=2 post=0** — candidato estático de present fora do nest intro,
   mas só corre **cedo** (smlogo); **não** re-arma pós thr. 194FFC / 2B21* =
   dead in-boot neste recipe.
5. **Sem circuit-breaker nosso** nos parents: guest para de chamar ao sair do
   loop CE0A0. Gate A continua RED; **sem fix de re-arm nesta task**.

---

## RE estática — porquê o hot loop sai no WAD

### `func_000CE0A0` (nest)

```
obj = *[[TOC-0x5E34]]
if u8[obj+4] != 0: return          # intro skip/done
movie_slot = [TOC-0x5E70]
loop:
  func_000CDBA4()                  # body (→ 156680 via frags)
  if u8[obj+4] != 0: break
  func_00194D3C(); func_000CDE3C()
  if *movie_slot == 4: break       # movie idx terminal
return
```

**Guards de saída (guest):**
1. **`u8[obj+4] != 0`** — flag de intro terminada / saltada (já instrumentado
   por `PS3_TRACE_INTROSEQ`).
2. **`*(TOC-0x5E70) == 4`** — índice/estado de filme atinge 4.

Quando FORCE SEQDONE / EOS fecha o movie e o FSM avança, um destes guards
sai do loop; B71 **não** re-chama CE0A0 — segue `func_00040090` + stream WAD
(`func_002508E4` ×4) e depois o main fica em TYPE15/CC9D0 sem present.

### `func_000CDBA4` + frags (hot body)

- Entrada: time-gate; se `last >= now` → trampoline `CDDDC → CDC08` (**sempre**
  neste smoke: CDBA4 tot == CDC08 tot).
- Movie state `*[TOC-0x5E70]==1` → `CDE1C → CDD00`.
- Se `obj+0x24 != 0` → `CDDE4 → CDD88` → `156680` (sempre neste smoke: CDD00==CDD88).
- Path residual directo em CDBA4/CDC08 para 156680 quando +0x24==0 (não visto).

### Outros parents (não hot / dead)

| fn | RE | In-boot |
|----|----|---------|
| `194FFC` | loop while `u8[obj+4]==0` + sleep 0x1F4 + 156680 | **0** |
| `2B21C4` / frags `2B21F8`/`2B2288` | frame path bitflags + optional 156680 | **0** |
| `2C07F8` | big frame/pump com 156680 no miolo | **2** cedo, **0 post** |

**Conclusão de guard:** o “corte no WAD” é **saída natural do nest CE0A0**
(flag/movie idx), **não** um branch morto dentro de 14FE18/2EFD60. Falta
**re-arm** de um path de present (menu/game frame) **após** thr — nenhum dos
parents estáticos de 156680 o faz neste boot.

---

## R9 (próximo)

Quem **deveria** re-chamar present **após** thr?
- RE call sites de `CE0A0` / `CDBA4` / `2C07F8` / `2B21C4` **pós** B71 product
  attach / AUTO_LOAD end.
- f4 writers TYPE15 (co-sinal M2) — product incompleto pode ser o que impede
  o schedule do frame path 2B21*/2C07F8.
- **Não** forjar SetFlip HLE. **Não** reabrir 17ACC.

---

## Commits

- gow2-recomp: `diag(gow2): R8 present-loop parents of 156680 pre/post R_Perm`
- ps3recomp: report task-R8 + INDEX (sem change runtime)
