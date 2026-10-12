# R9 — menu/game present schedule after thr (who re-arms?)

**Data:** 2026-07-23  
**Tipo:** diagnóstico (RE estática + enter pre/post R_Perm nos present menu + parents). **Sem** flip HLE forge.  
**Gate B:** GREEN · **Gate A:** RED (`SetFlip_after_R_Perm=0`)  
**Log canónico:** `/tmp/r9_menupresent.log`  
**Recipe:**
```bash
TIMEOUT=150 LOG=/tmp/r9_menupresent.log PS3_NO_RSX=1 PS3_TRACE_MENUPRESENT=1 ./rodar_gow2_menu_fast.sh
# TIMEOUT=90 pode cortar no CE03C wait-idle se o VT/EOS demorar — preferir 150 neste recipe
python3 count_menu_gate.py /tmp/r9_menupresent.log
# SUMMARY: rg MENUPRESENT /tmp/r9_menupresent.log | tail
```

Prior: R8 hot intro `CE0A0→CDBA4…→CDD88→156680` ~41k **post=0**; menu parents
`194FFC`/`2B21*` tot=0; `2C07F8` tot=2 pre-only.

---

## Probe shipped

| Artefacto | Função |
|-----------|--------|
| `recomp_mid_v2/patch_menu_present_schedule_probe.py` | Idempotente; inject entry + helpers (000–003) |
| Gate | `PS3_TRACE_MENUPRESENT=1` (first char `'1'`, default OFF) |
| post window | `g_ps3_rperma_full` (movie_hle) |
| Log | `[MENUPRESENT] probe armed` · enter cap · `SUMMARY` atexit/SIGTERM |

Sites (`recomp_macos_v2`):

| id | fn | chunk | papel |
|---:|----|-------|-------|
| 0 | `func_00194FFC` | 000 | menu present → 156680 (loop sleep 0x1F4) |
| 1 | `func_002B21C4` | 001 | frame present entry → optional 156680 |
| 2 | `func_002B21F8` | 002 | frag de 2B21C4 |
| 3 | `func_002B2288` | 002 | frag de 2B21C4 |
| 4 | `func_002C07F8` | 001 | movie FSM state 11 (present no miolo) |
| 5 | `func_000BB4E0` | 000 | **único** caller directo de 194FFC |
| 6 | `func_000BB424` | 000 | jump-table FSM (obj+0xC) → casos BB4* |
| 7 | `func_000B61F4` | 003 | caller de BB424 |
| 8 | `func_002B25AC` | 001 | único tramp estático p/ 2B21C4 (EA atrás) |
| 9 | `func_002C0508` | 001 | pump movie FSM → JT state 11 = 2C07F8 |
| 10 | `func_000CE03C` | 001 | intro 2nd Play + pump 2C0508 |
| 11 | `func_000CE0A0` | 000 | nest intro present (confirm 1×) |
| 12 | `func_000B71B8` | 000 | boot body → CE0A0 |
| 13 | `func_002B2E74` | 001 | parent de B71 (1×) |
| 14 | `func_0025C838` | 000 | parent de 2B2E74 |

---

## Caller map (RE estática)

### Menu present targets

| Target | Callers estáticos | Notas |
|--------|-------------------|-------|
| **194FFC** | `BB4E0` only (`func_00194FFC(ctx)`) | BB4E0 é caso do JT de **BB424** (também fallthrough morto após BB4D0→BB49C) |
| **2B21C4** | `2B25AC` only (tramp **para trás** pós-epílogo) | **sem** caller externo real; frags 2B21F8/2B2288 internos |
| **2B21F8** | `2B22EC` (ramo de 2B21C4) | |
| **2B2288** | `2B22E4` (ramo de 2B21C4) | |
| **2C07F8** | JT state **11** via **2C0508** (`TOC-0x10F4`); fallthrough morto de 2C07D8 | base JT `0x2C0598+0x260` |

### Schedule parents (cadeia)

```
# path menu 194FFC (NUNCA in-boot neste recipe)
B94EC → B6150 → B61F4 → BB424 [JT obj+0xC] → … → BB4E0 → 194FFC → 156680

# path frame 2B21* (NUNCA in-boot)
2B25AC ⇢ 2B21C4   (único link; suspeito fallthrough-back)
  (OPD/vtable entry de 2B21C4: sem cast estático no lift)

# path movie present 2C07F8 (2× cedo, só pre thr)
10230 → 10354 → 25C838 → 2B2E74 → B71B8 → CE0A0 (nest intro ~flips)
                         B71… → CE03C → 2C0508 ×N → [st=11] 2C07F8
```

