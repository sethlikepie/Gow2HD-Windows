# Sessão de RE — semântica do `w0` e a indexação da tabela de tipos — 2026-08-02

Sessão conduzida com [`docs/RE_PLAYBOOK_GOW2.md`](../RE_PLAYBOOK_GOW2.md). Segue-se
directamente a [`2026-08-02-wall-4-walk-de-tipos.md`](2026-08-02-wall-4-walk-de-tipos.md)
e **revoga parte das medições dela** (ver § *O que fica REFUTADO*).

**Veredicto em uma linha:** **nenhum dos dois lados está errado** — o produtor indexa pelo
`subtag` do **objecto que se regista** e os consumidores pelo `low16` do **nó que
despacham**; são o *mesmo* espaço de índice alcançado a partir de dois papéis diferentes,
e a divergência anterior era uma comparação de dois campos **do mesmo `w0`**, que é
categorialmente inválida. Fica **aberta** uma assimetria diferente e real (push vs pop),
medida ao byte.

**Zero builds, zero boots, zero escritas fora deste ficheiro.**

---

## Pergunta (uma frase)

> **Qual dos dois lados está errado — o produtor `0x0041FA90`, que indexa a tabela de tipos
> por `(w0>>16)&0xFFF`, ou os consumidores, que indexam por `w0 & 0xFFFF`?**

---

## Âncoras

| âncora | valor |
|---|---|
| **tabela de tipos** | símbolo global em **`0x00868D48`** (lido do ELF), alcançado por **88** entradas de TOC, uma delas `0x0053EF1C` (`r2 - 0x225C`, `r2 = 0x00541178`) |
| **EA guest** | `0x0041F700` (push), `0x0041F924` (pop), `0x0041FA90` + `0x0041ECD8` (registo), `0x00421C74` (des-registo), `0x002545B0` (parede 1), `0x00254954` (round-trip), `0x0025271C` (ctor de gestor), `0x00252ED8` (clone do `w0`), `0x002B0E78` (prova de campos independentes) |
| **símbolo Ghidra** | `.opd.FUN_0041f700 size=296`, `.opd.FUN_0041f924`, `.opd.FUN_0041fa90 size=560` |
| **vtable do gestor** | **`0x00515AA0`** (TOC `0x0053EF40`); `+0x08` = registo, `+0x40` = push, `+0x44` = pop, `+0x58` = criar objecto |
| **ELF** | `../gow2-recomp/EBOOT.ELF`, sha256 `23cfd435…b1163b` — igual ao `MANIFEST.json` do `ghidra_out` |
| **tag de log** | nenhuma (continua sem instrumentação própria) |

---

## Sondas de CONTROLO (corridas ANTES de qualquer achado)

O playbook exige-o e nesta sessão **mudou três conclusões**.

| # | sonda | resultado |
|---|---|---|
| C1 | **positiva** — `--grep 'PTR_DAT_0053ef1c' --max 100` | 18 funções (abaixo do limite ⇒ lista completa *para este método*) |
| C2 | **negativa** — `--grep 'PTR_DAT_0053ef1d'` e `--grep 'ZZZ_NAO_EXISTE_XYZ'` | `nenhum resultado`, rc=0 nos dois ⇒ o grep não inventa |
| C3 | **cruzada (binário vs Ghidra)** — varrer o ELF por `lwz rX,-8796(r2)` | **23** sítios / 23 funções, **5 que o grep de C não viu** |
| C4 | **cruzada (TOC)** — quantas entradas de TOC valem `0x00868D48`? | **88** ⇒ a C3 cobria **1/88** dos aliases |
| C5 | **varrimento completo** — todos os 88 deslocamentos | **420 sítios em 369 funções conhecidas** |
| C6 | **cruzada com in-boot** — valor estático de `PTR_DAT_0053ef1c` | `0x00868D48` = **exactamente** o `tab=0x00868D48` medido in-boot e registado no ROADMAP ✅ |

**C1 vs C5 é a lição da sessão.** "18 consumidores" não era o número de consumidores: era o
número de funções em que o **decompilador do Ghidra escolheu emitir aquele nome de símbolo**.
O número real de funções que carregam a tabela é **369**. Qualquer afirmação da forma
*"só N funções tocam nesta tabela"* feita por grep de C está errada por mais de uma ordem
de grandeza.

