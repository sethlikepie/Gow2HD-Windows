# O que é o `f4` — enum de estado, e uma correcção ao verdicto do 2b2e04

**Data:** 2026-07-25 · **Método:** Ghidra headless sobre o EBOOT (13.143 funções,
13.096 com C decompilado) + disassembly cru do EBOOT. Nada corrido no boot.

## 1. `f4` é o campo em `+0x4`, e é um enum inteiro

Da própria probe (`recomp_mid_v2/patch_type15_cc9d0_disc.py:228`):

```c
f4  = vm_read32(th + 0x4u);      /* 32 bits */
f54 = (uint8_t)...;              /* byte em +0x54 */
```

Portanto **`f4` = offset `+0x4`**, não `0xF4`. `f54` = byte em `+0x54`.

No decompilado, `_opd_FUN_000cc9d0` recebe `float *param_1` (o objecto é quase
todo floats), por isso o Ghidra **imprime o enum como denormais**. A conversão é
directa — `1.4013e-45` é o padrão de bits `0x00000001`:

| decompilado | bits | estado |
|---|---:|---:|
| `1.4013e-45`  | 0x1 | 1 |
| `2.8026e-45`  | 0x2 | 2 |
| `7.00649e-45` | 0x5 | 5 |
| `8.40779e-45` | 0x6 | 6 |
| `9.80909e-45` | 0x7 | 7 |
| `1.12104e-44` | 0x8 | 8 |
| `1.26117e-44` | 0x9 | 9 |

Confirma o que a R12 já dizia em prosa (*"enum f4∈{1,5,6,…}"*), agora com os
valores exactos.

## 2. `CC9D0` é o tick da máquina de estados do `f4`

Transições, todas dentro do bloco gated em `_opd_FUN_000cc9d0` (linhas 121-148
do decompilado):

```
6 -> 1     (chama 2A459C)
5 -> 8
7 -> 2
8 -> 0     (chama 2A466C, copia bloco de 0x18, limpa param_1[0x16])
0 -> nada  <-- nenhum case casa
```

Mais o ramo de saída em `f4 ∉ {0, 9}` e `prod[0xCE] < 1`, que faz `f4 = 0`.

O bloco inteiro está gated por **`f54 == 0`** (`*(char*)(param_1+0x15)`, que é o
byte `+0x54`) e por dois flags derivados de produtos de floats serem 0. E no topo
da função, `f54 != 0` desvia para `FUN_000cc824` e salta tudo.

**Consequência: `f4 = 0` é estado terminal.** Com `f4=0` nenhum case casa, nada
escreve `f4`, e o tick não faz progresso. Não é "campo por escrever" — é uma
máquina de estados parada no estado inicial porque **ninguém a arrancou**.

## 3. Correcção: `CB56C` NÃO toca em `+0x4`

`notes/2026-07-23-2b2e04-typewrites-diagnostic.md:131` afirma, como *"provado, não
só inferido"*, que `func_000CB56C` zera incondicionalmente `this+0x4` **e**
`this+0x54` no prólogo. Metade disso não se confirma.

Disassembly do EBOOT em `0x000CB56C` (r3 = objecto, r31 = cópia de r3):

```
0x000CB5A4  stw   r0, 0x8(r3)      <- +0x8
0x000CB5A8  stw   r0, 0x0(r3)      <- +0x0        (salta o +0x4)
0x000CB5AC..0x000CB5C0  loop x4: stw a +0x0/+0x4/+0x8 de r9, r9 = r3+12,+24,+36,+48
                                 -> cobre +0x0C..+0x38
0x000CB604  stb   r11, 0x54(r31)   <- +0x54 = 0   (f54: CONFIRMA)
```

O decompilado concorda: no corpo inteiro (51 linhas) há `*param_1 = 0` e
`param_1[2] = 0`, e **zero** ocorrências de `param_1[1]`.

Logo:
- **f54**: sim, `CB56C` zera-o de cada vez que corre. O verdicto vale.
- **f4**: não. `CB56C` salta deliberadamente o `+0x4`. O mecanismo
  "escrito uma vez certo, depois apagado por um reset incondicional"
  **não explica o `f4=0`**.

