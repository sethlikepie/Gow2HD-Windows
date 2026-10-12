# E9 — as paredes entram em produção, e dois gates mentiam

**Data:** 2026-08-05
**Antecessor:** [E8](2026-08-05-E8-estado-da-arte-e-a-parede-seguinte.md)
**Pedido:** promover o que está provado e correr uma corrida limpa **sem falsos positivos**.

Resultado curto: a produção passa a ter os dois fixes, o boot está saudável em
**6 de 6** corridas, e o pedido de "sem falsos positivos" produziu o achado mais
importante do dia — **os dois gates do projecto estavam a mentir, em direcções
opostas**.

---

## 1. Promoção: a produção tinha zero dos dois fixes

O E8 mediu que `recomp_macos_v2` (produção) não tinha nenhuma das duas paredes
corrigidas. Só existiam em cópias experimentais. Corrigido:

```
backup            recomp_macos_v2.pre_20260805_123215  (394M)
catalogo          ./apply_all_patches.sh recomp_macos_v2
marcadores        PAREDE1-E6-SUBTAG-SKIP-FIX = 8   Fase 11-03 FIX v2 = 1
gate do catalogo  7 de 7 PASS
build             boot_gow2  12:37:47  rc=0  errors=0
```

Os 2 patches `FAILED` não-probe (`patch_1856a8_stream_opd`, `patch_icg_ctor_opd`,
ambos "no ps3_indirect_call decl") são **falsos negativos dos próprios patches**:
a declaração está lá (1 ocorrência em `ppu_recomp_000.cpp`) e o resultado bate
certo com o lift verificado — 52 `ps3_call_opd` e 15 913 `ps3_indirect_call` nos
dois. Fica anotado como dívida: a detecção da decl nesses dois patches procura
uma forma que já não corresponde ao ficheiro.

---

## 2. A corrida limpa: 6 de 6 saudáveis

`./smoke_chain_gate.sh --bin ./boot_gow2 6` — sequencial, kill por PID, mesmo
harness que produziu os números do E8.

| corrida | `st620` | `startseq` | `nopic` | `r_perma` | `setflip_after_rperm` | `pad` |
|---|---|---|---|---|---|---|
| 1 | 11 | 2 | 4 | 1 | 9 | 1 |
| 2 | 11 | 2 | 4 | 1 | 10 | 1 |
| 3 | 11 | 2 | 4 | 1 | 9 | 1 |
| 4 | 11 | 2 | 4 | **0** | 0 | 0 |
| 5 | 11 | 2 | 4 | 1 | 18 | 1 |
| 6 | 11 | 2 | 4 | 1 | 17 | 1 |

`st620=11` e `startseq=2` em **6 de 6** — melhor que o `e6wall1` do E8, que tinha
um flake em 6. Na run1: 6311 linhas, 4067 `SetFlipCommand`, `decodeau=10`,
`FATAL 0x00514E80` = 0.

**Condições, declaradas:** a máquina **não** estava livre. Load 11,8 no início e
20,1 no fim, com um emulador Android (qemu) a ~500% de CPU durante toda a
medição. Isto não invalida o veredicto **porque o veredicto é positivo**: a
contenção só podia prejudicar, e o boot ficou saudável na mesma. O inverso não
seria verdade — se o resultado fosse mau, esta medição não serviria para nada.

---

## 3. Falso positivo #1 — o gate do catálogo estava cego aos dois fixes

`./apply_all_patches.sh --check recomp_macos_v2` dava **5 PASS, 0 FAIL, rc=0**
numa produção que não tinha **nenhum** dos dois fixes verificados. Nada estava
partido: os 5 checks são anteriores às duas paredes e verificam outras cinco
propriedades. Verde queria dizer "as propriedades que codifiquei continuam a
valer", e foi lido como "o build está bom".

Corrigido (commit `0e3ca75` no gow2-recomp) com dois checks de **contagem exacta
por ficheiro**, e não `>= 1`. A contagem exacta discrimina três estados com um
só check — calibrado contra os três lifts reais que existem em disco:

| lift | marcadores | veredicto |
|---|---|---|
| `recomp_macos_v2` (antes) | 0 | FAIL — *fix AUSENTE do lift* |
| `recomp_macos_e6wall1` | 1/5/2 = 8 | PASS |
| `recomp_macos_e6fix` | 2/10/4 = 16 | FAIL — *lift CONTAMINADO* |

O terceiro caso é a regressão de ontem (48d477b), que nenhum teste apanhou na
altura e que este apanha sozinho.

O `--check` foi provado **read-only** por hash antes e depois — precaução directa
da lição de ontem, em que se suspeitou de um comando de *verificação* ter escrito.

---

## 4. Falso positivo #2 — o gate do marco reprovava um boot saudável

Este é o pior dos dois. As **6 corridas saudáveis acima foram classificadas
`REGRESSAO`**, todas por `elo_stopped=AUTO_LOAD (nunca criada)`:

```
elo_stopped=nenhum em 0 de 6 (limiar: 4)
```

O elo é o que o E8 já tinha estabelecido como inatingível — e a justificação
estava escrita **no próprio ficheiro do harness**, pela pessoa que o escreveu
(`lib_boot_chain_metrics.sh`, nota "AVISO MAIOR"): a thread só nasce a jusante
de `0x002B2E14 bl 0x00242C94`, e `func_00242C94` só retorna em
`REQUEST_EXITGAME`. Um binário que passasse este elo teria **saído do jogo**.