C6 é a sonda que valida o oráculo: um número estático do ELF a bater com um número medido
num boot é a melhor prova de que os dois lados falam do mesmo objecto.

---

## Ferramentas usadas (comando + rc + o que devolveu)

Oráculo: `../gow2-recomp/ghidra_out` (13 143 funções, 13 096 com C). **Não regenerado.**
Ferramentas próprias desta sessão, todas em `/tmp/re_w0/` (descartáveis, nada commitado).

| # | ferramenta | rc | devolveu |
|---|---|---|---|
| T1 | `ghidra_lookup.py … --grep` (C1/C2) | 0 | ver sondas |
| T2 | script próprio de grep sobre `decompiled.json` **sem `break` por função** | 0 | o `--grep` do `ghidra_lookup.py` só imprime **a 1.ª ocorrência por função**; contagens por ocorrência exigem outro caminho |
| T3 | parser de ELF64 PPC BE (program headers, `vaddr→offset`) | 0 | PH0 `0x10000..0x50C7E0` (texto+rodata), PH1 `0x510000+` (dados) |
| T4 | **descodificador PPC próprio** (3.º descodificador independente) | 0 | `rlwinm`/`rldicl` bit a bit em `0x0041F700`, `0x0041F924`, `0x0041FA90` |
| T5 | varrimento de OPD + vtables no ELF | 0 | OPD de 8 bytes (`code`,`toc=0x00541178`); vtable `0x00515AA0` |
| T6 | varrimento de `lwz rX,d(r2)` para os 88 deslocamentos | 0 | 420 sítios / 369 funções |
| T7 | análise de **liveness do registo-base** para separar escritas reais | 0 | **16** funções escrevem na tabela (contra 72 de uma heurística ingénua que rejeitei) |

**Achado de tooling (T2), a acrescentar ao playbook §3.1:** o `--grep` do
`ghidra_lookup.py` faz `break` após o primeiro `match` **de cada função**. Serve para
*localizar*, nunca para *contar*. A tabela de tally da sessão anterior foi construída com
ele — e é onde estão dois dos erros corrigidos abaixo.

---

## O que o `w0` É — provado no binário original

### Layout de bits

```
 31    28 27          16 15                    0
+--------+--------------+-----------------------+
| flags4 |  subtag :12  |       low16 :16       |
+--------+--------------+-----------------------+
```

Três provas independentes, todas do binário:

**P1 — o carimbo universal do construtor-base.** 98 funções (102 sítios) contêm
exactamente:

```c
param_1[1] = *param_2 & 0xfffffff | 0x40000000;   /* w0 = (descritor[0] & 0x0FFFFFFF) | flag 0x4 */
```

(`0x0022D2B8`, `0x0022E6A4`, `0x0023131C`, `0x0025271C`, `0x002534E8`, … `0x00424BE0`).
O `w0` de **todo** objecto desta hierarquia é copiado de um **descritor de tipo estático**:
os bits 0..27 (subtag **e** low16) vêm juntos do descritor, e o nibble alto é flags.
`0x40030001` tem exactamente esta assinatura (`| 0x40000000`).

**P2 — os dois campos são preservados independentemente.** `0x00252ED8` (e as cópias
`0x0039DB68`, `0x0041C3D8`, `0x0041EF28`) clonam um objecto:

```c
*puVar4 = (uVar1 >> 0x10 & 0xfff | 0x8000) << 0x10 | uVar1 & 0xffff;
```

Reconstrói o `w0` **preservando o subtag e o low16 separadamente** e forçando o bit de flag
`0x8000` no meio-alto. Se fossem o mesmo campo, este código não existiria nesta forma.

**P3 — os dois campos DIFEREM num objecto real.** `0x002B0E78:65`:

```c
if (((*puVar7 & 0xffff) == 3) && ((*puVar7 >> 0x10 & 0xfff) == 1)) {
```

Um teste único, sobre a **mesma palavra**, que exige `low16 == 3` **e** `subtag == 1`.
⇒ **O invariante `low16 == subtag` é falso por desenho.** *(offline-unit)*

