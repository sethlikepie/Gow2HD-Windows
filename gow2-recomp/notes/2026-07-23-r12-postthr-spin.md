# R12 — post-thr spin: `2B2DD0 → B951C → CC9D0` (leave toward present re-arm)

**Data:** 2026-07-23  
**Tipo:** RE estática + probe in-boot (`PS3_TRACE_POSTTHR=1`). **Sem** flip forge / CC9D0-SKIP.  
**Gate B:** GREEN · **Gate A:** RED (`SetFlip_after_R_Perm=0`)  
**Log canónico:** `/tmp/r12_postthr2.log`  
**Recipe:**
```bash
python3 recomp_mid_v2/patch_postthr_spin_r12.py   # idempotent; after patch_postthr_pc_probe.py
./build_macos.sh
TIMEOUT=90 LOG=/tmp/r12_postthr2.log PS3_NO_RSX=1 \
  PS3_TRACE_POSTTHR=1 ./rodar_gow2_menu_fast.sh
rg 'POSTTHR-DISC|POSTTHR-ICALL|SUMMARY fn=(2B2DD0|B951C|CC9D0)' /tmp/r12_postthr2.log
python3 count_menu_gate.py /tmp/r12_postthr2.log
```

Prior R11: post-thr sample 100% em {CC9D0,2B2DD0,B951C}; menu arm OPD orphan.

---

## 1) RE — o que é cada função

### `func_002B2DD0` — **outer frame tick unit**

```
func_002B2DD0:
  func_002B2660()
  func_000B951C()     // scene / system half
  func_002B7188()
```

Chamado em loop por `func_002B2E04` enquanto `*( *(TOC-0x1488) ) != 0` (contador de
work). É o **pai imediato** do half B951C; **não** é um leaf TYPE15.

### `func_000B951C` — **scene tick half** (não menu arm)

```
func_000B951C:
  r3 = *(*(TOC-0x6268))
  func_000CCFF0(r3)          // scene walk → CD0F0 → CC9D0 ×2 (slots stride 0x6C)
  func_00040CD4()
  // vtable icall slot +0x40  (table[index])
  func_000B9204()            // mode/UI checks; NÃO chama B6150 / present
  // vtable icall slot +0x44
  return 0
```

**Não** há aresta estática para `B6150` / `B94EC` / `2EFD60` / SetFlip.  
`func_000B9204` só consulta estado (`+0x1B8` enum) e helpers `B6440`/`B6714`/`B38B0`.

### Cadeia TYPE15 / scene

```
2B2E04 (while count>0)
  └─ 2B2DD0
       └─ B951C
            └─ CCFF0 → CD0F0 → CC9D0(obj_slot0) + CC9D0(obj_slot1)
                 (também CCE34 → CC9D0×2 noutro ramo do walker)
```

Ratio medido post thr: **CC9D0 : 2B2DD0 : B951C = 2 : 1 : 1** — prova dinâmica de
que 2B2DD0 é o **parent** da walk de 2 componentes TYPE15 por tick.

Os 2 `this` de CC9D0 continuam `0x4066D798` / `0x4066D804` (factory CB56C).

---

## 2) Probe shipped

| Artefacto | Função |
|-----------|--------|
| `recomp_mid_v2/patch_postthr_spin_r12.py` | R12 disc + ICALL + gate alias |
| Gate | `PS3_TRACE_POSTTHR=1` **ou** `PS3_TRACE_POSTTHR_PC=1` (first char `'1'`) |
| Counts | herda R11 `[POSTTHR] SUMMARY` tot/post/pre |
| `[POSTTHR-DISC]` | ≤20 linhas: known TYPE15 f4/f54/prod/child_head em 2B2DD0/B951C/CC9D0 |
| `[POSTTHR-ICALL]` | ≤8 linhas: ctr dos 2 `ps3_indirect_call` em B951C |

OFF default; no-op no baseline.

---

## 3) Contagens in-boot (menu-fast, TIMEOUT=90, NO_RSX)

Gate: thr_end=1, R_Perm full=1, CLOSE-PRESERVE nodes=12, SetFlip_after=**0**, Gate A **RED**.

### SUMMARY final (representativo)

