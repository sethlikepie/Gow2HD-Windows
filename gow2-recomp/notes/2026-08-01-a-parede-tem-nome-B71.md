# A parede tem nome: `func_000B71B8` (B71) entra e nunca retorna

Data: 2026-08-01, depois de provar que o elo AUTO_LOAD não pertence à cadeia.

## Método: bissecção por sonda de entrada, com controlo obrigatório

31 funções instrumentadas na entrada (`PS3_TRACE_ALCHAIN`), mais uma sonda de
**CONTROLO** numa função que sei correr. Sem controlo não se lê um zero — foi a
lição das quatro sondas que mentiram nesta sessão. Três rondas:

**Ronda 1** — `main()` (`func_0025C838`) é uma sequência recta de nove chamadas,
sem condicionais:

```
func_002B37D4 ✅  func_00242700 ✅  func_002B4F04 ✅  func_0025C680 ✅
func_002B76EC ✅  func_002B2EEC ✅  func_002B2E74 ✅  func_002B2E04 ❌
```

**Ronda 2** — `func_002B2E74`, também recta, onze chamadas:

```
func_0024A7CC ✅  func_002AAC84 ✅  func_002AC328 ✅  func_002AB2F8 ✅
func_002B5C94 ✅  func_002B5508 ✅  func_002D2978 ✅  func_002287AC ✅
func_000B71B8 ✅  func_0023B654 ❌
```

**A última que imprime é a que não retorna: `func_000B71B8` — o B71.**

## O que isto corrige

O loop principal do jogo (`func_00242C94`) é a **oitava** chamada do `main()`,
imediatamente a seguir a `func_002B2E74`. Como esta nunca retorna, **o jogo
nunca entra no seu loop principal.**

Portanto:

- Os 2619 `SetFlipCommand` por corrida **não são** o loop principal. Vêm de
  outro sítio (movie/intro path).
- O despacho de estado `*(r30+0x460C)` — o mecanismo que faz o jogo avançar de
  estado — **nunca corre uma única vez** (`PS3_TRACE_STATE` = 0).