### O que cada campo significa

| campo | papel | evidência |
|---|---|---|
| **`low16`** | **índice do tipo na tabela** | escrito pela família de one-liners `*(short *)(param_2 + 6) = (short)*(undefined4 *)(param_1 + 0x20);` (`0x004117B0`, `0x0041C558`, `0x0041E8A4`, `0x0042C644`, `0x0042EC60`, `0x00254250`, `0x0024E198`); **verificado contra `< 0x21`** em `0x00254250` — 0x21 = 33 = o alcance `tags 0..32` já medido no ROADMAP; comparado com constantes de tipo (`== 4`, `== 0x14`, `== 3`) |
| **`subtag`** | **categoria / raiz de hierarquia** para os nós; **slot que gere** para um registante | comparado com constantes pequenas em ~80 sítios (`== 0`, `== 1`, `== 3`, `== 7`, `< 0xb`, `== 0xb`, `== 0xc`, `== 0xf`); usado como **terminador de subida na hierarquia** — `while ((*p >> 0x10 & 0xfff) != 1) p = p[0x1b];` (`0x0024C878`, `0x0024C974`, `0x0024CADC`, `0x0024D570`, `0x00254250`, `0x002A6C88`) |

Convenções de base confirmadas (e que explicam `+2` vs `+6` — não são campos diferentes):

```
h+0x00 = vptr          q = h + 4  (o ponteiro "canónico" que circula)
h+0x04 = w0            q+0 = w0 ; q+2 = low16(w0)  ==  h+6
h+0x08 = flags16 | slot16     q+4 = flags ; q+6 = slot no array do gestor
h+0x30 = ponteiro para o gestor (escrito por 0x0041FA90)
h+0x80 = lista intrusiva de FILHOS   ==  q + 0x7C   <-- o "+0x7C" da parede 1
```

---

## A resposta: **nenhum dos dois lados está errado**

### O produtor indexa pelo `subtag` **de quem se regista** — não de um nó

`0x0041FA90` recebe `param_1` = **o gestor** (`*param_1` é um vptr; usa `param_1[9]`,
`[0xd]`, `[0xe]`, `[0x11]`, `[0x35]`, `+0x48`, `+0xC8` — a mesma classe do push/pop) e faz:

```
0x0041FAB8  540094BA  rlwinm r0,r0,18,18,29   ; ((w0 >> 16) & 0xFFF) * 4
0x0041FAC0  7FCB012E  stwx   r30,r11,r0       ; tab[subtag(gestor.w0)] = gestor
```

`subtag(gestor.w0)` = **"o tipo que EU giro"**. Não é o tipo do gestor.

### Os consumidores indexam pelo `low16` **do nó** — o tipo desse nó

`tab[low16(no.w0)]` = **"o gestor do MEU tipo"**.

### A prova de que são o mesmo número: o *round-trip* de `0x00254954`

`0x0041FA90`, ao registar-se, guarda o objecto no array do gestor e escreve o índice do
slot **dentro do objecto**:

```c
piVar4 = (int *)(param_1[0x11] * 0xc + param_1[9]);   /* array do gestor */
*(int *)(iVar5 + *piVar4) = iVar3;                    /* array[slot] = obj+4  */
*(short *)(iVar2 + 10) = (short)param_1[0x35];        /* obj+0x0A = slot      */
*(int **)(iVar1 + 0x30) = param_1;                    /* obj+0x30 = gestor    */
```

E `0x00254954` recupera o objecto **pelo caminho inverso**, partindo do `low16`:

```c
*(int *)( (uint)*(ushort *)(iVar4 + 6) * 4                     /* obj.slot            */
        + *(int *)( *(int *)(puVar2 + (uint)*(ushort *)(iVar4 + 2) * 4) + 0x44) * 0xc
                  + *(int *)(puVar2 + (uint)*(ushort *)(iVar4 + 2) * 4) + 0x24 ) )
                        /*        ^^^ tab[ low16(obj) ]  ^^^                     */
```

A expressão do array — `gestor[0x11] * 0xc + gestor[9]` — é **byte-idêntica** à do
registador. O *round-trip* `obj → tab[low16(obj)] → array do gestor → obj` **só fecha se
`low16(obj) == subtag(desse gestor)`**. *(offline-unit)*

