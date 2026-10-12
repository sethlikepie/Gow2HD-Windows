# O `recomp_macos_e6fix` está contaminado — 16 marcadores onde deviam ser 8

**Data:** 2026-08-05
**Gravidade:** média — a cópia de trabalho está inutilizável para medições novas; a
**produção está intacta** e a medição já feita continua válida.

## O que foi medido

| lift | `PAREDE1-SKIP` | estado |
|---|---|---|
| `recomp_macos_e6fix` (cópia de trabalho) | **16** | contaminado — eram 8 |
| `recomp_macos_v2` (**produção**) | **0** | intacto, nunca tocado |

O `boot_gow2_p1fix` que deu `FATAL 0/3` foi construído quando a cópia tinha **8**. Essa
medição, e a de 0/6 do ledger E7, **continuam válidas** — foram feitas antes da contaminação.

## O patch NÃO é o culpado

Testado directamente sobre uma cópia limpa de `ppu_recomp_000.cpp` tirada da produção:

```
1a corrida  rc=0  marcadores=1
2a corrida  rc=0  marcadores=1
IDEMPOTENTE
```

`patch_24e1e8_wall1_subtag_skip.py` é idempotente por ficheiro. A duplicação veio de
**aplicações sucessivas ao longo da sessão** sobre uma cópia que já tinha o bloco — o
`apply_all_patches.sh` correu várias vezes sobre o `e6fix`, e entre corridas o conteúdo mudou
o suficiente para a agulha voltar a casar noutro sítio.

## A lição

Uma cópia de trabalho que sofre N aplicações do catálogo **não é equivalente a um lift limpo
com o catálogo aplicado uma vez**. Idempotência por ficheiro não garante idempotência sobre um
ficheiro que outro patch alterou no intervalo.

⇒ **Para medir, regenerar a cópia do zero.** Não reaproveitar uma cópia que já levou várias
levas de patches. O custo é um `cp -R` mais uma passagem do catálogo; o custo de não o fazer é
uma medição inválida que ninguém detecta.

## Acção

- `recomp_macos_e6fix` deve ser **apagado e regenerado** antes de qualquer medição nova.
- A produção não precisa de nada — está limpa.
- Os dois fixes commitados (`patch_outer_subtag_prune.py`, `patch_24e1e8_wall1_subtag_skip.py`)
  não precisam de correcção: ambos passam o teste de idempotência sobre lift limpo.

Ver também: `~/.claude/skills/measuring-expensive-systems/SKILL.md` (secção sobre rebuild
forçado e condições que se têm de poder nomear).