`2C0508` também é pumpado no wait-idle de CE03C (intro movie #1→idle).

---

## Smoke metrics (menu-fast, TIMEOUT=150, NO_RSX)

| Métrica | Valor |
|---------|------:|
| R_Perm full (20169344) | 1 |
| thr_auto_load end | 1 |
| PARK / FATAL | 0 / 0 |
| SetFlip_total | 22566 |
| **SetFlip_after_R_Perm** | **0** |
| Pad_after_R_Perm | 0 |
| ICALL_BAD_after | 12 |
| Gate B | **GREEN** |
| Gate A | **RED** |

---

## Contagens enter (SUMMARY final, `rperma_full=1`)

| fn | tot | post | pre | Papel |
|----|----:|-----:|----:|------|
| **2C0508** | **6147** | **0** | 6147 | pump movie FSM (CE03C wait + body) — **só pre** |
| **2C07F8** | **2** | **0** | 2 | state 11 smlogo — **não** re-arm |
| CE03C | 1 | 0 | 1 | 1× intro |
| CE0A0 | 1 | 0 | 1 | nest intro 1× |
| B71B8 | 1 | 0 | 1 | boot 1× |
| 2B2E74 | 1 | 0 | 1 | parent B71 1× |
| 25C838 | 1 | 0 | 1 | parent 2B2E74 1× |
| 194FFC / BB4E0 / BB424 / B61F4 | 0 | 0 | 0 | **never** |
| 2B21C4 / 2B21F8 / 2B2288 / 2B25AC | 0 | 0 | 0 | **never** |

`grand=6154` ≈ 6147 + 2C07F8×2 + oneshots. **Zero** enter pós R_Perm em **qualquer** site.

---

## Hipóteses (kill table)

| H | Claim | Veredito | Evidência |
|---|-------|----------|-----------|
| **H1** | Callers nunca agendados pós thr (enter=0 post) | **VIVA / confirmada** | todos `post=0`; menu path tot=0; 2C0508 só pre |
| **H2** | Callers entram mas early-return antes de 156680 | **DEAD** p/ menu path | sem enter ⇒ sem early-return a provar; 2C07F8 pre só (já R8) |
| **H3** | TYPE15 f4=0 bloqueia caller | **co-sinal, não sole** | TYPE15 CLOSE-PRESERVE/attach **correm** pós R_Perm (linhas ~25263+) sem nenhum MENUPRESENT post; main em TYPE15/CC9D0 **sem** re-chamar present parents |
| **H4** | Skip/breaker nosso no path | **DEAD** | nenhum `patch_*.py` skip em B61/BB424/2B21/194FFC/2C07F8/25C838; probes gated OFF default |

**Sem fix de re-arm nesta task** (nenhum gate nosso errado claro).

---

## Interpretação

1. **Intro present** (R8) morre na saída de CE0A0; **movie pump** `2C0508`/`2C07F8` morre com o fim do player (st→0) **antes** de thr — nunca reentra com `rperma_full=1`.
2. **Path menu** `B61F4→BB424→BB4E0→194FFC` e **path frame** `2B21*` estão **mortos in-boot** neste recipe (tot=0). Não há “early-return pós thr” — **nunca são agendados**.
3. Cadeia thr-adjacente `25C838→2B2E74→B71` é **one-shot** (tot=1 post=0). Após return, o main **não** re-chama essa cadeia nem o menu FSM.
4. Gate A continua RED; wall = **quem deveria schedule** o parent de present **depois** de thr (não o leaf 156680).

---

## R10 (próximo hard gate)

**Quem agenda o parent de menu/frame present após thr / product TYPE15?**

Prioridade RE/probe:

1. **Pós-return de `25C838` / `2B2E74` / `B71`:** o que o main/thread faz a seguir (callers de `10354` / reentrada `10230` / thread entry AUTO_LOAD residual).
2. **Arm do path `B61F4` / `BB424`:** quem chama `B6150`/`B94EC` e o que escreve `obj+0xC` do JT menu — **nunca** visto in-boot.
3. **Entry real de `2B21C4`:** OPD/vtable (sem call estático); correlacionar com product f4 / mode bits TOC-0x14EC.
4. **Não** reabrir 17ACC / SetFlip HLE. **Não** promover probes a default.

Aceite R10: identificar o scheduler (fn+cond) que **deveria** re-chamar present pós thr, com enter probe pre/post; se H2 no scheduler, branch probe ≤20 linhas.

---

## Commits

- gow2-recomp: `diag(gow2): R9 MENUPRESENT schedule parents pre/post R_Perm`
- ps3recomp: report task-R9 + (opcional) INDEX one-liner