### E o jogo compara os dois campos directamente

Três instanciações irmãs do mesmo *template* — `0x00256B1C:140`, `0x0039F4D8:139`,
`0x0041CF6C:139` — contêm literalmente:

```c
if ((uint)*(ushort *)(iVar6 + 2) != (*(uint *)(param_1 + 4) >> 0x10 & 0xfff)) { … }
/*        low16(filho)            !=        subtag(dono)                        */
```

O jogo **testa a igualdade entre um `low16` e um `subtag`**. Dois campos que se comparam
para igualdade vivem, por construção, na mesma numeração. *(offline-unit)*

### Conclusão

| lado | indexa por | de quem | veredicto |
|---|---|---|---|
| produtor (`0x0041FA90`, `0x0041ECD8`, +8) | `subtag` | do **gestor que se regista** | ✅ correcto |
| consumidores (a grande maioria) | `low16` | do **nó despachado** | ✅ correcto |

**São dois espaços de índice legítimos? Não — é UM espaço de índice, lido de dois papéis
diferentes.** `subtag(gestor) == low16(instância)` é o invariante que os liga, garantido
pelos construtores (`low16 = mgr[0x20]`) e verificado pelo próprio jogo.

**O erro da sessão anterior foi calcular `low16` e `subtag` do MESMO `w0` e chamar
"divergência" à diferença.** São campos de objectos com papéis diferentes; compará-los
dentro de uma só palavra não tem significado.

---

## O que fica REFUTADO por medição

1. **"A tabela é escrita por UMA ÚNICA função (`0x0041FA90`)"** (T4 da sessão anterior).
   **FALSO.** Com liveness do registo-base: **16 funções** escrevem na tabela. Das que
   fazem `stwx` indexado, **10 mostram literalmente `rlwinm rX,r0,18,18,29`** (= `subtag*4`):
   `0x00413424`, `0x00413714`, `0x0041CA58`, `0x0041DB54`, `0x0041ECD8`, `0x0041FA90`,
   `0x004243EC`, `0x0042B078`, `0x0042EF88`, `0x0042FCD0`. Há ainda um **des-registador**,
   `0x00421C74` (`*(…)(tab + uVar7) = 0`), que os dois métodos só encontram em conjunto.
   *(offline-unit)* — A **direcção** observada mantém-se e sai reforçada: o registo é
   **universalmente** por `subtag`, nunca por `low16`.
2. **"18 consumidores da tabela."** **FALSO.** 88 aliases de TOC, **420 sítios em 369
   funções**. O 18 era o alcance do nome de símbolo no C do Ghidra. *(offline-unit)*
3. **"`0x00254250` indexa por `subtag`"** (linha da tabela de tally anterior). **FALSO.**
   Indexa por `low16` — `*(ushort *)(puVar4[2] + 2)` — e ainda com verificação `< 0x21`.
   Os seus `>> 0x10 & 0xfff` são o **terminador do ciclo de subida na hierarquia**
   (`while (subtag != 1) p = p[0x1b];`), **não** um índice. *(offline-unit)*
4. **"Coincidem em `0xC0010001` e divergem em `0x40030001` (1 vs 3)"** — o cálculo está
   certo mas **não é uma divergência**: compara campos de papéis diferentes. *(offline-unit)*
5. **"O `low16` seria degenerado se fosse sempre `0x0001`."** O `low16` **não** é
   degenerado: o binário testa-o contra `3` (`0x002B0E78`), `4` e `0x14`
   (`0x00254250`), e limita-o a `< 0x21`. *(offline-unit)*
6. **"A parede 1 é um método de contentor a correr sobre um objecto que não é contentor."**
   Enfraquecida: `func_002545B0` é **o mesmo walk do grafo de tipos** que a parede 4 —
   `iVar6 = param_2 - 4`, filhos em `iVar6 + 0x80` (= `arg + 0x7C`), `node[2] + 4`,
   despacho `tab[low16(filho)]->vt[0x18]`. O `+0x7C` **é** a lista de filhos do tipo. O
   código faz exactamente o que foi desenhado para fazer. *(offline-unit)*

