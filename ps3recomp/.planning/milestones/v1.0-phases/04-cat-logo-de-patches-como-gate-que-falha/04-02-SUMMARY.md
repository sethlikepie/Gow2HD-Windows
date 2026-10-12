---
phase: 04-cat-logo-de-patches-como-gate-que-falha
plan: 02
status: complete
completed: 2026-07-26
---

# 04-02 — `patch_14a01c_opd.py` ganha veredicto de três ramos (D-4.7)

> **Nota de proveniência.** O agente executor foi terminado por um erro de API
> (`Connection closed mid-response`) já depois de commitar todo o trabalho técnico,
> enquanto escrevia este ficheiro. O SUMMARY foi completado pelo orquestrador, que
> **re-mediu tudo do zero** em vez de reconstruir o que o agente teria escrito. Todos os
> números abaixo são medições feitas após a falha, não citações do relatório perdido.

## O que foi entregue

`patch_14a01c_opd.py` era **o único dos 87 patches incapaz de sair não-zero em qualquer
circunstância**. Passa a ter o mesmo tri-estado dos seis corrigidos hoje:

| estado | significado | rc |
|---|---|---|
| `CONVERTIDO` | ≥1 conversão nesta corrida | 0 |
| `JÁ-APLICADO` | 0 conversões mas os sítios já estão em `ps3_call_opd` | 0 |
| `SEM-EFEITO` | 0 conversões **e** 0 já convertidos | **3** |

Commits: `9342a72` (teste RED), `55a98a8` (fix + posse única).

## Duas correcções ao que o plano assumia

**São 4 sítios, não 2.** A decisão D-4.7 falava de duas funções
(`func_0014A01C`, `func_0014AD94`) e uma leitura literal daria duas conversões. São
**2 + 2 = 4** — medido: `func_0014A01C: already=2`, `func_0014AD94: already=2`.

**O bug de coordenação era real.** O `patch_icg_ctor_opd.py` reivindicava as mesmas duas
funções, precisamente *porque* o `14a01c` estava partido. Corrigir um faria o outro
imprimir `SEM-EFEITO` — e no desenho novo isso é `rc≠0`. Ou seja: **corrigir um patch
faria outro falhar.** A posse foi transferida.

## Verificação (medida após a falha do agente, na ordem real do glob)

O `apply_all_patches.sh` corre por glob alfabético, logo `14a01c` vem **antes** de
`icg_ctor`. É essa a ordem que importa e é essa que foi testada:

```
--- patch_14a01c_opd.py ---
func_0014A01C: fixed 0 already=2 remain_indirect=0
func_0014AD94: fixed 0 already=2 remain_indirect=0
OK patch_14a01c_opd (JA-APLICADO: 4 sitios ja em ps3_call_opd)
rc=0

--- patch_icg_ctor_opd.py ---
func_00151248: JA-APLICADO ... call_opd=3
func_00329490: JA-APLICADO ... call_opd=2
func_0032854C: SEM-EFEITO  ... indirect 1->1
func_00328B18: SEM-EFEITO  ... indirect 1->1
rc=0
```

O `icg_ctor` **já não menciona** `func_0014A01C` nem `func_0014AD94` — a posse é única.

Os dois `SEM-EFEITO` do `icg_ctor` não fazem o patch falhar, e está certo: o veredicto é
por patch, não por alvo. Um patch só é `SEM-EFEITO` quando **todos** os seus alvos ficam
sem efeito. `func_0032854C` e `func_00328B18` são dívida pré-existente, não regressão
desta task.

**A cadeia inteira mantém as conversões:**
```
ps3_call_opd(ctx,  em recomp_macos_v3  =  47      (alvo: 47)
```

**O rc de falha funciona** — contra um lift sem nenhum dos alvos:
```
FALHA patch_14a01c_opd (SEM-EFEITO): 0 conversoes e 0 sitios ja convertidos.
  As agulhas nao casam o shape actual do lift.
rc=3
```

## Estado dos patches capazes de falhar por não-efeito

Sete, agora: `patch_32e200_opd.py`, `patch_39e5d8_opd.py`, `patch_icg_ctor_opd.py`,
`patch_14b1f0_opd.py`, `patch_b71_2f3f0_guard.py`, `patch_ba808_wadld_eof.py` e
`patch_14a01c_opd.py`.

Os restantes 80 continuam a devolver `rc=0` quando não produzem efeito — é o que os
planos 04-04 (`CONTRACTS.tsv`) e 04-05 (seis estados no runner) atacam, verificando a
**pós-condição** em vez de confiar no `rc` que o patch se auto-atribui.

## O que ficou

- `STATE.md` / `ROADMAP.md` / `REQUIREMENTS.md` não foram tocados por este plano: três
  agentes da Wave 1 correram em paralelo sobre esses ficheiros partilhados e a
  consolidação pertence ao orquestrador.
- O espelho `games/gow2/recomp_mid_v2/patch_14a01c_opd.py` no monorepo continua com a
  versão antiga — parte da divergência subtree conhecida, fora do âmbito desta task.
