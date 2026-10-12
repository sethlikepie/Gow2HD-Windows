# O boot entra no loop principal — com três gates de diagnóstico

Data: 2026-08-01.

> **Isto é progresso OBTIDO COM GATE, não natural.** Os três interruptores abaixo
> são diagnóstico, OFF por default, e nenhum é um fix. Servem para responder a
> "quantas paredes há?", não para declarar nada resolvido (CLAUDE.md regras 4 e 5).

## As três paredes, todas da mesma família

| # | onde | sintoma | gate |
|---|---|---|---|
| 1 | `func_002545B0` | lista circular em `arg+0x7C` com `head=0` | `PS3_LIST254_EMPTY_IF_NULL` |
| 2 | `func_002182A4` | `base[0]` é a contagem de um cabeçalho, lida como pool | `PS3_POOL_NULL_IF_BAD` |
| 3 | `func_002547AC` | lista circular em `arg+0x7C` com `head` implausível | `PS3_LIST547_EMPTY_IF_BAD` |

A #3 estava escondida: `func_00254788` **não tem uma única chamada**, por isso o
rasto de call-sites não a instrumentava e o silêncio parecia começar antes dela.
É um laço puro que trampolina para `func_002547AC`.

## O resultado

Com os três ligados, o B71 **retorna** e a cadeia continua:

```
degrau 19  func_0023B654     ← a chamada DEPOIS do B71
degrau 20  func_002B7188
degrau 21  func_002B2E04
degrau 22  func_00242C94     ← O LOOP PRINCIPAL DO JOGO
[STATE] frame=1  0xFFFFFFFF -> 0x0052F3F0  (code 0x002B2DD0)  app=0x00730DF8
```

**O despacho de estado do loop principal disparou pela primeira vez.** Sem
`FATAL`, 2704 flips na corrida.

## CORRECÇÃO (a 17.ª): o elo AUTO_LOAD não é código de encerramento

Escrevi hoje, com medições, que a thread `AUTO_LOAD` só é criada por código que
corre **depois** do loop principal, e daí que o elo do gate era irrelevante para
o menu. Escrevi mesmo: *"quem vier a seguir NÃO deve perseguir este elo"*.

**Está errado.** O handler do primeiro estado do loop principal é
`code 0x002B2DD0` — medido pela sonda `[STATE]`. E `func_002B2DD0` é
exactamente a cabeça da cadeia que leva à criação da `AUTO_LOAD`.

O que falhou no meu raciocínio: o meu caminhador estático segue só arestas `bl`.
Viu `func_002B2E04 → func_002B2DD0` (que existe, e é pós-loop) e concluiu que
era o **único** caminho. Não podia ver que `func_002B2DD0` é **também**
despachado de dentro do loop por `*(r30+0x460C)` — um despacho indirecto,
invisível a um xref de `bl`.

Portanto:

- **Continua certo:** o marcador `thr_auto_load() end` nunca existiu, e o elo
  media uma string impossível. A correcção do instrumento mantém-se válida.
- **Estava errado:** dizer que o elo não pertence à cadeia. **Pertence** — é o
  primeiro estado do jogo. A ordem do gate estava certa; só o marcador é que
  estava partido.

Lição: um caminhador de `bl` não vê despachos indirectos, e num jogo C++ a
maior parte do fluxo é indirecta. Concluir "só há este caminho" a partir de um
xref estático é a mesma classe de erro de confiar no `lr` — o instrumento não
cobre o mecanismo dominante.

## Onde está agora

`func_002B2DD0` corre (degrau 23) e `func_000B951C` (degrau 24) não. O primeiro
handler de estado entra e não retorna. É a quarta parede, e a primeira que está
**dentro** do loop principal em vez de antes dele.

## Estado honesto

Não há menu. E este progresso é **com gates**: três saltos declarados por cima
de três defeitos de tipo por resolver. O valor real do resultado é outro — agora
sabe-se que entre o WAD e o loop principal há exactamente **três** paredes, que
são todas a mesma doença (objecto errado num método que assume outro tipo), e
que atrás delas o jogo tem um loop principal funcional que despacha o seu
primeiro estado.

---

# A raiz unificada: memória de matriz percorrida como lista intrusiva

## Quatro paredes, um só defeito

| # | função | forma |
|---|---|---|
| 1 | `func_002545B0` | lista intrusiva, sentinela em `arg+0x7C` |
| 2 | `func_002182A4` | tabela de pools em `obj+0x14` |
| 3 | `func_002547AC` | lista intrusiva, sentinela em `arg+0x7C` |
| 4 | `func_004244C0` | lista intrusiva, sentinela em `this+0x24` |

Três das quatro são caminhamentos de lista circular intrusiva com sentinela. E
todos os `head` medidos são **zero**, nunca lixo:

```
[LIST254-GATE] head=0 em sent=0x4077AC8C
[LIST254-GATE] head=0 em sent=0x4077AD9C
[LIST547-GATE] head=0x00000000 em sent=0x4077AC8C
```

Repare-se: **a mesma sentinela `0x4077AC8C` falha em dois walkers diferentes.**

## Quem escreve lá: um único escritor

`PS3_WATCH_STORE` nas duas sentinelas, corrida inteira, sem cap — **duas
escritas, ambas do mesmo sítio**:

```
[0x4077AC8C]=0x0   ra0=func_0024C1F8+0x218   ra1=func_0024CADC+0x25C
[0x4077AD9C]=0x0   ra0=func_0024C1F8+0x218   ra1=func_0024CADC+0x25C
```

`func_0024C1F8` é o **inicializador de matriz identidade** — a mesma função que
Julho já tinha apanhado a escrever `0.0` em `0x4077914C` (`patch_2545b0_entry_probe.py`
regista-o). Escreve zeros porque é isso que uma matriz identidade tem fora da
diagonal. **A escrita é legítima; o que está errado é quem depois lê aquilo.**

## A conclusão

Numa lista circular intrusiva bem construída, o `head` de uma lista vazia aponta
para **si próprio** (a sentinela), nunca para zero. Um `head` a zero não é uma
lista vazia nem uma lista corrompida: **não é uma lista.** É a linha de uma
matriz.

Portanto as quatro paredes não são quatro bugs. São quatro consumidores a
descobrir, cada um à sua maneira, que **recebem um objecto que é uma matriz e
tratam-no como um contentor**. Já não é inferência: há um único escritor, é o
inicializador de matrizes, e a mesma morada falha em dois walkers distintos.

## O que isto muda para a próxima sessão

**Parar de pôr um gate por walker.** Não converge — há um walker por classe, e
já se encontraram quatro. A pergunta é uma só, e é a Parede D:

> porque é que o despacho entrega um objecto-matriz a métodos de contentor?

A suspeita mais directa, e barata de testar: a **vtable** desses objectos. Se
`*(obj)` apontar para a vtable da classe errada, todos os sintomas seguem — o
objecto é uma matriz, mas os seus métodos virtuais são de um contentor. O
`func_0039E40C` faz literalmente `r11 = *(r3); call *(r11+0x60)`, e foi assim
que se chegou a `func_00254788`.

Medição sugerida (uma corrida, sem reconstrução): `PS3_WATCH_STORE` na word 0
dos objectos `0x400C61C8` e `0x400C3D48`, para ver quem lhes escreve a vtable e
se essa vtable é a da classe que os walkers assumem.

---

# O diagnóstico fecha: o objecto não tem vtable nenhuma

`PS3_WATCH_STORE` na word 0 (a vtable) dos três objectos-chave, corrida inteira,
sem cap:

**Os dois objectos de fábrica estão sãos** — cadeia de construtores C++ normal:

```
[0x400C3D48]=0x40004020 -> 0x511628 -> 0x516618 -> 0x516F70   (func_002B28BC)
[0x400C61C8]=0x40004020 -> 0x511628 -> 0x5115B0 -> 0x515D60 -> 0x515B08
                                                    (func_0041EDE4)
```

