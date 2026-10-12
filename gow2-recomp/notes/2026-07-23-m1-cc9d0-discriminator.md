# M1 — Discriminador CC9D0 / TYPE15 pós-attach (2026-07-23)

**Tipo:** diagnóstico only (sem fix de produto — isso é M2).  
**Probe:** `recomp_mid_v2/patch_type15_cc9d0_disc.py` → lift local `recomp_macos_v2/ppu_recomp_000.cpp`.  
**Gate env (default OFF):** `PS3_TYPE15_DISC=1` (preferido).  
Também aceita `PS3_TRACE_CC9D0=1` para as linhas DISC, mas **não** use isso se quiser evitar o spam `[CC9D0] iter=…` do logger antigo.  
**Gate contract (Python `env_on` ≡ C inject):** ON iff first char is `'1'`; unset/empty/`0`/`false`/… = OFF.  
**Cap:** ≤20 linhas totais `[CC9D0-DISC]` + `[CB56C-DISC]` (`g_ps3_type15_disc_n`).  
**Offline:** `python3 recomp_mid_v2/test_trace_cc9d0_env.py` → PASS (`"0"`/unset/`false` = off; `"1"` = on).

## Recipe

```bash
# A — UNSTICK off + disc
TIMEOUT=90 LOG=/tmp/m1_a_unstick0.log PS3_NO_RSX=1 \
  PS3_TYPE15_UNSTICK=0 PS3_TYPE15_DISC=1 ./rodar_gow2_menu_fast.sh

# B — default menu-fast + disc (Gate B must hold)
TIMEOUT=90 LOG=/tmp/m1_b_default.log PS3_NO_RSX=1 \
  PS3_TYPE15_DISC=1 ./rodar_gow2_menu_fast.sh

# C — skip attach contrast + disc
TIMEOUT=90 LOG=/tmp/m1_c_skip_attach.log PS3_NO_RSX=1 \
  PS3_TYPE15_SKIP_ATTACH=1 PS3_TYPE15_DISC=1 ./rodar_gow2_menu_fast.sh
```

Kill por PID (script nativo). Logs em `/tmp/m1_*.log` (não commitados).

## Contagens

| Boot | thr_end | R_Perm full | attach=full | SKIP_ATTACH | DISC lines | pad | FATAL | notas |
|------|---------|-------------|-------------|-------------|------------|-----|-------|-------|
| **A** UNSTICK=0 | 1 | 1 | 2 | 0 | **20** | 0 | 0 | thr@~34s |
| **B** default | 1 | 1 | 2 | 0 | **20** | 0 | 0 | 1ª tentativa flaky st620 0→0; **retry** Gate B OK |
| **C** SKIP_ATTACH=1 | 1 | 1 | 0 | 2 | **20** | 0 | 0 | thr@~32s |

Gate **B** (thr_end≥1, R_Perm, FATAL=0, sem PARK): **não regrediu** no recipe default (boot B retry).  
Gate **A** (SetFlip/pad pós R_Perm): continua vermelho (esperado; M0).

## Quotes (evidência)

### Após attach (boot B default; A idêntico na forma)

```
[TYPE15] product list RESET prod=0x42F85AE4 was_head=0x42F85F10 -> circular empty (2A4FE4-safe)
[POSTINTRO] CB56C obj+8=product=0x42F85AE4 attach=full reused=1
[CB56C-DISC] obj=0x4066D798 prod=0x42F85AE4 reused=1 vt=0xE0150015 after_attach child_head=0x42F85B54
[CB56C-DISC] obj=0x4066D804 prod=0x42F85AE4 reused=1 vt=0xE0150015 after_attach child_head=0x42F85B54
```

Sentinel vazio circular: `prod+0x70 = 0x42F85AE4+0x70 = 0x42F85B54` == `child_head`.  
Lista **vazia** pós-RESET (e continua vazia após full attach / 2A4FE4).

### Spin CC9D0 (A/B/C — mesma forma)

