# A cadeia completa: do menu que não aparece até ao alocador que falha

**Data:** 2026-08-01 · Cada elo medido, e cada leitura de código verificada contra o PPC
original do `EBOOT.ELF`.

## A cadeia, de cima para baixo

```
o menu não aparece
  └─ thr_auto_load nunca termina (gate 0/6, elo AUTO_LOAD)
      └─ FATAL: stuck calling 0x00514E80 — laço infinito em func_002545D4
          └─ o nó da lista tem payload 0  →  lê o "tipo" do endereço guest 2
              └─ o objecto que lhe entregam nunca foi alocado
                  └─ func_00220284 corre com this=0
                      └─ func_002182A4 passa sem verificar o retorno
                          └─ func_00227788 devolve 0
                              └─ func_00263554 (pop da free-list) devolve 0
                                  └─ func_002635A4 (crescer o pool) devolve 0
                                      └─ func_00263040 (ALLOC do heap) FALHA
```

## As duas medições que fecharam os dois últimos elos

**1. O pool não é "mal configurado".** A sonda `PS3_TRACE_POOLGROW` apanhou 40 passagens
pelo caminho de crescimento, e **zero** com `grow=0`:

```
[POOLGROW] #1 pool=0x40331608 grow=32  elem=80  hdr=16 heap=0x40004020 head=0
[POOLGROW] #2 pool=0x42F85B50 grow=16  elem=216 hdr=16 heap=0x42F83D48 head=0
[POOLGROW] #8 pool=0x4007F8B0 grow=128 elem=8   hdr=4  heap=0x40004020 head=0
```

`head=0` é o **estado normal** de um pool com preenchimento preguiçoso — acontece 40 vezes
e resolve-se 40 vezes. Não é anomalia.

**2. Logo o NULL vem da outra saída de `func_002635A4`:**

```c
if (grow == 0) { return NULL; }               // <- MEDIDO: nunca acontece
r3 = *(pool + 0xC);                           // o heap
func_00263040(r3, hdr + grow*elem, 8);        // ALLOC
if (r3 == 0) { return NULL; }                 // <- é ESTE
```

**`func_00263040` não consegue alocar.**

E foi confirmado pelo outro lado, na mesma corrida: 237 alocações passam bem
(`alloc=0x406387E0`, índices 0..10, slots válidos) e a 238.ª — de outro alocador,
`0x400C6B50` — devolve zero, com o `this=0` a aparecer **na linha seguinte do log**.

## Porque isto importa

`func_00263040` é o alocador de boundary-tags — **exactamente o que este projecto persegue
desde os seis `FREELIST-TAG-GUARD`**. A cadeia de hoje liga, pela primeira vez e elo a
elo, o sintoma visível (o menu não aparece) ao problema que já era conhecido.

Também explica porque nenhum dos quatro sintomas era corrupção de memória: o
`PS3_WATCH_STORE` mostrou quatro vezes que ninguém estraga bytes. Não estragam — **falta
memória**, e o código do jogo não verifica nenhum dos quatro retornos pelo caminho. Num
PS3 real cada um deles rebentaria na página nula, o que quer dizer que no console **a
alocação nunca falha** — logo o defeito é do nosso lado do heap, não do jogo.

## O que foi eliminado, por medição e não por argumento

- **Não é o lifter.** Cada passo desta cadeia foi desmontado do `EBOOT.ELF` e comparado:
  o `beq` do `func_002545D4`, o prólogo do `func_002210BC` com `rldicl r26,r3`, o `bl` em
  `0x00218394` com `mr r3,r29`, a entrada real `0x002545B0`. Todos fiéis.
- **Não é corrupção de memória.** Quatro watches, quatro vezes limpo.
- **Não é o índice de classe de tamanho.** 237 passagens com índices 0..10 e slots válidos.
- **Não é o pool mal configurado.** 40 passagens de crescimento, zero com `grow=0`.
- **Não é o `F2B-STREAM-PUMP`** (esse era real e foi corrigido hoje, mas é outro defeito).

## O próximo passo, e é uma pergunta só

**Porque falha `func_00263040`?** Heap esgotado, ou free-list/boundary-tags num estado em
que o walk não encontra bloco? O projecto já tem seis guards nessa família e o
`PS3_TRACE_POOLCNT` para distinguir leak de refill — a instrumentação existe, é aplicá-la
a esta alocação concreta (`heap=0x40004020`, o tamanho pedido é `hdr + grow*elem`).

---

# CORRECÇÃO: `func_00263040` **não** falha. O que é nulo é o ponteiro do pool.

A conclusão acima — *"`func_00263040` não consegue alocar"* — era **inferência**, não
medição: eu tinha provado que o `grow=0` nunca acontece e concluí, por eliminação, que a
outra saída NULL de `func_002635A4` era a culpada. **Errado.** A sonda directa mede:

```
[POOLALLOC] ok pool=0x40331608 heap=0x40004020 pedido=2576 r3=0x40331830 (ok=1 falhas=0)
[POOLALLOC] ok pool=0x42F85B50 heap=0x42F83D48 pedido=3472 r3=0x42F85BA8 (ok=2 falhas=0)
...
[POOLALLOC] ok pool=0x4007F8B0 heap=0x40004020 pedido=1028 r3=0x40680A58 (ok=8 falhas=0)

falhas = 0
```

**Oito alocações, oito sucessos.** O heap está bem e o alocador de boundary-tags faz o
seu trabalho.

## Onde está o zero, então

Relendo `func_00263554` com o valor medido:

```c
r31 = r3;                 // <- o POOL, que vem de *(alloc + (idx<<2) + 0x8C)
r3  = *(r31 + 4);         // head da free-list
```

E o `[SZCLASS] #238` diz `slot=0x00000000`. Esse `slot` **é o `r3` que entra**, ou seja o
**ponteiro do pool**, não a cabeça da free-list. Com `r31 = 0`, a função lê `*(0+4)` e
segue com lixo — e por isso o `[FLHEAD]` apanhou `pool=0x00000005` noutra corrida: são
leituras da base da memória guest, não de nenhuma lista.

Ou seja:

| | 237 primeiras | a 238.ª |
|---|---|---|
| alocador | `0x406387E0` | **`0x400C6B50`** |
| `*(alloc + (idx<<2) + 0x8C)` | ponteiro de pool válido | **`0`** |

E o `PS3_WATCH_STORE` já tinha dito o resto: **uma única escrita** em `0x400C6BDC`, a
escrever `0`, vinda de `func_0022E6A4` (construtor, chamado de `func_002B11B8`).

**O alocador `0x400C6B50` é construído com a tabela de pools a zero e ninguém a preenche.**

## O que isto muda na pergunta

Não é "porque falha o heap" — o heap não falha. É **"quem devia criar os pools de
`0x400C6B50`, e porque não corre"**. É uma pergunta de inicialização, não de memória.

## A lição, outra vez

Provei que uma das duas saídas NULL não acontecia e **concluí por eliminação** qual era a
outra, sem a medir. A medição custou uma corrida e desfez a conclusão. Numa cadeia com dez
elos, "só pode ser a outra" não é um elo medido — é um palpite com boa reputação.

---

# O fim: dois construtores, um faz metade do trabalho

`PS3_WATCH_STORE` nas duas entradas de índice 0 — a do alocador que funciona e a do que
não — na mesma corrida:

```
alocador SÃO   (0x406387E0), entrada 0x4063886C:
  w32 [0x4063886C]=0x00000000   ra0=func_00228368+0x15C  ra1=func_00411A5C+0x1D8   <- zera
  w32 [0x4063886C]=0x407719A8   ra0=func_0022851C+0x2FC  ra1=func_00411A5C+0x1D8   <- POPULA

alocador PARTIDO (0x400C6B50), entrada 0x400C6BDC:
  w32 [0x400C6BDC]=0x00000000   ra0=func_0022E6A4+0x8C0  ra1=func_002B11B8+0x6FB8  <- zera
                                                                                   <- e mais nada
```

**A construção do alocador tem dois passos: zerar a tabela de pools e depois populá-la.**

| | zera | popula | chamados de |
|---|---|---|---|
| `0x406387E0` (são) | `func_00228368` | **`func_0022851C`** | `func_00411A5C` |
| `0x400C6B50` (partido) | `func_0022E6A4` | **nunca** | `func_002B11B8` |

O caminho que passa por `func_002B11B8` faz o primeiro passo e não faz o segundo. E
`func_00227788` lê `*(alloc + (idx<<2) + 0x8C)` e passa-o directo ao pop **sem verificar** —
porque no desenho do jogo essa entrada nunca pode ser nula.

## A cadeia inteira, agora com o primeiro elo

```
func_002B11B8 constrói o alocador 0x400C6B50 e não popula a tabela de pools
  └─ *(0x400C6B50 + 0x8C) fica a 0
      └─ func_00227788 passa esse 0 ao func_00263554 como se fosse um pool
          └─ o pop lê *(0+4) e devolve lixo/0
              └─ func_002182A4 não verifica o retorno
                  └─ func_002210BC põe-no em r26
                      └─ func_00220284 corre com this=0 e escreve por ponteiros lidos
                         da base da memória guest
                      └─ e o objecto que devia ter sido alocado nunca existe
                          └─ o nó da lista fica com payload 0
                              └─ func_002545D4 lê o "tipo" do endereço 2 e despacha
                                 por tab[lixo]=0
                                  └─ FATAL: stuck calling 0x00514E80
                                      └─ thr_auto_load nunca termina
                                          └─ o menu não aparece
```

