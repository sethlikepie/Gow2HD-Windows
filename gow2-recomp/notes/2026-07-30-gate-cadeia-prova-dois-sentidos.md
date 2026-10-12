# GATE-01/GATE-02: prova nos dois sentidos, binários reais

**Data:** 2026-07-30 · **Método:** `smoke_chain_gate.sh --bin`, 2 corridas por
binário, contra dois binários reais já guardados. **MEDIDO nesta sessão**, não
inferido a partir de tabelas antigas.

## O que se prova

07-CONTEXT.md mediu o buraco: o smoke do v1.0 (`smoke_relift_equiv.sh`) passa
com `st620=11` num binário que não chega ao `thr_auto_load` — porque só mede o
primeiro elo da cadeia. `smoke_chain_gate.sh` fecha esse buraco: anda pelos 5
elos bloqueantes por ordem e nomeia o primeiro que falhar, nunca um rc mudo.

A prova de que o gate novo funciona é rejeitar hoje o binário que sabemos estar
partido e aceitar hoje o binário que sabemos estar bom — os dois já existiam
antes desta sessão, não são fixtures.

## Corrida 1 — `boot_gow2` (produção, 29 Jul, partido)

```
./smoke_chain_gate.sh --bin ./boot_gow2 2 /tmp/gate01_prod.tsv
```

| run | st620 | startseq | nopic | thr_end | r_perma | setflip_after_rperm | pad_total | elo_stopped | class |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| 1 | 11 | 1 | 0 | 0 | 0 | 0 | 0 | **2o movie (StartSeq)** | REGRESSAO |
| 2 | 11 | 1 | 0 | 0 | 0 | 0 | 0 | **2o movie (StartSeq)** | REGRESSAO |

**rc=1** (MEDIDO). `elo_stopped=nenhum` em 0 de 2 (limiar 2). As DUAS corridas
nomeiam o mesmo elo: **"2o movie (StartSeq)"** — a intro corre inteira
(`st620=11`, acima do limiar de 3 que o smoke antigo exige), mas o segundo
`StartSeq(handle=` nunca acontece (`startseq=1`, precisa `>=2`). Consistente
com 07-CONTEXT.md (StartSeq=1/thr_end=0 para este binário) e com as quatro
corridas independentes já medidas na Fase 6 para o mesmo comportamento — não é
flakiness, é regressão real e repetida.

## Corrida 2 — `boot_gow2.pre_v3` (25 Jul, bom)

```
./smoke_chain_gate.sh --bin ./boot_gow2.pre_v3 2 /tmp/gate01_prev3.tsv
```

| run | st620 | startseq | nopic | thr_end | r_perma | setflip_after_rperm | pad_total | elo_stopped | class |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| 1 | 11 | 2 | 4 | 1 | 1 | 0 | 0 | nenhum | OK |
| 2 | 11 | 2 | 4 | 1 | 1 | 0 | 0 | nenhum | OK |

**rc=0** (MEDIDO). `elo_stopped=nenhum` em 2 de 2 (limiar 2). Os 5 elos
bloqueantes passam nas duas corridas: `st620=11>=3`, `startseq=2>=2`,
`nopic=4>=4`, `thr_end=1>=1`, `r_perma=1>=1` (a coluna `r_perma` da
`extract_counts()` é uma contagem de linhas `grep -c`, não bytes — o valor
`1` aqui corresponde ao mesmo evento que 07-CONTEXT.md regista como
`R_Perm=199`/`bytes_read=20169344`; ambos medem "o WAD encheu", só que um
conta ocorrências de linha e o outro bytes).

### Elos 6/7 (SetFlip_after_R_Perm / Pad_total) — informativos, medidos AGORA

**Honestamente: os dois deram 0 nas duas corridas, mesmo no binário bom.**
07-CONTEXT.md só tinha confirmado `StartSeq=2/thr_end=1/R_Perm=199/NOPIC=4`
para este binário — nunca afirmou nada sobre SetFlip/Pad pós-`R_Perm`. Esta
sessão mede-os pela primeira vez para o `pre_v3` e o resultado é **zero nos
dois**, na recipe menu-fast usada aqui (que não segura o boot tempo
suficiente para o jogo desenhar depois do WAD, nem envia input de pad). Isto
**não invalida GATE-01** — os elos 6/7 são informativos por desenho (Task 1
do Plano 07-01), não bloqueantes; ficam registados aqui para não se ler esta
nota como "elos 6/7 também passam", o que seria falso.

## Uma nota de método: flakiness observada e descartada

A primeira tentativa de medir `boot_gow2.pre_v3` (antes da tabela acima)
ficou presa em `st620=1` nas duas corridas — um resultado que contradiz tanto
a referência de 07-CONTEXT.md como uma corrida imediatamente anterior de
`bisect_regression.sh --bin ./boot_gow2.pre_v3` no MESMO binário, que deu
`StartSeq=2 thr_end=1` sem problema. A máquina tinha outra compilação a
correr em fundo nesse momento (load average ~2,3 — ver nota de concorrência
do pedido desta fase). Não fica registada como medição: foi descartada e a
corrida foi repetida, dando o resultado limpo acima, reprodutível numa
segunda repetição do `bisect_regression.sh`. Não é um defeito do
`smoke_chain_gate.sh` nem do `lib_boot_chain_metrics.sh` -- é o mesmo tipo de
timing sensitivity que `smoke_relift_equiv.sh` já documenta (classe
`FLAKE_SUSPEITA`), só que noutro sinal (`st620` preso em vez do heap
allocate). Fica registado para quem repetir esta prova noutra máquina/momento
sob carga: se `st620` ficar preso em 1 logo na intro, repetir a corrida antes
de concluir regressão.

## Veredicto

| binário | rc | elo_stopped | veredicto GATE-01 |
|---|---:|---|---|
| `boot_gow2` (29 Jul, produção) | **1** | 2o movie (StartSeq) | **REJEITADO** (correcto -- é o binário partido) |
| `boot_gow2.pre_v3` (25 Jul) | **0** | nenhum | **ACEITE** (correcto -- é o binário bom) |

GATE-01 e GATE-02 estão provados nos dois sentidos, com binários reais
guardados, sem fixtures sintéticas: `smoke_chain_gate.sh` rejeita hoje
exactamente o binário que sabemos estar partido, e aceita hoje exactamente o
binário que sabemos estar bom, nomeando sempre o elo onde a cadeia parou
(nunca um rc mudo).