---

## A assimetria que SOBREVIVE — push por `low16`, pop por `subtag`

Único achado novo que **não** se dissolve, e confirmado por **três descodificadores
independentes** (C do Ghidra, o nosso lifter na sessão anterior, e o meu descodificador
directo dos bytes do ELF):

| | função | vt | índice da tabela | instrução crua |
|---|---|---|---|---|
| **push** | `0x0041F700` | `+0x40` | **`low16(filho) * 4`** | `0x0041F78C  540913BA  rlwinm r9,r0,2,14,29` → máscara `0x0003FFFC` |
| **pop** | `0x0041F924` | `+0x44` | **`subtag(filho) * 4`** | `0x0041F9B0  540914BA  rlwinm r9,r0,2,18,29` → máscara `0x00003FFC`, sobre o `subtag` já extraído em `0x0041F9A8  rldicl r0,r0,48,52` |

Os dois lêem o `w0` **do mesmo nó filho** (`lwz r9,8(r11)` → `lwz r0,4(r9)`), na mesma
lista (`h+0x80`), com a mesma guarda (`subtag(filho) != subtag(pai)`). Não é o gestor
contra o nó — **são os dois o nó**.

Consequência mecânica, se existir um filho com `low16 != subtag` na travessia:

- o **push** incrementa o cursor `+0xC8` de `tab[low16(filho)]`;
- o **pop** decrementa o cursor `+0xC8` de `tab[subtag(filho)]`;
- ⇒ um gestor sobe sem descer, outro desce sem subir. Com `+0x48..+0xC8` = `0x80` bytes =
  **32 slots** e o cursor a ser um `char` **com sinal**, o primeiro passa de 127 e escreve
  **abaixo** do gestor; o segundo fica negativo e o pop faz `iVar6 = 0`, lendo o endereço
  guest **4**.

**Isto é a mesma previsão da sessão anterior, mas derivada de um mecanismo diferente e
correcto.** A sessão anterior chegou-lhe por uma comparação inválida; aqui vem de duas
instruções medidas ao byte no binário que saiu em disco.

**Não afirmo que isto seja um bug do jogo.** O mais provável continua a ser o inverso: que
na travessia real do jogo **todo** o nó satisfaça `low16 == subtag`, e que a nossa corrida
esteja a alimentar o walk com nós que a consola nunca lhe entregaria.

---

## O guard que o `func_0041F700` NÃO tem

`func_002545B0` (parede 1) faz o **mesmo** walk mas **atrás de um bit de flag**:

```c
if ((*(ushort *)(param_2 + 4) & 4) == 0) {      /* q+4 = flags16 (== h+8 meio-alto) */
    …percorre a lista de filhos em h+0x80…
}
```

E `0x00254954` **escreve** nessa mesma halfword: `*(ushort *)(iVar4 + 4) |= 3;`.

⇒ O cabeçalho **tem** um campo de flags por objecto usado exactamente para cortar
travessias, e **`func_0041F700` não o consulta**. Isto é uma refinação muito melhor
fundamentada da H1 anterior ("o lifter perdeu a memoização"): **não** se perdeu nada — mas o
mecanismo de poda existe na família e este membro não o usa. Ficam por medir: quem põe e
quem limpa o bit `4`, e se algum caller o deveria ter posto antes do walk.

---

## Veredicto sobre "a parede 4 é consequência da parede 1?"

**NÃO — não no sentido em que a hipótese foi formulada.** Avaliação estática:

1. **O conjunto de filhos de um nó é a lista intrusiva em `h+0x80`.** É **estrutural**: os
   nós estão fisicamente ligados por ponteiros dentro dos objectos (`*puVar2` = próximo,
   `puVar2[2]` = objecto). **Não depende de nenhum índice de tipo.**
2. O índice **só escolhe qual visitante trata o filho** — nunca **quais** filhos existem.
3. ⇒ **Nenhuma correcção de indexação pode transformar um grafo completo numa árvore.** A
   previsão *"a recursão exponencial desaparece por construção, sem visited-set"* está
   **refutada por construção**. *(offline-unit)*