**Onze elos, todos medidos, nenhum inferido.**

## A pergunta que fica, e é uma só

Porque é que o caminho de `func_002B11B8` não corre o passo de popular? Duas hipóteses,
ambas baratas de testar:

1. **Falta uma chamada** — o construtor equivalente ao `func_0022851C` existe e não é
   invocado neste caminho. Comparar `func_00411A5C` (que chama os dois) com o troço de
   `func_002B11B8` que chama o `func_0022E6A4`.
2. **A chamada existe e sai cedo** — algum argumento (contagem de classes, tamanho) chega
   a zero e o laço de populagem não itera.

A (1) resolve-se lendo `func_00411A5C` e o sítio de `func_002B11B8+0x6FB8`; a (2) com uma
sonda na entrada do popular. Nenhuma das duas precisa de adivinhar.

---

# E fecha o círculo: `0x400C6B50` não é um alocador de pools

`func_0022E6A4` — a única função que escreve na tabela do objecto partido — **nunca cria
pools**. As suas chamadas, todas:

```
func_002637D8 (×4)   func_00252564   func_0025BE3C
```

Nenhuma é `func_002BB1B0` (o criador de pool), nem `func_0022851C`, nem `func_00228368`.
E carimba `0xDEADBEEF` no início — um magic de sentinela, coisa de quem inicializa memória
que ainda não tem dono.

Compare-se com o construtor a sério, `func_00228368`, que zera as **três** tabelas do
objecto (`+0x48`, `+0x8C`, `+0xD0`, 17 entradas cada) e depois **ou** cria os pools de raiz
(`func_0022851C`, quando `r3==0`) **ou** copia os ponteiros de um template.

**Logo `0x400C6B50` não é um objecto desta família.** A escrita de `0` em `0x400C6BDC` que
o watch apanhou não é "a tabela de pools inicializada e nunca preenchida" — é outra
estrutura, de outra classe, que calha ocupar aquela memória.

## O que isto quer dizer

O círculo fecha no padrão do dia: **`func_00227788` recebeu, da sua primeira chamada
virtual, um objecto que não é o alocador que ela assume.** É o mesmo mecanismo do
`func_002545B0` a receber um objecto-matriz, e do `func_002545D4` a ler um "tipo" do
endereço 2.

Não é memória corrompida (quatro watches limpos), não é o heap (8 alocações, 8 sucessos),
não é o lifter (cada passo desmontado e comparado). É **despacho**: métodos a correr sobre
objectos da classe errada, e o candidato natural continua a ser a resolução de tipos
`tab[(tipo<<2)]` — a mesma tabela `0x00868D48` que atravessa esta investigação toda.

## O que fica por fazer, em ordem

1. Sondar a **primeira chamada virtual** dentro de `func_00227788` (`*(vt+0x50)` do
   singleton `*(TOC-0x2A78)+0xC`): que objecto devolve, e qual é o seu tipo. Se devolver
   `0x400C6B50` quando devia devolver um `0x406387E0`-like, o defeito está no que essa
   vtable resolve.
2. Cruzar com o registo de tipos: `0x400C6B50` está registado como que tipo, e por quem.

Ambas são medições com a instrumentação que já existe (`PS3_TRACE_ICALL_TO`,
`PS3_TRACE_TYPESLOT`, sonda na entrada). Nenhuma precisa de código novo.

---

# A resposta: o singleton devolve um alocador de OUTRA CLASSE

Sondando o resultado da primeira chamada virtual dentro de `func_00227788` — a que produz
o "alocador" que ela depois indexa — a corrida inteira tem **dois** resultados distintos:

```
[ALLOCSRC] #1 alloc=0x406387E0 vt=0x00514F28 tab8C[0]=0x407719A8   <- são
[ALLOCSRC] #2 alloc=0x400C6B50 vt=0x00515008 tab8C[0]=0x00000000   <- partido
```

**As vtables são diferentes.** `0x00514F28` e `0x00515008` são duas classes distintas, e só
a primeira tem a tabela de pools em `+0x8C`.

E o `PS3_WATCH_STORE` no `0x400C6B50` já tinha mostrado a construção completa desse
objecto — a cadeia de vptr da herança, todas escritas por `func_00411AC8`:

```
[0x400C6B50]=0x511628   func_00411AC8+0x55C
[0x400C6B50]=0x5115B0   func_00411AC8+0x630
[0x400C6B50]=0x511538   func_00411AC8+0x74C
[0x400C6B50]=0x515008   func_00411AC8+0x870   <- classe final
```

**O objecto está bem construído.** Não falta inicialização nenhuma: é um objecto completo
de uma classe que simplesmente não é um alocador de pools.

## O que isto estabelece, e é o fim desta linha

`func_00227788` faz `this->singleton->vt[0x50]()` e usa o resultado como alocador. Numa
das passagens o singleton devolve um objecto de classe `0x00515008` em vez de
`0x00514F28`, e a partir daí tudo o que se segue — o `pool=0`, o `this=0`, o nó com
payload nulo, o `tab[lixo]`, o `FATAL` — é consequência mecânica.

Fecha o padrão do dia com o mesmo veredicto que os outros quatro sítios: **não é memória,
não é o heap, não é o lifter, não é inicialização em falta. É despacho** — um método
virtual a devolver um objecto da classe errada.

## A pergunta seguinte, e é só uma

O que distingue as duas invocações do `*(vt+0x50)`? O singleton é o mesmo
(`*(TOC-0x2A78)+0xC`); o que muda é o contexto. Medir `this`, `r4` (que vem de
`*(obj+0x24)+0x14`) e o `code` resolvido em cada uma das duas passagens diz se o singleton
está a escolher mal, ou se lhe estão a pedir a coisa errada.

É uma sonda no mesmo sítio, com dois campos a mais. Nenhum código novo no motor.

---

# O último elo: uma chave concreta devolve a classe errada

Sondando o pedido e a resposta, na mesma corrida:

```
[SING50]  109 despachos, TODOS iguais:
          sing=0x400C6210 vt=0x00514FA0 opd=0x0051B0A0 code=0x0039D3C4
          -- o singleton e o método são CONSTANTES; só a `key` varia.

[ALLOCSRC] dois resultados:
          alloc=0x406387E0 vt=0x00514F28 tab8C[0]=0x407719A8   (são)
          alloc=0x400C6B50 vt=0x00515008 tab8C[0]=0x00000000   (partido)
```

E a correlação, linhas consecutivas:

```
[SING50]   #107 ... key=0x4077ED10 obj24=0x4077CAB8
[ALLOCSRC] #2   alloc=0x400C6B50 vt=0x00515008 tab8C[0]=0
[vm] UNCOMMITTED write32 ... ra=func_00220284+0x1244
```

**Uma chave concreta — `0x4077ED10` — devolve um objecto de outra classe.** As outras 108
devolvem a classe certa. E o `obj24=0x4077CAB8` é o mesmo objecto que aparece como
`p50=0x4077CAB8` no `[CPY284] #13`, o que corre com `this=0`.

O `code=0x0039D3C4` é da família `0x39Dxxx` — **o registo de tipos**, o mesmo mecanismo que
atravessa esta investigação do princípio ao fim (`func_0039D428`, `func_0039D764`,
`tab[(tipo<<2)]` em `0x00868D48`).

## A cadeia completa, doze elos, todos medidos

```
o registo devolve a classe errada para a chave 0x4077ED10
 └─ alloc=0x400C6B50 (vt 0x00515008) não tem tabela de pools em +0x8C
   └─ *(alloc+0x8C) = 0 vai como "pool" para o pop
     └─ o pop lê *(0+4) e devolve lixo
       └─ func_002182A4 não verifica o retorno
         └─ func_002210BC põe-no em r26
           └─ func_00220284 corre com this=0
           └─ e o objecto que devia existir nunca é alocado
             └─ o nó da lista fica com payload 0
               └─ func_002545D4 lê o "tipo" do endereço 2 → tab[lixo]=0
                 └─ FATAL: stuck calling 0x00514E80
                   └─ thr_auto_load nunca termina
                     └─ gate 0/6 no elo AUTO_LOAD
                       └─ o menu não aparece
```

## O que sobra, e é a mesma pergunta de sempre — mas agora com uma chave

**Porque é que o registo de tipos devolve a classe errada para `0x4077ED10`?** É a mesma
tabela `0x00868D48` e a mesma família de código dos `[WADLD-VT28]`, do `CB56C`, do TYPE15.
A diferença é que agora há uma chave concreta para seguir, em vez de um sintoma.

---

# O índice é ZERO — e isso é a Parede D, com o mecanismo escrito

`func_0039D3C4`, lido do lift, é o lookup do registo:

