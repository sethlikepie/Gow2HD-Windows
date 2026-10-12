# E8 — o estado da arte medido, e a parede seguinte tem nome

**Data:** 2026-08-05
**Antecessores:** [E6](2026-08-05-E6-parede1-mesma-doenca.md), [E7](2026-08-05-E7-fix-verification-parede-1.md)
**Objectivo:** fechar as duas perguntas que o E7 deixou em aberto (`startseq` 1-vs-2 e
o critério de "chegou ao menu"), medir onde o boot está HOJE com os dois fixes, e
nomear o elo seguinte.

Todos os números abaixo saem de logs que já existiam em disco — nenhuma corrida
nova foi feita para este documento. Ver a secção **Condições** para o que isso
permite e não permite afirmar.

---

## 1. A divergência do `startseq` está RESOLVIDA — e não era contenção

O E7 registou `startseq` como "não confirmado" e eu suspeitei de contenção
(duas medições em janelas sobrepostas). **Estava errado.** A causa é a
**duração da janela**, e é demonstrável a partir dos logs.

| medição | binário | janela | `startseq` | `decodeau` |
|---|---|---|---|---|
| `/tmp/p1fix_run{1,2,3}.log` | `boot_gow2_p1fix` | **30 s** | 1, 1, 1 | **0, 0, 0** |
| `/tmp/chain_gate_..._e6wall1_run{1..6}` | `boot_gow2_e6wall1` | **~90 s** | 2 (5 de 6) | 10 (5 de 6) |

A prova está na posição do marcador dentro do log:

- em `p1fix_run1.log`, o **único** `StartSeq` está na linha **2865 de 2872** —
  a corrida foi morta 7 linhas depois dele;
- em `e6wall1_run1`, o 1º `StartSeq` está na linha 2586 e o 2º na 2632, seguidos
  de `Close(handle=0)`, do arranque do overlay VT e dos 10 `DecodeAu`.

Ou seja: a janela de 30 s termina **exactamente no primeiro `StartSeq`**. O
primeiro é o estéril (0 `DecodeAu` entre os dois, já estabelecido no E1); o
segundo é o que trabalha. `decodeau=0` nas três corridas de 30 s confirma que
nenhuma delas chegou a decodificar.

**Nenhuma das duas medições estava errada — mediram janelas diferentes.** O erro
foi meu, ao comparar dois números como se fossem do mesmo instrumento.

> Lição para o `measuring-expensive-systems`: antes de atribuir uma divergência a
> contenção, comparar as **janelas**. A janela é um parâmetro do instrumento, e
> um contador de eventos tardios é uma função dela. Contenção foi a minha
> primeira hipótese porque era a que eu tinha acabado de aprender — e foi a
> errada.

**Janela mínima válida para `startseq`/`decodeau`: 90 s.** Uma janela de 30 s
mede o boot até ao primeiro `StartSeq` e mais nada; não serve para julgar o
caminho do vídeo.

---

## 2. O `AUTO_LOAD` não é uma parede — e a resposta já estava no repo

A pergunta "o `AUTO_LOAD` é atingível num smoke?" **já tinha sido respondida e
medida**, em `gow2-recomp/lib_boot_chain_metrics.sh` (bloco "AVISO MAIOR"). Cito
porque isto fecha o assunto:

- a thread só pode ser criada por `func_00146CB8`, chamada de `0x000BB1E4` e
  `0x000BBD70`; ambos os sítios estão **a jusante** de `0x002B2E14 bl 0x00242C94`,
  o loop principal do jogo;
- `func_00242C94` só retorna em `REQUEST_EXITGAME`;
- medido numa corrida saudável, com sonda de CONTROLO a funcionar (=2):
  `SetFlipCommand` 2619, linhas após o loop principal **0**, degraus da cadeia
  `AUTO_LOAD` **0**.

**`AUTO_LOAD` nunca criada é o comportamento CORRECTO de um jogo que ainda está a
correr.** Um binário que "passasse" este elo teria saído do jogo. Perseguir este
elo é perseguir a prova de que o jogo terminou.

O mesmo bloco deixa o alvo certo, e adopto-o: **o caminho para o menu passa pelo
despacho indirecto `*(r30+0x460C)` dentro de `func_00242C94`**, não pelo
`AUTO_LOAD`.

Custou-me duas sessões a redescobrir uma coisa que estava escrita no ficheiro do
harness. O aviso estava no sítio certo; eu é que li o critério e não a
justificação ao lado dele.

---

## 3. Estado da arte: o boot ENTRA no loop principal e desenha

`boot_gow2_e6wall1` é o lift com **os dois fixes juntos** (parede 4 =
`Fase 11-03 FIX v2`, 1 marcador; parede 1 = `PAREDE1-SKIP`, 8 marcadores), limpo:
o binário (10:43:45) é mais recente do que todas as suas fontes (última 10:42:01).

6 corridas sequenciais, ~90 s cada, gates de diagnóstico OFF:

| corrida | linhas | `st620` | `FATAL` | `startseq` | `decodeau` |
|---|---|---|---|---|---|
| 1 | 4087 | 11 | 0 | 2 | 10 |
| 2 | 3338 | 11 | 0 | 2 | 10 |
| **3** | **1818** | **1** | 0 | **0** | **0** |
| 4 | 3111 | 11 | 0 | 2 | 10 |
| 5 | 3072 | 11 | 0 | 2 | 10 |
| 6 | 4149 | 11 | 0 | 2 | 10 |

**5 de 6 saudáveis; a corrida 3 é o flake conhecido** (log curto, `st620` para em
1). 1 em 6 é consistente com a flakiness histórica do `vm commit`, mas **não a
confirmei** contra o critério — fica declarado como não confirmado.

