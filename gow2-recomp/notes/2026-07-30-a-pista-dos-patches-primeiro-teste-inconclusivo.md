# A pista dos patches: primeiro teste feito, e é INCONCLUSIVO

**Data:** 2026-07-30 · Segue-se a `2026-07-30-a-pista-que-sobra-sao-os-patches-nao-o-lifter.md`
**Veredicto: INCONCLUSIVO por contaminação do próprio teste.** Não refuta nem confirma.

## O que se fez

Extraí os dois conjuntos de `patch_*.py` sem tocar no checkout (`git show e6d65a2:<path>`
para um directório temporário) e apliquei cada um a uma cópia do mesmo lift limpo:

```
patches HOJE     : 89
patches e6d65a2  : 74      (15 novos desde o último binário OK)
```

Aplicação (uma passagem, ordem alfabética):

```
hoje    -> liftA : 21 aplicaram, 68 falharam
e6d65a2 -> liftB : 16 aplicaram, 58 falharam
```

Diff dos lifts resultantes: **4 chunks diferem**, 568 linhas no total.

## Porque é inconclusivo — três defeitos do teste, todos meus

**1. O diff está dominado pelos meus próprios probes.** Os marcadores mais frequentes no
diff são `[CB56CTY]` e `[29AF0]` — os dois probes que **eu** acrescentei nesta sessão, que
por definição só existem no conjunto de hoje e são no-op com o gate desligado. Medi a
minha própria contribuição.

**2. Uma passagem não é o runner.** O `apply_all_patches.sh` corre até convergir (duas
passagens) e tem classificação de seis estados. Um loop `for p in patch_*.py` não
reproduz isso — 68 e 58 falhas numa passagem é o esperado, não um sinal.

**3. `PATCH_DIR` é fixo** (`apply_all_patches.sh:67`, `$REPO/recomp_mid_v2`), por isso não
consegui usar o runner a sério contra um conjunto alternativo sem duplicar o repositório.

## O que o teste mostra mesmo assim, e é útil

O diff **não toca nenhuma função do caminho crítico**:

| alvo | linhas no diff |
|---|---:|
| `func_00147038` (`thr_auto_load`) | **0** |
| `func_002C00DC` | 0 |
| `func_000CE03C` / `CE03C` / `INTROSEQ` | 0 |
| `MOVIEFSM` | 0 |
| `func_002B4274` | 0 |

Isto é sinal fraco mas real: a diferença entre os dois conjuntos de patches, tal como
medida, **não chega ao `thr_auto_load` nem ao caminho do movie**. Combina com o que a
Fase 6 mediu independentemente — que o corpo de `func_00147038` é byte-a-byte idêntico
entre o lifter do último binário OK e o de hoje.

## O desenho correcto, para a Fase 8

Para transformar isto em resultado é preciso corrigir os três defeitos:

1. **Excluir os patches novos que são diagnóstico puro** (os `PROBE` do
   `PATCH_CATALOG.tsv`, `no_gate=1`) — são no-op com o gate desligado e só fazem ruído.
   O catálogo já os classifica; usar essa coluna.
2. **Usar o runner a sério.** Copiar o repositório para um sandbox (ou tornar `PATCH_DIR`
   configurável por env, que é uma melhoria por si) e correr `apply_all_patches.sh`.
3. **E, no fim, medir o binário, não o lift.** É a lição de toda esta sessão: contagens e
   diffs enganam; o critério é `thr_auto_load end >= 1`.

O passo (2) tem valor independente — um `PATCH_DIR` configurável torna qualquer bissect
futuro de patches trivial, tal como o `bisect_regression.sh` tornou o de binários.

## Ressalva

Nada aqui refuta a pista. Diz que o **primeiro teste que lhe fiz não serve**, e porquê. A
hipótese "um dos 38 patches alterados ou 15 novos partiu o caminho" continua aberta e
continua a ser a lacuna estrutural mais óbvia — é o único andar da cadeia
lifter → patches → build que nunca foi testado a sério.