```c
nivel = *(sing + 0x44);
base  = *(sing + 0x24);
array = *( base + nivel*12 );
idx   = *(uint16*)(key + 6);        // <- o TIPO do objecto-chave
slot  = array[idx];
return slot ? slot - 4 : 0;
```

E a medição, para a chave que a `[SING50]` já tinha identificado independentemente:

```
[REGLOOKUP] #278 key=0x4077ED10 idx=0 slot=0x400C6B54 obj=0x400C6B50 vt=0x00515008
```

**`idx = 0`.** O campo de tipo do objecto-chave (`*(uint16*)(key+6)`) vale zero, e o
`array[0]` guarda um objecto da classe `0x00515008` — que não é um alocador de pools.

Outras chaves com o mesmo `idx=0` caem no mesmo slot:

```
#19  key=0x42F85594 idx=0 -> 0x400C6B50
#38  key=0x406387E4 idx=0 -> 0x400C6B50
#278 key=0x4077ED10 idx=0 -> 0x400C6B50
```

## Porque isto é a Parede D

O `idx` é o **tipo** do objecto. Valer zero significa que o objecto **nunca foi registado
com um tipo** — e o registo devolve então o slot 0, o do "sem tipo", que contém outra
coisa qualquer.

É exactamente o que o `CLAUDE.md` chama Parede D (*"Registry de shaders / typemap"*): o
registo não está populado, os objectos saem com tipo 0, e a partir daí cada consumidor
recebe a classe errada. Os cinco sintomas do dia — `rec=0`, objecto-matriz, `pool=5`,
`this=0`, `tab[lixo]` — são todos a jusante disto.

## Ressalva honesta sobre esta medição

A agulha desta sonda casou em **103 sítios** do lift, não só no `func_0039D3C4` — é um
idioma genérico de leitura indexada. As outras 299 linhas do log **não são de confiança**:
os campos `key`/`idx` não significam nada fora deste lookup.

A linha `#278` é fiável por um motivo específico: a chave `0x4077ED10` foi medida
**independentemente** pela sonda `[SING50]`, que está num único sítio, e bate certo. As
outras duas linhas com `idx=0` são consistentes mas não têm essa confirmação cruzada.

Uma agulha mais estreita (ancorada nas linhas próprias do `func_0039D3C4`) é o primeiro
passo de quem retomar, antes de tratar qualquer outra linha como facto.

---

# O fecho: o tipo não estava por atribuir — foi ATRIBUÍDO A ZERO

`PS3_WATCH_STORE` no campo de tipo do objecto-chave (`0x4077ED16`, u16) dá a vida inteira
do campo, e desfaz a leitura anterior:

```
w32 [0x4077ED10]=0x4077ED4C   func_002635A4+0xDA8   <- alocado (grow do pool)
w32 [0x4077ED10]=0x40000003   func_00210720+0x244   <- construído
w16 [0x4077ED14]=0x20         func_00210720+0x260
w16 [0x4077ED16]=0x2          func_00210720+0x340   <- TIPO = 2
w16 [0x4077ED16]=0x2          func_004117B0+0x54    <- TIPO = 2 (reafirmado)
w16 [0x4077ED16]=0x0          func_0024E3D0+0x204   <- TIPO = 0
                              ra1=func_0024F028+0x8F8
```

**O tipo era 2 e passou a 0.** E o escritor está na cadeia do walker de registos do WAD —
`func_0024F028` é o elo `#7` do backtrace original desta investigação.

E `func_0024E3D0`, lido do lift, **não zera nada**: atribui.

```c
tipo_do_registo = *(uint16*)(r28 + 2);          // tipo lido do registo do WAD
idx     = (tipo_do_registo << 2) & 0x3FFFC;
fabrica = *(r23 + idx);                          // tab[idx] -- a tabela 0x00868D48
resultado = fabrica->vt[0x48]( fabrica );        // chama a fábrica
*(uint16*)(chave + 6) = *(resultado + 0x20);     // <- ESCREVE o tipo no objecto
```

**O valor escrito é `*(resultado + 0x20)`, e veio 0.** Ou seja: a fábrica de tipo
devolveu um objecto cujo campo de identificação está a zero.

## A cadeia, agora completa e circular

```
tab[idx] (a tabela de tipos 0x00868D48) devolve uma fábrica cujo produto tem +0x20 = 0
 └─ func_0024E3D0 escreve esse 0 no campo de tipo do objecto 0x4077ED10
   └─ o lookup do registo passa a devolver array[0] -> classe 0x00515008
     └─ essa classe não tem tabela de pools em +0x8C
       └─ o "pool" é 0, o pop devolve lixo, ninguém verifica
         └─ this=0, objecto nunca alocado, nó com payload 0
           └─ tab[lixo]=0 -> FATAL 0x00514E80 -> thr_auto_load nunca termina
             └─ o menu não aparece
```

**É a mesma tabela `0x00868D48` no princípio e no fim.** A Parede D não é uma metáfora:
o registo de tipos incompleto produz uma fábrica cujo produto não se sabe identificar, e
essa ignorância propaga-se por treze elos até ao ecrã.

## O que medir a seguir, e é curto

Qual `idx` usa o `func_0024E3D0` nessa passagem, e o que está em `tab[idx]`. Se for uma
das entradas que o `[WADLD-VT28]` mostrou resolver bem, o problema é o `+0x20` do produto;
se for uma entrada vazia ou de outro tipo, voltamos ao registo — e aí liga-se directamente
ao trabalho do TYPE15 e do `CB56C` que já existe no projecto.

---

# O topo: a fábrica está registada e devolve NULL

```
[TYPEASSIGN] #1 obj=0x4077ED10 reg=0x4077ED10 regtipo=0x3  idx=0xC  tab=0x00868D48 produto=0x00000000 tipo_escrito=0
[TYPEASSIGN] #2 obj=0x4077F0E0 reg=0x4077F0E0 regtipo=0xF  idx=0x3C tab=0x00868D48 produto=0x00000000 tipo_escrito=0
```

**`produto = 0`.** A chamada `fabrica->vt[0x48](fabrica)` devolve NULL, e o código a seguir
faz `*(uint16*)(obj+6) = *(0 + 0x20)` — lê a base da memória guest e escreve o que lá está
(zero) como tipo do objecto.

E as entradas da tabela **existem e são válidas**: `idx=0xC` é o tipo 3, e o mapa de tipos
desta mesma investigação já o tinha mostrado —

```
[WADLD-T1SZ] ... idx=0xC tab=0x00868D48 obj=0x400C6210
```

`0x400C6210` é **exactamente o singleton** que o `[SING50]` mediu 109 vezes
(`sing=0x400C6210 vt=0x00514FA0`). Ou seja: a fábrica está registada, é a certa, e é a
mesma que serve tudo o resto — **só que o seu `vt[0x48]` devolve NULL**.

Dois registos do WAD são afectados na mesma corrida: tipo `0x3` e tipo `0xF`.

## A cadeia inteira, catorze elos

```
fabrica(tab[0xC] = 0x400C6210)->vt[0x48]() devolve NULL
 └─ func_0024E3D0 lê *(NULL+0x20) e escreve 0 como tipo do objecto
   └─ o lookup do registo passa a devolver array[0] -> classe 0x00515008
     └─ essa classe não tem tabela de pools em +0x8C
       └─ o "pool" é 0, o pop devolve lixo, ninguém verifica
         └─ this=0 no func_00220284; o objecto nunca é alocado
           └─ nó de lista com payload 0
             └─ func_002545D4 lê o "tipo" do endereço 2 -> tab[lixo]=0
               └─ FATAL: stuck calling 0x00514E80
                 └─ thr_auto_load nunca termina -> gate 0/6
                   └─ o menu não aparece
```

## Onde a próxima sessão começa

**Porque é que `0x400C6210->vt[0x48]()` devolve NULL?** É uma pergunta com um objecto
concreto (`0x400C6210`), um slot concreto (`vt+0x48`, vtable `0x00514FA0`) e dois registos
que a disparam (tipos `0x3` e `0xF`). A sonda é a mesma que se usou o dia inteiro: na
entrada da função que o `vt[0x48]` resolve, com `PS3_TRACE_ICALL_TO` a nomeá-la primeiro.

E liga-se directamente ao trabalho de TYPE15/`CB56C` que já existe no projecto — é a mesma
tabela `0x00868D48`, o mesmo mecanismo de fábricas, o mesmo `0x39Dxxx`.

---

# O TOPO ABSOLUTO: o cursor `+0xC8` vale −1, e este projecto já o imprimia

A função que devolve NULL tem catorze linhas:

```c
func_0039E5A8(fab):                       // "dá-me o produto corrente"
    cursor = *(int8_t*)(fab + 0xC8);      // índice COM SINAL
    if (cursor < 0) return NULL;          // <- pilha vazia
    return *(uint32_t*)(fab + 0x48 + cursor*4);
```

E a medição, nas duas fábricas que disparam a falha:

