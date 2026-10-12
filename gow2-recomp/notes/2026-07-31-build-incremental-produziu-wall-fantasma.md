# O build incremental produziu um "wall fantasma"

**Data:** 2026-07-31 · **Gravidade: alta** — afecta a confiança em medições desta sessão.

## O que aconteceu

O gate de 6 corridas reportado às 19:14–19:21 (`/tmp/gate_type15_combined.tsv`) mostrava
`'Orbo'` a chegar ao `CB56C` em 4 das 6 corridas, apesar de os fixes `5d5e5e0`
(auto-ponteiros) e `6e7be2f` (shell rehome) estarem commitados e aplicados ao clone.

**Os fixes não estavam no binário.** O `build_macos.sh` não recompilou o chunk porque a
mtime não indicava mudança, e o link usou um `.o` **obsoleto**.

Reconstruído com `FORCE_REBUILD_LIFT=1`:

```
'Orbo' no log:   antes 4/6 corridas   ->   depois 0/6
*fl (head):      0x40100638 (arena antiga)  ->  0x47D00418 (traduzido, dentro do pin)
```

**Os fixes funcionam.** A parede que reportei como "o produto não vem da free-list
reparada" era um artefacto de build.

## O que isto invalida

A conclusão de que *"o `REPLENISH` corre e o `CB56C` recebe `'Orbo'` na mesma, logo o
produto vem de outro sítio"* — **falsa**. Vinha da free-list, sim; o binário é que não
tinha o fix que a reparava.

## O que continua válido

O gate reconstruído dá:

```
run  st620  startseq  nopic  thr_end  r_perma  elo_stopped
1    1      0         0      0        0        intro (st620)
2    11     2         4      0        1        AUTO_LOAD (thr_end)
3    11     2         4      0        1        AUTO_LOAD (thr_end)
4    11     2         4      0        1        AUTO_LOAD (thr_end)
5    1      0         0      0        0        intro (st620)
6    11     2         4      0        1        AUTO_LOAD (thr_end)
```

**4 de 6** chegam ao `AUTO_LOAD`, `'Orbo'` eliminado em 6/6. O `thr_end` continua 0.

E a parede real, confirmada nas 4 corridas: a **2.ª chamada a `func_0039E794`
(`type high-bit 0x15 -> 0x80000015`) nunca retorna** — sem `[FACTORY] leave`, sem
`icall2`, sem `obj+8=product`. É a mesma já documentada em
`2026-07-31-type15-shell-rehome-fix-e-nova-parede-hang.md`.

O `UNSTICK-SKIP` não ajuda aqui porque actua a jusante (em `CC9D0`); não serve de nada
quando o construct em si nunca devolve.

## A cadeia causal do `'Orbo'`, para o registo

Medida com `PS3_WATCH_W32` + `dladdr` antes de se descobrir o build obsoleto — continua
correcta como descrição do bug que os fixes resolvem:

1. `func_0039E794` faz dupla indirecção no pop: `head = *fl` → `slot = *head`, e devolve
   **`slot - 4`** como produto.
2. `slot = 0x4F726273` (`'Orbs'`) ⇒ produto `= 0x4F72626F` (`'Orbo'`). Aritmética exacta.
3. `head` valia `0x40100638` — arena antiga **não protegida**. O autor da escrita é o
   próprio REHOME, no copy RAW: `[TYPE15] REHOME freelist old=0x40100620 pin=0x47D00400
   *fl=0x40100638`.
4. Entre o REHOME e a leitura, o guest constrói ~15 outras fábricas que reciclam a arena
   antiga — e o conteúdo vira ASCII.

## A regra operacional que fica

**Antes de qualquer gate sobre um clone recém-repatchado: `FORCE_REBUILD_LIFT=1`.**

O build incremental decide por mtime, e um patch que reescreve um chunk pode não alterar a
mtime de forma detectável. O resultado é um binário que parece testar o fix e testa o
código anterior — **sem um único aviso**.

É a mesma família dos outros silêncios que esta sessão encontrou: o `SKIP` de agulha morta,
o patch documental que só faz `print()`, o `pgrep -f` que casa consigo próprio. Nenhum
falha; todos mentem.

⚠️ **Por verificar:** se o `recomp_macos_v2`/`boot_gow2` de **produção** sofrem do mesmo.
Se sofrerem, medições anteriores desta sessão podem ter testado binários obsoletos.
