# M2 — TYPE15 product/list CLOSE-PRESERVE (H1 survivor) (2026-07-23)

**Tipo:** fix always-on (não env-gated).  
**Script:** `recomp_mid_v2/patch_type15_list_preserve.py` → lift local `recomp_macos_v2/ppu_recomp_001.cpp`  
**Survivor M1:** H1 — `ps3_type15_product_list_reset` esvaziava `product+0x70` e apagava `was_head=0x42F85F10`.

## Binding (M1)

| Campo | Antes (wipe RESET) |
|-------|---------------------|
| prod | `0x42F85AE4` vt=`0xE0150015` (live, não shell) |
| was_head pré-RESET | `0x42F85F10` |
| child_head pós-attach | `0x42F85B54` == prod+0x70 (empty circular) |
| f4 / f54 | sempre 0 |
| CC9D0 this | só `0x4066D798` / `0x4066D804` |
| 2A4FE4 hang | já resolvido por CLOSE-TAIL + LOOP-CAP; RESET full era **over-aggressive** |

## Fix (opção A)

`ps3_type15_product_list_reset`:

1. head 0 / poison / OOB → empty circular (shells / freelist pin `0x47D00800`)
2. head == sentinel → só garante prev=sent
3. head válido → **walk next@+0**, cap 64k; em 0/poison/OOB fecha next=sentinel (**não esvazia** a lista)
4. Log: `[TYPE15] product list CLOSE-PRESERVE prod=… head=… closed_at=… nodes=…`

2A4FE4 CLOSE-TAIL / LOOP-CAP **mantidos** (defesa em profundidade).  
CB56C continua a chamar reset antes de icall2+2A4FE4 (sem SKIP permanente).

## Recipe

```bash
python3 recomp_mid_v2/patch_type15_list_preserve.py   # idempotent
./build_macos.sh
TIMEOUT=90 LOG=/tmp/m2_menu_fast_disc.log PS3_NO_RSX=1 \
  PS3_TYPE15_DISC=1 ./rodar_gow2_menu_fast.sh
```

## After (in-boot)

```
[TYPE15] product list RESET prod=0x47D00800 was_head=0x00000000 -> circular empty (2A4FE4-safe)
[TYPE15] product list CLOSE-PRESERVE prod=0x42F85AE4 head=0x42F85F10 closed_at=0x42F86858 nodes=12
[POSTINTRO] CB56C obj+8=product=0x42F85AE4 attach=full reused=1
[POSTINTRO] CB56C after 2A4FE4
[CB56C-DISC] obj=0x4066D798 prod=0x42F85AE4 reused=1 vt=0xE0150015 after_attach child_head=0x42F85F10
[TYPE15] product list CLOSE-PRESERVE prod=0x42F85AE4 head=0x42F85F10 closed_at=0x42F86858 nodes=12 (already circular)
[CB56C-DISC] obj=0x4066D804 prod=0x42F85AE4 reused=1 vt=0xE0150015 after_attach child_head=0x42F85F10
[CC9D0-DISC] this=0x4066D798 f4=0x00000000 f54=0x00 prod=0x42F85AE4 vt=0xE0150015 child_head=0x42F85F10 parent=0x00000000
[CC9D0-DISC] this=0x4066D804 f4=0x00000000 f54=0x00 prod=0x42F85AE4 vt=0xE0150015 child_head=0x42F85F10 parent=0x4419999A
```

## Gate B (menu-fast)

| Métrica | Valor |
|---------|-------|
| thr_end | **1** |
| R_Perm full (20169344) | **1** |
| FATAL | **0** |
| PARK | **0** |
| attach=full | 2 |
| after 2A4FE4 | 2 (sem hang) |
| CLOSE-TAIL / LOOP-CAP | 0 (lista já circular pós-preserve) |

## M2 criterion

| Critério | Resultado |
|----------|-----------|
| **1** child_head != prod+0x70 após attach | **PASS** — `0x42F85F10` ≠ `0x42F85B54` (12 nodes) |
| 2 f4/f54 non-zero via guest | FAIL residual — ainda 0 |
| 3 >2 distinct CC9D0 this | FAIL residual — ainda 2 EAs |

**M2 PASS** via critério 1 (lista populada preservada).

## Before vs after

| | M1 (wipe) | M2 (CLOSE-PRESERVE) |
|--|-----------|---------------------|
| log list | `RESET … was_head=0x42F85F10 -> circular empty` | `CLOSE-PRESERVE … head=0x42F85F10 nodes=12` |
| child_head | `0x42F85B54` (sentinel) | **`0x42F85F10`** (first node) |
| thr / R_Perm / FATAL | green | green (não regrediu) |

## Ainda aberto (não M2)

- f4/f54 continuam 0; CC9D0 só 2 `this` — H3 / install de work real / writers de f4 é **M3+**.
- Os 12 nodes são o pool NULL-terminated reaproveitado (stride 0xD8); fechar o tail torna a walk 2A4FE4-safe, mas **não prova** que são children de UI/menu. Próximo: se icall2 devia *substituir* o pool por children reais, ou se f4 depende de outro path pós-lista.
- Gate A (pad/flip pós R_Perm) continua vermelho.

## Constraints

- Sem PARK / StartSeq forge / claim de menu.
- Sem reativar walk poison sem CLOSE-TAIL.
- Sem `CC9D0-SKIP` permanente no produto live.
- `PS3_TYPE15_SKIP_ATTACH` / UNSTICK só diagnóstico.
- Shell freelist (`0x47D00800`, head=0) ainda RESET → empty (correto).