```
[FAB48] #1 fab=0x400C6210 vt=0x00514FA0 opd=0x0051B2E8 code=0x0039E5A8 cursor+C8=-1  <NEGATIVO>
[FAB48] #2 fab=0x403008E8 vt=0x00516858 opd=0x0051B2E8 code=0x0039E5A8 cursor+C8=-1  <NEGATIVO>
```

**`cursor = −1` significa "nada empilhado".** O `push` que devia pôr um produto na fábrica
nunca correu.

## E este campo não é novo neste projecto

O `host_gow2_factory.cpp` já o lê e já o imprime, há meses, na linha do TYPE15:

```
[TYPE15] REHOME old=0x401002F0 pin=0x47D00000 tab[0x54]=0x47D00000 vt=0x00516D70
         +24=0x47D00400 +44=0x00000000 +48=0x00000000 +C8b=-1 +D4=0x00000000
```

**`+C8b=-1`** — o mesmo campo, o mesmo valor, registado desde sempre ao lado do trabalho da
fábrica de tipos. O que faltava não era o dado: era a cadeia que liga esse `-1` ao menu que
não aparece.

## A cadeia completa: quinze elos, do cursor ao ecrã

```
o cursor +0xC8 da fábrica vale -1 (nada empilhado)
 └─ func_0039E5A8 devolve NULL
   └─ func_0024E3D0 lê *(NULL+0x20) e escreve 0 como tipo do objecto
     └─ o lookup do registo devolve array[0] -> classe 0x00515008
       └─ essa classe não tem tabela de pools em +0x8C
         └─ o "pool" é 0, o pop devolve lixo, ninguém verifica
           └─ this=0 em func_00220284; o objecto nunca é alocado
             └─ nó de lista com payload 0
               └─ func_002545D4 lê o "tipo" do endereço 2 -> tab[lixo]=0
                 └─ FATAL: stuck calling 0x00514E80
                   └─ thr_auto_load nunca termina -> gate 0/6 no elo AUTO_LOAD
                     └─ o menu não aparece
```

## Onde isto põe o trabalho, e é concreto

**Quem devia fazer o `push` na fábrica, e porque não corre?** É a mesma pergunta que o
TYPE15 e o `CB56C` perseguem — mas agora com o campo exacto (`+0xC8`), o valor exacto
(`-1`), as duas fábricas exactas (`0x400C6210`, `0x403008E8`) e a consequência medida elo a
elo até ao ecrã.

E há uma vantagem prática: o `host_gow2_factory.cpp` **já manipula estas fábricas** (o
`ps3_type15_*`, o REHOME, o replenish). O sítio para procurar o `push` em falta já está
aberto e instrumentado.

---

# CORRECÇÃO FINAL: o `push` corre — 150 vezes. A consulta é que é feita fora do par.

Escrevi acima que *"o `push` que devia pôr um produto na fábrica nunca correu"*. **Errado
outra vez**, e o watch no cursor desfá-lo em três linhas:

```
w8 [0x400C62D8]=0xFF  func_002B11B8+0x1DF8   <- init: -1
w8 [0x400C62D8]=0x00  func_0039DA78+0x150    <- PUSH  (cursor 0)
w8 [0x400C62D8]=0xFF  func_0039DAB0+0x1A8    <- POP   (cursor -1)
w8 [0x400C62D8]=0x00  func_0039DA78+0x150    <- PUSH
w8 [0x400C62D8]=0xFF  func_0039DAB0+0x1A8    <- POP
...  301 escritas ao todo, alternando, e com aninhamento (cursor chega a 1)
```

**`func_0039DA78` é o push e `func_0039DAB0` é o pop, e a pilha está equilibrada.** O
mecanismo da fábrica está são. O `-1` é o estado **correcto** de uma pilha vazia.

## O que isto quer dizer, e é melhor do que a leitura anterior

Não falta um `push`. O que acontece é que **`func_0024E3D0` pergunta "qual é o produto
corrente?" num momento em que não há produto corrente** — fora de qualquer par push/pop.

Ou seja, mais uma vez, **contexto errado** — o mesmo veredicto dos outros cinco sítios do
dia. A fábrica responde correctamente "não tenho nada" (NULL), e é o chamador que não devia
estar a perguntar ali, ou devia estar dentro de um push que não abrange este ponto.

## A pergunta final, e agora é mesmo a última desta cadeia

**Porque é que o walk de registos do WAD chama `vt[0x48]` fora de um push?** Duas leituras,
ambas mediveis com a instrumentação que já existe:

1. **O push devia envolver este ponto** e não envolve — um `func_0039DA78` em falta, ou
   com âmbito curto demais.
2. **A chamada não devia acontecer de todo** para estes dois registos (tipos `0x3` e `0xF`)
   — chega lá por um ramo que não devia ser tomado.

Um `PS3_TRACE_ICALL_TO=0x0039DA78` com timestamps ao lado do `[FAB48]` distingue as duas:
se houver um push imediatamente antes noutro objecto, é (1); se não houver push nenhum na
vizinhança, é (2).

## Nota de método — a nona correcção do dia

Escrevi "o push nunca correu" a partir de um único facto (`cursor = -1`) sem medir o
próprio push. Correu 150 vezes. É o mesmo erro de sempre, na mesma forma: **um estado
observado não conta a história de como se lá chegou.** O watch conta.

---

# Sem truncagem: a pilha está equilibrada e a consulta chega DEPOIS do último pop

O cap do meu próprio watch estava escrito a martelo (300) e cortava em silêncio. Com
`PS3_WATCH_STORE_CAP=-1`:

```
352 escritas do cursor (eram 301, truncadas)
push = 175      pop = 175      <- perfeitamente equilibrado

...
w8 [0x400C62D8]=0x00  func_0039DAB0   <- pop
w8 [0x400C62D8]=0xFF  func_0039DAB0   <- pop FINAL, cursor volta a -1
[FAB48] #1 fab=0x400C6210 ... cursor+C8=-1  <NEGATIVO>
[FAB48] #2 fab=0x403008E8 ... cursor+C8=-1  <NEGATIVO>
```

**Não havia contradição nenhuma.** A "última escrita = 0" que eu tinha lido era só o cap a
esconder o pop final. A pilha esvazia-se normalmente e a consulta acontece **imediatamente
a seguir**, na linha seguinte do log.

## A leitura correcta, e é mais precisa que a anterior

Não falta um `push` e não há desequilíbrio: **a consulta chega depois do ciclo terminar.**
O walker pergunta "qual é o produto corrente?" quando a fábrica já fechou o último par
push/pop e voltou ao estado vazio.

Duas leituras possíveis, e é aqui que a próxima sessão pega:

1. **O produto devia ter sido guardado antes do último pop** — alguém devia ter copiado o
   `*(fab+0x48+cursor*4)` para outro lado enquanto ainda estava empilhado, e não copiou.
2. **A consulta devia acontecer mais cedo**, dentro do último par — o walker chama
   `vt[0x48]` fora da janela em que a resposta existe.

A ordenação no log (pop final e depois a consulta, linhas consecutivas) favorece (2), mas
**não a prova**: falta ver se há um caminho em que a consulta corre antes do pop.

## Terceira armadilha de instrumentação do dia

O `lr` do guest, o tracer que só cobria metade dos despachantes, e agora **um cap escrito a
martelo**. As três mentiram da mesma maneira: por omissão, e a omissão parecia prova. O cap
ficou configurável (`PS3_WATCH_STORE_CAP`, `<0` = sem limite) e o comentário no motor conta
porquê.

---

# Com os dois caps desligados, os números batem — e a leitura fica firme

```
PS3_TRACE_ICALL_TO=0x0039DA78 (o push), sem cap:

  2610 pushes no total
   175 para a fábrica 0x400C6210      <- exactamente o que o watch do cursor viu
```

Os dois instrumentos independentes concordam. E o push mais recente antes da consulta que
falha é para **outra** fábrica (`0x400FE150`), não para a `0x400C6210`.

**A sequência é:** os 175 push/pop da `0x400C6210` correm e fecham (cursor a −1) → o
trabalho continua noutras fábricas → e só então o walker consulta a `0x400C6210`, que já
está vazia há muito.

Não é "falta um push". Não é desequilíbrio. **É uma consulta feita fora da janela em que a
resposta existe.**

## O balanço de instrumentação deste dia

Quatro ferramentas mentiram-me, todas por omissão:

| ferramenta | como mentiu | custo |
|---|---|---|
| `lr` do guest | fica preso no último `bl`; num `bctrl` aponta para a função errada | uma ronda a provar que `func_0024D5BC` estava saudável |
| rbp frame walk | trampolins e tail-calls não mapeiam 1:1 | uma ronda a medir 224 despachos que não eram o caminho |
| `PS3_TRACE_ICALL_TO` | só cobria `ps3_indirect_call`, não o `ps3_indirect_tail` | quase escrevi que `func_00220F88` não era chamada indirectamente |
| caps a martelo (300 e 64) | cortam em silêncio e o corte parece o fim dos dados | duas conclusões erradas, ambas escritas antes de medir |

As quatro estão corrigidas e documentadas no motor, ao lado do código que as causou. **É o
que fica de mais reutilizável desta sessão** — mais do que qualquer elo individual da
cadeia.

