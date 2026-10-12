# E46 — a cadeia inteira em dezassete linhas de log

> **LEITURA CORRIGIDA pelo [E49](2026-08-07-E49-nao-e-fase-e-sitio.md)** (detalhe na
> seccao «O pedido chega um push cedo demais») **e enquadramento pelo
> [E52](2026-08-07-E52-dois-dos-seis-sao-por-desenho.md)**. A adjacencia medida
> sobrevive; cai a leitura de quase-acerto. As medicoes ficam todas.

**Data:** 2026-08-07 · **Antecessor:** [E45](2026-08-07-E45-o-vazio-visto-em-vez-de-inferido.md)

Vigia `PS3_WATCH_STORE=0x400C62D8` (todas as larguras, **sem cap**) mais a sonda
`EMPTYFAB`, na mesma corrida. Responde ao passo 1 do E45: **onde caem os pushes
em relação aos pedidos que encontram a fábrica vazia.**

---

## A fábrica é enchida — 352 escritas antes do primeiro nulo

Primeira escrita do cursor na linha 408 (`0xFF`, `func_002B11B8` — a init para
vazio). Primeiro pedido nulo na linha **12658**. Entre as duas: **352 das 353
escritas**.

Isto refuta de vez a leitura de que a fábrica nunca chega a ser enchida. É
enchida e esvaziada 176 vezes antes de alguém lhe pedir produto e falhar.

## A janela

```
12648  [WATCHSTORE] w8 [0x400C62D8]=0xFF   func_0039DAB0+0x1A8   <- o pop esvazia
12658  [EMPTYFAB] fab=0x400C6210 cursor=-1 VAZIO   lr=0x0024E424
12660  [EMPTYFAB] fab=0x403008E8 cursor=-1 VAZIO   lr=0x0024E424
12662  [vm] UNCOMMITTED read32 0x726D612E   func_00263554+0x56C   <- "rm_." como endereço
12663  [vm] UNCOMMITTED write32             func_00220284+0x1244
12664  [vm] UNCOMMITTED write32             func_00220284+0x1274
12665  [vm] UNCOMMITTED write32             func_00220284+0x12A4
```

**Dezassete linhas do esvaziamento ao sintoma.** A cadeia que custou dezasseis
ledgers a construir — fábrica vazia → produto nulo → tipo 0 → gestor errado →
pool 0 → texto como cabeça de free-list → escritas em memória não commitada —
aparece agora como uma corrida contígua de log, numa só medição, em ordem.

O `0x726D612E` é o `"rm_."` do E25/E26, lido como endereço. Está a 4 linhas do
produto nulo.

## O pedido chega um push cedo demais

> **LEITURA CORRIGIDA em 2026-08-07 pelo [E49](2026-08-07-E49-nao-e-fase-e-sitio.md).**
> A adjacencia medida abaixo (12787 VAZIO / 12788 push) **sobrevive** -- e' real e
> foi medida. **CAI** a leitura de quase-acerto («por uma margem de um push»):
> num binario determinista, o sitio `0x0024E424` e' consultado 2 vezes e falha as
> 2, enquanto `0x0027C724` e' consultado 238 vezes e acerta as 238. A particao
> por chamador e' limpa (253/253 contra 6/6), logo nao e' margem apertada nem
> fase da corrida -- e' regime diferente entre chamadores.

O segundo pedido nulo à mesma fábrica é ainda mais estreito:

```
12787  [EMPTYFAB] fab=0x400C6210 cursor=-1 VAZIO   lr=0x002B2DE0
12788  [WATCHSTORE] w8 [0x400C62D8]=0x0    func_0039DA78+0x150   <- o push que o teria servido
12789  [WATCHSTORE] w8 [0x400C62D8]=0xFF   func_0039DAB0+0x1A8
```

**O push que satisfaria o pedido acontece na linha seguinte.** Não é uma fábrica
morta nem um caminho errado: é um pedido fora de fase, por uma margem de um
push.

## O que isto fecha, e o que não fecha

**Fecha:** a natureza do defeito. Não é contagem (E43), não é underflow (E43),
não é falta de guard a montante (E44), não é a fábrica nunca ser enchida (aqui).
É **ordem**.

**Não fecha:** *qual* ordem. Duas leituras continuam vivas e levam a fixes
diferentes:

1. **corrida entre threads** — o produtor e o consumidor são threads distintas e
   falta sincronização (ou o nosso giant lock/scheduler deixa-as correr numa
   ordem que o CELL não deixaria);
2. **ordem de programa** — o mesmo fio pede antes de empurrar, e é um defeito
   de sequência (nosso, de lift, ou do jogo sob HLE).

## Dívida de instrumento, outra vez

**Nenhuma das duas sondas imprime thread.** A única linha do log com `tid` é um
`[SYS] sys_ppu_thread end` sem relação.

A ordem das linhas é ordem temporal real — cada sonda faz `fflush` e o stderr
serializa — mas **adjacência no log não é adjacência em ordem de programa**: as
linhas 12787 e 12788 podem ser dois fios. Enquanto isso não estiver medido, a
leitura «um push cedo demais» descreve o tempo, não o fio.

Discriminador, e é pequeno: acrescentar o `tid` do host às sondas `EMPTYFAB` e
`WATCHSTORE`. Se os seis nulos e os pushes vizinhos forem do mesmo fio, é ordem
de programa; se forem de fios diferentes, é corrida.

## Refutado por medição

1. **«a fábrica nunca é enchida antes do pedido»** — 352 das 353 escritas do
   cursor acontecem antes do primeiro nulo.
2. **«o nulo é um caminho morto, uma fábrica que ninguém usa»** — é usada 176
   vezes na mesma corrida.
3. Tudo o que E11–E17 e E19–E45 já refutaram.

## Próximo

1. **`tid` nas duas sondas** — separa corrida de ordem de programa. É o único
   passo que decide qual dos dois fixes é o certo;
2. quem é `func_002B2DD0+0x10` (o `lr` do pedido das 12787) e porque é que corre
   antes do push vizinho;
3. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
