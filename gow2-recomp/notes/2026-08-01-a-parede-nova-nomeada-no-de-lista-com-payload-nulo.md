# A parede pós-fix, nomeada ao endereço: um nó de lista com payload NULO

**Data:** 2026-08-01 · Tudo abaixo é **MEDIDO**, 3/3 corridas salvo onde se diz o contrário.

## O caminho até aqui, e um erro meu corrigido por medição

Depois de o fix do `F2B-STREAM-PUMP` parar de destruir os objectos de tipo, o boot passou
a abortar sempre em:

```
[ppu] FATAL: stuck calling 0x00514E80 (2000 times) -- aborting run
```

O `lr=0x0024E2D4` apontava para o walker `func_0024D5BC`. **Mentiu.** O lift só escreve
`ctx->lr` antes dos `bl`; num `bctrl` fica preso no último salto directo. Duas medições
desfizeram a atribuição errada:

1. A sonda `PS3_TRACE_D5BC` mostrou o walker **saudável** — 758 nós, todos com vtable,
   OPD e código válidos (`vt=0x00517B48 opd=0x00530920 code=0x002CEC34`), 3/3 corridas.
2. Simbolizando o `ICALL-BAD` e o rbp frame walk com `dladdr` (ps3recomp `338706e`), a
   cadeia real apareceu de uma vez:

```
ppu_run → func_00010354 → func_0025C838 → func_002B2E74 → func_000B71B8
        → func_0010F5E8 → func_0024F028 → func_0024E270      (walker de registos do WAD)
        → icall → func_0039D428 → icall → func_0039D764      (fábrica de tipos)
        → icall → func_002545D4+0x5AC                        ← o sítio real
```

**Esse `dladdr` é agora permanente** — qualquer `ICALL-BAD` futuro nomeia a cadeia
inteira em vez de dar ponteiros host inúteis entre corridas.

## O defeito, lido e depois confirmado

`func_002545D4` percorre uma lista circular (sentinela em `r28-4+0x80`) e, por nó:

```c
loc_00254628:
    r10 = *(no + 0x8);        // payload
    r31 = *(no + 0x0);        // next
    r11 = 0;
    if (r10 == 0) goto usa;   // <- r11 FICA A ZERO e despacha na mesma
    r11 = r10 + 4;            // registo = payload + 4
usa:
    r4  = r11;
    tipo = *(uint16*)(r11 + 2);
    idx  = (tipo << 2) & 0x3FFFC;
    obj  = *(r29 + idx);      // tab[idx], r29 = *(TOC-0x225C)
    vt   = *(obj + 0);
    opd  = *(vt + 0x18);
    ctr  = *(opd); bctrl
```

A sonda `PS3_TRACE_TYPESLOT` mediu, **idêntico em 4/4 corridas**:

```
[TYPESLOT] NULO idx=0x25F7C tipo=0x97DF tab=0x00868D48 slot=0x00000000 rec=0x00000000
```

- **`rec=0x00000000`** — o payload do nó é nulo, exactamente como o código estático
  sugeria. Não é um tipo mal lido de um registo válido: **não há registo.**
- `tipo=0x97DF` é então `*(uint16*)(0x2)` — memória da base do espaço guest lida como se
  fosse um campo de tipo. Daí ser idêntico em todas as corridas.
- `idx = 0x25F7C` cai muito fora da tabela (os índices válidos observados vão até `0x6C`),
  `tab[idx]` lê 0, e o despacho segue por ponteiro nulo → `ctr = 0x00514E80`.

Os tipos válidos observados no mesmo boot são todos `< 0x40`: `0x03, 0x05, 0x0F, 0x11,
0x15, 0x16, 0x19, 0x20`.

## O pump não é o culpado disto — discriminado

Como o fix passou o pump a escrever **dentro** do ring real do guest (antes escrevia numa
janela hard-coded que nem era a do guest), era preciso excluir que fosse eu a desalinhar o
stream. O `PS3_F2B_PUMP_BYTES` foi acrescentado só para isso, e a matriz é limpa:

| `PS3_F2B_PUMP_BYTES` | `R_PermA` completo | `TYPESLOT` NULO | `FATAL` |
|---|---:|---:|---:|
| `0` (3 corridas) | **0** | 0 | 0 |
| `262144` (3 corridas) | **1** | 40 | 1 |