Isto mata a hipótese A do 2b2e04 (*"CB56C deveria preservar +0x4 no caminho de
reuse"*) enquanto explicação do `f4` — não há nada a preservar, ele não é tocado.

## 4. Quem poderia arrancar a máquina

O construtor `_opd_FUN_002a64f4` (chamado por `2A6608`) põe `param_1[1] = 0`, ou
seja `f4` **nasce 0**.

Varrendo os 13.096 corpos decompilados por escritas de `{1,2,5,6,7,8,9}` a um
offset `+4`:

- `CC9D0` escreve 1, 2 e 8 — mas só como transições a partir de 5/6/7.
- **Ninguém escreve 5, 6 ou 7.**
- O único candidato a instalar um estado de arranque é
  **`_opd_FUN_000cd498`** (`+4 = 9`), chamado por **`_opd_FUN_000cd7b4`**.

## 5. Pergunta seguinte, agora com endereço

Não é mais *"quem escreve o f4"* em abstracto. É:

> **`CD7B4 → CD498` chega a correr no boot? Se não, o que o devia ter chamado?**

`f4 = 9` é também o valor que o ramo de reset em `CC9D0` trata como excepção
(`f4 != 9` é condição para limpar), o que é coerente com 9 ser "trabalho
instalado, em curso".

## Ressalvas

- A varredura cobre as formas que o Ghidra emitiu (`[1] =`, `+ 4) =`,
  `+ 0x4) =`). Uma escrita por `memcpy`/cópia de struct não aparece.
- `0x00266130` também escreve 7 e 9 a um `+4`, mas não está provado que seja o
  mesmo tipo de objecto.
- Nada disto foi observado em execução — é análise estática do binário original.
  O passo in-boot é instrumentar `CD7B4`/`CD498`.

---

# Adenda in-boot (2026-07-25): medido, com controlo positivo

Probe `recomp_mid_v2/patch_cd498_enter_probe.py` (gated `PS3_TRACE_CD498=1`,
OFF por omissão). Recipe menu-fast headless, `TIMEOUT=90`, corrida que chegou ao
pós-load: `rperma_full=1`, `thr_auto_load() end`, `bytes_read=20169344 == size`.

```
[CD498] SUMMARY fn=CD7B4       tot=0
[CD498] SUMMARY fn=CD498       tot=0
[CD498] SUMMARY fn=CD9DC       tot=0
[CD498] SUMMARY fn=1D7FCC      tot=0
[CD498] SUMMARY fn=1A9D24      tot=0
[CD498] SUMMARY fn=CC9D0(ctrl) tot=7840456  post=7840456  pre=0
```

**Controlo positivo**: `CC9D0` correu **7.840.456 vezes**, todas depois do
R_PermA, nenhuma antes — e com `r3` a alternar entre `0x4066D798` e
`0x4066D804`, exactamente os dois `this` que as notas de 22-07 documentam. A
instrumentação dispara, conta e lê o registo certo; portanto os cinco zeros
significam mesmo *não são alcançados*.

## Os cinco sítios que sabem instalar um estado de arranque

Varrimento de instruções sobre as 957k palavras do `.text` (`li rX,{5,6,7,9}`
seguido de `stw rX, 0x4(rY)` — independente do que o decompilador rendeu):

| endereço | escreve | função contentora |
|---|---:|---|
| `0x001D8408` | 6 | `func_001D7FCC` |
| `0x001D8414` | 9 | `func_001D7FCC` |
| `0x001D8438` | 7 | `func_001D7FCC` |
| `0x001D845C` | 5 | `func_001D7FCC` |
| `0x001AA124` | 5 | `func_001A9D24` |
| `0x000CD62C` | 9 | `func_000CD498` |

**Nenhum é atingido.**

## Estado do wall, agora preciso

Não é "campo por escrever" nem "campo apagado por um reset" (esse mecanismo foi
refutado para o `+0x4` — ver secção 3). É:

> uma máquina de estados a girar **7,8 milhões de vezes** sobre um estado que
> **ninguém instala**.

O tick é armado pelo load (`pre=0`, `post=7840456`); o *trabalho* é que nunca é
instalado.

## Pergunta seguinte

1. O que deveria chamar `func_001D7FCC`? (é quem detém os quatro estados)
2. Porque está a cadeia `CD9DC → CD7B4 → CD498` inerte — sem chamador estático
   **e** sem uma única referência ao seu OPD (`0x51E028/30/38`) em toda a
   imagem carregada? Vtable construída em runtime, ou código morto neste build?

## Ressalva

Binário compilado com WIP não-committado de outra sessão em `movie_eos_arm.c`
(fonte do `st620`). Não invalida um resultado binário sobre funções guest, mas
é a primeira variável a eliminar se algo aqui for contestado.

---

# Adenda 2: as classes do instalador nunca são construídas

18 sítios, mesmo binário, mesma corrida (pós-load: `rperma_full=1`, R_PermA
completo).

```
CD7B4 CD498 CD9DC 1D7FCC 1A9D24 ............................ tot=0
C96014 C96FF0 C9DCF4 C9FBE0 CA3AB0 CA3B68 ................... tot=0
CAD870 CADA28 CADBD4 CCAE1C CCAE90 CDAFB4 ................... tot=0
CC9D0 (controlo) ............................ tot=7737370  post=7737370  pre=0
```

## Como se chegou aos 12 construtores

`1D7FCC` e `1A9D24` não têm chamador estático — mas, ao contrário do `CD9DC`,
**têm o OPD referenciado**: em `0x513F48` e `0x513528`. A zona repete o padrão
`00 FFFFFFFFF 000 FFFFFFFFF 00` — vtables Itanium com RTTI removido
(offset-to-top e typeinfo a zero) e 9 métodos virtuais cada. Nas duas, o nosso é
o **slot 2** (`+8`): são o mesmo método virtual de duas classes irmãs.

Procurar quem despacha o slot `+8` dá 538 sítios — inútil. Inverteu-se a
pergunta: cada vptr (`0x513F40`, `0x513520`) é referenciado **uma vez**, em
entradas de TOC (`TOC-13600` e `TOC-13380`, `TOC=0x541178` lido do OPD). Doze
sítios do `.text` carregam essas entradas, todos à entrada da função — padrão de
construtor. **Nenhum dos doze corre.**

## O lead era válido: o layout confere

`0x001D83B4  lwz r3, 0x8(r25)` e `0x001D840C  stw r0, 0x4(r25)`.

Isso é exactamente a forma do objecto TYPE15 que a probe do `CC9D0` lê:
`f4 = vm_read32(th+0x4)`, `prod = vm_read32(th+0x8)`. Somado a escrever
precisamente os estados de arranque `{5,6,7,9}` que o tick consome, o caso é
forte — ainda que não seja prova de identidade de tipo.

## Conclusão

O enquadramento muda outra vez, e para mais fundo:

> Não é "um campo não é escrito". É **um subsistema inteiro que nunca é
> instanciado**. As duas classes que detêm o método instalador de estado nunca
> têm um construtor executado neste boot, enquanto o tick que consumiria esse
> estado gira 7,7 milhões de vezes.

## O que isto exclui, e o que reabre

Exclui: qualquer explicação do `f4=0` baseada em "quem escreve foi apagado" ou
"o escritor corre mas falha" — os seis únicos sítios do binário capazes de
escrever um estado de arranque nunca são atingidos, e as classes que os detêm
nunca existem.

Reabre a hipótese que a nota de 22-07 já tinha levantado e que ficou por
testar (`2026-07-22-type15-cb56c-attach-diagnostic.md:105`):

> *"`f4=0` may be the correct idle value for an object waiting on that"*

Se o objecto vivo em `0x4066D798` é de uma classe **diferente** daquelas duas, o
`f4=0` pode ser legítimo e o wall não está aqui — está em quem devia ter criado
os objectos das classes instaladoras.

## Próximo discriminador proposto

Ler o vptr do objecto vivo (`vm_read32(0x4066D798)`) durante o boot e compará-lo
com `0x513F40` / `0x513520`. Uma leitura, resposta binária:

- **igual** → é a mesma classe, e a pergunta é porque não passou pelo construtor
  instrumentado (construção por outro caminho? placement? cópia?);
- **diferente** → o `f4` daquele objecto não é o mesmo campo semântico, o lead
  inteiro cai, e o wall está noutro sítio.

---

# Adenda 3: o discriminador refuta o meu próprio lead

```
[CD498] VPTR obj=0x4066D798 vptr=0x00000000  DIFERENTE das duas
[CD498] VPTR obj=0x4066D804 vptr=0x00000000  DIFERENTE das duas   (x3 cada)
[CD498] SUMMARY fn=CC9D0(ctrl) tot=7620252 post=7620252 pre=0
```

`vptr = 0` não é "outra classe". É **nenhuma vtable** — e a premissa da comparação
estava errada. O `+0x0` deste objecto não é um ponteiro:

```c
_opd_FUN_000cc9d0:  if (*param_1 == 1.4013e-45)      /* +0x0 == 1 */
                    if (*param_1 != 2.8026e-45)      /* +0x0 == 2 */
_opd_FUN_000cb56c:  *param_1 = 0;                    /* zera +0x0 */
```

É **outro enum pequeno** ∈ {0,1,2}, adjacente ao `f4`. O objecto vivo é uma
struct simples, não um objecto polimórfico.

## Consequência: o lead `1D7FCC` está morto

`1D7FCC`/`1A9D24` só são alcançáveis pelo slot 2 de vtables de classes
polimórficas. O objecto que o `CC9D0` tica não tem vtable, logo **não pode ser
instância dessas classes**. A coincidência de layout que me convenceu
(`stw ...,0x4(r25)` + `lwz ...,0x8(r25)`) era mesmo coincidência: dois enums
adjacentes seguidos de um ponteiro é um shape comum.

Confirmação independente: os 12 construtores dessas classes dão `tot=0` — nunca
são instanciadas de todo. Coerente, e agora explicado.

## O que sobra provado, depois de três rondas

1. `f4` = campo em `+0x4`, enum ∈ {0,1,2,5,6,7,8,9}; `f54` = byte em `+0x54`.
2. `CC9D0` é o tick; `f4=0` é estado terminal; gira ~7,6-7,8M vezes pós-load,
   zero antes.
3. `CB56C` zera `+0x54` mas **não** toca `+0x4` (corrige o verdicto do 2b2e04).
4. Nenhum dos 6 sítios que escrevem um literal `{5,6,7,9}` a um `+0x4` é
   alcançado — e nenhum pertence a este tipo de objecto.
5. O objecto vivo não é polimórfico (`+0x0` é enum, não vptr).

## Porque é que o varrimento não encontrou o escritor certo

O varrimento procurou `li rX,{5,6,7,9}` seguido de `stw rX, 0x4(rY)`. Não
apanha: valor vindo de outro registo/campo, offset calculado, `stmw`, `memcpy`
de um template, ou escrita pelo host (HLE/patch). O escritor legítimo do `f4`
deste objecto pode estar em qualquer dessas formas — ou não existir, e `f4=0`
ser o idle correcto, como a nota de 22-07 suspeitava.

## Próximo experimento: watchpoint de escrita, não mais busca estática

Parar de procurar o escritor no binário e **apanhá-lo em flagrante**. O runtime
já tem o gancho: `ps3_type15_block_stomp()` é chamado de dentro do `vm_write32`
(`runtime/ppu/ppu_loader.cpp:1114-1116`). Um watchpoint gated no mesmo sítio,
a vigiar `0x4066D79C` (= `this+0x4`) e `0x4066D808`, responde de vez:

- **alguém escreve** → temos o culpado, com o valor e o momento;
- **ninguém escreve em 7,6M ticks** → o `f4` nunca é tocado depois da
  construção, e a pergunta passa a ser se isso é o comportamento correcto
  (hipótese de 22-07) ou se falta um subsistema inteiro a montante.

Nota: os endereços `0x4066D798`/`0x4066D804` repetiram-se em todas as corridas
desta sessão e nas notas de 22-07 e 23-07, portanto são estáveis o suficiente
para servirem de alvo; mesmo assim o watchpoint deve aceitar o endereço por env
var em vez de o fixar.

---

# Adenda 4: o watchpoint apanha o escritor — e corrige a minha adenda 3

`PS3_WATCH_W32=0x4066D79C,0x4066D808`, corrida com 8.207.786 ticks de `CC9D0`.
**Exactamente 3 escritas** (cap de 200 nunca atingido):

```
[0x4066D79C]=0x40004020  ra0=func_00263178+0x9E0  ra1=func_002A6608+0x278
[0x4066D79C]=0x00000000  ra0=func_000CB56C+0x674  ra1=func_000CBB2C+0x45C
[0x4066D808]=0x00000000  ra0=func_000CB56C+0x674  ra1=func_000CBB2C+0x45C
```

## Correcção: a secção 3 desta nota estava ERRADA

Eu tinha escrito que `CB56C` não toca no `+0x4`, com base em o disassembly só
mostrar `stw r0,0x0(r3)` e `stw r0,0x8(r3)`. **Está errado.** O `CB56C` escreve
através de um ponteiro DERIVADO (`r9`, que o loop faz avançar a partir de
`r3+12`), não de `0x4(r3)`. Procurei a forma literal e concluí de menos.

O watchpoint prova o que a análise estática me escondeu: a escrita de `CB56C`
**aterra em `this+0x4`**.

**Portanto o verdicto de `2026-07-23-2b2e04-typewrites-diagnostic.md:131` está
CONFIRMADO**, e por medição directa, não por inferência: `2A6608` (pelo
alocador, `263040`→`263178`) escreve `0x40004020` no `+0x4` durante a
construção; `CB56C`, invocado por `CBB2C`, apaga-o; e nada o volta a escrever.

## O que é novo: discrimina entre as duas hipóteses do 2b2e04

O 2b2e04 deixou duas hipóteses em aberto, deliberadamente por decidir:

- **A** — `CB56C` deveria preservar `+0x4` no caminho de reuse
- **B** — `CBB2C`/`CB56C` estão a ser invocados repetidamente (todos os ticks)
  quando deviam correr uma vez

**A medição mata a B.** Em 8,2 milhões de ticks houve **uma única** escrita de
zero por objecto. O `CB56C` não está a correr repetidamente — zerou uma vez e
nunca mais. A frequência de invocação não é o bug.

Sobra a **A**, ou uma terceira que a medição sugere: o reset é legítimo, e o que
falta é quem devia **re-preencher** o `+0x4` a seguir — que nunca corre.

## Ressalva por resolver

O valor de construção é `0x40004020` — um **ponteiro** guest, não um enum de
`{1,2,5,6,7,8,9}`. Isso não casa com o `CC9D0` a comparar `param_1[1]` contra
denormais pequenos. Ou o `th` da probe do `CC9D0` não é a mesma base que o
`param_1` do decompilado, ou o campo é reinterpretado. Fica por resolver e é o
próximo fio: **não construir nenhum fix sobre a leitura "enum" enquanto isto não
fechar.**

## Ferramenta

`PS3_WATCH_W32` passou a aceitar até 8 moradas separadas por vírgula e a
simbolizar `ra0`/`ra1` por `dladdr` — que em código liftado devolve o
`func_<EA guest>` envolvente. Sem isso os `ra` eram ponteiros host slidados por
ASLR, e simbolizá-los exigia `vmmap` no pid vivo (ver ledger, 2026-07-23).

---

# Adenda 5: a máquina lida em assembly — e `f4=0` é um caminho normal

Reli o `CC9D0` ao nível da instrução, depois de descobrir que o `ppu_disasm`
imprimia FPRs como `rN` (corrigido em `ps3recomp` `25ecd24`) e que o Ghidra
tipava o objecto como `float*`, transformando os enums em denormais. **As duas
representações que usei nas adendas anteriores eram enganosas.**

## O que a máquina faz, de facto

```
CC9D0(obj):                       ; obj em r31
  0xCC9FC  f54 = obj[+0x54]       ; byte
  0xCCA0C  se f54 != 0 -> 0xCCBF0 ; outro caminho
  0xCCA10  f4  = obj[+0x4]
  0xCCA14  se f4 == 1 -> 0xCCCBC
  0xCCA1C  se f4 == 0 -> 0xCCA40  <-- o caso observado
  0xCCA24  se f4 == 9 -> 0xCCA40
           senao: se prod[+0xCE] <= 0 -> 0xCCC4C (escreve f4 = 0)

0xCCA40:   s0 = obj[+0x0]
  0xCCA44  se s0 == 1 -> 0xCCB9C
  0xCCA4C  se s0 == 2 -> 0xCCC0C
           senao (s0 == 0) -> 0xCCA54

0xCCA54:   li r9,4 ; addi r11,r29,12 ...   <-- loop de 4 canais a partir de +12
```

Escritas de `f4` no corpo: `0` (0xCCC5C), `8` (0xCCC04), `1` (0xCCCB4).

## Correcção: `f4=0` NÃO é estado terminal

Escrevi na adenda 1 que "com `f4=0` nenhum case casa". Errado. O `f4=0` tem um
`beq` próprio (0xCCA20) para `0xCCA40`, que despacha no **`+0x0`** — a variável
de estado primária. Com `+0x0 = 0` segue para `0xCCA54`, o loop de interpolação
de 4 canais (decaimento por frame), e depois chama `2A3AA4` duas vezes.

Ou seja: **o tick está a executar um caminho de idle bem definido, não a bater
numa parede.** O `f4` é sub-estado; o estado primário é o `+0x0`.

## Consequência para o wall

Isto sustenta a hipótese que a nota de 22-07 levantou e que ficou por testar:

> *"`f4=0` may be the correct idle value for an object waiting on that"*

Um objecto interpolador de 4 canais, com estado primário 0 e sub-estado 0, a
decair por frame e a reportar duas flags — é exactamente o que um componente
**ocioso** faz. Não há evidência de que esteja avariado.

**Reenquadramento honesto:** as rondas R12 e as minhas quatro rondas
perseguiram um sintoma que é provavelmente comportamento correcto. A pergunta
não é *"porque é que o `f4` não avança"*, é *"porque é que ninguém dá trabalho
a este componente"* — e essa pergunta é sobre quem devia escrever o `+0x0`
(estado primário) ou agendar trabalho a montante, não sobre o `+0x4`.

## O que fica provado e reutilizável

- `f4` = `+0x4`, sub-estado inteiro; `f54` = `+0x54`, byte; `+0x0` = estado primário
- construção escreve `+0x4 = 0x40004020`; `CB56C`←`CBB2C` zera-o, **uma vez por
  objecto** em 8,2M ticks (mata a hipótese "invocado a cada tick")
- `CC9D0` corre ~7,6-8,2M vezes, sempre pós-R_Perm, nunca antes
- nenhum dos 6 escritores literais de `{5,6,7,9}` a um `+0x4` é alcançado, e as
  classes que os detêm nunca são construídas

## Lição de método

Duas conclusões erradas nesta sessão vieram de confiar em representações
derivadas: o decompilador (que tipou o objecto como `float*`) e o nosso próprio
disassembler (que imprimia FPR como GPR). **O assembly é o árbitro.** O Ghidra
serve para orientar e para responder depressa; a decisão final lê-se na
instrução.

---

# Adenda 6: o `+0x0` tem os MESMOS dois escritores — e nenhum instala trabalho

`PS3_WATCH_W32=0x4066D798,0x4066D804`, 8.428.284 ticks. **Duas escritas:**

```
[0x4066D798]=0x4063686C  ra0=func_00263178+0x9C4  ra1=func_002A6608+0x278
[0x4066D798]=0x00000000  ra0=func_000CB56C+0x36C  ra1=func_000CBB2C+0x45C
[0x4066D804]=0x00000000  ra0=func_000CB56C+0x36C  ra1=func_000CBB2C+0x45C
```

Exactamente o mesmo par que escreve o `+0x4`: construção pelo alocador
(`263178`←`2A6608`) e reset (`CB56C`←`CBB2C`). **Nunca 1, nunca 2** — os dois
valores que o tick sabe despachar.

Corroborado estaticamente: das 9 funções que comprovadamente tocam neste
objecto, só `2A64F4` (ctor), `CB56C` e `CBB2C` escrevem o `+0x0`; o `CC9D0`
nunca o escreve. Nenhuma instala estado de trabalho.

## Os dois campos "de estado" nascem com PONTEIROS

`+0x0 = 0x4063686C` e `+0x4 = 0x40004020` na construção — ambos endereços de
heap guest, não enums. Depois o reset zera os dois. A leitura "máquina de
estados com enum" é o que o tick FAZ com os campos depois de zerados; o que o
construtor lá põe são referências.

## `CBB2C` é o teardown, e falta-lhe o contraparte

```
0x000CBB64  bl    0xCB56C            ; reset deste componente
0x000CBB68  cmpw  cr7, r30, r29      ; ... em loop sobre uma lista
0x000CBB6C  bne   cr7, 0xCBB5C
0x000CBB88  stw   r28, 0x0(r9)       ; limpa um global (TOC-0x5EB0)
0x000CBB8C  stw   r29, 0xD8(r31)     ; e campos do gestor (+0xD8..+0xE8)
0x000CBB94  stfs  f31, 0xE4(r31)
```

`CBB2C` percorre uma lista, reseta cada componente e depois limpa campos do
objecto GESTOR (o `r31` dele vai até `+0xE8`, logo é maior que os `0xD0` do
componente). É um **reset-all**.

## Resposta à pergunta

**Ninguém escreve o `+0x0` para além da construção e do reset.** Não há, neste
build, nenhum caminho que ponha o componente em estado de trabalho depois do
`CBB2C` correr.

O que falta não é um escritor do `+0x0` — é o **contraparte de setup do
`CBB2C`**: o que quer que normalmente re-popule os componentes depois de um
reset-all. O `CBB2C` desliga tudo e nada volta a ligar.

## Próximo fio

Quem chama o `CBB2C`, e o que é suposto correr a seguir nesse mesmo caminho.
O `stw r28, 0x0(r9)` para o global em `TOC-0x5EB0` é um bom alvo: se esse
global for o "há trabalho agendado", vigiá-lo com o `PS3_WATCH_W32` diz se
alguém alguma vez o volta a pôr.

---

# Adenda 7: o `CBB2C` é INIT, não teardown — e isso mata as duas hipóteses do 2b2e04

Cadeia de chamadores, cada elo com **exactamente um** chamador estático:

```
main → ppu_run → 10230 → 10354 → 25C838 → 2B2E74 → B71B8 → CBC20 → CBB2C
```

Corroborado por duas notas anteriores, escritas por outra via:

- `notes/2026-07-20-postmerge-rebaseline.md:31`
  `main → ppu_run → func_00010230 → func_00010354 → func_0025C838`
- `notes/2026-07-21-smpd-consumer.md:164-171`
  `func_002B2E74` ← `func_0025C838`, *"~11 chamadas de init em linha, sem
  condição"*, *"~9 chamadas de init em linha, sem condição"*, *"os dois
  dispatchers intermédios não têm NENHUM `if` — são listas planas de
  InitSubsystem()"*

## Consequência

O `CBB2C` **não é um teardown que dispara indevidamente**. Corre uma vez, sem
condição nenhuma, a partir do entry point, como parte da sequência de
inicialização do jogo. Zerar os componentes ali é *inicializá-los*.

Isto fecha as duas hipóteses que o `2b2e04` deixou em aberto:

| hipótese | estado |
|---|---|
| **B** — `CBB2C`/`CB56C` invocados a cada tick | **MORTA** — 1 escrita por objecto em 8,4M ticks (adenda 4) |
| **A** — `CB56C` deveria preservar `+0x4` no caminho de *reuse* | **MORTA** — não há reuse. É init, corre uma vez, incondicional |

## Onde isto deixa o wall

O componente é **correctamente inicializado a ocioso** no arranque. Os campos
`+0x0` e `+0x4` são referências que o construtor põe e que o init limpa — e
depois o tick roda 8,4M vezes a fazer o seu trabalho de idle, que é o correcto
para um componente sem nada agendado.

Não há bug neste objecto, neste tick, nem neste reset. O que falta está a
jusante: **quem devia dar-lhe trabalho depois do load**. É o mesmo sítio onde o
Gate A falha (`SetFlip_after_R_Perm=0`), e não uma segunda avaria independente.

## Recomendação

Encerrar a linha `f4`/`TYPE15 state` como causa. Sete adendas, quatro leads
mortos com medição, e o veredicto é que o subsistema está são. O esforço deve
voltar ao Gate A propriamente dito: o re-arm do present pós-load.

E antes disso, ao **RDY-0**: o lift em produção é de 20 de Julho e não tem as
correcções do lifter desta semana. Enquanto o re-lift não fechar, qualquer
diagnóstico corre sobre um artefacto desactualizado — incluindo este.