---

# Uma hipótese de bug do lifter, levantada e REFUTADA por medição

Auditando o walker do WAD por `cr3` (o `ctx->cr >> 12` que decide o ramo que leva ao
`func_0024E3D0`), cinco fragmentos **usam `cr3` sem o definirem**:

```
func_0024E1E8   set=[27]  use=[42,62]     ok
func_0024E208   set=[19]  use=[34,54]     ok
func_0024E26C   set=[]    use=[29]        <- usa sem definir
func_0024E29C   set=[]    use=[15]        <- usa sem definir
func_0024E2A8   set=[]    use=[12]        <- usa sem definir
func_0024E270   set=[]    use=[28]        <- usa sem definir  (é o elo #6 do backtrace)
func_0024E284   set=[]    use=[22]        <- usa sem definir
```

Estaticamente, isto parece exactamente o **fallthrough cross-fragment** que o `CLAUDE.md`
manda auditar (o padrão do fix `func_002550C8`). No modelo do lifter é legítimo — o `ctx`
é partilhado e `cr3` sobrevive à queda — **mas só se esses fragmentos nunca forem entrados
de fora**. E todos os cinco estão na tabela de lookup, logo podem ser alvo de um `bctrl`.

**Medido, com o cap já corrigido (`PS3_TRACE_ICALL_TO_CAP=-1`):**

```
entradas indirectas nos cinco fragmentos: 0
[FAB48] (a falha) continua a acontecer:   2
```

Zero. Numa corrida que chega ao ponto de falha. **Nenhum deles é entrado de fora**, `cr3`
chega sempre do fragmento anterior, e a hipótese cai.

Vale registá-la na mesma: é a primeira vez nesta sessão que um "zero" é de confiança, e é
por causa do cap corrigido meia hora antes. Antes disso teria sido mais um falso negativo.

---

# A experiência do topo: o `FATAL` é mesmo consequência daquela escrita

Gate declarado no topo da cadeia (`PS3_24E3D0_KEEP_TYPE_ON_NULL=1`, OFF por default, uma
linha por salto — **não é um fix**): quando o produto é NULL, não escrever o tipo lido de
`*(0x20)` e deixar o campo como estava.

```
[24E3D0-GATE] produto=NULL -> tipo do obj 0x4077ED10 mantido em 2
[24E3D0-GATE] produto=NULL -> tipo do obj 0x4077F0E0 mantido em 2
```

| | sem gate | com gate |
|---|---:|---:|
| `FATAL: stuck calling 0x00514E80` | 1 | **0** |
| `StartSeq` / `R_PermA` | 2 / 1 | 2 / 1 |
| `thr_auto_load end` | 0 | **0** |
| `[vm] OOB access` (ponteiro-texto novo) | — | **40** |

**O `FATAL` desaparece.** Isso prova, por experiência directa, que os últimos oito elos da
cadeia são todos consequência daquela única escrita — e não problemas independentes.

**Mas o boot não avança**, e aparece uma família nova de acessos fora de mapa
(`0xE5726D65`, outro ponteiro-texto). Faz sentido: manter o tipo em `2` **não é a resposta
certa** — é só o valor anterior. O tipo correcto é o que o produto teria dado, e o produto
não existe.

## O que isto delimita, e é útil

- **Confirmado:** a escrita de `*(NULL+0x20)` é a causa do `FATAL` e de tudo o que vem
  depois dele.
- **Confirmado:** repor o tipo anterior não basta — o objecto precisa do tipo *certo*, não
  de um tipo qualquer.
- **Logo o alvo real não é a escrita**, é fazer com que a fábrica tenha produto quando esta
  consulta acontece. O que devolve à pergunta de ordenação: porque é que a consulta corre
  depois do ciclo fechar.

O gate fica commitado como ferramenta de delimitação, com o resultado no cabeçalho, para
que ninguém o confunda com uma correcção.

**Nota sobre a amostra — e uma correcção ao que aqui estava escrito:** primeiro escrevi
"o gate disparou em 1 de 3 corridas". **Errado, e pelo motivo de sempre:** li o resultado
com a 2.ª corrida ainda a decorrer e tomei o estado intermédio por final. As três
completaram:

```
run1 GATE=2 thr_end=0 startseq=2 r_perma=1 FATAL=0
run2 GATE=2 thr_end=0 startseq=2 r_perma=1 FATAL=0
run3 GATE=2 thr_end=0 startseq=2 r_perma=1 FATAL=0
```

**3/3.** O gate dispara sempre e o `FATAL` desaparece sempre, sem regressão nos elos a
montante. O resultado é mais forte do que eu tinha escrito — mas o erro é o mesmo das
outras dez vezes desta sessão: **um estado observado a meio não é o resultado.**

---

# O interruptor: `r26 == 0` é o que manda o walker pedir o produto

Lido de `func_0024E1E8` (o `cr3` que decide o ramo do `func_0024E3D0`):

```c
linha 27:  cmpwi cr3, r26, 0                  // cr3 = (r26 == 0)
linha 42:  if (cr3 != EQ) -> func_0024E430    // r26 != 0 -> outro caminho
           // r26 == 0 -> segue para loc_0024E270, o walker de registos
linha 62:  (o outro uso de cr3, que leva ao func_0024E3D0)
```

**`r26` é o interruptor.** Com `r26 == 0` o walker corre e acaba a pedir o produto corrente
à fábrica — assumindo que ele existe. Com `r26 != 0` toma outro caminho e a consulta não
acontece.

Isso reformula a última pergunta com precisão: não é só *"porque é que a consulta chega
tarde"*, é **"porque é que `r26` vale 0 nestas duas passagens"** — porque é `r26` que
escolhe o caminho que assume um produto empilhado.

`r26` é callee-saved e vem da entrada de `func_0024E1E8` ou do seu chamador
(`func_0024F028`). Uma sonda na entrada — a técnica que se provou a única fiável neste
lift — dá o valor e a origem numa corrida.

---

# Segunda suspeita de bug do lifter — também refutada, e antes de a escrever como facto

`func_0024E1E8` (onde o `cr3` decide o ramo) é um **fragmento**: restaura os callee-saved
do stack mas nunca define `r26`. O prólogo real está em `0x0024E19C`
(`stdu r1,-224(r1)` … `0x0024E1D8: mr r26, r5`), e `func_0024E19C` **não existe no lift**.

Isso tinha toda a cara do padrão que o `CLAUDE.md` manda auditar. Verifiquei antes de o
escrever:

1. **Chamadas directas a `0x0024E19C` no PPC original: zero.** Ninguém lá salta por `bl`.
2. **O prólogo está liftado** — sob outro nome, `func_0024E198` (o lifter começou a função
   quatro bytes antes, no `mfcr`/`cmpwi` que precede o `stdu`), e contém as duas linhas que
   interessavam:

```c
vm_write64(ctx->gpr[1] + 0xB0, ctx->gpr[26]);   // std r26, 176(r1)
ctx->gpr[26] = ctx->gpr[5];                     // mr  r26, r5
```

**Não há bug.** É a segunda hipótese de defeito do lifter que levanto hoje e a segunda que
cai por verificação — a primeira foi o `cr3` cross-fragment. Ambas pareciam sólidas
estaticamente.

## O facto que fica, e é o topo da cadeia em termos do próprio jogo

`r26` é o **terceiro argumento** (`r5`) de `func_0024E198`. `r26 == 0` significa que **o
chamador passou `r5 = 0`** — e é isso que selecciona o caminho que assume um produto
empilhado na fábrica.