| fn | tot | post | pre | Nota |
|----|----:|-----:|----:|------|
| **CC9D0** | ~9.38M | ~9.38M | ~2.9k | 2 objs/tick |
| **2B2DD0** | ~4.69M | ~4.69M | ~1.4k | outer parent |
| **B951C** | ~4.69M | ~4.69M | ~1.4k | 1:1 com 2B2DD0 |
| **B6150 / B94EC / B61F4** | **0** | 0 | 0 | menu arm still never |
| CDBA4 | ~66k | **0** | all | intro pre-only |
| CB56C | 2 | 0 | 2 | factory oneshot |

### DISC (≤20)

- `f4=0x00000000` e `f54=0x00` em **todas** as linhas (set único `{0}`).
- Sem progresso de f4/list growth observado no budget (coerente M1/M2 residual).
- (Orçamento esgotou-se em torno de R_Perm/attach pré thr_end neste run; f4 residual
  pós thr já estabelecido por `PS3_TYPE15_DISC` / R11.)

### ICALL B951C (≤8)

```
[POSTTHR-ICALL] slot=vt+0x40 ctr=0x27182800 r3=0x00000220
[POSTTHR-ICALL] slot=vt+0x44 ctr=0x27182800 r3=0x400C3D88
```

- **Único** ctr: `0x27182800` — **não** é `B6150`/`B61*`/`2EFD60` (fora do text guest útil).
- `r3=0x400C3D88` no slot +0x44 = **mesmo** objecto freelist do ICALL-BAD R1.
- **Conclusão:** B951C **não** tenta present/menu arm; os icalls caem em lixo/freelist
  (co-sinal R1, não re-arm).

---

## 4) Discriminadores

| Q | Veredito | Evidência |
|---|----------|-----------|
| 2B2DD0 é **parent** da walk CC9D0? | **SIM** | call tree estático + ratio 2:1:1 post thr |
| B951C tenta present / B61*? | **NÃO** | 0 enters B6150; ICALL ctr ≠ menu EA; sem aresta estática |
| f4 / list growth com CLOSE-PRESERVE? | **NÃO** (residual M2) | f4=0 set; nodes=12 freelist product, **não** UI work |
| Bug nosso no “gate” 2B2DD0/B951C? | **NÃO** | funções correm como tick outer; CF fiel; spin é produto TYPE15 incompleto |

---

## 5) Hipóteses

| H | Claim | Veredito |
|---|-------|----------|
| **H1** | 2B2DD0 parent de CC9D0 | **CONFIRMADA** |
| **H2** | B951C agenda present/B6150 | **DEAD** |
| **H3** | Bug de early-return / circuit-breaker em 2B2DD0/B951C | **DEAD** (sem nosso gate; path natural) |
| **H4** | Wall = TYPE15 tick incompleteness (f4=0) **dentro** do parent 2B2DD0 | **VIVA / PRIMARY** |
| **H5** | M2 CLOSE-PRESERVE nodes=12 já são UI work | **DEAD** | freelist/product nodes; f4 still 0 |

---

## 6) Fix?

**Nenhum** fix mínimo em 2B2DD0/B951C — não há bug de gate nosso aí.

**Wall documentado:** incompleteness do tick TYPE15 (product f4 writers / work instalado
nos 12 nodes) **dentro** do parent `2B2DD0→B951C`. Liga ao residual M2 (lista preservada
≠ UI work). Critério Gate A inalterado: `SetFlip_after_R_Perm≥1`.

**Não:** flip HLE, CC9D0-SKIP default, HEAP40 default, OPD forge B6150.

---

## 7) Wall next

**TYPE15 f4 writers / install de trabalho real** nos products (enum f4∈{1,5,6,…} no
fluxo natural) + discriminar o que os 12 nodes de CLOSE-PRESERVE **deveriam** fazer
no tick — **não** reabrir OPD B94EC (R11 DEAD) nem CE03C wait.

---

## Commits

- gow2-recomp: `diag(gow2): R12 post-thr spin 2B2DD0→B951C+CC9D0 (TYPE15 f4 residual)`
- ps3recomp: report + INDEX + Gate A synthesis R5–R12