4. Além disso, **não há indexação errada para corrigir** (a resposta central desta sessão),
   por isso a hipótese fica sem premissa.

**O que É verdade é uma relação mais forte e mais útil:** paredes 1 e 4 são **o mesmo
walk sobre a mesma lista de filhos** (`h+0x80` = `q+0x7C`), a partir do mesmo despacho
`tab[low16]`, diferindo só no slot de vtable (`+0x18` vs `+0x40`). São **irmãs**, não
causa e efeito. Um objecto que carregue um `low16` que não é o seu tipo real, ou que esteja
enfileirado numa lista de filhos onde não pertence, dispara **as duas**. O defeito é
**a montante**, no produtor do objecto ou em quem o enfileira — exactamente onde a sessão
anterior já apontava, e por uma razão agora melhor fundamentada.

---

## O que a sonda E1 (`PS3_TYPEWALK_TRACE=1`) tem de medir

Sonda no push `0x0041F700`, **OFF por default**, limitada a ~200 linhas. Para **cada
filho** visitado, imprimir:

| campo | expressão | porquê |
|---|---|---|
| `w0` | `vm_read32(child_h + 4)` | a palavra crua |
| `low16` | `w0 & 0xFFFF` | índice do push |
| `subtag` | `(w0 >> 16) & 0xFFF` | índice do pop e guarda |
| `flags` | `w0 >> 28` | distingue `0x4` (ctor-base) de `0x8`/`0xC` (clonado) |
| `hflags` | `vm_read16(child_h + 8)` | o bit `4` que a parede 1 respeita e o push não |
| `tab[low16]`, `tab[subtag]` | `vm_read32(0x00868D48 + idx*4)` | os dois gestores |
| `cursor` | `(int8_t)vm_read8(mgr + 0xC8)` | do gestor **do push** (`tab[low16]`) |
| `mgr_subtag` | `(vm_read32(tab[low16] + 4) >> 16) & 0xFFF` | **o teste do invariante** |

### As duas previsões opostas (é isto que torna a sonda discriminadora)

| **A — o invariante está QUEBRADO nesta corrida** | **B — o invariante ESTÁ DE PÉ; a explosão é outra coisa** |
|---|---|
| aparecem filhos com **`low16 != subtag`** | **todos** os filhos têm `low16 == subtag` |
| para esses, `tab[low16]` e `tab[subtag]` são **ambos não-nulos e diferentes** | `tab[low16] == tab[subtag]` sempre (ou `tab[subtag]` é nulo/nunca usado) |
| **`mgr_subtag != low16`** para ≥1 nó ⇒ o *round-trip* de `0x00254954` está partido e a tabela está a devolver o gestor errado | **`mgr_subtag == low16` para 100% dos nós** ⇒ registo e consumo concordam; o achado central desta sessão fica confirmado in-boot |
| o cursor `+0xC8` de `tab[low16]` **sobe monotonicamente** e passa de 32 (e depois de 127, ficando negativo) | o cursor sobe e desce, e fica sempre em `[0, 32)` |
| ⇒ o fix é **a montante** (quem constrói o objecto com o `low16` errado, ou quem o enfileira) | ⇒ a explosão é combinatória pura; o próximo alvo é o **bit de flag `hflags & 4`** — porque é que o push não o consulta e quem o devia ter posto |

**`mgr_subtag == low16` é o discriminador central desta sessão.** É o invariante que
declaro provado estaticamente; se um único nó o violar in-boot, esta página está errada e
tem de ser corrigida. É de propósito que é fácil de matar.

**E3 — sonda de CONTROLO, obrigatória e primeiro.** A parede continua sem tag de log.
Antes de qualquer bissecção, uma contagem de entradas em `0x0041F700` que **tem** de
aparecer — senão um silêncio não distingue "não passou aqui" de "a sonda não foi
compilada". Foi assim que as Fases 7–10 mediram uma string inexistente, e foi assim que
nesta sessão o "18 consumidores" quase virou um facto.

---

## O que esta sessão NÃO afirma

- **Não** afirma ter medido um único `w0` numa corrida. Tudo aqui é estático. Os valores
  `0xC0010001` / `0x40030001` são **entrada** de outra sessão (2026-08-01/02, ROADMAP),
  aceites como dados.