- A leitura que eu tinha escrito uma hora antes ("o loop corre e nunca
  retorna") estava errada na primeira metade. Zero linhas depois da chamada é
  igualmente compatível com "a chamada nunca aconteceu" — e é esse o caso. O
  que me faltava era medir a entrada, não só a saída.

## Porque é que o B71 não retorna

```
[ICALL-BAD] ctr=0x00514E80 lr=0x0024E2D4 r3=0x00000000 r11=0x00000000   (x2000)
[ppu] FATAL: stuck calling 0x00514E80 (2000 times) -- aborting run
[ppu] rbp frame walk:  #0 ret=func_002545D4+0xAD0
```

É a parede documentada em Julho (`patch_2545b0_entry_probe.py`): o walker de
registos do WAD (`lr=0x0024E2D4`) entrega a `func_002545B0` um objecto cuja
lista circular tem `head = 0`; o teste de vazio do jogo é `head == sentinela`,
que um 0 nunca satisfaz; o walk entra com um nó nulo e despacha em ciclo sobre
NULL até o breaker dos 2000 chamar `exit(3)`.

**O `exit(3)` é o que impede o B71 de retornar.** O boot não fica pendurado —
morre.

## O re-teste que tinha de ser feito

Em Julho concluiu-se que saltar esta parede (`PS3_LIST254_EMPTY_IF_NULL=1`)
**não desbloqueia**, porque `thr_auto_load` continuava a 0. Mas essa métrica
media uma string que não existe em binário nenhum — não podia disparar. A
conclusão tinha de ser refeita com o instrumento corrigido.

Refeita hoje, com as sondas de entrada:

| | gate OFF | gate ON |
|---|---:|---:|
| `FATAL` | 1 | **0** |
| `LIST254-GATE` (saltos) | 0 | 1 |
| último degrau | B71 | **B71** |
| `STATE` (despacho de estado) | 0 | 0 |

**A conclusão de Julho estava certa** — mas por uma razão diferente da que se
registou. Com o gate, o `FATAL` desaparece e o processo deixa de morrer; passa
a **pendurar** dentro do B71, num ciclo de stream FIOS:

```
85  [SPUJOB] spu job returned cleanly
82  [FIOSOPEN] F2B-STREAM-ENSURE ...
46  [cellGcmSys] SetFlipCommand(bufferId=0/1/2)
37  [FIOSOPEN] F2B-STREAM-FILL ...
16  [vm] UNCOMMITTED write32 ... ra=func_00220284+...
```

Ou seja: há um **segundo** bloqueador dentro do B71, logo a seguir ao primeiro.
Morrer e pendurar são estados diferentes; a métrica antiga dava o mesmo número
para os dois.

## Onde continuar

O B71 é longo (é a sequência de boot que chama a intro). A bissecção por
entrada não chega lá dentro sem instrumentar dezenas de chamadas. O próximo
passo é localizar o ciclo dentro do B71 — e as pistas já medidas são o walker
do WAD (`0x0024E2D4`), o `func_00220284` a escrever 16 words através de um
ponteiro não-comprometido, e o ciclo de stream FIOS.

## Contabilidade

Não cheguei ao menu. Mas a parede deixou de ser "o boot não chega ao AUTO_LOAD"
(um elo que se provou ser código de encerramento) e passou a ser **uma função
nomeada, com a cadeia inteira desde o `main()` medida degrau a degrau**.

---

# A cadeia fecha: do `main()` até ao objecto de Julho

Rasto de cada chamada dentro do B71 (`PS3_TRACE_B71`, 147 pontos de passagem,
uma reconstrução). O último número impresso é a chamada que não voltou:

```
[B71] func_000B71B8 #041 -> func_0010F5E8
[B71] func_0010F5E8 #001 -> func_0024F24C      ✅ volta
[B71] func_0010F5E8 #002 -> func_0024C878      ✅ volta
[B71] func_0010F5E8 #003 -> ps3_indirect_call  this=0x400C5048  ctr=0x0039D51C
[B71] func_0039D51C #001 -> ps3_indirect_call  this=0x400C5048  ctr=0x0039D3C4  ✅ volta
[B71] func_0039D51C #002 -> ps3_indirect_call  this=0x400C61C8  ctr=0x0039E40C
                                                     ^^^^^^^^^^ (silêncio a partir daqui)
```

`func_0010F5E8` é um **lookup no registry de tipos**:

```c
r0  = rlwinm(*(uint16*)(obj+2), 2, 14, 29);   // (tipo << 2) & 0x3FFFC
r11 = *(TOC-0x4D94 + r0);                      // fabrica = tab[idx]  -> 0x400C5048
ps3_indirect_call(*(vt+0x20));                 // -> func_0039D51C
```

`idx = (tipo<<2) & 0x3FFFC` é a fórmula do registry de tipos que o CLAUDE.md
documenta. É a Parede D, alcançada pelo caminho natural.

E `this = 0x400C61C8` no último salto é **o mesmo objecto** das sondas de
Julho:

```
[E545B0] #1 this=0x400C61C8 arg=0x4063858C sent=0x40638608 head=0x40007F34  <- lista válida
[E545B0] #2 this=0x400C61C8 arg=0x407790D0 sent=0x4077914C head=0x00000000  <- laço infinito
```

## A cadeia inteira, medida ponta a ponta

```
main() func_0025C838
  └─ func_002B2E74                       (7ª de 9 chamadas)
       └─ func_000B71B8  = B71           (9ª de 11)
            └─ #41 func_0010F5E8         lookup no registry de tipos
                 └─ #03 → func_0039D51C  this=0x400C5048 (fábrica)
                      └─ #02 → func_0039E40C  this=0x400C61C8
                           └─ … → func_002545B0 com head=0
                                └─ despacho virtual sobre NULL, em ciclo
                                     └─ breaker aos 2000 → exit(3)
  ✗ func_002B2E04  (8ª) — NUNCA ALCANÇADA
       └─ func_00242C94 — O LOOP PRINCIPAL DO JOGO, nunca corre
```

Cada seta desta cadeia foi **medida**, não inferida. As duas hipóteses de bug
do lifter que levantei pelo caminho foram ambas refutadas contra o binário.

## O que isto vale

O trabalho de Julho tinha nomeado o objecto (`0x400C61C8`) e o sintoma
(`head=0`), mas não sabia **onde na execução** isso acontecia nem **o que
bloqueava**. Agora sabe-se as duas coisas: bloqueia o `main()` na sétima
chamada, e por isso o jogo nunca entra no loop principal.

A pergunta operacional deixa de ser "porque é que o boot não avança" e passa a
ser uma pergunta com sujeito: **porque é que `func_0039E40C`, chamada sobre
`0x400C61C8`, entrega a `func_002545B0` um objecto cuja lista tem `head=0`.**

---

# A ponta da cadeia: `table[0]` vale 5, e é lido como ponteiro

## Onde pendura, exactamente

`vt[0x60]` de `0x400C61C8` é **`func_00254788`** (o mesmo `func_002547AC` que o
histórico já marcava como terceira parede). Dentro do ciclo terminal repetem-se
16 escritas por volta:

```
[vm] UNCOMMITTED write32 ... ra=func_00220284+0x1244/0x1274/0x12A4/0x12D4
```

— escreve-se 16 words através de um ponteiro vindo do pop da free-list.

## O pop recebe o número 5 como pool

```
[FLHEAD] TEXTO pool=0x00000005 pool+4=0x00000009 head=0x726D612E lr=0x00218364
```

**O `lr` mentiu.** Só existe um sítio no lift inteiro que põe `lr=0x00218364`, e
a sonda que lá pus nunca viu o pool mau. O `lr` do guest fica congelado no
último `bl` — num despacho indirecto aponta para o chamador errado. Foi preciso
a cadeia do **host** (`__builtin_return_address` + `dladdr`) para nomear o
chamador real:

```
host#0 func_002182A4+0x99C
host#1 ps3_indirect_call+0xB10
host#2 func_00227528+0x1E0
```

## E o índice está correcto

```
[POOLIDX] MAU pool=0x00000005 slot=0x4007FCE8 base=0x4007FCE8 idx=0
          divisor=130 r28=0x00000004 r30=0x00000001 obj=0x400C6C68
[POOLIDX] ok  pool=0x40773748 slot=0x40773630 base=0x40773630 idx=0
          divisor=128 r28=0x00000005 r30=0x00000001 obj=0x406388F8
```

`idx = 0` nos dois casos. Não é índice fora de alcance. **A tabela do objecto
`0x400C6C68` tem o número 5 na entrada [0]; a do `0x406388F8` tem um ponteiro
válido no mesmo sítio.**

## Quem escreve o 5

```
[WATCHSTORE] w32 [0x400C6C7C]=0x4007FCE8  ra0=func_00263178+0x9CC   <- instala a tabela em obj+0x14
[WATCHSTORE] w32 [0x4007FCE8]=0x00000005  ra0=func_00263178+0x9E8   <- escreve 5 em table[0]
```

Ambas de `func_00263178`, a 0x1C de distância, no mesmo bloco de inicialização.
**O 5 não é corrupção — é o que o alocador escreve de propósito.**

Logo o defeito é um desacordo de layout: `func_00263178` põe um inteiro pequeno
em `table[0]`, e `func_002182A4` lê `table[0]` como ponteiro de pool. Para o
objecto são o mesmo slot tem um ponteiro — os dois objectos foram construídos
por caminhos diferentes (`divisor` 128 vs 130).

## Quinta vez, e desta vez o limite fui eu

O `[POOLIDX] MAU` estava no log desde a primeira corrida. Eu vi 13 linhas, fiz
`head -8`, li "todas ok" e escrevi que a sonda não tinha visto o evento —
chegando a inventar uma explicação (fragmentos duplicados) para uma contradição
que não existia.

As quatro anteriores foram caps hard-coded em sondas. **Esta foi um `head` meu
na linha de comandos.** A regra que escrevi de manhã — cap fixo é mentiroso por
omissão — vale igual para o `head`/`tail` com que se lê o log. Contar primeiro
(`grep -c`), truncar depois.

---

# O 5 é uma contagem, e o lift está fiel: é o objecto que está errado

## O store nomeado

Sonda em todos os 16 `vm_write32` de `func_00263178` (`PS3_TRACE_ALLOCST`):

```
[ALLOCST] store#12 [0x4007FCE8]=0x00000005
```

E `store#12`, no bloco de saída do laço:

```c
r0  = sraw(fim - inicio, 2);   // CONTAGEM de elementos
r7  = inicio;
vm_write32(r12 + 0, r11);      // store#8   *(obj+0x14) = inicio
vm_write32(r7 + 4, r28);       // store#10
vm_write32(r7 + 8, r26);       // store#11
vm_write32(r7 + 0, r0);        // store#12  *(inicio) = contagem
```

**`0x4007FCE8` é um cabeçalho de bloco de três words, e a palavra 0 é a
contagem.** O 5 nunca foi um ponteiro corrompido — é o número de elementos.

## Verificado contra o binário: o lift está fiel

```
0x00263254  rldicl r7,r11,0,32      <- r7 == r11 (o lift diz o mesmo)
0x00263288  stw    r11,0(r12)       <- store#8
0x00263290  stw    r28,4(r7)        <- store#10
0x00263294  stw    r26,8(r7)        <- store#11
```

Nenhum offset perdido, nenhum `addi` em falta. **Terceira hipótese de bug do
lifter levantada hoje, terceira refutada por verificação.**

## Logo o defeito é de tipo, não de tradução

`func_002182A4` faz `base = *(obj+0x14)` e lê `base[idx]` como ponteiro de pool.
Para `obj=0x406388F8` isso dá um ponteiro válido; para `obj=0x400C6C68` dá a
contagem de um cabeçalho. Os dois objectos foram publicados pelo mesmo
alocador mas a partir de contextos diferentes (`ra1` = `func_002BA4B4` num caso,
`func_002B11B8` no outro).

**`0x400C6C68` não é um dono de tabela de pools.** Chegou às mãos de um método
que assume outro tipo.

## E isto é a terceira instância do MESMO padrão

| onde | sintoma | objecto |
|---|---|---|
| `func_002545B0` (Julho) | lista circular com `head=0` — na verdade uma matriz identidade | `0x407790D0` |
| `func_0039E6B4` (hoje, manhã) | tabela de produtos vazia — na verdade uma cópia congelada | `0x47D00000` |
| `func_002182A4` (hoje, agora) | tabela de pools — na verdade um cabeçalho de bloco | `0x400C6C68` |

Três métodos virtuais diferentes, três objectos do tipo errado. Duas das três
já foram explicadas por causas nossas (o stomp do pump, o paliativo TYPE15) e
resolvidas. A terceira tem a mesma forma.

Isto deixa de ser "um bug" e passa a ser uma **hipótese estrutural**: o registry
de tipos entrega a fábrica errada para certos tipos, e cada consumidor descobre
isso à sua maneira. É a Parede D vista de três ângulos.

## O que fica para a próxima sessão

Pergunta fechada, com dois endereços: **quem decide que `0x400C6C68` é o objecto
a passar a `func_002182A4`** — e é o mesmo `idx=(tipo<<2)&0x3FFFC` de
`func_0010F5E8` que já está medido a alimentar esta cadeia.

---

# Quem devia ter inicializado o objecto mau: ninguém o fez

## Os dois campos, lado a lado

`func_002182A4` faz `r29 = r3 + 0x118` e lê `*(r29+0x14)` como tabela de pools.
Vigiando esse campo nos dois objectos:

```
[0x4063890C]=0x00000000   ra0=func_004117C0+0x850     <- zero-init (objecto SÃO)
[0x4063890C]=0x40773630   ra0=func_0022851C+0xD8C     <- array de pools
[0x400C6C7C]=0x4007FCE8   ra0=func_00263178+0x2054    <- bloco CRU (objecto MAU)
```

**Produtores diferentes.** O são recebe um array de pools de `func_0022851C`;
o mau recebe um bloco cru do alocador `func_00263178`, cuja palavra 0 é a
contagem — o 5.

## E o produtor correcto só conhece um objecto

Sonda de entrada em `func_0022851C` (`PS3_TRACE_ALCHAIN`, sem cap):

```
107 chamadas, TODAS com r3 = 0x406387E0
     49x  lr=0x0024E710
     44x  lr=0x0024E654     <- o walker de registos do WAD
     14x  lr=0x002BCAC8
```

`0x406387E0 + 0x118 = 0x406388F8` — o objecto **são**. O pai do objecto mau
(`0x400C6B50`) **nunca é passado a `func_0022851C`**.

> Nota de instrumento: o `ra1` da vigia dizia `func_00411A5C+0x1D8`, mas a sonda
> de entrada mostra que `func_00411A5C` **nunca corre**. Frames do host não
> mapeiam 1:1 em chamadas guest quando há trampolins — o mesmo aviso que já
> tinha escrito hoje sobre o `lr` do guest. Só o `ra0` (o frame imediato) e as
> sondas de entrada são de fiar.

## O que isto quer dizer

O objecto `0x400C6B50` nunca passou pela inicialização de tabela de pools. Não
é que a inicialização tenha corrido mal: **não corre para ele.** E depois um
método que assume essa tabela é invocado sobre ele.

É exactamente a terceira instância do padrão já registado — o método certo, o
objecto errado. E os chamadores do produtor correcto são o walker de registos do
WAD (`0x0024E6xx`/`0x0024E7xx`), a mesma família que aparece nas outras duas
instâncias.

## Estado honesto

Não cheguei ao menu. A parede está localizada até ao nível de "que objecto, que
campo, que produtor, e quem o devia ter chamado". O que falta é a decisão a
montante: **porque é que o registry entrega `0x400C6B50` a um método que exige
um objecto inicializado por `func_0022851C`** — e essa é a mesma pergunta da
Parede D, agora com três testemunhas independentes em vez de uma.

---

# O gate de diagnóstico: move, mas não abre

`PS3_POOL_NULL_IF_BAD=1` recusa usar o número 5 como ponteiro e devolve bloco
nulo. Não inventa memória nem estampa valores — só recusa a conversão.

| | LIST254 só | LIST254 + POOL |
|---|---:|---:|
| `FATAL` | 0 | 0 |
| saltos do gate | — | 1 |
| escritas `UNCOMMITTED` de `func_00220284` no fim | 16/volta | **0** |
| `func_00411A5C` (chamador do produtor correcto) | **0** | **corre** |
| último degrau | B71 | B71 |
| loop principal (`STATE`) | 0 | 0 |

**Move duas coisas reais:** as 16 escritas por ponteiro inválido desaparecem, e
`func_00411A5C` — que nunca corria — passa a correr. É o chamador de
`func_0022851C`, o produtor correcto da tabela de pools. Ou seja, saltar o pop
mau deixa a execução alcançar o caminho de inicialização certo.

**Mas não abre:** o B71 continua sem retornar, o loop principal continua a
nunca correr, e o boot acaba no mesmo ciclo de stream FIOS
(`F2B-STREAM-ENSURE`/`FILL` + SPU jobs + flips).

Conclusão honesta: esta parede não é a última. É pelo menos a terceira em fila
dentro do B71 — `func_002545B0` (head=0), o pop sobre lixo, e o que quer que
prenda o ciclo de stream. Cada uma tem de cair pela raiz, não por gate.

## Balanço da sessão

Entregue:

- **Dois defeitos NOSSOS removidos**, com prova de não-regressão medida:
  o `F2B-STREAM-PUMP` a escrever 770 KiB para lá do ring real, e o paliativo
  TYPE15 REHOME que, morto o stomp, passou a ser a causa de 13 despachos nulos.
- **Dois instrumentos consertados:** o gate media `thr_auto_load() end`, string
  inexistente em todos os 26 binários; e não havia marcador de fim de thread.
- **Um elo do gate provado irrelevante:** o AUTO_LOAD só é criado por código de
  pós-loop; um binário que "passasse" esse elo teria saído do jogo.
- **A cadeia inteira medida**, do `main()` ao campo concreto, degrau a degrau,
  com sonda de controlo em cada bissecção.
- **Três hipóteses de bug do lifter levantadas e as três refutadas** contra o
  binário desmontado, não por opinião.

Não entregue: **o menu.** O gate oficial continua 0/3.