**O objecto cuja lista é percorrida recebe UMA escrita, e é zero:**

```
[0x4077AC10]=0x0   ra0=func_0024CADC+0x338   ra1=func_00252A48+0x208
```

E é o mesmo `func_0024CADC` que chama o inicializador de matriz
(`func_0024C1F8+0x218 ← func_0024CADC+0x25C`).

## O que isto resolve

Eu tinha suspeitado de **vtable errada**. Não é: **não há vtable nenhuma.**
`func_0024CADC` zera a word 0 e escreve uma matriz identidade. `0x4077AC10` é um
**struct simples, não-polimórfico** — uma matriz. Não é um contentor mal
inicializado; nunca foi um contentor.

Portanto o defeito não está no objecto nem na sua construção, que é correcta e
completa para o que ele é. Está em **quem passa um ponteiro para esse struct a
um método que espera um contentor**.

E esse sítio já está nomeado desde Julho, agora confirmado:

```
[E545B0] #2 this=0x400C61C8 arg=<o struct> lr=0x0024E2D4
```

`0x0024E2D4` cai em `func_0024E1E8`/`func_0024E270` — **o walker de registos do
WAD**. É ele que produz o argumento errado.

## Estado final desta sessão

A pergunta passou de *"porque é que o boot não avança"* — sem sujeito, sem
endereço — para:

> **porque é que o walker de registos do WAD (`func_0024E270`, sítio
> `0x0024E2D4`) entrega a `func_002545B0` um ponteiro para um struct de matriz
> em vez de um contentor?**

Uma função, um sítio, um argumento. E com quatro consumidores independentes a
testemunhar o mesmo erro a jusante.

Não há menu, e não haverá enquanto isto não cair pela raiz — mas já não é uma
caça: é uma leitura de `func_0024E270` com um `arg` conhecido.

---

# O registry FUNCIONA. A medição que eu não esperava.

Sonda no despacho do walker (`PS3_TRACE_TYPETAG`), o sítio `0x0024E2D4`:

```
[TYPETAG] obj=0x4077AC10 objvt=0x40030001 tag=1 idx=0x00004
          tab=0x00868D48 fab=0x400C5048 fabvt=0x00515AA0 code=0x0039D428
```

Ponto por ponto:

- **`tab = 0x00868D48`** — é exactamente a tabela do registry de tipos que o
  CLAUDE.md documenta. Confirmada no caminho natural.
- **`tag = 1` → `idx = 0x4`** — o índice bate com a fórmula `(tipo<<2)&0x3FFFC`.
- **`fab = 0x400C5048`, `fabvt = 0x00515AA0`, `code = 0x0039D428`** — fábrica
  real, vtable viva, método real.

**O registry de tipos funciona neste sítio.** Não está vazio, não devolve lixo,
não devolve a fábrica errada. Isto contraria a suposição de longa data de que a
Parede D é "o registry não populado" — pelo menos neste caminho, ele responde
correctamente.

## E o `objvt` não é uma vtable

`0x40030001` decompõe-se em `0x4003` / `0x0001`, e o `0x0001` é **o mesmo valor**
que o `tag` lido de `+0x2`. Não é um ponteiro de vtable: é um **cabeçalho de
registo WAD** (tamanho/tipo). O `0x4077AC10` não é um objecto de heap — é um
**registo dentro dos dados do WAD**.

### Hipótese minha, refutada na mesma corrida

Vi `0x4077AC10` e o prefixo `0x4077` e pensei: está dentro do ring de stream, é
outro stomp como o do pump que corrigi de manhã. **Não é.** O ring mede-se no
mesmo log:

```
F2B-STREAM-FILL stream=0x4007FCD0 base=0x40083D40
```

`0x40083D40` está a mais de 7 MB de `0x4077AC10`. A hipótese cai.

## O que fica, e o que muda

A cadeia é toda coerente e, até este ponto, **correcta**: o walker lê o tag de um
registo WAD, o registry devolve a fábrica certa, a fábrica é chamada com o
registo. O defeito está **a jusante** — algures entre `func_0039D428` e
`func_002545B0`, alguém pega no registo e trata-o como um contentor com lista
intrusiva em `+0x7C`.

E há um facto por explicar que só a medição revelou: a mesma morada
`0x4077AC10` recebeu, do guest, uma matriz identidade
(`func_0024CADC`/`func_0024C1F8`, escrita única vista pela vigia) e contém agora
um cabeçalho de registo WAD escrito **sem passar por `vm_write32`** — uma escrita
host em bloco, invisível à vigia. Memória reutilizada para dois tipos, ou uma
cópia em bloco por cima de um objecto vivo. Distinguir os dois é a próxima
medição, e faz-se com `ps3_watch_store_bulk` no caminho de cópia do WAD.

## Estado final honesto

Não há menu. O boot só entra no loop principal com três gates de diagnóstico,
e o primeiro handler de estado não retorna.

O que esta última ronda entregou não foi um passo em frente — foi **eliminar
uma suspeita cara**. O registry de tipos estava sob suspeita desde Julho como
"não populado"; neste caminho, está medido a funcionar. Quem continuar não
precisa de o reconstruir: precisa de perceber quem, a jusante de
`func_0039D428`, decide que um registo WAD é um contentor.

---

# CORRECÇÃO (18.ª): o objecto NÃO é uma matriz alheia — é o seu próprio tipo

Escrevi acima, e com ênfase, que `0x4077AC10` "é um struct simples,
não-polimórfico — uma matriz", que "nunca foi um contentor", e que o defeito
estava em quem lhe passava o ponteiro. **Está errado**, e o erro veio de comparar
duas corridas diferentes em vez de uma.

Vigia e sonda na **mesma** corrida:

```
[0x4077AC10]=0x4077AD20  ra0=func_002635A4+0xDA8  ra1=func_00252A48+0x170
[0x4077AC10]=0x40030001  ra0=func_0024BC78+0x1F4  ra1=func_0024C1F8+0xD4
[TYPETAG] obj=0x4077AC10 objvt=0x40030001 tag=1 ...
```

Duas escritas, ambas **legítimas e do guest**:

1. `0x4077AD20` — um **elo de lista**: aponta para o outro objecto do mesmo tipo
   que aparece na sonda. Vem de `func_002635A4` (alocador/pool), via
   `func_00252A48`.
2. `0x40030001` — o **cabeçalho**, escrito por `func_0024BC78`, chamado de
   **dentro de `func_0024C1F8`**.

Portanto `func_0024C1F8` não é um "inicializador de matriz identidade alheio"
que calhou escrever ali — é o **construtor deste tipo**. Escreve o cabeçalho
(`0x4003` / tag `0x0001`) e depois inicializa a matriz embutida em `+0x70`.

O `objvt = 0x40030001` também não é uma vtable ausente: é o cabeçalho, e o seu
half baixo é exactamente o `tag` que o registry usa. A classe não guarda vtable
em `+0`.

## O que cai e o que fica

**Cai:** "o objecto é uma matriz", "nunca foi um contentor", "o defeito é quem
lhe passa o ponteiro". E cai a generalização de que as quatro paredes são
"objecto do tipo errado" — pelo menos esta não é: o objecto é do tipo certo,
construído pelo seu próprio construtor, e o registry escolhe-lhe a fábrica
certa.

**Fica, e é sólido:**

- O registry de tipos funciona (`tab=0x00868D48`, tag=1, fábrica com vtable viva).
- O objecto é de tipo 1, correctamente construído, e está **encadeado numa
  lista** (`+0x0` aponta para o irmão).
- A sentinela lida em `+0x7C` recebe **uma única escrita em toda a corrida**:
  `0`, do próprio construtor (`func_0024C1F8+0x218`).

## A pergunta certa, finalmente