```
[CC9D0-DISC] this=0x4066D798 f4=0x00000000 f54=0x00 prod=0x42F85AE4 vt=0xE0150015 child_head=0x42F85B54 parent=0x00000000
[CC9D0-DISC] this=0x4066D804 f4=0x00000000 f54=0x00 prod=0x42F85AE4 vt=0xE0150015 child_head=0x42F85B54 parent=0x4419999A
```

- `this` **só** `0x4066D798` / `0x4066D804` (orçamento 18 linhas CC9D0-DISC; nunca outro EA).
- `f4`/`f54` **sempre 0** (incl. boot A com `PS3_TYPE15_UNSTICK=0`).
- `prod=0x42F85AE4` (live reuse, **não** pin-shell `0x47D00800–0x47D00C00`).
- `vt=0xE0150015` (**não** classe shell `0x0020xxxx`).
- `CC9D0-SKIP` count = **0** em A/B/C (UNSTICK não salta o produto live).

### Contrast C (SKIP_ATTACH)

```
[TYPE15] CB56C SKIP_ATTACH product=0x42F85AE4 (legacy)
[CB56C-DISC] obj=0x4066D798 prod=0x42F85AE4 reused=1 vt=0xE0150015 after_attach child_head=0x42F85B54
```

Mesmo `child_head` vazio / `f4=0` / mesmos 2 `this` — full attach **não** diferencia o spin face a skip (ambos deixam lista vazia e campos idle).

## Tabela H1–H5

| H | Status | Evidence |
|---|--------|----------|
| **H1** child list empty after reset → tick never installs work | **LIVE (primary)** | `child_head=0x42F85B54 == prod+0x70` após RESET **e** após attach; `was_head=0x42F85F10` existia **antes** do sanitize e foi esvaziado; 2A4FE4 em lista vazia é no-op |
| **H2** f4/f54 never written by real guest (only UNSTICK) | **DEAD as sole** | UNSTICK=0 (boot A) → ainda `f4=0 f54=0`; UNSTICK **não escreve** f4 — só skip pin-shell (e pin-shell nem entra no hot path). Residual: campos ficam 0 pós-reset CB56C (sintoma de H1/falta de install, não causa UNSTICK) |
| **H3** parent scene list only two shell objs forever | **LIVE (co)** | `this` únicos forever: `0x4066D798`/`0x4066D804` (2 componentes TYPE15; produto partilhado live — não pin-shell) |
| **H4** giant-lock/yield starves pad/flip | **DEAD** | `CC9D0-SKIP=0`; yield UNSTICK não engata no produto live; pad=0 / flip pós-R_Perm=0 com e sem UNSTICK |
| **H5** product still shell vt class | **DEAD** | `vt=0xE0150015`, `prod=0x42F85AE4` (fora `0x47D00800–0x47D00C00`; shell freelist `vt=0x00200000` só no REPLENISH path, não no tick) |

### Survivor

**Primary: H1** — lista `product+0x70` fica circular-empty após `ps3_type15_product_list_reset`; o tick CC9D0 dos 2 componentes nunca vê filhos / nunca avança `f4`/`f54`.

**Co-live (≤2 shortlist): H3** — a walk do parent só re-tiqueta os 2 objectos CBB2C (stride 0x6C). H3 sozinho não explica *porque* não avançam; H1 explica a ausência de trabalho instalado no product.

**M2 target:** reconstruir/repopular `product+0x70` com filhos **válidos** (não a cadeia NULL/poison que hangava 2A4FE4), *ou* achar o writer natural de f4/f54 / install de work **sem** reintroduzir o hang — sem promover `CC9D0-SKIP` no live `0x42F85AE4` a “fix”.

## Constraints honrados

- Sem PARK / StartSeq forge / claim de menu.
- Probe gated OFF default; cap 20.
- `PS3_TYPE15_SKIP_ATTACH` e UNSTICK tratados como diagnóstico.
- 1ª run do boot B flaky (sem WAD) documentada; retry Gate B green.