O chamador é `func_0024F028` (elo #7 do backtrace original). A pergunta final, em termos
do jogo e não da máquina:

> **Porque é que `func_0024F028` chama `func_0024E198` com o terceiro argumento a zero
> para os registos de tipo `0x3` e `0xF`?**

É uma sonda na entrada de `func_0024F028` a registar os seus próprios argumentos — a mesma
técnica que se provou a única fiável neste lift, e que resolve numa corrida.

---

# O topo, na lógica do próprio jogo: `r5 = (x == r31)`

O sítio em `func_0024F028` que produz o terceiro argumento:

```c
r3 = <resultado da chamada indirecta anterior>;
r3 = r31 ^ r3;                        // XOR
r0 = sraw(r3, 31);                    // 0 se >=0, -1 se <0
r5 = (r0 ^ r3) - r0;                  // = abs(r3)
r5 = r5 - 1;                          // abs - 1
r5 = rlwinm(r5, 1, 31, 31);           // extrai o bit que fica a 1 só quando abs==0
func_0024E198(r3 = r29, r4 = r26, r5);
```

É o idioma clássico de **`r5 = (x == r31) ? 1 : 0`**: se `x == r31`, o XOR dá 0, o `abs`
dá 0, o `-1` dá `0xFFFFFFFF` e o `rlwinm` extrai um 1. Se forem diferentes, dá 0.

**`r5 == 0` significa portanto: `x != r31`.**

E é esse `r5 = 0` que vira `r26 = 0` dentro de `func_0024E198`, que activa o `cr3`, que
manda o walker pedir o produto à fábrica — o produto que não existe.

## A cadeia, do princípio ao fim, em uma frase por elo

```
uma comparação em func_0024F028 dá "diferente" (x != r31)  ->  r5 = 0
 -> func_0024E198 recebe r26 = 0
   -> cr3 encaminha para o ramo que assume um produto empilhado
     -> func_0024E3D0 pede o produto corrente à fábrica 0x400C6210
       -> o cursor +0xC8 está a -1 (pilha vazia) e func_0039E5A8 devolve NULL
         -> escreve *(NULL+0x20) = 0 como tipo do objecto
           -> o lookup do registo devolve array[0] -> classe 0x00515008
             -> essa classe não tem tabela de pools em +0x8C
               -> o "pool" é 0, o pop devolve lixo, ninguém verifica
                 -> this=0 em func_00220284; o objecto nunca é alocado
                   -> nó de lista com payload 0
                     -> func_002545D4 lê o "tipo" do endereço 2 -> tab[lixo]=0
                       -> FATAL: stuck calling 0x00514E80
                         -> thr_auto_load nunca termina -> gate 0/6
                           -> o menu não aparece
```

## A pergunta que abre a próxima sessão

**O que é `x` e o que é `r31` nessa comparação, e porque diferem para os registos de tipo
`0x3` e `0xF`?** `x` vem da chamada indirecta imediatamente anterior; `r31` é
callee-saved de `func_0024F028`. Uma sonda nesse ponto — a registar `x`, `r31` e o `r5`
resultante — responde numa corrida, e é a mesma técnica que resolveu todos os outros elos.

O gate `PS3_24E3D0_KEEP_TYPE_ON_NULL` já provou (3/3) que resolver isto derruba os oito
elos finais de uma vez.

---

# CORRECÇÃO SUBSTANCIAL: os dois elos do topo estão MAL ATRIBUÍDOS

Sondei a comparação que eu tinha descrito como a origem do `r26 = 0`. O resultado
contradiz-me:

```
[CMP4F028] #1 x=0x406380F8 r31=0x406380F8 iguais=1 ...      <- IGUAIS, logo r5 = 1
[TYPEASSIGN] #1 ... produto=0 tipo_escrito=0                 <- e mesmo assim o walker corre
[TYPEASSIGN] #2 ... produto=0 tipo_escrito=0                    (duas vezes, contra UMA comparação)
```

E `func_0024E198` tem **dois** call sites, não um:

```
0x0024F120:  r5 = (x == r31)     -> medido: 1
0x0024F188:  r5 = 1              -> literal, no código
```

**Nenhum dos dois passa `r5 = 0`.** Portanto o `r26 = 0` que eu li em `func_0024E1E8`
**não vem do terceiro argumento**, e os dois elos que acrescentei por cima do
`produto = NULL` — o `r26` e a comparação — estão mal atribuídos.

## O que continua de pé, e o que cai

**De pé (tudo medido, elo a elo):**

```
o cursor +0xC8 da fábrica está a -1  ->  func_0039E5A8 devolve NULL
 -> func_0024E3D0 escreve *(NULL+0x20) = 0 como tipo do objecto
   -> o lookup do registo devolve array[0] -> classe 0x00515008
     -> sem tabela de pools -> pool=0 -> pop devolve lixo -> ninguém verifica
       -> this=0, objecto nunca alocado, nó com payload 0
         -> tab[lixo]=0 -> FATAL 0x00514E80 -> thr_auto_load 0 -> sem menu
```

E o gate `PS3_24E3D0_KEEP_TYPE_ON_NULL` prova (3/3) que remover a escrita remove o `FATAL`
— portanto esta metade da cadeia é causal, não correlação.

**Cai:** a explicação de *porque* `func_0024E3D0` corre. O `cr3`/`r26` que eu identifiquei
como interruptor não pode vir do `r5` (é 1 nos dois sítios). Ou `r26` é alterado entre o
prólogo e o uso, ou a função que corre com `r26 = 0` é outra.

## A décima segunda correcção, e a mesma forma

Liguei a comparação ao sintoma **sem verificar a correspondência de contagens**: uma
comparação contra dois registos afectados. Isso, sozinho, já dizia que não podia ser a
origem — e eu escrevi-o na mesma.

A regra que falhei é a mesma que já tinha escrito duas vezes hoje: **contar antes de
concluir.** Um elo que dispara uma vez não explica um sintoma que aparece duas.

---

# E a medição no ponto de uso derruba o resto: `r26 = 1`

Sondado onde interessa, em vez de inferido:

```
[R26] #1 r26=0x00000001 <nao-zero: outro ramo>  r30=0x4077EBB0 r31=0x4077EBB0 lr=0x0024E1F4
[TYPEASSIGN] dispara na mesma, duas vezes
```

**`r26` vale 1.** O ramo que eu tinha identificado como o caminho para `func_0024E3D0` **não
é tomado**, e a função corre à mesma.

Ou seja: não errei só a origem do `r26` — errei o **caminho inteiro**. `func_0024E3D0` é
alcançada por outra via, que continua por identificar.

## O estado real do conhecimento, sem embelezar

**MEDIDO, elo a elo, e com causalidade provada por experiência (gate 3/3):**

```
cursor +0xC8 = -1  ->  func_0039E5A8 devolve NULL
 -> func_0024E3D0 escreve *(NULL+0x20) = 0 como tipo do objecto
   -> lookup do registo devolve array[0] -> classe 0x00515008 (sem pools)
     -> pool=0 -> pop devolve lixo -> ninguém verifica
       -> this=0, objecto nunca alocado, nó com payload 0
         -> tab[lixo]=0 -> FATAL 0x00514E80 -> thr_auto_load 0 -> sem menu
```

**DESCONHECIDO (duas tentativas, ambas refutadas por medição):**

- porque é que `func_0024E3D0` é alcançada — não é pelo `cr3`/`r26` que eu apontei
- porque é que o cursor da fábrica está a `-1` nesse instante

## Nota de método — e é a que fecha o dia

Nos últimos ciclos acrescentei elos mais depressa do que os verifiquei, e **tive de
retirar dois**. As duas retractações têm a mesma causa: escrevi a explicação e só depois a
medi.

A metade de baixo da cadeia é sólida porque foi construída ao contrário — medir primeiro,
escrever depois. A metade de cima não existe: são hipóteses que caíram.

Fica assim marcado no documento, para que ninguém herde as minhas suposições como se
fossem factos.

---

# CORRECÇÃO DA CORRECÇÃO: li a polaridade ao contrário. O elo estava certo.

Os sete sítios que saltam para `func_0024E3D0` têm todos a mesma condição:

```c
if ((!((ctx->cr >> 12) & 2))) { g_trampoline_fn = func_0024E3D0; return; }
```

`(cr >> 12) & 2` é o bit **EQ** de `cr3`, e `cr3` foi posto por `cmpwi cr3, r26, 0`.
Logo a condição é **`r26 != 0`** — e não `r26 == 0`, como eu tinha escrito.

**Com `r26 = 1` medido, o ramo É tomado.** Não havia contradição nenhuma entre o `[R26]` e
o `[TYPEASSIGN]`: eram consistentes, e fui eu que inverti a leitura.

## O que isto repõe

A retractação anterior (*"os dois elos do topo estão mal atribuídos"*) **estava ela própria
errada**. A cadeia estava certa; a descrição é que tinha o sinal trocado. Reposta:

```
o call site passa r5 = 1  ->  r26 = 1
 -> cr3 diz "r26 != 0"  ->  ramo para func_0024E3D0
   -> pede o produto corrente à fábrica 0x400C6210
     -> cursor +0xC8 = -1 (pilha vazia)  ->  func_0039E5A8 devolve NULL
       -> escreve *(NULL+0x20) = 0 como tipo do objecto
         -> lookup devolve array[0] -> classe 0x00515008 (sem tabela de pools)
           -> pool=0 -> pop devolve lixo -> ninguém verifica
             -> this=0, objecto nunca alocado, nó com payload 0
               -> tab[lixo]=0 -> FATAL 0x00514E80 -> thr_auto_load 0 -> sem menu
```

**Cada elo desta cadeia está agora medido**, e o gate `PS3_24E3D0_KEEP_TYPE_ON_NULL` prova
(3/3) que o troço final é causal.

## O que continua genuinamente desconhecido

**Uma coisa só: porque é que o cursor `+0xC8` da fábrica está a `-1` no instante em que o
walker pede o produto.** A pilha está equilibrada (175 push / 175 pop) e o pedido chega
depois do último pop — mas não se sabe se o produto devia ter sido guardado antes, ou se o
pedido devia acontecer dentro do par.

## Décima quarta correcção — e a primeira que devolve um elo em vez de o tirar

Retirei um elo correcto por ter lido `!(EQ)` como "igual a zero". As treze anteriores foram
conclusões a mais; esta foi uma conclusão a menos, pelo mesmo defeito de fundo: **ler
depressa e escrever antes de conferir**. A diferença é que desta vez a verificação era
gratuita — estava tudo no ecrã.

---

# A última pergunta, respondida: o walker NUNCA empilha

De onde vêm os 175 pushes para a fábrica `0x400C6210`:

```
172  func_0041F700+0x718     (lr=0x0042AF5C)
  3  func_00256B64+0x1394    (lr=0x00256CE0)
  ---
  0  da família do walker de registos do WAD (func_0024Exxx)
```

**Zero.** O walker que pergunta "qual é o produto corrente?" **nunca empilha nada** na
fábrica. Os pushes todos vêm de outro subsistema (`func_0041F700`, 172 dos 175), noutra
fase da execução.

Portanto o cursor estar a `-1` no instante do pedido não é anomalia da fábrica: é o estado
normal fora da janela em que **outro** código a está a usar. O walker faz uma pergunta que
só tem resposta enquanto esse outro subsistema estiver a meio de um par push/pop — e nesse
instante não está.

## A cadeia, completa e inteiramente medida

```
o walker de registos do WAD pergunta pelo produto corrente sem nunca ter empilhado
 └─ o cursor +0xC8 da fábrica 0x400C6210 está a -1 (estado normal fora da janela)
   └─ func_0039E5A8 devolve NULL
     └─ func_0024E3D0 escreve *(NULL+0x20) = 0 como tipo do objecto
       └─ o lookup do registo devolve array[0] -> classe 0x00515008 (sem tabela de pools)
         └─ o "pool" é 0, o pop devolve lixo, ninguém verifica o retorno
           └─ this=0 em func_00220284; o objecto nunca é alocado
             └─ nó de lista com payload 0
               └─ func_002545D4 lê o "tipo" do endereço 2 -> tab[lixo]=0
                 └─ FATAL: stuck calling 0x00514E80
                   └─ thr_auto_load nunca termina -> gate 0/6
                     └─ o menu não aparece
```

**Cada elo medido. O troço final provado causal pelo gate (3/3).**

## O que isto significa para quem retomar

A pergunta deixa de ser "porque falha X" e passa a ser de **desenho**: no console, o que
garante que a fábrica tem produto quando o walker do WAD pergunta? Duas leituras, e a
resposta decide o fix:

1. **O walker devia empilhar** — falta-lhe um `func_0039DA78` próprio antes de perguntar.
2. **O walker não devia perguntar** — o caminho `func_0024E3D0` só devia correr quando
   invocado de dentro do contexto de `func_0041F700`, e chega lá por outra via.

A (2) é consistente com tudo o que se mediu hoje (cinco sítios a receber contexto errado).
A (1) verifica-se num minuto: procurar no PPC original um `bl` para a fábrica dentro do
corpo do walker.

---

# E o último dado: `func_0024F028` é genérica, usada em dois contextos

```
chamadores estáticos de func_0024F028: >10 sítios distintos
  0x00021468  0x00026478  0x000C7378  0x00038D90  0x00038FBC
  0x00039014  0x0009F840  0x00144248  ...  + 2 trampolins
```

Não é uma função do carregamento de WAD — é **utilitária, chamada de todo o lado**. E o
backtrace da falha mostra-a alcançada por
`func_002B2E74 → func_000B71B8 → func_0010F5E8 → func_0024F028`, ou seja pela
**inicialização do motor**, não pelo caminho onde a fábrica está a produzir.

Enquanto isso, os 175 pushes vêm de `func_0041F700` (`lr=0x0042AF5C`), que é outro
contexto por completo.

**A leitura final:** o mesmo utilitário corre em dois contextos — dentro da janela de
produção da fábrica (onde a pergunta tem resposta) e fora dela (a inicialização, onde não
tem). O boot falha no segundo.

Isso encaixa exactamente no padrão que esta sessão inteira mediu: **cinco sítios a receber
contexto errado**, e este é o sexto — o mesmo código a correr onde a sua pré-condição não
se verifica.

## Estado final do documento

Tudo acima está medido. A única coisa que fica por decidir é de **desenho**, e são duas
hipóteses testáveis:

1. o caminho `func_0024E3D0` não devia correr no contexto de inicialização;
2. ou devia, e falta quem estabeleça a janela de produção antes.

Nenhuma se resolve por leitura estática — ambas precisam de uma corrida com sonda nos dois
contextos, a comparar o estado da fábrica.

---

# Com o gate ligado, a parede seguinte é a MESMA função com o tipo errado

Estendido o `[OOBRA]` a todos os OOB (estava limitado a `a >= 0xFFFF0000`, o que excluía
precisamente a classe que o seu comentário descreve — quinta armadilha de instrumentação
da sessão). Com `PS3_24E3D0_KEEP_TYPE_ON_NULL=1`:

```
[OOBRA] @0xE5726D65 ra1=func_002545D4+0x750   <- a MESMA função da cadeia
[OOBRA] @0xE5726D6F ra1=func_0039D764+0x80C   <- e a MESMA fábrica
[OOBRA] @0xE5726D67 ra1=func_0039D764+0x840
```

`0xE5726D65` contém `72 6D 65` = `"rme"` — outra vez texto usado como ponteiro.

**Confirma o que o gate já sugeria:** manter o tipo em `2` não é a resposta. Com o tipo
errado, o objecto é lido com o layout errado e os campos vêm texto — e o walk volta às
mesmas duas funções (`func_002545D4`, `func_0039D764`) por outro caminho.

O objecto precisa do **tipo certo**, e o tipo certo só existe se a fábrica tiver produto.
Fecha o círculo no mesmo sítio, e por isso o alvo continua a ser esse, não a escrita.

---

# FRONTEIRA: com AMBOS os gates ligados o boot continua a não chegar ao AutoLoad

Corrida com os dois gates de diagnóstico e o disjuntor levantado
(`PS3_24E3D0_KEEP_TYPE_ON_NULL=1 PS3_LIST254_EMPTY_IF_NULL=1
PS3_STUCK_ICALL_LIMIT=2000000`):

```
run1  st620=1                                        (nao passou a intro — flakiness ~1/3)
run2  st620=11 startseq=2 r_perma=1 setflip=2386 FATAL=0  G1=2 G2=1  thr_end=0
run3  st620=11 startseq=2 r_perma=1              FATAL=0  G1=2 G2=1  thr_end=0
```

**2/2 das corridas que passaram a intro**: os dois gates disparam, o `FATAL` desaparece, e
o `thr_auto_load` continua a não chegar.

E onde fica preso:

```
[vm] UNCOMMITTED read16 access 0x65726D69 (+2) ra=func_002547AC+0x454
... a mesma linha, em loop, ate ao fim do log
```

`0x65726D69` = `"ermi"` em ASCII — texto usado como ponteiro, outra vez. E
`func_002547AC` é vizinha directa de `func_002545D4`/`func_002545B0`: **a mesma família,
o mesmo sintoma.**

## O que esta medição delimita, e é importante não a suavizar

A cadeia de quinze elos deste documento **é real e está medida**, e o gate prova que o
troço final é causal. Mas **não é a única barreira entre o estado actual e o menu**:
removidas as duas paredes conhecidas, aparece imediatamente uma terceira do mesmo género.

Isso significa que resolver a pergunta em aberto (a fábrica sem produto) **não garante o
menu** — garante passar deste obstáculo. É a diferença entre "encontrei a causa" e
"encontrei uma causa", e vale a pena que fique escrito antes que alguém (eu incluído)
sobre-estime o alcance do diagnóstico.

## O padrão, agora com sete ocorrências

Todos os obstáculos medidos hoje têm a mesma forma: **um objecto lido com o layout de outro
tipo, e os campos vêm texto ou zero.** `func_002545D4`, `func_002545B0`, `func_00263554`,
`func_00220284`, `func_0024E3D0`, `func_0039D764`, e agora `func_002547AC`.

Sete sítios, uma doença. A pergunta que interessa não é qual deles corrigir — é **porque é
que os objectos chegam com o tipo errado**, que é a Parede D do `CLAUDE.md` e o que o
trabalho de TYPE15/`CB56C` já persegue.

---

# A Parede D não se moveu — medido, e é o que decide a próxima frente

Sinais do registo de shaders, comparados entre o binário de antes das correcções de hoje,
o baseline actual, e a corrida com os dois gates ligados:

| | `LDRSH` | `SHADERSRC` | `ICGLdr` | `set_shader` | `draw_*` | `bind_texture` |
|---|---:|---:|---:|---:|---:|---:|
| antes das correcções | 0 | 0 | 0 | 23 | 20 | 8 |
| baseline actual | 0 | 0 | 0 | 23 | 20 | 8 |
| com ambos os gates | 0 | 0 | 0 | 23 | 20 | 8 |

**Idênticos nas três.** Os 23 `set_shader`, 20 draws e 8 `bind_texture` são todos do
caminho da intro/filme — não mudaram nada. E o registo de shaders continua com **zero**
entradas.

## O que isto estabelece, e é o resultado mais accionável do dia

Tudo o que corrigi e diagnostiquei hoje — o stomp do pump, a cadeia de quinze elos, os dois
gates — **não moveu a Parede D um único ponto**. Ela está exactamente onde estava.

Isso não invalida o trabalho: o stomp era real e destruía memória do jogo, e a cadeia
explica o `FATAL`. Mas diz, com números, que **continuar a escavar esta cadeia não vai
chegar ao menu**. Os sete sintomas medidos são todos a jusante de objectos com o tipo
errado, e o registo que devia dar-lhes o tipo certo está vazio.

**A próxima frente é a Parede D**, atacada de frente: o nested walk do typemap pós-R_PermA
até o `ICGLdrShader` ser invocado pelo caminho real, com `SHADERSRC` a passar de 0 para
N>0. É o item 1 dos "Próximos passos" do `CLAUDE.md`, e continua a ser o que bloqueia
pixels de jogo.

---

# PAREDE D, vista de dentro: o jogo diz "Unknown component"

Corrida com o `PS3_GATE_FORCE=1` (a via **forçada** que já existia no motor — não é aceite
como caminho natural, CLAUDE.md regra 5 — mas o diagnóstico que produz é válido):

```
[GATE-FORCE] base=0x00700DF8 component=0x4306ADF0 typemap=0x4306B160 vt=0x005130B8
[GATE-FORCE] stream setup stream=0x0FEFE960 data=0x0FEFEA60 type@data=0xF85F9B1E
[GATE-FORCE] calling OPD 0x522E70 (171244) this=0x4306B160 ...
[OPD-ICG] #1 opd=0x00522E70 code=0x00171244 toc=0x00541178 lr=0x002B9CA8
[vm] UNCOMMITTED read32 access 0x65726D61 (+4) ra=ppu_opd_resolve+0x24
[vm] UNCOMMITTED read32 access 0x65726D65 (+4) ra=ppu_opd_resolve+0x3C
Unknown component 0xf85f9b1e in component loader ICGLdrShader.
Unknown component 0xf85f9b1e in component loader ICGLdrShader.
[GATE-FORCE] returned from 171244 path r3=0x008695EC
```

Duas coisas, e as duas são novas:

**1. O `ICGLdrShader` É invocado e responde.** O caminho chega lá — o que faltava não é a
chamada. E a resposta é a mensagem de erro do **próprio jogo**:
`"Unknown component 0xf85f9b1e in component loader ICGLdrShader."` Ou seja o loader não
reconhece o tipo que o registo diz ser o dele.

**2. O `ppu_opd_resolve` lê através de um ponteiro-texto:** `0x65726D61` = **`"erma"`** em
ASCII — de `R_P`**`erma`**. A tabela de componentes tem **strings onde deviam estar OPDs**.

## Porque isto importa mais do que a cadeia de hoje

É o **oitavo** sítio com a mesma assinatura — texto onde devia estar um ponteiro — mas é o
primeiro **dentro do loader de componentes**, e o primeiro em que o jogo diz por palavras
o que está mal. `LDRSH`, `SHADERSRC` e `TYMAP-171` continuam todos a `0`: o loader corre e
rejeita.

A Parede D deixa de ser "o ICGLdr nunca é invocado" (que era a formulação nas notas
antigas) e passa a ser: **o ICGLdr é invocado e não reconhece o componente, porque a tabela
que o descreve tem texto em vez de ponteiros.**

Isso é uma reformulação com consequências: o trabalho não é fazer a chamada acontecer — é
perceber porque é que a tabela de componentes tem `"...erma..."` onde devia ter OPDs. E
liga-se directamente ao `R_PermA`, o WAD de 20 MB, cujo nome é literalmente o texto que lá
está.

**Ressalva:** isto foi obtido com `PS3_GATE_FORCE=1`, um caminho forçado. Não conta como
aceite de que o caminho natural funciona. Conta como diagnóstico do que acontece quando se
chega lá.

---

# Quem passa o OPD-texto: `func_001856A8`, e é a linha antes do "Unknown component"

Acrescentado um `[OPD-BAD]` ao `ppu_opd_resolve` (gated, `PS3_TRACE_OPD_BAD`), a denunciar
qualquer OPD implausível com a cadeia host simbolizada. Corrida com `PS3_GATE_FORCE=1`:

```
 13x  opd=0x00000000    ra1=func_0039E6B4+0x760    <- a fábrica do tipo WAD
  1x  opd=0x00000000    ra1=func_002B0AA8+0x3B8
  1x  opd=0x65726D61    ra1=func_001856A8+0x72C  ra2=func_0032109C+0x2AC
      [vm] UNCOMMITTED read32 access 0x65726D61 ra=ppu_opd_resolve
      [vm] UNCOMMITTED read32 access 0x65726D65 ra=ppu_opd_resolve
      Unknown component 0xf85f9b1e in component loader ICGLdrShader.
```

**Dois defeitos distintos, os dois no loader:**

1. **`func_0039E6B4+0x760` chama com OPD nulo, 13 vezes.** É a fábrica do tipo WAD — a
   mesma que o `[WADLD-VT28]` mostra com `code=0x0039E6B4`. Um slot de despacho a zero.

2. **`func_001856A8+0x72C` chama com OPD = `0x65726D61`** — `"erma"`, de `R_P`**`erma`**.
   Chamada de `func_0032109C`, que é um dos OPDs que o motor já rastreia no `[OPD-ICG]`.
   E é **a linha imediatamente antes** da mensagem de erro do jogo.

O (2) é o que produz o `"Unknown component 0xf85f9b1e"`: o loader vai buscar o handler do
componente, encontra o **nome do WAD** onde devia estar um ponteiro de função, e desiste.

## O estado da Parede D ao fim do dia

Antes: *"o ICGLdr nunca é invocado (TYMAP-171=0, LDRSH=0)"*.

Agora, medido: **o ICGLdr é invocado, corre, e rejeita o componente — porque
`func_001856A8` lhe passa o nome do WAD como ponteiro de OPD.** Mais 13 despachos com OPD
nulo na fábrica do tipo WAD, a jusante.

O alvo passa a ser `func_001856A8+0x72C`: de onde vem o valor que ele usa como OPD, e
porque é o texto `"...erma..."`. É a mesma pergunta dos outros oito sítios — objectos e
tabelas com o conteúdo de outro tipo — mas agora no sítio que o próprio jogo assinala.

**Ressalva mantida:** tudo isto sob `PS3_GATE_FORCE=1`, caminho forçado. Diagnóstico
válido; não é aceite de que o caminho natural funcione.

---

# CORRECÇÃO: o OPD-texto era artefacto do gate forçado. Retirado.

Estendida a sonda `[STREAM-OPD]` (que já existia no sítio) para mostrar o objecto e a
vtable:

```
[STREAM-OPD] #1 opd=0x65726D61 code=0 obj=0x0FEFE960 vt=0x00000000 vt+8ea=0x00000008
```

`obj = 0x0FEFE960` é **o stream sintético que o próprio `PS3_GATE_FORCE` monta na pilha**
(o log do gate di-lo: `stream setup stream=0x0FEFE960 data=0x0FEFEA60`). Esse stream falso
não tem vtable (`*(obj) = 0`), portanto `opd = *(0 + 8) = *(0x8)` — a leitura do endereço
guest 8, que por acaso contém `"erma"`.

**O `"...erma..."` não vem de nenhuma tabela do jogo. Vem de `*(0x8)`, porque o objecto que
o gate forjou tem vtable nula.**

Corrida de controlo, **sem** `PS3_GATE_FORCE`:

| | com gate forçado | natural |
|---|---:|---:|
| `opd=0x65726D61` (texto) | 1 | **0** |
| `Unknown component 0xf85f9b1e` | 2 | **0** |
| `opd=0x00000000` | 14 | **14** |

## O que cai e o que fica

**CAI** — tudo o que escrevi acima sobre "a tabela de componentes tem strings onde deviam
estar OPDs" e sobre o `"Unknown component"` ser um sintoma do jogo. **São os dois
consequência do stream sintético.** A mensagem de erro do jogo é a resposta correcta a um
componente que o gate inventou.

**FICA, e é real** — no caminho **natural**, sem gate nenhum:

```
13x  ps3_call_opd(opd = 0x00000000)  de func_0039E6B4+0x760   <- a fábrica do tipo WAD
 1x  ps3_call_opd(opd = 0x00000000)  de func_002B0AA8+0x3B8
```

**A fábrica do tipo WAD despacha treze vezes através de um slot de OPD a zero.** Isso é do
jogo, não da instrumentação, e é o achado sólido desta frente.

## Décima quinta correcção — e a mais perigosa do dia

As catorze anteriores foram conclusões erradas sobre dados reais. **Esta foi quase
publicar, como diagnóstico do jogo, sintomas fabricados pela minha própria instrumentação
forçada.** Escrevi um documento inteiro a explicar um `"Unknown component"` que só existe
porque eu liguei um gate que inventa um componente.

A regra que falhei está no CLAUDE.md e eu própria a citei duas horas antes: **um caminho
forçado não é aceite de nada.** Aplica-se também ao diagnóstico, não só ao resultado: os
sintomas que ele produz podem ser dele.

Regra operacional daqui em diante: **qualquer achado obtido sob `PS3_GATE_FORCE` (ou
qualquer outro gate) tem de ser reproduzido sem ele antes de ser escrito.** Esta corrida de
controlo custou 90 segundos.