E havia um segundo estrago, que só se vê a correr: **o elo do WAD vem depois
deste na ordem da cadeia**. Como o `AUTO_LOAD` parava a caminhada, o `r_perma=1`
de 5 corridas nunca chegava a ser reportado. *Um elo bloqueante inatingível não
falha só a si próprio — cega todos os que estão a jusante.*

Corrigido (commit `a869961`): `nunca criada` deixa de bloquear; `criada e não
termina` **continua** a bloquear, porque aí a thread nasceu, o jogo saiu do loop
e não acabar é um hang a sério. `thr_created`/`thr_end` continuam medidos e
impressos — nenhum dado se perde, só deixam de decidir o `rc`.

Reavaliadas as 6 tuplas medidas com a função corrigida (executada, não calculada
à mão):

```
run1..3, run5, run6   elo_stopped=nenhum
run4                  elo_stopped=WAD (R_PermA)
elo_stopped=nenhum em 5 de 6 (limiar: 4)  ->  PASSA
```

**GATE-01 passa pela primeira vez em produção**, e a única corrida que falha é
agora atribuída ao elo certo em vez de ficar escondida.

---

## O padrão que os dois gates partilham

Não é coincidência os dois falharem no mesmo dia, e o modo de falha é o mesmo
visto de dois lados:

| | o que o gate afirmava | o que se passava |
|---|---|---|
| catálogo | verde | nunca olhou para a propriedade |
| cadeia | vermelho | olhou para uma propriedade que não podia ser satisfeita |

Nos dois casos o gate estava a **medir o que sabia medir** em vez do que
importava, e nos dois casos a informação para o corrigir já existia no repo —
num caso os marcadores dos patches, no outro um aviso de 30 linhas escrito pelo
próprio autor. O trabalho não foi descobrir nada novo: foi ler o critério ao
lado da sua justificação.

> Regra que fica: **ao acrescentar um fix, perguntar o que falharia se ele
> desaparecesse em silêncio.** Se a resposta for "nada", o fix está desprotegido
> por muito bem que tenha sido verificado quando entrou. E ao ler um vermelho,
> ler a fonte do próprio critério antes de acreditar nele.

---

## Estado no fim do E9

| | estado |
|---|---|
| parede 4 (outer walk) | quebrada e **em produção** |
| parede 1 (0024E1E8) | quebrada e **em produção** |
| `boot_gow2` produção | reconstruído 12:37:47, `st620=11 startseq=2` em 6/6 |
| GATE-01 | **passa 5/6** (antes: 0/6, por falso negativo) |
| gate do catálogo | 7/7, e detecta ausência *e* contaminação |
| backup da produção | `recomp_macos_v2.pre_20260805_123215` + `boot_gow2.pre_promo_20260805` |

## Adenda — o probe da parede seguinte NÃO foi medido

Tentei fechar o ciclo e correr o `PS3_TRACE_CPY284` na produção. **Não deu
resultado, e o resultado nulo não é sobre o código.**

| corrida | janela | linhas | flips | `[CPY284]` | `UNCOMMITTED` |
|---|---|---|---|---|---|
| gate run1 (12:38) | 90 s | 6311 | 4067 | — (probe OFF) | 19 |
| cpy284 run1 | 90 s | 1511 | 419 | **0** | 0 |
| cpy284 run2 | 90 s | 2534 | 1415 | **0** | 0 |
| cpy284 longa | **240 s** | 3054 | 1900 | **0** | 0 |

O instrumento está bom, e verifiquei-o em vez de o assumir: a tag `[CPY284]` e o
gate `PS3_TRACE_CPY284` estão no código gerado, `PS3_TRACE_*` activos = 2 dentro
da corrida, gates de diagnóstico = 0 dentro da corrida.

O que falhou foi **chegar lá**. No `e6wall1` os acessos de `func_00220284`
aparecem na **linha 4013 de 4087** — 98 % do caminho, na cauda de uma corrida
inteira. Nenhuma destas corridas passou de 3054 linhas.

E a janela **não** é a explicação: 240 s deram *menos* linhas (3054) do que os
90 s da corrida do gate (6311), 40 minutos antes. A variável que mudou é a
máquina — o emulador Android manteve-se a ~497 % de CPU durante tudo isto.

Fica registado como **não medido**, e não como "o probe não encontrou nada". A
diferença importa: a segunda formulação fecharia uma porta que continua aberta.
Repetir numa máquina livre; a corrida do gate das 12:38 prova que este binário
chega lá quando tem CPU.

## Por fazer (E10), por ordem

1. **`func_00220284`** — a parede seguinte, já nomeada no E8. O probe
   discriminador **já existe e já está na produção**, gated OFF:
   `PS3_TRACE_CPY284=1` (cap `PS3_TRACE_CPY284_CAP`, default 40). Uma corrida
   responde à pergunta que decide o rumo: `this` são com o campo `+0x60` mau
   → problema de DADOS; `this` também lixo → problema de DESPACHO.
2. **run4** — porque é que `r_perma=0` em 1 de 6. Agora que o elo do WAD deixou
   de estar escondido, é uma pergunta que se pode fazer.
3. Os 2 patches OPD com detecção de decl desactualizada (secção 1).
4. `*(r30+0x460C)` em `func_00242C94` — o despacho indirecto do loop principal,
   o caminho declarado para o menu.