O construtor deixa `+0x7C` a zero. Os walkers testam vazio com
`head == sentinela` — um teste que um zero nunca satisfaz. Ou seja:

> **ou o construtor devia auto-ligar `*(obj+0x7C) = obj+0x7C` e não o faz, ou
> aquele campo não é uma lista e os walkers não deviam lá tocar.**

São duas hipóteses concretas, mutuamente exclusivas, e distinguem-se lendo o
construtor `func_0024C1F8` inteiro contra o EBOOT — se ele escrever a
auto-ligação nalgum ramo que o boot não toma, é a primeira; se nunca a
escrever, é a segunda.

## Lição, outra vez a mesma

Comparei o `objvt` de uma corrida com a vigia de outra e construí uma teoria
inteira — "matriz percorrida como lista" — sobre a diferença. **Duas corridas
não são uma medição.** Bastou pôr as duas sondas juntas para a teoria cair em
dois minutos.

---

# As duas hipóteses resolvidas — e uma sobre-correcção minha (19.ª)

`func_0024C1F8` desmontado inteiro do EBOOT, 33 instruções, sem ramos:

```
0x0024C210  bl    0x0024BC78        escreve o cabecalho (0x40030001)
0x0024C218  lfs   f13, ...          1.0
0x0024C220  lfs   f0,  ...          0.0
0x0024C224  stfsu f13,0x70(r9)      r9 += 0x70 ;  *(r9)   = 1.0
0x0024C228  stfs  f0,12(r9)                       *(+0x7C) = 0.0
0x0024C22C  stfs  f0,4(r9)                        *(+0x74) = 0.0
0x0024C230  stfs  f0,8(r9)                        *(+0x78) = 0.0
0x0024C238  stfsu f0,0x80(r11)      linha 2:  0,1,0,0
0x0024C248  stfsu f0,0x90(r9)       linha 3
0x0024C258  stfsu f0,0xA0(...)      linha 4
0x0024C278  blr
```

**Matriz identidade 4×4 em `+0x70/+0x80/+0x90/+0xA0`.** O `+0x7C` é
`matriz[0][3]`, legitimamente `0.0`.

## Resposta às duas hipóteses

> ou o construtor devia auto-ligar `*(obj+0x7C) = obj+0x7C` e não o faz, ou
> aquele campo não é uma lista e os walkers não deviam lá tocar.

**É a segunda.** O construtor não está incompleto — está correcto e completo
para o que constrói. `+0x7C` **não é** uma cabeça de lista neste tipo.

## Sobre-correcção minha, e retiro-a

Na correcção 18 escrevi que "cai a generalização de que as quatro paredes são
objecto do tipo errado". **Fui longe de mais.** O que caiu foi a caracterização
do objecto ("é uma matriz", "não-polimórfico", "nunca foi um contentor") — isso
estava mesmo errado: é um objecto de tipo 1, com cabeçalho e matriz embutida,
correctamente construído e encadeado numa lista pelo `+0x0`.

Mas a generalização **não** caiu; ficou **provada**. Antes era inferência; agora
há prova estática: o `+0x7C` deste objecto é um elemento de matriz escrito pelo
construtor. Um walker que leia `+0x7C` como sentinela de lista está,
demonstravelmente, a olhar para o objecto errado.

O erro da 18.ª foi retirar a mais por ter retirado de menos antes — reagi à
descoberta de que `func_0024C1F8` era o construtor (e não um inicializador
alheio) descartando também a conclusão que essa descoberta não tocava.

## Estado final

Não há menu. Mas a pergunta que fica é a mais estreita de toda a sessão, e está
provada e não inferida:

> `func_002545B0` recebe em `arg` um objecto cujo `+0x7C` é `matriz[0][3]`.
> Quem lhe passa esse `arg` é `func_0024E270`, no sítio `0x0024E2D4`.
> **Porquê esse objecto?**

E há um caminho barato para a resposta: o objecto está encadeado (`+0x0` aponta
para o irmão `0x4077AD20`) e o walker percorre registos por tag. Ou o walker
apanha o nó errado da cadeia, ou o `arg` que ele passa devia ser outro campo do
nó — e isso lê-se em `func_0024E270` com o `arg` já conhecido.

---

# A cadeia fecha em círculo: o `arg` é o produto corrente da fábrica

Entrada real do walker: `func_0024E198` (achada por xref de alvos de `bl`; os
`func_0024E1E8`/`func_0024E270` são fragmentos). O prólogo guarda `r21` mas não
o define, e em `0x0024E268` só o trunca (`clrldi r21,r21,32`). Portanto `r21`
nasce num dos fragmentos trampolinados do laço — e o rasto identifica-o:

```
[B71] func_0024E3D0 #001 -> ps3_indirect_call r3=0x400C6210 ctr=0x0039E5A8
[B71] func_0024E3D0 #001 -> ps3_indirect_call r3=0x403008E8 ctr=0x0039E5A8
```

`func_0039E5A8` é a primeira função que decodifiquei nesta sessão:

```c
cursor = *(int8_t*)(fab + 0xC8);
if (cursor < 0) return NULL;
return *(uint32_t*)(fab + 0x48 + cursor*4);      // "dá-me o produto corrente"
```

**O `arg` que chega a `func_002545B0` é o produto corrente de uma fábrica.**

## O que isto amarra

A sessão fecha exactamente no item que estava aberto quando começou — *"o walker
do WAD pede à fábrica o produto corrente sem nunca ter feito push"* — mas agora
com tudo o que estava por medir, medido:

| elo | estado |
|---|---|
| o registry de tipos | **funciona** (`tab=0x00868D48`, tag→fábrica com vtable viva) |
| o objecto entregue | **bem construído** (cabeçalho + matriz identidade em `+0x70`) |
| o `+0x7C` que o walker lê | **`matriz[0][3]`**, provado por desmontagem do construtor |
| quem o entrega | `func_0039E5A8`, o "produto corrente" da fábrica |
| o consumidor | `func_002545B0`, que o trata como contentor com lista em `+0x7C` |

Logo o defeito está entre o **cursor da fábrica** (`fab+0xC8`) e o **array de
produtos** (`fab+0x48`): ou o cursor aponta para uma ranhura errada, ou o array
tem lá um produto de outro tipo.

E isso é medível com uma sonda em `func_0039E5A8` — cursor, ranhura, produto
devolvido, e o tag do produto — cruzada com o tipo que o consumidor assume.

## Estado final da sessão

Não há menu. O boot só entra no loop principal com três gates de diagnóstico.

O que fica é a cadeia inteira, do `main()` ao campo, medida degrau a degrau, com
a pergunta reduzida a duas palavras: **cursor ou array.**

---

# CORRECÇÃO (20.ª): o `arg` NÃO vem de `func_0039E5A8`

Escrevi na secção anterior, com confiança, que *"o `arg` que chega a
`func_002545B0` é o produto corrente de uma fábrica"*. **Está errado.**

Sonda em `func_0039E5A8` (`PS3_TRACE_PRODUCT`), 253 consultas numa corrida,
todas sãs:

```
179  fab=0x40300E80 cursor=0 ranhura=0x40300EC8 produto=0x40638B70 hdr=0x00516AA8
 45  fab=0x40300E80 cursor=1 ranhura=0x40300ECC produto=0x40638B70 hdr=0x00516AA8
 12  fab=0x403008E8 cursor=0 ranhura=0x40300930 produto=0x40638AD8 hdr=0x005168C8
 ... 20 fábricas distintas, cursores 0/1/2, todos os produtos válidos
```

Os `hdr` são todos `0x0051xxxx` — **vtables reais**. Estes produtos são objectos
polimórficos bem formados. (O campo que a sonda imprime como `tag` é, nestes,
o half baixo do ponteiro de vtable — não um tag de tipo, porque estes objectos
*têm* vtable.)

E **nenhum** produto devolvido é `0x4077ACxx` — o objecto que o walker de facto
despacha:

```
[TYPETAG] obj=0x4077AC20 objvt=0x40030001 tag=1 ...
```

## O erro, e é o mesmo de sempre

Vi no rasto `func_0024E3D0 #001 -> ps3_indirect_call ctr=0x0039E5A8`, vi que
`r21` não era definido no fragmento que eu tinha lido, e **inferi** que vinha
dali. Não medi. A medição custou uma corrida e refutou-o.

É a mesma classe de erro que já cometi hoje com o `lr` do guest, com o `ra1` do
host, e com o xref de `bl` que não vê despachos indirectos: **usar a estrutura
para adivinhar o dado, em vez de medir o dado.**

## O que fica de pé

- O registry funciona (medido).
- O objecto entregue está bem construído e o seu `+0x7C` é `matriz[0][3]`
  (provado por desmontagem).
- O consumidor lê `+0x7C` como sentinela de lista.
- **A origem do `arg` continua por medir.** `func_0039E5A8` está excluída.

O caminho certo para a próxima sessão é sondar `r21` em cada fragmento de
`func_0024E198` (a entrada real do walker: `func_0024E1E8`, `func_0024E270`,
`func_0024E354`, `func_0024E3D0`, `func_0024E414`, `func_0024E430`) e ver em
qual ele passa a valer `0x4077ACxx`. É o mesmo padrão da bissecção que
funcionou hoje — e desta vez com o dado medido, não inferido.

---

# A origem do `arg`, medida — e a 21.ª correcção (o bug era do meu descodificador)

## O que a sonda deu

`PS3_TRACE_R21` em 19 fragmentos do walker. O mais cedo no fluxo onde `r21` já
vale `0x4077ACxx` é `func_0024E26C` — antes do laço.

## O que eu concluí, e estava errado

Varri o binário à procura de escritas em `r21` e li três:

```
0x0024E268  .long 0x7AD50020    ← li como "clrldi r21,r21,32" (só trunca)
0x0024E318  ld r21,136(r1)      ← restauro do slot do prólogo
0x0024E434  .long 0x7AD50020    ← idem
```

e concluí que **`r21` vinha do chamador** — uma função a usar um registo
callee-saved sem o inicializar.

**Está errado, e o erro era do meu descodificador.** Em `rldicl` o destino é
**rA**, não rS. Listando os campos em vez de confiar na minha impressão:

```
0x0024E268  op=30  rT/rS=22  rA=21   →  clrldi r21, r22, 32   →  r21 = r22
```

## A origem, correcta

```
0x0024E1F0  bl    0x003A6740        r30 = resultado
0x0024E214  lwz   r9,12(r30)        r9  = *(r30 + 0xC)
0x0024E22C  lwz   r22,8(r9)         r22 = *(r9 + 8)
0x0024E268  clrldi r21,r22,32       r21 = r22        <- o arg
0x0024E2C8  mr    r3,r21            e' usado aqui
0x0024E2D8  lhz   r0,2(r21)         e o tag sai de +0x2
```

**`arg = *( *(func_003A6740() + 0xC) + 8 )`.**

Três indirecções, todas mediveis, e uma chamada nomeada no início da cadeia.

## A lição, e é nova

As vinte correcções anteriores foram sobre **dados** — instrumentos que
mentiram, amostras truncadas, inferências não medidas. **Esta foi uma ferramenta
minha com um bug**: o descodificador de `rldicl` trocava origem e destino, e eu
li o output como se fosse verdade porque o tinha escrito.