- **Não** confirma nem refuta os rótulos *"listas"* e *"registos WAD"* daquela tabela. O
  que mede é que **ambos os valores têm a assinatura do construtor-base** (`| 0x40000000`,
  98 construtores) e **ambos têm `low16 = 1`** — logo **ambos despachariam para `tab[1]`
  independentemente da família**. Se os rótulos descrevem *layout*, podem estar certos; como
  afirmação sobre *despacho*, não se sustentam. Não re-medi nenhum dos dois.
- **Não** afirma que `tab[1]` é "o gestor de gestores". É uma inferência plausível
  (`0x00252688` e `0x00254250` lêem `tab[1]` como slot fixo e bem-conhecido) que **não
  foi medida**.
- **Não** afirma que o slot 3 está preenchido, nem o que contém. A tabela vive em
  `0x00868D48`, fora do `filesz` do ELF ⇒ é `.bss`, populada em runtime. **Q3 do briefing
  é estruturalmente inacessível offline**; o que se responde é a *regra*: o slot `S` contém
  o gestor cujo `subtag(w0) == S`, e as instâncias desse gestor carregam `low16 == S`.
- **Não** afirma que o pop é sequer alcançado numa corrida real, nem que o push e o pop
  operam sobre a mesma instância de gestor num dado momento.
- **Não** afirma que o jogo original tem um bug. Um jogo que saiu em disco não fazia 27 M
  de iterações no boot.
- **Não** toca em pixels, shaders ou `SHADERSRC`.
- **Não** reabre nada da lista *"O que já está ELIMINADO por medição"* do ROADMAP. As
  correcções acima são a **medições da sessão anterior**, não àquela lista.

---

## Próximo experimento para quem retomar

1. **E1 acima**, com E3 (controlo) primeiro. Uma corrida decide entre A e B.
2. **Se B (invariante de pé):** o alvo passa a ser o **bit `hflags & 4`** — `--grep` por
   quem escreve `*(ushort *)(… + 4) |= ` e `&= ~4` nesta família, e porque é que
   `func_002545B0` o respeita e `func_0041F700` não. É o único mecanismo de poda por
   objecto que o binário tem.
3. **Se A (invariante partido):** subir ao produtor — quem escreveu `mgr[0x20]` antes do
   `*(short *)(node + 6) = (short)mgr[0x20]` que carimbou o `low16` errado. `mgr[0x20]`
   nasce a `0xFFFFFFFF` no construtor (`0x0025271C:32`), portanto **há sempre um escritor
   intermédio** — encontrá-lo é o passo.
4. **Independente e barato:** os descritores de tipo estáticos. O `w0` vem de
   `descritor[0] & 0x0FFFFFFF`; se a tabela de descritores for localizada em `.rodata`,
   lê-se o par `(subtag, low16)` de **todos** os tipos do jogo directamente do ELF, e o
   invariante fica provado ou morto **sem boot nenhum**. Esta sessão não chegou lá: os
   construtores são todos alcançados indirectamente e o `--callers` não os resolve.

---

## Anti-padrões: o que esta sessão recusou fazer

- Não "corrigiu" o índice do push nem do pop. A resposta é que **não há lado errado** a
  corrigir na produção/consumo, e a assimetria push/pop precisa de E1 antes de qualquer
  divergência do PPC.
- Não publicou o primeiro número de escritores que obteve (72). Era uma heurística sem
  liveness do registo-base; o número defensável é 16, e a diferença está declarada.
- Não repetiu "18 consumidores" só porque estava escrito no ledger anterior. Mediu de novo,
  por um caminho independente, e o número mudou por mais de uma ordem de grandeza.
- Não editou nenhum `ppu_recomp_*.cpp`, não correu builds, não tocou em
  `recomp_macos_v3_e2e*` (outra sessão a liftar em paralelo).
- Não correu boots ⇒ sem `pkill`, sem órfãos, sem logs no git. Scripts descartáveis em
  `/tmp/re_w0/`, nada commitado.
- Não declarou a parede 4 fechada. Continua **aberta** — com uma hipótese a menos e um
  mecanismo candidato melhor.