Com o pump a escrever zero, o WAD de 20 MB **nunca chega a ser lido inteiro** — logo o
pré-enchimento de um ring é o que segura a leitura, e o despacho nulo só aparece no
caminho que corre **depois** disso. O pump habilita o código que bate na parede; não a
cria.

(O gate cego `PS3_FIOS_STREAM_PUMP=0` não servia para isto: desliga o bloco inteiro,
levando atrás o rebind de limit/cursor e o arranque do `f2b_stream_fill` — medido:
`st620` 11→3, `StartSeq` 2→0, `R_PermA` 1→0, filme 0.)

## O que fica por decidir, e é a próxima medição

Duas leituras possíveis, e **nenhuma está provada**:

1. **A lista tem um nó com payload nulo** — dados montados a partir do WAD com um campo
   por preencher. Nesse caso a correcção é a montante, onde o nó é criado.
2. **O ramo está mal liftado** — no PPC original o `beq` saltaria para *continuar o laço*
   (`loc_00254628`) e não para o corpo que usa `r11`. Num PS3 real, desreferenciar `r11=0`
   faria fault na página nula, portanto o jogo nunca pode ter tomado este caminho com
   payload nulo. Isso torna a hipótese do lift **plausível e barata de verificar**.

A verificação é directa: desmontar o PPC original em torno de `0x00254628`–`0x00254644` no
`EBOOT.ELF` e comparar o alvo do `beq` com o que o lift gerou. Se o alvo real for
`loc_00254628`, é um bug do lifter da mesma família do `fallthrough cross-fragment` de
`func_002550C8` — e vira `patch_*.py` idempotente, como esse.

## O estado do objectivo, sem rodeios

**O menu continua por alcançar.** O gate oficial dá 0/6, elo `AUTO_LOAD (thr_end)`. Os
cinco elos anteriores passam em 6/6 e o `setflip_after_rperm` estabilizou em 8–10 (a Fase
7 tinha registado 0 mesmo no binário de referência). A parede está agora nomeada ao
endereço e à instrução, o que não acontecia esta manhã.

---

# Continuação: a "lista" é uma MATRIZ. O objecto é do tipo errado.

**Medido a seguir, no mesmo dia.**

## A lista partida, e as saudáveis ao lado

A sonda `PS3_TRACE_LIST254` mostrou duas listas perfeitamente sãs e uma partida:

```
[LIST254] sent=0x40638608 no=0x40007F34 payload=0x40669528 next=0x40007F28   (13 nós, fecha)
[LIST254] sent=0x40638130 no=0x40007E2C payload=0x40638528 next=0x40007E20   ( 5 nós, fecha)
[LIST254] sent=0x4077914C no=0x00000000 payload=0x00000000 next=0x2F725F7C   <-- PAYLOAD NULO
[LIST254] sent=0x4077914C no=0x2F725F7C payload=0x00000040 next=0x00000000
```

A cabeça é 0; lendo `*(0)` vem `0x2F725F7C`, e lendo `*(0x2F725F7C)` vem 0 outra vez —
**um ciclo de dois**, que é exactamente o laço infinito das 2 000 000 iterações.

## Quem escreve o 0, e porquê não é corrupção

`PS3_WATCH_STORE=0x4077914C` deu **uma única escrita**, e é legítima:

```
[WATCHSTORE] w32 [0x4077914C]=0x0  ra0=func_0024C1F8+0x218  ra1=func_0024CADC+0x25C
```

E `func_0024C1F8`, lido inteiro, é um **inicializador de matriz identidade**: escreve
`f13`(=1.0) e `f0`(=0.0) em `obj+0x70/0x80/0x90/0xA0` e nos três offsets seguintes de cada
linha — as quatro linhas de uma 4×4.

`0x4077914C` = `0x407790D0 + 0x7C` = **elemento [0][3] da matriz**. É legitimamente 0.0.

## O verdadeiro defeito

`func_002545D4` calcula a sentinela da lista como `r28 - 4 + 0x80`, ou seja `obj + 0x7C` —
**o mesmo endereço que a matriz usa para [0][3]**. Duas leituras incompatíveis do mesmo
objecto:

| offset | segundo `func_0024C1F8` | segundo `func_002545D4` |
|---|---|---|
| `obj+0x7C` | `matriz[0][3]` = 0.0 | sentinela da lista circular |