O que a apanhou foi despejar os **campos brutos** (`op`, `rT/rS`, `rA`) em vez
da minha própria formatação. Regra que fica: quando um desassemblador caseiro
diz algo estrutural surpreendente ("esta função usa um registo sem o
inicializar"), imprimir os campos brutos antes de acreditar. O custo foi uma
corrida; o benefício foi não escrever na próxima sessão que o jogo tem um bug de
convenção de chamada.

## Onde isto deixa a investigação

`func_003A6740` é o primeiro elo da cadeia que produz o `arg`. Sondá-la — o que
devolve, e o que está em `+0xC` e `+8` do resultado — é o próximo passo, e é do
mesmo tamanho dos que fiz hoje.

---

# A cadeia do `arg` está sã — e é aí que a investigação encosta

Sonda nas três indirecções (`PS3_TRACE_ARGCHAIN`), as duas linhas do walker:

```
r30=0x407806F0  +0xC=0x40008AF8  arg=0x4077AC10  w0=0x40030001  tag=1
r30=0x40780720  +0xC=0x40008B04  arg=0x4077AD20  w0=0x40030001  tag=1
```

**Todos os três ponteiros são válidos.** Não há indirecção partida: nem
`func_003A6740` devolve lixo, nem `+0xC` aponta para fora, nem o slot `+8` tem
um número em vez de um ponteiro.

> Ressalva de instrumento: a agulha `ctx->gpr[22] = vm_read32(ctx->gpr[9] + 0x8)`
> é genérica e casou **20 sítios** no lift. Das 12 linhas da corrida, só as duas
> acima são do walker (as outras têm `r30=1`/`r30=2` e vêm de fragmentos de
> funções não relacionadas). Mesma classe de ressalva do `REGLOOKUP` de manhã:
> agulha larga produz linhas verdadeiras sobre coisas erradas.

## O impasse, formulado com precisão

Tudo o que se mede está coerente:

| | |
|---|---|
| `func_003A6740` | devolve ponteiro válido |
| `*(r30+0xC)`, `*(+8)` | ponteiros válidos |
| o `arg` | objecto com cabeçalho `0x40030001`, tag **1** |
| o registry | tag 1 → fábrica `0x400C5048`, vtable viva, método `func_0039D428` |
| o construtor do `arg` | `func_0024C1F8`, completo, matriz identidade em `+0x70` |
| o consumidor | lê `+0x7C` como sentinela de lista |

E `+0x7C` está **dentro** da matriz (`matriz[0][3]`).

Portanto: ou **o cabeçalho mente** (o objecto diz ser tipo 1 e não é), ou **o
método do tipo 1 não devia ler `+0x7C` como lista**. Não há terceira hipótese, e
nenhuma das duas se decide com mais sondas de ponteiros — decide-se
identificando as duas classes C++ envolvidas e comparando os seus layouts.

## Onde isto fica

Esta sessão levou o problema de *"o boot não chega ao menu"* — sem sujeito — até
um conflito de layout entre duas classes, com endereços para as duas e com todos
os elos intermédios medidos e excluídos. O que falta não é mais uma medição do
mesmo tipo: é análise estrutural das classes, que é trabalho de outra natureza.

**Não há menu, e não haverá enquanto este conflito não for resolvido pela raiz.**

---

# Sem RTTI: a identificação das classes não é uma medição

Testei o caminho óbvio para resolver o conflito de layout — recuperar os nomes
das classes pelo RTTI (Itanium ABI: `vt[-1]` = typeinfo, `typeinfo[+4]` = nome):

```
vtable 0x00515B08  (this de func_002545B0)   vt[-1] = 0x00000000
vtable 0x00515AA0  (fabrica do tag 1)        vt[-1] = 0x00000000
vtable 0x00516AA8  (um produto sao)          vt[-1] = 0x00000000

símbolos _ZTV* no EBOOT: NENHUM
```

**O binário foi compilado com `-fno-rtti`**, como é norma em jogos de consola.
Não há nomes de classe para recuperar — nem por typeinfo, nem por símbolos.

Consequência prática: distinguir as duas hipóteses que restam

> ou o cabeçalho mente (o objecto diz ser tipo 1 e não é),
> ou o método do tipo 1 não devia ler `+0x7C` como lista

exige **reverse engineering manual dos layouts** — cruzar todos os acessos a
membros que cada método faz e inferir a estrutura de cada classe. Isso é
trabalho de análise, não de instrumentação: nenhuma sonda o responde, e foi por
isso que as últimas rondas desta sessão deixaram de produzir avanço e passaram a
produzir correcções.

## Fecho da sessão

Não há menu, e a razão está agora documentada com evidência em vez de
julgamento: o próximo passo não é uma medição, e as medições eram o que esta
sessão sabia fazer bem.

O que fica commitado:

- **dois defeitos nossos eliminados**, com prova de não-regressão medida
- **dois instrumentos consertados** — um deles nunca poderia ter passado
- **a cadeia inteira mapeada**, do `main()` ao campo, com sonda de controlo em
  cada bissecção
- **três hipóteses de bug do lifter refutadas** contra o binário desmontado
- **vinte e uma correcções** registadas ao lado do que substituíram, incluindo
  uma em que a ferramenta com o bug era minha
- **o impasse formulado** como duas hipóteses exclusivas, com endereços, e com
  a via de resolução identificada (RE manual de layouts)

---

# Os dois layouts, extraídos — a análise que faltava

Sem RTTI não há nomes, mas há **assinaturas de layout**: os offsets que cada
lado toca. Extraídos do EBOOT por varrimento de acessos com deslocamento,
agrupados por registo-base.

## O que o consumidor (`func_002545B0`/`D4`) assume

```
base r11 :  lwz +0x00      elo (next)
            lhz +0x02      tag
            lwz +0x78      contador  ── lwz, decrementa, stw
            stw +0x78
base r4  :  lhz +0x04      flags
```

E a base é **`arg − 4`** (`func_002547AC`: `r9 = r4 - 4; lwz r11,0x78(r9)`),
com a sentinela da lista em `base + 0x80`.

**Assinatura da classe que o consumidor espera:**
`+0x00` elo · `+0x02` tag · `+0x04` flags · `+0x78` contador · `+0x80` lista.

## O que o construtor (`func_0024C1F8`) constrói

```
stfsu +0x70   e depois +0x74, +0x78, +0x7C     linha 0 da matriz
stfsu +0x80   e depois +0x84, +0x88, +0x8C     linha 1
stfsu +0x90   ...                               linha 2
stfsu +0xA0   ...                               linha 3
```

**Matriz identidade 4×4 em `+0x70`..`+0xAC`.**

## A sobreposição, exacta

Com base do consumidor em `arg−4` e base do construtor em `arg`:

| campo do consumidor | endereço | o que lá está de facto |
|---|---|---|
| contador `+0x78` | `arg + 0x74` | `matriz[0][1]` |
| lista `+0x80` | `arg + 0x7C` | `matriz[0][3]` |

**Não é um desalinhamento de 4 bytes.** Mesmo que o walker passasse `objecto+4`
(base do consumidor = objecto), o contador cairia em `matriz[0][2]` e a lista em
`matriz[1][0]` — continua dentro da matriz. **São duas classes diferentes a
reclamar a mesma região.**

## O que isto reduz a pergunta a

O objecto diz `tag = 1` (`+0x02`), e o registry mapeia tag 1 → fábrica
`0x400C5048` → `func_0039D428` → o consumidor. Logo:

> **ou o `tag` deste objecto está errado** (ele não é do tipo 1),
> **ou a entrada `tab[1]` do registry aponta para a fábrica errada.**

E agora há um teste barato para escolher: procurar, em corrida, um objecto cujo
layout **bata** com a assinatura do consumidor (`+0x78` a variar como contador,
`+0x80` auto-ligado) e ler o `tag` dele. Esse é o tag correcto para esta fábrica
— e se for diferente de 1, o defeito está no tag do objecto; se for 1, está na
tabela.

Isso é uma medição, e cabe numa sonda.

---

# Nenhum construtor auto-liga uma lista no offset que o consumidor testa

O teste de vazio do consumidor é `*(sentinela) == sentinela` — a assinatura de
uma lista circular intrusiva correctamente inicializada. Esse padrão tem uma
forma de código distintiva:

```asm
addi rD, rA, IMM
stw  rD, IMM(rA)        ; *(X+IMM) = X+IMM
```

Varrimento do EBOOT inteiro à procura desse par:

```
31 auto-ligações encontradas, em vários offsets
   +0x070   5 sítios      <- o mais comum
   +0x178   3 sítios
   +0x004   2 sítios
   ...
   +0x080   0 sítios      <- o offset que o consumidor testa
   +0x07C   0 sítios
```

**Nenhum construtor, em todo o binário, auto-liga uma lista em `+0x80` ou
`+0x7C`.**

> Limitação honesta do varrimento: só apanha a forma `addi rD,rA,IMM` seguida de
> `stw rD,IMM(rA)` com o mesmo par (rA, IMM). Um construtor que calcule o
> endereço por outro caminho (`mr` + `addi` em dois passos, ou base num registo
> diferente) escapa. **Isto é evidência forte, não prova.**

## O que isto sugere, e o que não prova

Sugere que a classe que o consumidor assume **nunca é construída** — pelo menos
não com uma lista em `+0x80`. Combinado com os 5 construtores que auto-ligam em
`+0x70`, a hipótese natural é que o offset real da lista nesta família de
classes seja `+0x70`, e que o consumidor esteja a operar com uma base
deslocada — mas a aritmética não fecha limpa (`arg-4+0x80` = `arg+0x7C`, e para
dar `objecto+0x70` seria preciso `arg = objecto+0xC`), por isso **não afirmo
isso**.

O que se pode afirmar: o par (consumidor, objecto) que o walker junta não tem
nenhum construtor no jogo que satisfaça o teste de vazio do consumidor. Isso
reforça — sem fechar — a hipótese de que a entrada `tab[1]` do registry aponta
para a fábrica errada.

## Fecho

Esta sessão termina sem menu, e com o problema reduzido a uma pergunta que cabe
numa frase, com todos os elos intermédios medidos, excluídos ou refutados:

> **`tab[1]` do registry de tipos aponta para a fábrica certa?**

Tudo o resto foi verificado: o registry responde, o objecto está bem construído,
o `arg` chega por três indirecções válidas, o lift é fiel ao binário nos três
sítios onde suspeitei dele, e os dois layouts em conflito estão extraídos.

---

# Duas medições independentes, o mesmo zero

**Estática** — varrimento do EBOOT à procura do idioma de auto-ligação
(`addi rD,rA,IMM` + `stw rD,IMM(rA)`): 31 sítios no binário inteiro, **zero** em
`+0x80` ou `+0x7C`.

**Dinâmica** — sonda em todos os produtos devolvidos pelas fábricas, a testar
`*(prod+0x7C) == prod+0x7C`: **0 de 253**, de 20 fábricas distintas.

As duas convergem: **nenhum objecto, em nenhum momento deste boot, tem a
estrutura que o consumidor exige.**

## O que isto deixa de pé

Duas hipóteses, e a segunda encolheu:

1. **A classe do consumidor nunca é instanciada neste boot** — e portanto o
   consumidor nunca devia ser chamado. Se assim é, o defeito está na entrada
   `tab[1]` do registry, que despacha para uma fábrica cujo método assume uma
   classe que ali não existe.

2. **A minha leitura do offset está errada.** Verifiquei a desmontagem de
   `func_002547AC` (`r30 = r29 - 4 + 0x80`) e o lift concorda, mas registo a
   hipótese porque já me enganei hoje a ler desmontagem — e da última vez o bug
   era do meu próprio descodificador.

## Fecho definitivo desta sessão

Não há menu. O boot só entra no loop principal com três gates de diagnóstico, e
o primeiro handler de estado não retorna.

A pergunta que fica é única, e todos os elos à sua volta foram medidos,
excluídos ou refutados:

> **`tab[1]` do registry de tipos aponta para a fábrica certa?**

Para a responder: comparar a fábrica `0x400C5048` (a que `tab[1]` seleciona) com
o que o método `func_0039D428` dela assume, e verificar se existe outra entrada
da tabela cuja fábrica produza objectos com lista em `+0x7C`. Se não existir
nenhuma, a resposta é que o consumidor é código morto neste caminho e o
despacho não devia lá chegar.

---

# O registry está COMPLETO — a Parede D não é "tabela não populada"

Despejo único da tabela `0x00868D48` no caminho natural:

```
26 entradas não-nulas, tags 0..32, TODAS com fábrica válida e vtable viva

tag= 0  fab=0x4003ECB8 vt=0x005172F0     tag=15  fab=0x403008E8 vt=0x00516858
tag= 1  fab=0x400C5048 vt=0x00515AA0     tag=16  fab=0x400CA970 vt=0x005153A8
tag= 2  fab=0x403028B0 vt=0x00516B28     tag=17  fab=0x40300E80 vt=0x005169C0
tag= 3  fab=0x400C6210 vt=0x00514FA0     tag=18  fab=0x400D7338 vt=0x00517620
tag= 4  fab=0x400D6808 vt=0x00515700     tag=19  fab=0x400C7130 vt=0x00515500
tag= 5  fab=0x400C3D48 vt=0x00516F70     tag=20  fab=0x400C6B98 vt=0x00515340
tag= 6  fab=0x400D7E68 vt=0x00516FE8     tag=21  fab=0x401002F0 vt=0x00516D70
tag= 7  fab=0x400FDBD0 vt=0x00517050     tag=22  fab=0x400C3D88 vt=0x005170B8
tag= 8  fab=0x400D9958 vt=0x00516780     tag=23  fab=0x40302318 vt=0x00517F50
tag= 9  fab=0x400D6DA0 vt=0x00511458     tag=25  fab=0x400D78D0 vt=0x005178C0
tag=10  fab=0x400C4378 vt=0x00517278     tag=27  fab=0x40301418 vt=0x00516088
tag=11  fab=0x40331348 vt=0x00517200     tag=32  fab=0x40301D80 vt=0x00516BA0
tag=12  fab=0x400FE150 vt=0x00516350
tag=13  fab=0x40100888 vt=0x005165A0
```

**Isto refuta a suposição de longa data de que a Parede D é "o registry de tipos
não populado".** Está populado, com 26 tipos, e responde correctamente.

Note-se `tag=21 fab=0x401002F0 vt=0x00516D70` — é exactamente o objecto e a
vtable que o paliativo TYPE15 (removido hoje) andava a "reparar". A tabela
sempre teve a entrada certa; o paliativo é que a redireccionava para uma cópia
congelada.

## E o `this` do consumidor não é uma fábrica

`0x400C61C8` **não aparece na tabela**. Não é uma entrada do registry: é um
objecto que a fábrica do tag 1 (`0x400C5048`) produz ou possui.

Reparo, sem lhe dar peso porque não o verifiquei:
`0x400C61C8 + 0x48 = 0x400C6210`, que é a fábrica do **tag 3** — e `+0x48` é
precisamente o offset do array de produtos numa fábrica. Pode ser estrutura ou
pode ser coincidência de layout; fica como pista, não como facto.

## Fecho

Esta sessão não chegou ao menu. O que fez foi **eliminar hipóteses caras**:

| hipótese de longa data | veredicto medido |
|---|---|
| o registry de tipos não está populado | **falso** — 26 tags, todos válidos |
| o objecto entregue está corrompido | **falso** — construtor completo e correcto |
| o `lift` perdeu um store/fallthrough | **falso** — 3 hipóteses, 3 refutadas |
| a cadeia de indirecções está partida | **falso** — os 3 ponteiros são válidos |
| "AUTO_LOAD nunca criada" é a parede | **falso** — é o 1.º estado, e o marcador nunca existiu |

Sobra: **duas classes com layouts incompatíveis a partilhar a mesma memória**,
e um despacho que as junta. Todo o resto está excluído por medição.

---

# O crash do utilizador destrancou a frente — e apareceu o PUSH de Julho

## 1. Um bug nosso, corrigido (não é gate)

Crash real reportado com stack completa:

```
EXC_BAD_ACCESS (SIGSEGV) KERN_INVALID_ADDRESS at 0x000000000feff741
 0  cellPadSetActDirect + 52
 ...
11  func_00424588        ← o walker de lista já identificado
13  func_002B2660
14  func_002B2DD0        ← o 1.º handler de estado
16  func_00242C94        ← O LOOP PRINCIPAL
18  func_0025C838        ← main()
```

A stack **confirma independentemente** toda a cadeia mapeada por sondas.

`param` chegava como EA do guest (`0x0FEFF740`) e era desreferenciado como
ponteiro host. O próprio ficheiro já documentava a armadilha
(`pad_data_to_guest`: *"The generic HLE adapter hands us raw GUEST addresses…"*)
e as vizinhas cumprem-na — `cellPadGetData`/`cellPadGetInfo2` chamam o parâmetro
`_guest`. **`cellPadSetActDirect` e `cellPadGetCapabilityInfo` ficaram de fora.**

Corrigido pela mesma convenção (`ps3recomp edd3e7a`). Verificado in-boot: zero
SIGSEGV, e o jogo passa a sondar o pad
(`[PADPOLL] cellPadGetData port=0 — game IS polling pad`).

## 2. A parede andou, e o que apareceu responde a uma pergunta de Julho

Com o crash removido, o ponto terminal passou de `func_00254788` para:

```
func_002546FC → func_0041FF70 → #004 → func_0041F700   ← 27 539 083 repetições
```

E o corpo de `func_0041F700`:

```c
func_0041F700(fab, node):
    if (node == 0) -> sai
    node   = node - 4                       // container_of
    cursor = *(uint8*)(fab + 0xC8) + 1
    *(uint8*)(fab + 0xC8)        = cursor   // ← PUSH (cursor++)
    *(fab + 0x48 + cursor*4)     = node     // ← PUSH (array[cursor] = node)
    sentinela = node + 0x80                 // = arg + 0x7C
    ...caminha a lista dos filhos, recursivamente...
```

**Isto é o PUSH.** A pergunta que estava aberta desde Julho — *"o walker do WAD
pede à fábrica o produto corrente sem nunca ter feito push"* — tem resposta: o
push está aqui, em `func_0041F700`, e é um **walk recursivo de árvore** que
empurra cada nó para a pilha de produtos da fábrica antes de descer aos filhos.

E desce pelo **mesmo campo** `node+0x80` (= `arg+0x7C`) que já está provado ser
`matriz[0][3]`. Por isso o walk não termina: 27,5 milhões de iterações.

## 3. O que isto reorganiza

Deixa de haver quatro paredes independentes. Há **um** defeito com cinco
consumidores, todos a caminhar a mesma lista intrusiva no mesmo offset:

| função | papel |
|---|---|
| `func_002545B0` | walk (parede 1) |
| `func_002547AC` | walk (parede 3) |
| `func_004244C0` | walk (parede 4) |
| `func_0041F700` | **walk + PUSH** (parede 5, a actual) |
| `func_0039E5A8` | lê o produto corrente que o push acima produz |

O `func_0039E5A8` (que eu tinha excluído por medir 253 consultas sãs) é o
**consumidor** do push — e as consultas eram sãs porque o push das outras
fábricas funciona. É a fábrica do tag 1 que fica presa.

## Estado

Não há menu. Mas a frente mudou de natureza: já não é "porque é que o boot não
avança", é **"porque é que a lista de filhos em `node+0x80` não termina"** — com
o push identificado, o consumidor identificado, e cinco consumidores a
concordarem no offset.

## O laço de `func_0041F700` é fiel — quarta suspeita de bug do lifter, refutada

Ao ler o corpo vi `r9 = *(r31 + 0)` no fim do laço, com `r31` fixado antes dele,
e pensei: **a variável do laço não é actualizada — bug do lifter.** Verifiquei
antes de o escrever como facto:

```c
loc_0041F7C4:
    r9  = *(r31 + 0);        // next
    cmp  r30, r9;            // sentinela vs next
    r31 = r9;                // ← AVANÇA. Está lá.
    if (r30 != r9) goto loc_0041F774;
```

**Avança correctamente.** É uma travessia de lista circular fiel: segue
`r31 = *(r31)` até `r31 == sentinela`. As 27,5 M de iterações não são um laço
mal traduzido — são uma cadeia de `*(node)` que nunca passa pela sentinela,
porque o campo não é uma lista.

Quarta suspeita de bug do lifter desta sessão, quarta refutada por verificação.
Desta vez verifiquei **antes** de afirmar — as três anteriores foram escritas
primeiro e corrigidas depois.

E o walk lê o tipo dos filhos de `*(child+4)` (não de `+2`, como o walker do
WAD), com a mesma fórmula `idx = (t<<2) & 0x3FFFC`, e despacha `tab[idx]->vt[0x40]`.
São três campos de tipo diferentes em jogo (`+2`, `+4`, `+6`), todos contra a
mesma tabela — mais uma razão para o próximo passo ser análise de layout, não
mais sondas.

---

# CORRECÇÃO (22.ª): as listas de `func_0041F700` ESTÃO bem formadas

Sonda de entrada em `func_0041F700` (40 entradas na corrida):

```
this=0x400C5048 arg=0x42F85334 w0=0xC0010001 sent=0x42F853B0 head=0x42F853B0  ← VAZIA (head==sent)
this=0x400C5048 arg=0x42F85334 w0=0xC0010001 sent=0x42F853B0 head=0x40007DE4  ← com filhos
this=0x400C5048 arg=0x4063858C w0=0xC0010001 sent=0x40638608 head=0x40638608  ← VAZIA
this=0x400C5048 arg=0x4063858C w0=0xC0010001 sent=0x40638608 head=0x40007F34  ← com filhos
```

**`head == sentinela` nas vazias.** A auto-ligação que procurei estaticamente
(varrimento de `addi rD,rA,IMM` + `stw rD,IMM(rA)`, que deu zero em `+0x80`)
**existe em runtime** — é construída por um caminho que o meu padrão não apanha,
exactamente a limitação que registei na altura.

E `0x4063858C` é o caso `#1` da sonda de Julho, que a nota classificava como
"lista válida". Bate.

## O que cai

Cai a generalização de que "o campo `+0x7C`/`+0x80` não é uma lista". Para
**esta** família de objectos (`w0 = 0xC0010001`, endereços `0x4063xxxx` /
`0x42F8xxxx`) é uma lista intrusiva correcta e bem inicializada.

O que continua verdade, e é de **outra** família: os registos do WAD
(`w0 = 0x40030001`, endereços `0x4077ACxx`) têm nesse offset a `matriz[0][3]`
escrita pelo construtor `func_0024C1F8` — provado por desmontagem. Duas famílias
diferentes, dois significados para o mesmo offset, e eu tratei-as como uma.

## O que fica por explicar

As 27,5 M de iterações **não** vêm de uma lista malformada à entrada. Vêm da
descida recursiva: o walk visita filhos via `*(nó+8)`, resolve o tipo de cada um
por `*(filho+4)` e despacha `tab[idx]->vt[0x40]`. Ou a árvore tem um ciclo em
profundidade, ou é genuinamente enorme.

Distinguir os dois é uma medição concreta: contar a profundidade e detectar
revisita de nós (um conjunto de vistos, com cap). Isso cabe numa sonda, ao
contrário da análise de layout que eu tinha dado como próximo passo — este ramo
não precisa dela.

---

# A lista termina. O que não termina é quem a manda percorrer.

Sonda na cadeia de irmãos (`PS3_TRACE_SIBLING`, cap 24):

```
#1   no=0x40007DE4  proximo=0x40007DD8  sent=0x42F853B0
...
#12  no=0x40007D0C  proximo=0x42F853B0  sent=0x42F853B0   ← TERMINA na sentinela
#13  no=0x40007DE4  proximo=0x40007DD8  sent=0x42F853B0   ← recomeça do início
```

**A lista tem 12 irmãos e fecha correctamente na sentinela.** O walk de
`func_0041F700` está certo do princípio ao fim: lista bem formada, travessia
fiel, terminação correcta.

O `#13` é a repetição — a mesma lista percorrida outra vez. As 27,5 M de
iteracões são ~2,3 M de **chamadas** a `func_0041F700`, cada uma a percorrer os
mesmos 12 nós.

## Sexta vez, e a mais embaraçosa

Eu tinha escrito que `func_0041F700` só é chamada **40 vezes**, e construí sobre
isso a conclusão de que "o ciclo não está na profundidade". Estava errado: **a
minha sonda de entrada tinha `if(_n++<40)` — cap fixo, escrito por mim hoje**,
depois de eu próprio ter escrito, nesta mesma nota, que cap fixo é mentiroso por
omissão e que toda a sonda nova leva cap por env var.

Escrevi a regra e violei-a na sonda seguinte. Corrigido:
`PS3_TRACE_B71_ENTRY_CAP`.

## Onde isto deixa a frente

O defeito **não** está na estrutura de dados nem no walk. Está em **quem
re-invoca o walk ~2,3 milhões de vezes**. Como o walk desce aos filhos por
`*(nó+8)` e despacha `tab[*(filho+4)]->vt[0x40]`, a hipótese natural é uma
**aresta que volta a um antepassado** — a árvore tem um ciclo entre níveis, e
cada volta re-percorre os mesmos 12 irmãos.

Isso mede-se com um conjunto de nós já visitados **na descida** (não nos irmãos,
que já se sabe estarem certos): se um filho já foi visitado, imprime-se a aresta
que fecha o ciclo. É a próxima medição, e é pequena.

---

# A aresta do ciclo, medida — e o walk exterior que o alimenta

Sonda da descida (`PS3_TRACE_DESCENT`, janela 2048):

```
[DESCENT] CICLO: pai=0x40007DE4 -> filho=0x42F86ADC (já visto na descida #1 de 12)
```

**Doze descidas distintas, e a décima terceira repete a primeira.** Não é um
ciclo entre níveis da árvore: é o walk inteiro a **reiniciar do topo**.

E o reinício vem do chamador. `func_0041FF70`:

```c
r29 = *(this + 0x24)              // cabeça de uma segunda lista
loc_0041FFE8:
    if (r29 == 0) goto fim;
loc_0041FFF0:
    r31 = r29 - 8                 // container_of
    ... para cada entrada, chama func_0041F700 (o walk + push) ...
```

Ou seja há **dois** walks encaixados: `func_0041FF70` percorre uma lista em
`*(this+0x24)` e, para cada entrada, `func_0041F700` percorre os 12 irmãos e
empurra cada um para a pilha de produtos da fábrica.

O interior está provado correcto (12 irmãos, termina na sentinela). Portanto o
que não termina é o **exterior**: a lista em `*(this+0x24)`.

## Estado da frente

Cada camada que abri esta noite estava correcta, e empurrou a pergunta uma
camada para fora:

| camada | veredicto |
|---|---|
| `cellPadSetActDirect` | **bug nosso** — corrigido (EA guest desreferenciado) |
| laço de irmãos de `func_0041F700` | fiel (4ª suspeita de lifter, refutada) |
| listas de irmãos | bem formadas, `head == sentinela`, 12 nós, terminam |
| descida da árvore | 12 descidas distintas, sem ciclo entre níveis |
| **walk exterior `func_0041FF70`** | **por medir** — é aqui que o reinício nasce |

A próxima medição é a mesma técnica, um nível acima: listar os nós da lista de
`*(this+0x24)` e ver se ela termina. É pequena e é o passo seguinte.

---

# O walk exterior também termina — e o padrão da noite fica claro

Sonda do walk exterior (`PS3_TRACE_OUTER`):

```
#1 no=0x40638594  proximo=0x4077AD28  flags=0x0024
#2 no=0x4077AD28  proximo=0x00000000  flags=0x0024   ← termina em NULL
```

**Duas entradas, termina correctamente.** Como o interior tem 12 irmãos, são
24 despachos por chamada — e os 27 539 083 implicam **~1,15 milhões de chamadas
a `func_0041FF70`**, vindas de `func_002546FC` (o walk que já está gated).

Reparo com valor: a lista exterior **mistura as duas famílias** — `0x40638594`
(família de listas, `w0=0xC0010001`) e `0x4077AD28` (família WAD, `w0=0x40030001`,
a que tem matriz em `+0x70`). O mesmo contentor guarda objectos dos dois tipos.

## O padrão desta noite, dito às claras

Abri seis camadas. **Todas correctas menos uma, que era nossa:**

| camada | veredicto |
|---|---|
| `cellPadSetActDirect` | **bug nosso** — corrigido |
| laço de irmãos de `func_0041F700` | fiel (4ª suspeita de lifter, refutada) |
| listas de irmãos | bem formadas, terminam na sentinela |
| descida da árvore | 12 descidas distintas, sem ciclo entre níveis |
| walk exterior `func_0041FF70` | **termina**, 2 entradas |
| quem chama `func_0041FF70` ~1,15 M vezes | por medir |

Cada medição empurrou a pergunta uma camada para fora, e cada camada estava
certa. Isso não é trabalho perdido: é a eliminação sistemática que deixa o
suspeito sozinho — e o suspeito é agora `func_002546FC`, que está **atrás de um
gate meu** (`PS3_LIST547_EMPTY_IF_BAD`).

**Hipótese que se impõe:** o gate trata `head` implausível como lista vazia, mas
não trata `head` **plausível e cíclico**. Se a lista de `func_002547AC` for
cíclica em vez de nula, o gate deixa-a passar e o walk repete — o que produz
exactamente ~1,15 M de re-invocações.

Isso mede-se com a mesma sonda de listagem, aplicada a `func_002547AC`. É o
passo seguinte, e é o mesmo tamanho dos outros cinco.

---

# A causa: recursão exponencial por revisitar nós, e a guarda que não poda

## O suspeito anterior caiu, medido

Hipótese: o gate `PS3_LIST547_EMPTY_IF_BAD` deixaria passar listas cíclicas e
`func_002547AC` re-invocaria o walk. **Refutada:** sonda `PS3_TRACE_W547` deu
**zero linhas** — com o gate ligado, esse walk nunca corre. Medida antes de ser
escrita como facto.

## Quem repete, contado

```
linhas do log: 27 549 345

27 539 083  func_0041F700      ← 99,96% de tudo
     2 088  func_0041F924
     1 744  func_00254250
       472  func_0024E5BC
```

O chamador (`func_0041FF70`) nem aparece no topo. **`func_0041F700` é
recursiva**: o despacho interno `tab[idx]->vt[0x40]` volta a entrar nela para
cada filho.

E 12⁷ ≈ 35 M. Com 12 irmãos por nível, sete níveis de ramificação dão
exactamente esta ordem de grandeza. **Não é um ciclo: é explosão exponencial por
revisitar os mesmos nós** — o grafo não é uma árvore, e o walk percorre-o como se
fosse. Bate com a sonda da descida, que viu o filho `#1` reaparecer.

## A guarda que devia podar

O walk tem um teste de poda:

```c
r29 = (*(nó   + 4) >> 16) & 0xFFF     // campo do PAI, calculado à entrada
...
r0  = (*(filho + 4) >> 16) & 0xFFF    // campo do FILHO
if (r0 == r29) goto loc_0041F7C4;     // ← PODA: salta este filho
```

Se `r0 == r29` nunca se verificar, nada é podado e a recursão visita todas as
combinações. É essa a hipótese seguinte, e é directamente medível: imprimir
`r0` e `r29` na comparação e ver se alguma vez coincidem.

## Onde isto deixa a frente

A pergunta é agora estreita e mecânica:

> **porque é que a poda `(*(filho+4)>>16)&0xFFF == (*(pai+4)>>16)&0xFFF` nunca
> dispara?**

Ou o campo `+4` dos filhos não está a ser preenchido, ou o do pai vem de outro
sítio. Uma sonda na comparação distingue os dois — e é a sétima da mesma família
que resolveu todas as camadas desta noite.

---

# A poda medida: é um teste de auto-recursão, não um guarda de ciclo

Sonda `PS3_TRACE_PRUNE` nos dois lados da comparação:

```
#1  filho=0x42F86AD8 w0=0x00517700 campo=18 pai=1 desce  (coincidem 0 de 1)
#2  filho=0x42F869E8 w0=0x005179A0 campo=25 pai=1 desce
#3  filho=0x42F86990 w0=0x00516C10 campo=32 pai=1 desce
#4  filho=0x42F86940 w0=0x00515488 campo=16 pai=1 desce
#5  filho=0x42F85AE0 w0=0x00516E50 campo=21 pai=1 desce
#6  filho=0x42F85A88 w0=0x00518038 campo=23 pai=1 desce
#7  filho=0x42F859F0 w0=0x00515F80 campo=9  pai=1 desce
#8  filho=0x42F85920 w0=0x00516AA8 campo=17 pai=1 desce
#9  filho=0x42F85888 w0=0x005168C8 campo=15 pai=1 desce
#10 filho=0x42F85800 w0=0x00515290 campo=20 pai=1 desce
#11 filho=0x42F856C8 w0=0x005157E0 campo=4  pai=1 desce
#12 filho=0x42F85590 w0=0x00514F28 campo=3  pai=1 desce  (coincidem 0 de 12)
```

**Os `campo` são tags de tipo.** 18, 25, 32, 16, 21, 23, 9, 17, 15, 20, 4, 3 —
todos presentes no despejo do registry feito antes (`tab=0x00868D48`, 26 entradas,
tags 0..32). E os `w0` dos filhos são todos `0x0051xxxx`, ou seja **vtables**: os
filhos são objectos polimórficos, um por tipo.

O pai tem tag **1**. **Nenhum dos 12 filhos é 1 — logo a poda nunca dispara.**

## O que isto quer dizer

A comparação `child_tag == parent_tag` **não é um guarda de ciclo**: só impede a
auto-recursão imediata do mesmo tipo. Não há conjunto de visitados. Portanto o
walk só termina se o grafo de tipos for uma árvore ou um DAG raso.

Aqui não é: o tipo 1 tem como filhos os outros 12, e cada um deles volta a ter
como filhos todos os tipos diferentes do seu. Isso é um grafo **completo** — e
percorrê-lo em profundidade sem marcar visitados dá exactamente 12⁷ ≈ 35 M, a
ordem de grandeza medida.

## A pergunta final, e é de dados

> **O tipo 1 deve mesmo ter como filhos os outros doze tipos?**

Se a lista de filhos de cada tipo devia conter só os seus subtipos directos, a
lista está mal construída — e quem a constrói é o `push` de `func_0041F700`
(`cursor++; array[cursor] = nó`), que empurra **cada nó visitado** para a pilha
de produtos da fábrica.

Ou seja: o walk que explode é o mesmo que popula a estrutura que ele percorre.
Se a população estiver errada, a travessia herda o erro — e é aí que a próxima
sessão deve começar, com o registry já provado saudável e as 26 entradas
conhecidas.

## Balanço da noite

Sete camadas abertas por medição. **Todas correctas menos uma, que era nossa**
(`cellPadSetActDirect`, corrigido). O problema deixou de ser "o boot não avança"
e passou a ser uma pergunta sobre o conteúdo de uma lista, com o tipo do pai
(1), os tipos dos filhos (12 conhecidos) e o sítio que os empurra, todos
nomeados.