Na melhor corrida (run1, 4087 linhas):

| sinal | valor | leitura |
|---|---|---|
| `SetFlipCommand` | **2205** | o loop principal corre e desenha (referência saudável do harness: 2619) |
| `set_shader` | 23 | há shaders a ser postos |
| `bind_texture` | 8 | há texturas a ser ligadas |
| `cellPadGetData` | 1 | `game IS polling pad` |
| `R_PermA` | `bytes_read=20298800` de `size=20169344`, 99 preads | stream completo |
| `FATAL 0x00514E80` | 0 | parede 1 ausente |

**2205 flips é o número que importa.** Não estamos parados numa parede: o jogo
entra no loop principal, desenha milhares de frames e sonda o pad. As paredes 1 e
4 saíram do caminho.

---

## 4. O bloqueio real: os dois fixes NÃO estão em produção

| lift | parede 4 | parede 1 |
|---|---|---|
| `recomp_macos_v2` (**produção**) | **0** | **0** |
| `recomp_macos_e6wall1` | 1 | 8 |
| `recomp_macos_e6fix` | 1 | 16 (contaminado, ver 48d477b no gow2-recomp) |

**A produção não tem nenhum dos dois fixes.** As duas paredes estão quebradas
apenas em cópias experimentais. Enquanto isso não mudar, "as paredes estão
quebradas" é verdade sobre um binário que ninguém constrói por omissão.

Isto é o próximo passo com maior retorno, e é trabalho de encanamento, não de
investigação: promover os dois patches ao catálogo de produção, reconstruir, e
medir.

---

## 5. A parede seguinte tem nome: `func_00220284`

24 leituras/escritas `UNCOMMITTED` na run1, e a distribuição não é uniforme:

| `ra` | nº |
|---|---|
| **`func_00220284`** | **16** |
| `ppu_opd_resolve` | 2 |
| `func_0041C9D0` | 2 |
| `func_0039D428` | 2 |
| `func_00263554`, `func_00455800` | 1 cada |

Os endereços tocados por `func_00220284` formam um **walk de stride 4** a partir
de uma base fixa:

```
escritas: 0x726D612E (+0x12A4)   0x726D6132 (+0x1244)   0x726D613A (+0x1274)
leituras: 0x726D6146 614A 614E 6152 6156 615A 615E 6162 6166 616A   (stride 4)
```

**O valor da base é ASCII.** `0x72 0x6D 0x61 0x2E` = `"rma."` — o meio de
**`r_perma.wad`** em minúsculas. Um campo que devia conter um ponteiro contém,
em vez disso, quatro caracteres do nome do ficheiro, e o código percorre-o como
se fosse um array.

Isto qualifica como parede nomeável pelo critério do
`measuring-expensive-systems` (endereço + contagem de iterações), e é **a mesma
doença** das paredes 1 e 4: um campo lido como ponteiro quando guarda outra
coisa. O mesmo endereço base é tocado por três funções diferentes
(`func_00220284`, `func_00263554+0x56C`, `func_00455800+0x134`), o que sugere um
struct partilhado e não um erro local.

**O que está provado:** a base é lixo com valor de string, e há um walk de ≥10
passos sobre ela.
**O que NÃO está provado:** qual o campo, em que offset, e porque é que lá está o
nome. Nomear o mecanismo é o trabalho do E9 — não confundir com tê-lo corrigido.

---

## Condições (o que estes números permitem afirmar)

- As 6 corridas `e6wall1` são **sequenciais** — provado pelos timestamps dos
  nomes dos ficheiros, ~92 s de intervalo, sem sobreposição.
- **Não posso certificar que a máquina estava livre** às 10:49–10:57. Sequencial
  prova-se retroactivamente; "sem contenção" não. Contadores de trabalho
  assíncrono ficam com essa reserva.
- Ao escrever este documento a máquina tinha um **emulador Android (qemu) a 521%
  de CPU** e load 14. Nenhuma medição nova foi feita nestas condições, de
  propósito.
- Nenhum gate de diagnóstico ligado nas corridas usadas
  (`PS3_LIST254_EMPTY_IF_NULL` e companhia UNSET).

## Refutado por medição — não voltar a perseguir

1. **`startseq=1` como regressão** — é uma janela de 30 s a cortar no primeiro
   `StartSeq` (linha 2865 de 2872). Janela mínima: 90 s.
2. **Contenção como causa da divergência do `startseq`** — a diferença de janela
   explica-a inteiramente.
3. **`AUTO_LOAD` como elo do caminho para o menu** — só nasce a jusante de
   `func_00242C94`, que só retorna em `REQUEST_EXITGAME`. Medido: 0 degraus, 0
   linhas pós-loop, controlo a funcionar.
4. **"O boot está parado numa parede"** — 2205 `SetFlipCommand`, pad sondado,
   23 `set_shader`. O loop principal corre.
5. **O patch da parede 1 como causa da contaminação dos 16 marcadores** — testado
   idempotente sobre cópia limpa (2 corridas, 1 marcador, hash idêntico). A causa
   foi aplicar o catálogo N vezes sobre a mesma cópia de trabalho.

## Próximo (E9), por ordem de retorno

1. **Promover os dois fixes a produção** (encanamento, sem investigação).
2. **Corrida limpa** de 90 s sobre a produção promovida, máquina livre,
   3 corridas sequenciais — a linha de base verdadeira.
3. **`func_00220284`:** de onde vem a base `0x726D612E`. Ler o decompilado em
   Ghidra (projecto `gow2_full`) e encontrar o offset do campo.
4. `*(r30+0x460C)` em `func_00242C94` — o despacho indirecto do loop principal,
   que é o caminho declarado para o menu.