E o teste de "lista vazia" é `head == sentinela` — nunca satisfeito com `head = 0.0`, por
isso o walk entra com um nó nulo em vez de sair.

**Logo: o método virtual de um tipo está a ser invocado sobre um objecto de outro tipo.**
E o despacho vem da fábrica de tipos (`func_0039D428` → `func_0039D764` → `func_002545D4`),
que é o mesmo mecanismo `tab[(tipo<<2)]` de toda esta parede.

## O que se excluiu por medição, e não por argumento

- **Não é bug do lifter no ramo.** O PPC original em `0x0025463C` é
  `bc b(true) cr7.eq -> 0x00254644` — exactamente o que o lift gerou.
- **Não é fragmento mal entrado.** `func_002545B0` (a entrada real, com o prólogo e o
  `mr r28, r4`) faz `ctx->gpr[28] = ctx->gpr[4]` e só depois trampolina para
  `func_002545D4`. O `r28` chega correcto.
- **Não é o walker `func_0024D5BC`.** 758 nós com vtable/OPD/código válidos, 3/3.
- **Não é o meu fix do pump.** A matriz `PS3_F2B_PUMP_BYTES` 0 vs 262144 mostra que o pump
  habilita o código que bate na parede; não a cria.

## A próxima medição

Registar, no despacho da fábrica, **que tipo** produziu o objecto `0x407790D0` e **que
tipo** está a ser invocado sobre ele. Se forem diferentes, o defeito é o índice de tipo;
se forem iguais, é o layout do objecto que diverge — e aí a pergunta passa a ser quem
construiu o objecto com um layout de matriz.

---

# O contra-exemplo saudável, na mesma corrida: o layout está certo, o objecto é que não

Sondando a **entrada real** (`func_002545B0`, o prólogo com o `mr r28, r4`; o
`func_002545D4` do backtrace é um fragmento), a função é chamada **exactamente duas vezes**
numa corrida inteira:

```
[E545B0] #1 this=0x400C61C8 arg=0x4063858C lr=0x0004200C sent=0x40638608 head=0x40007F34
[E545B0] #2 this=0x400C61C8 arg=0x407790D0 lr=0x0024E2D4 sent=0x4077914C head=0x00000000
```

**Mesmo `this`. Dois `arg` diferentes.** A #1 tem uma lista circular perfeitamente válida
— e é o mesmo código, o mesmo offset, o mesmo tudo. A #2 tem `head = 0` e entra em laço.

Isto **fecha uma questão que estava em aberto**: o layout que `func_002545B0` assume
(`lista em arg+0x7C`) não está errado — funciona no #1. O que está errado é o **objecto**
que lhe é passado no #2.

E esse objecto, base `arg-4 = 0x407790CC`, é o mesmo que `func_0024C1F8` inicializou como
**matriz identidade**: o `0x4077914C` que o `PS3_WATCH_STORE` apanhou é `base+0x80`, a
primeira coluna da linha 1 da matriz.

| offset sobre `0x407790CC` | `func_0024C1F8` | `func_002545B0` |
|---|---|---|
| `+0x80` | `matriz[1][0]` = 0.0 | cabeça da lista circular |

O `lr=0x0024E2D4` da chamada #2 cai em `func_0024E1E8`/`func_0024E270` — **o walker de
registos do WAD**. É de lá que o objecto errado vem.

## Duas vias de investigação que morreram, e ficam registadas

- **O `lr` do guest** num `bctrl` fica preso no último `bl`. Mandou-me para
  `func_0024D5BC` e gastei uma ronda a provar que esse walker está saudável (758 nós).
- **O rbp frame walk simbolizado** deu `#2 func_0039D764+0x220`, mas a sonda
  `PS3_TRACE_VT64` mediu os **224** despachos `*(vt+0x64)` dessa função e nenhum tinha
  este objecto nem chamava `0x002545B0`. Com trampolins e tail-calls os frames host não
  mapeiam 1:1 nas chamadas guest — **nem o `lr` guest nem o backtrace host são fiáveis
  para atribuir uma chamada indirecta neste lift.** A sonda na entrada da função é.

---

# O que há por trás da parede: nada — e uma pista a montante

Com o gate de diagnóstico `PS3_LIST254_EMPTY_IF_NULL=1` (declarado, OFF por default,
uma linha de log por salto — **não é um fix**), medido em **3/3 corridas idênticas**:

| | sem gate | com gate |
|---|---:|---:|
| `FATAL: stuck calling 0x00514E80` | 1 | **0** |
| `thr_auto_load end` | 0 | **0** |
| `StartSeq` / `R_PermA` | 2 / 1 | 2 / 1 |

**Saltar a parede não desbloqueia nada.** O abort desaparece e o boot vai directo para o
sampler. Isso responde à pergunta que o gate existia para responder: o problema real está
**a montante**, e não vale a pena trabalhar nesta parede como se fosse a última.

## E o log disse onde

Imediatamente antes do salto, uma rajada de escritas por ponteiro-texto. Simbolizando o
`ra` do `[vm] UNCOMMITTED` (antes era um ponteiro host inútil entre corridas), fica assim:

```
[vm] UNCOMMITTED read32  access 0x726D432E ra=func_00263554+0x1CC
[vm] UNCOMMITTED write32 access 0x726D4332 ra=func_00220284+0xF78
[vm] UNCOMMITTED write32 access 0x726D433A ra=func_00220284+0xFA8
...  16 escritas de func_00220284, endereços a incrementar de 8 em 8
```

`0x726D432E` é ASCII `rmC.` — **texto usado como ponteiro**. E `func_00263554` é da família
**FREE/coalesce do allocator de boundary-tags** — o próprio comentário do
`PS3_TRACE_POOLCNT` no `ppu_loader.cpp` lista `func_00263318/400/554/5A4` como a região de
FREE.

Traduzindo: **a free-list do allocator tem uma string lá dentro.** O `func_00263554` lê-a
como ponteiro e o `func_00220284` escreve uma estrutura inteira através dela. Isso é a
mesma família de corrupção que este projecto persegue desde os seis `FREELIST-TAG-GUARD` —
agora com leitor e escritor nomeados.

## A ordem de trabalho que isto define

1. `PS3_WATCH_STORE` no slot da free-list que recebe a string — quem lá põe o texto.
2. Só depois voltar ao `func_002545D4`; é provável que o objecto do tipo errado seja
   consequência desta mesma corrupção.

---

# Correcção: NÃO há texto dentro da free-list. O pool é que vale 5.

A conclusão anterior — *"a free-list do allocator tem uma string lá dentro"* — **estava
errada**, e a sonda do próprio pop desfez-a numa corrida:

```
[FLHEAD] TEXTO pool=0x00000005 pool+4=0x00000009 head=0x726D5B2E
```

**`pool = 5`.** O `r3` que chega a `func_00263554` não é um ponteiro — é o inteiro 5. A
função lê então `*(5+4) = *(9)` e traz o que estiver no endereço guest 9. O
`0x726D5B2E` não vem de lista nenhuma: é memória da base do espaço guest, tal como o
`tipo=0x97DF` de antes vinha do endereço 2. Por isso o valor varia ligeiramente entre
corridas (`0x726D432E`, `0x726D5B2E`, `0x726D55xx`) — muda o `pool`, muda o endereço lido.

A cadeia real é:

```
func_00220284  chama  func_00263554(r3 = 5)      // pool que não é pool
               -> pop devolve lixo como "bloco"
               -> escreve 16 words através dele  ([vm] UNCOMMITTED write32 ×16)
```

## O padrão do dia, agora visível três vezes

| sítio | o que devia ser ponteiro | o que lá está |
|---|---|---|
| `func_002545D4` | `rec` (payload do nó) | `0` → lê "tipo" do endereço 2 |
| `func_002545B0` #2 | `arg` (objecto com lista) | objecto de outro tipo (matriz) |
| `func_00263554` | `pool` | **`5`** |

Três funções a receber argumentos que não são o que assumem. **Não é corrupção de
memória** — a memória escrita está toda certa onde foi escrita. É **despacho**: métodos a
correr com `this`/argumentos errados.

Isso reenquadra o trabalho: parar de caçar quem estraga bytes (o `PS3_WATCH_STORE` já
mostrou, três vezes, que ninguém estraga) e passar a caçar **quem escolhe o alvo da
chamada** — que é exactamente onde o `PS3_TRACE_ICALL_TO` opera.

## A próxima medição

De onde vem o `5` que `func_00220284` passa a `func_00263554`. É um argumento local, logo
uma sonda na entrada de `func_00220284` (o mesmo padrão do `[E545B0]`, que foi o único
método fiável hoje) responde directamente.
