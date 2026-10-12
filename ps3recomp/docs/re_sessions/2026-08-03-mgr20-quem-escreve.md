# Sessão de RE — quem escreve `mgr[+0x20]`, e o que o valor significa — 2026-08-03

Sessão conduzida com [`docs/RE_PLAYBOOK_GOW2.md`](../RE_PLAYBOOK_GOW2.md). Continuação directa de
[`2026-08-02-invariante-low16-subtag.md`](2026-08-02-invariante-low16-subtag.md), que por sua vez
seguia [`2026-08-02-w0-semantica-indexacao.md`](2026-08-02-w0-semantica-indexacao.md) e
[`2026-08-02-wall-4-walk-de-tipos.md`](2026-08-02-wall-4-walk-de-tipos.md).

**Veredicto em uma linha:** `+0x20` **não é a fonte do `low16`** — a sessão anterior trocou a base
(`h` vs `q`) e leu `q+6` como `low16` quando é `h+0x0A`, o *slot* no array do gestor. O `+0x20` é o
**índice de alocação do próprio objecto no array do seu gestor** (contador de append com reciclagem
por free-list, **por gestor**, nunca global, nunca comparado com o `subtag`). Mas a divergência
`low16 != subtag` **é mesmo estrutural** — e a origem foi localizada e verificada ao byte noutro
sítio: **`0x00254C40`, o desserializador do stream de tipos, força `low16 := 1` e copia o `subtag`
dos dados** (`rlwinm rX,rY,0,4,15` + `ori rX,rX,1`). São os **únicos 2 sítios do binário inteiro**
com esse padrão. `0x00030001 | 0x40000000` = **`0x40030001`**, o valor medido in-boot.

**Zero builds, zero boots, zero escritas fora deste ficheiro.**

---

## Pergunta (uma frase)

> **Quem escreve o valor real de `mgr[+0x20]`, e o que esse valor significa?** Se for um contador
> global de ordem de registo, a divergência `low16 != subtag` é estrutural e a parede fecha offline.

---

## Âncoras

| âncora | valor |
|---|---|
| **ELF** | `../gow2-recomp/EBOOT.ELF`, sha256 `23cfd435…` (bate com o `MANIFEST.json` do `ghidra_out`) |
| **desserializador (o achado)** | **`0x00254C40`** `.opd.FUN_00254c40 size=1832`; sítios do `w0`: **`0x00254E2C`–`0x00254E3C`** e **`0x00255054`–`0x00255074`** |
| **escritores reais de `+0x20`** | `0x00256C10`, `0x0039F5CC`, `0x0041D060`, `0x00420348`, `0x0042A754`, `0x0042CDDC`, `0x004303D0` — todos a **`+0xF4`** do seu `vt+0x58` |
| **leitores de `+0x20`** | one-liners `0x004117B0`, `0x0041C558`, `0x0041E8A4`, `0x0042C644`, `0x0042EC60` (+ `0x00254294` em `0x00254250`) |
| **vtable do gestor** | `0x00515AA0` (TOC `0x0053EF40`) — `+0x08` registo (`0x0041FA90`), `+0x40` push (`0x0041F700`), `+0x44` pop (`0x0041F924`), `+0x58` criar (`0x0041EDE4`) |
| **tabela de tipos** | `0x00868D48`, via **dois** aliases de TOC directos: `PTR_DAT_0053ef1c` **e** `PTR_DAT_0053eec8` |
| **walk / round-trip** | `0x0041F700` (push), `0x0041F924` (pop), `0x00254250` (walk por `low16`, jump table `0x0025431C`), `0x00254954` (round-trip), `0x002545B0` (parede 1) |
| **tag de log** | **nenhuma** — a parede continua sem instrumentação própria |

---

## Sondas de CONTROLO (corridas ANTES de qualquer achado)

O playbook exige-o. Nesta sessão apanharam **um erro meu** e validaram a ferramenta três vezes.

| # | sonda | resultado |
|---|---|---|
| **C1** | **positiva** — o meu leitor/descodificador tem de reproduzir 9 instruções já publicadas por 3 sessões (`0x0041F78C 540913BA`, `0x0041F9B0 540914BA`, `0x002B1068 5520043E`, `0x002B106C 2F800003`, `0x002B1074 79208522`, `0x002B1078 2F800001`, `0x0041FAB8 540094BA`, `0x0041FAC0 7FCB012E`) | **9/9 na semântica.** A única discrepância de *bytes* (`0x0041F9A8`: li `78008522`, esperava `79208522`) era **erro de transcrição meu** ao copiar da página anterior — o disassembly (`rldicl r0,r0,48,52`) bate exactamente com o que lá está escrito. **A sonda apanhou o meu erro, não o da ferramenta.** |
| **C2** | **cruzada, contagem** — reproduzir por caminho independente os **654** `stw rX,0x20(rA)` da sessão anterior | **654, exacto.** |
| **C3** | **cruzada, contagem** — reproduzir os **140** `oris rX,rY,0x4000` da sessão anterior | **140, exacto.** |
| **C4** | **cruzada, dados↔decompilador** — `PTR_DAT_0053ef1c` e `0x0053EF40` lidos do ELF | `0x00868D48` (= o `tab` medido in-boot ✅) e `0x00515AA0` (= a vtable do gestor das sessões anteriores ✅). Achado novo: `PTR_DAT_0053eec8` é um **segundo** alias directo de `0x00868D48` |
| **C5** | **cruzada, estrutura** — os 5 one-liners têm de ser byte-idênticos | `80030020 38600001 B0040006 4E800020` nos **5**. ✅ |
| **C6** | **cruzada, ramo** — o meu descodificador tem de reproduzir um ramo não-trivial que o Ghidra decompila (`0x00255080`–`0x002550B8`: `rldicl 48,52` → `cmpwi 3` → `lbz`/`extsb`/`cmpwi 0` → `stw …,108` vs `bl 0x002B2BA0`) | bate instrução a instrução com o C (`if (subtag != 3) { if (*(char*)(p+1)=='\0') R[0x1b]=0; else R[0x1b]=FUN_002b2ba0(); }`) ✅ |
| **C7** | **divergência declarada** | a minha triagem dá **98 `li -1`** e **216 `li 0`** entre os 654; a sessão anterior publicou **93** e **53**. Backscans diferentes (o meu recua 96 instruções e aceita qualquer registo). **Não trato nenhum dos dois como autoritativo** — a triagem que uso abaixo é por *função contentora*, não por esta contagem |

---

## Ferramentas usadas (comando + rc + o que devolveu)

Oráculo: `../gow2-recomp/ghidra_out` (13 143 funções, 13 096 com C). **Não regenerado.**
Ferramentas próprias em `/tmp/re_mgr20/` (descartáveis; nada commitado, o EBOOT nunca saiu de lá).

| # | ferramenta | rc | devolveu |
|---|---|---|---|
| T1 | leitor ELF64 PPC BE + descodificador PPC próprio (`elfppc.py`) | 0 | PH0 `0x10000+0x4FC7E0`, PH1 `0x510000+0x66EA4`; sha256 bate |
| T2 | varrimento + classificação dos 654 `stw rX,0x20(rA)` por origem do registo | 0 | ver C7; `sites20.json` |
| T3 | mapa OPD (pares `(code, 0x00541178)`) + xrefs de dados | 0 | **16 259** descritores, **13 006** códigos, **4 681** OPDs referenciados por dados |
| T4 | localizador de vtables ancorado no **slot `+0x08` = registador** e no **slot `+0x58`** | 0 | **94 vtables** da hierarquia de tipos; **886 funções** ocupam os seus slots |
| T5 | cruzamento dos 654 sítios com as 886 funções da hierarquia | 0 | **40 sítios** (§ *A resposta*) |
| T6 | varrimento de `sth rX,{2,6}(rA)`, `stb rX,{2,3,6,7}(rA)`, `stw rX,4(rA)` | 0 | 155/216/543/528/284/263/8710 no binário; 23/20/… na hierarquia |
| T7 | varrimento de `rlwinm rX,rY,0,4,15` (máscara `0x0FFF0000`) + `ori` nas 5 seguintes | 0 | **128 sítios; exactamente 2 com `ori …,1` — ambos em `0x00254C40`** |
| T8 | desmontagem alvo de `0x0041EB7C`, `0x0041CF6C+0xC0`, `0x0041BF00+0x110`, `0x00254C40` | 0 | as bases dos argumentos, ao nível do registo |

---

## A resposta (1): **`+0x20` é `h+0x20` — o índice do objecto no array do seu gestor**

### Quem escreve

Dos **654** `stw rX,0x20(rA)` do binário, **40** caem dentro das 886 funções que ocupam slots das
94 vtables da hierarquia de tipos. Desses 40:

| classe | nº | o quê |
|---|---|---|
| **inicialização de construtor** | **29** (22 × `li -1`, 7 × `li 0`) | `0x002527D4` (ctor do gestor), `0x0041EEC0` (em `0x0041EDE4`), `0x00250284` (em `0x002501A8`), … — é o `param_1[8] = 0xffffffff` já conhecido |
| **escritores reais** | **7** | `0x00256C10`, `0x0039F5CC`, `0x0041D060`, `0x00420348`, `0x0042A754`, `0x0042CDDC`, `0x004303D0` |
| **refutados (não é `h+0x20`)** | 2 | `0x0041C03C` (em `0x0041BF00`) escreve `q+0x20` = **`h+0x24`**; `0x002552AC` (em `0x00254C40`) é init em bloco de uma struct de offsets |
| **por triar** | 2 | `0x002902E4` (em `0x0029023C`), `0x002A76EC` (em `0x002A74B8`) |

Os **7 escritores reais estão todos a `+0xF4` do início da sua função**, e as 7 funções são as
**7 implementações "grandes" do mesmo slot virtual `+0x58`** (1236–1348 bytes cada).

> **Correcção ao briefing:** estes 7 **não** são "irmãos" de `0x0041F700`. `0x0041F700` é o
> **`vt+0x40`** (push) da vtable do **gestor** `0x00515AA0` (296 bytes); os 7 são o **`vt+0x58`**
> da hierarquia dos **nós**. Slots diferentes, hierarquias diferentes. A comparação
> "1 de 8 irmãos é assimétrico" não se sustenta com esta ancoragem. *(offline-unit)*

### O que o valor significa

`0x0041CF6C` (representante dos 7), com `param_1` = `h` do objecto-tipo e `param_2` = `q` do
objecto novo:

```c
iVar5 = *(int *)(param_1 + 0x30);                       /* h+0x30 = o GESTOR M */
iVar7 = *(int *)(iVar5 + 0x38);
if (*(int *)(iVar5 + 0x3c) == iVar7) {                  /* free-list vazia   */
  piVar9  = (int *)(*(int *)(iVar5+0x44) * 0xc + *(int *)(iVar5+0x24));
  iVar11  = piVar9[1];                                  /* <- contador de append */
  *(int **)(iVar11 * 4 + *piVar9) = piVar8 + 1;         /*    array[idx] = q     */
  piVar9[1] = iVar11 + 1;                               /*    count++            */
} else {                                                /* free-list tem entradas */
  iVar11 = *(int *)(iVar7 * 4 + *(int *)(iVar5+0x34));  /* <- índice RECICLADO   */
  *(int *)(iVar5 + 0x38) = iVar7 + 1;
  ...
}
piVar8[8] = iVar11;                                     /* h_novo[0x20] = índice */
```

Verificado ao byte em `0x0041D034`–`0x0041D060`:
`lwz r7,4(r10)` (o `count` do chunk) → `addi r11,r7,1` → `stw r11,4(r10)` (`count++`) →
**`stw r7,32(r30)`** (o índice *antes* do incremento). *(offline-unit, 2 descodificadores)*

⇒ **`h+0x20` é o índice de alocação do objecto no array do seu gestor.** É um contador de ordem —
mas **por gestor**, e **reciclável** (free-list). **Não** é global, **não** é um id de tipo, e
**nada no binário o compara com `subtag`** (confirma-se a medição negativa da sessão anterior).

---

## A resposta (2): **os one-liners NÃO escrevem o `low16`** — a sessão anterior trocou a base

O one-liner é o **`vt+0x58`** com **3 argumentos**. O sítio de chamada, desmontado
(`0x0041EB7C`, `vt+0x14`):

```
0x0041EC1C  lwz r10,0(r30)     ; vptr(this)
0x0041EC20  rldicl r31,r3,0,32 ; r31 = objecto NOVO, devolvido por vt+0x4C
0x0041EC24  mr  r5,r28         ; r5 = origem
0x0041EC28  mr  r3,r30         ; r3 = this
0x0041EC2C  mr  r4,r31         ; r4 = objecto NOVO
0x0041EC30  lwz r9,88(r10)     ; +0x58
0x0041EC44  bctrl              ; vt+0x58(this, novo, origem)
```

E a **outra** implementação do **mesmo slot** (`0x0041CF6C`) faz `piVar8 = (int *)(param_2 + -4)`.
⇒ **`param_2` é `q` (= `h+4`)**, sem ambiguidade. Logo:

```
one-liner:  sth r0,6(r4)   ->  q+6   ==  h+0x0A
```

**`h+0x0A` não é o `low16`.** O `low16` são os 16 bits baixos do `w0`, isto é **`h+0x06`** = `q+0x02`.
`h+0x0A` é a *halfword de slot*, a metade baixa da palavra `flags16|slot16` em `h+0x08`.

Três sítios independentes concordam com esta leitura:

| sítio | escreve/lê | prova |
|---|---|---|
| `0x0041FA90` (registo do gestor) | `*(short *)(h + 10) = (short)M[0x35]` | `M[0x35]` é o índice acabado de atribuir no array de M |
| `0x0041CF6C` (`vt+0x58` grande) | `*(short *)(h_novo + 10) = *(short *)(h_this + 10)` | mesma halfword que o one-liner escreve via `q+6` |
| `0x00254954` (round-trip) | lê `q+6` como índice no array de `tab[low16(q)]` | fecha o ciclo `obj → tab → array → obj` |

E `0x0041BF00` faz `*(ushort *)(param_2 + 4) |= 4` com `param_2 = q` ⇒ `q+4` = `h+8` = as flags —
o mesmo bit `4` que a parede 1 (`0x002545B0`) consulta. Coerente.

> ⇒ **REFUTADO:** *"o `low16` é recarimbado depois, por one-liners, a partir de `mgr[+0x20]`"*.
> Esse mecanismo **não existe**. É uma confusão `h`↔`q` — exactamente a armadilha que o playbook
> §2 nomeia (*"o mesmo campo aparece como `+0x7C` numa função e `+0x80` noutra… confirma a base"*).
> Os one-liners propagam o **slot**, não o tag. *(offline-unit, verificado ao registo)*

---

## A resposta (3): **a divergência É estrutural — mas nasce no desserializador do stream**

Com o mecanismo do `+0x20` fora do caminho, a pergunta volta a ser *"de onde vem um `low16`
diferente do `subtag`?"*. Varrimento exaustivo (**T7**): **128** sítios no binário aplicam a máscara
`0x0FFF0000` (guardar só o `subtag`); **exactamente 2** seguem-na de um `ori …,1` — e são **os dois
dentro de `0x00254C40`**:

```
0x00254E20  80160000  lwz    r0,0(r22)      ; w0 do registo do STREAM
0x00254E2C  5400011E  rlwinm r0,r0,0,4,15   ; & 0x0FFF0000  -> guarda o subtag, ZERA o low16
0x00254E34  60000001  ori    r0,r0,0x1      ; low16 := 1
0x00254E38  93A90004  stw    r29,4(r9)      ; novo[4] = 0   (cabeça da lista de filhos)
0x00254E3C  90090000  stw    r0,0(r9)       ; novo[0] = w0

0x0025504C  813D0000  lwz    r9,0(r29)
0x00255054  5529011E  rlwinm r9,r9,0,4,15
0x0025505C  61290001  ori    r9,r9,0x1
0x00255074  912B0000  stw    r9,0(r11)
```

E `0x00254C40` é, sem dúvida, um **desserializador**: faz *byte-swap* explícito de dezenas de
campos (`uVar >> 0x18 | uVar >> 8 & 0xff00 | (uVar & 0xff00) << 8 | uVar << 0x18`), aloca
`0x70` bytes por registo (`bl 0x00263B70`), copia `0x4C` bytes do stream (`bl 0x0036B4A8`) e liga
o pai em `novo[0x1b]` (= `+0x6C`, exactamente o campo por onde `0x00254250` sobe a hierarquia com
`while ((*p >> 0x10 & 0xfff) != 1) p = p[0x1b];`).

O *byte-swap* é ele próprio um dado: num CPU **big-endian** (PPC), trocar bytes na leitura
significa que **o ficheiro é little-endian** — coerente com GoW2 HD ser um port de dados de PS2
(MIPS LE). É o formato do conteúdo do jogo, não um artefacto nosso.

**Consequência aritmética, directa:**

```
registo do stream com subtag S   ->   R.w0 = (S << 16) | 1
carimbo do ctor-base             ->   obj.w0 = (R.w0 & 0x0FFFFFFF) | 0x40000000
S = 3                            ->   obj.w0 = 0x40030001      <-- o valor MEDIDO in-boot
```

E imediatamente a seguir ao segundo sítio, o próprio jogo trata o `subtag` 3 como caso especial:

```
0x00255084  78008522  rldicl r0,r0,48,52   ; subtag
0x00255088  2F800003  cmpwi  cr7,r0,3      ; == 3 ?
```

Além disso, `0x00254C40` só entra neste caminho quando `subtag != 1` (`if (uVar16 != 1)`), e trata
`subtag == 0x2A` à parte. ⇒ o **`subtag 1` é o caso raiz/contentor**, e todos os outros recebem
`low16 = 1`.

### Leitura de desenho que isto sustenta

| campo | papel |
|---|---|
| **`low16`** | **o slot da tabela que me TRATA** — para tudo o que vem do stream, `1` (o gestor genérico de registos) |
| **`subtag`** | **a minha CATEGORIA** no grafo de tipos — vem dos dados, usada nas comparações e no terminador `while (subtag != 1)` |

Nesta leitura, `low16 != subtag` **não é corrupção: é o desenho**. E o par push/pop fica assim:

| | guarda | índice | com `low16 = 1`, `subtag = 3` |
|---|---|---|---|
| push `0x0041F700` | `subtag(filho) != subtag(pai)` | **`tab[low16(filho)]`** | `tab[1]` — **coerente com o desenho** |
| pop `0x0041F924` | `subtag(filho) != subtag(pai)` | **`tab[subtag(filho)]`** | `tab[3]` — **outro gestor** |

Reconfirmado por leitura própria do C: o pop calcula `uVar5 = subtag(filho)` **uma vez** e usa-o
nas duas pontas; o push extrai `subtag` para a guarda e `low16` para o índice. ⇒ o cursor `+0xC8`
de `tab[1]` sobe uma vez por nó visitado e **nunca desce**; o de `tab[subtag]` desce sem ter subido.
*(offline-unit)*

**Não afirmo que isto seja um bug do jogo** (regra 13 do playbook, regra 8 do `CLAUDE.md`). Afirmo
o que está medido: a assimetria é interna a `0x0041F700`/`0x0041F924`, e o desserializador produz
sistematicamente nós que a activam. Falta a medição in-boot de *quais* nós chegam ao push.

---

## A resposta (4): **paredes 1 e 4 e o desserializador são métodos da MESMA classe**

Nenhuma destas funções tem `bl` directo (varrimento próprio: **0** chamadas directas às quatro).
Todas são alcançadas por vtable — e todas pela **mesma**, `0x00515B08` (a base confirmada por
`+0x58` conter `0x00256B1C`, uma das 7 implementações grandes):

| slot | função | papel |
|---|---|---|
| `+0x0C` | `0x0041FF70` | o *walk exterior* (achado da 1.ª sessão) |
| `+0x28` | `0x00252ED8` | o **clone do `w0`** (preserva `subtag` e `low16` em separado) |
| `+0x58` | `0x00256B1C` | `vt+0x58` grande — **escreve `h+0x20`** (`0x00256C10`) |
| `+0x64` | **`0x002545B0`** | **a PAREDE 1** |
| `+0x88` | `0x00421C74` | des-registador da tabela de tipos |
| `+0xA0` | **`0x00254C40`** | **o DESSERIALIZADOR** (`low16 := 1`) |
| `+0xD0` | `0x00254250` | walk por `low16` + jump table `0x0025431C` (33 entradas) + `low16 < 0x21` |
| `+0xD8` | `0x00254954` | o **round-trip** `obj → tab[low16] → array → obj` |

Note-se que `+0x40`/`+0x44` desta classe são os *stubs* genéricos `0x0039D3B0`/`0x0039D3B4` — o
par push/pop (`0x0041F700`/`0x0041F924`) é `+0x40`/`+0x44` da vtable do **gestor**, `0x00515AA0`.

⇒ **a parede 1, a parede 4 e a origem do `low16 = 1` vivem no mesmo objecto.** As sessões
anteriores já suspeitavam que 1 e 4 eram "irmãs"; isto é a prova estrutural, e acrescenta-lhes o
loader. *(offline-unit)*

---

## O que fica REFUTADO por medição

1. **"O `low16` é recarimbado por one-liners a partir de `mgr[+0x20]`"** (mecanismo central da
   sessão de 2026-08-02). **FALSO.** `sth r0,6(r4)` com `r4 = q` escreve `h+0x0A` (slot), não
   `h+0x06` (`low16`). Verificado ao registo em `0x0041EC1C`–`0x0041EC44` e pela implementação
   irmã do mesmo slot (`0x0041CF6C`, `param_2 - 4`). *(offline-unit)*
2. **"`mgr[+0x20]` é um contador GLOBAL de ordem de registo"** (hipótese do briefing). **FALSO.**
   É **por gestor** e **reciclável**: append `chunk[1]++` só quando a free-list está esgotada;
   caso contrário um índice devolvido de `M[0x34][M[0x38]++]`. *(offline-unit)*
3. **"`0x0041F700` é o único de 8 irmãos que guarda por `subtag` e indexa por `low16`"**
   (briefing). **A premissa não se sustenta.** As 7 funções citadas são o `vt+0x58` da hierarquia
   dos **nós** (1236–1348 bytes); `0x0041F700` é o `vt+0x40` da vtable do **gestor** `0x00515AA0`
   (296 bytes). Não partilham slot nem hierarquia. A assimetria push/pop **mantém-se** (é entre
   `0x0041F700` e `0x0041F924`, `+0x40` vs `+0x44` da *mesma* vtable) — o que cai é a moldura de
   "8 irmãos". *(offline-unit)*
4. **"O `+0x20` é um campo do gestor"** (framing das duas sessões anteriores). **Impreciso.**
   É um campo de **qualquer objecto desta hierarquia** (o gestor é um objecto como os outros); o
   one-liner lê o `+0x20` do **objecto-tipo**, não de um "gestor". *(offline-unit)*
5. **"Os descritores de tipo vivem em runtime e não são enumeráveis offline"** (sessão anterior).
   **Continua verdade — e agora sabe-se PORQUÊ e ONDE nascem:** são desserializados por
   `0x00254C40` a partir de um stream little-endian, com `low16` forçado a 1. A busca em `.rodata`
   continua correctamente encerrada. *(offline-unit)*

---

## Veredicto: **avançado — a divergência é estrutural, e a causa está localizada; a parede continua ABERTA**

O que mudou face à sessão anterior:

- a pergunta do briefing (*"quem escreve `mgr[+0x20]`"*) está **respondida**, e a resposta
  **dissolve a hipótese** que lhe dava importância: `+0x20` não alimenta o `low16`;
- a divergência `low16 != subtag` **é estrutural e por desenho**, e a sua origem tem um **EA e um
  par de instruções**: `0x00254E2C`/`0x00254E34` e `0x00255054`/`0x0025505C` em `0x00254C40`;
- o alvo deixa de ser *"encontrar o escritor intermédio de `mgr[+0x20]`"* (não existe tal coisa) e
  passa a ser **medir a distribuição de `low16` entre os filhos que chegam a `0x0041F700`**.

O que **não** mudou: continua sem medição in-boot; a parede não fecha.

---

## É estrutural (bug) ou é estado que não temos?

**É estrutural — mas não no sítio onde o briefing esperava, e não é evidência de bug do jogo.**

- **Estrutural:** o `low16 = 1` é gravado por instruções fixas do binário original, sobre todos os
  registos do stream com `subtag != 1`. Não depende de estado que nos falte. *(offline-unit)*
- **Não é "o nosso lift está errado":** as instruções são o binário que saiu em disco; nada disto
  passa por código nosso.
- **Mas também não prova bug do jogo:** um jogo que saiu em disco não fazia 27 M de iterações no
  boot. Restam três mundos, e **só uma corrida os separa**:
  1. **o desenho é coerente e o pop é que compensa noutro sítio** que ainda não vi;
  2. **a poda `hflags & 4` (`h+0x08`, bit 4)** — que `0x002545B0` respeita, que `0x0041BF00` e
     `0x0039D764` *põem*, e que `0x0041F700` **não consulta** — é o que na consola impede a
     re-visita, e no nosso boot não está posta a tempo;
  3. **a nossa corrida entrega ao push um conjunto de nós que a consola nunca lhe entregaria.**

---

## Onde vai o fix, e de que forma

**Ainda não vai a lado nenhum como *fix*** — e isso é uma escolha escrita, não uma omissão:

- **não há "escritor errado de `mgr[+0x20]`" para corrigir** (achado 2 desta sessão);
- o sítio que produz a divergência (`0x00254C40`) é **fiel ao formato dos dados do jogo**; mexer
  nele seria estampar um valor à mão (anti-padrão 3 do playbook) e divergir do CELL (regra 8 do
  `CLAUDE.md`);
- corrigir o índice do push ou do pop sem E1 é divergir do PPC por inferência estática — foi
  recusado pelas três sessões anteriores e é recusado aqui também.

**O que vai já, e com que mecanismo — escolha e razão:**

| | mecanismo | razão escrita |
|---|---|---|
| **sonda E1** | **`[[midasm_hook]]`** em `games/gow2/config/gow2_recomp.toml` + corpo em `games/gow2/hooks/gow2_midasm_hooks.cpp`, no topo do laço de `0x0041F700` | é uma sonda que tem de correr **ao lado** da instrução, sem substituir nada. Um `[[functions_override]]` (weak) obrigaria a reimplementar 296 bytes de PPC só para observar — precisamente o risco de divergência que queremos evitar. Gated por `PS3_TYPEWALK_TRACE=1`, **OFF por default** |
| **fix (se E1 confirmar)** | **a montante**, e só depois de nomeado | se o mundo (2) vencer, o alvo é quem devia ter posto `hflags & 4` antes do walk — aí o mecanismo natural é **weak override** da função que o devia pôr (o fix *envolve* o corpo), não um mid-asm. Se vencer (3), o alvo é o produtor/enfileirador dos nós e nem sequer é este ficheiro |

Um `[[functions_override]]` de `0x0041F700` a trocar `tab[low16]` por `tab[subtag]` (ou o inverso
no pop) é **explicitamente rejeitado** enquanto não houver E1: seria "corrigir divergindo do CELL".

---

## O que a sonda E1 tem de ver — previsões OPOSTAS

Sonda no push `0x0041F700`, `PS3_TYPEWALK_TRACE=1`, **OFF por default**, ~200 linhas.
**E3 (controlo) primeiro e obrigatório:** uma contagem de entradas em `0x0041F700` que **tem** de
aparecer — sem ela, um silêncio não distingue *"não passou aqui"* de *"a sonda não foi compilada"*.

O discriminador **mudou outra vez**. `mgr20_lo16` da sessão anterior **deixa de ser interessante**
(mede o slot do array, não o tag). Por cada filho visitado:

| campo | expressão | porquê |
|---|---|---|
| `w0` | `vm_read32(child_h + 4)` | a palavra crua |
| `low16` / `subtag` | `w0 & 0xFFFF` / `(w0>>16)&0xFFF` | **o discriminador central desta sessão** |
| `flags` | `w0 >> 28` | `0x4` = ctor-base; `0x8`/`0xC` = passou pelo clone `0x00252ED8` |
| `slot16` | `vm_read16(child_h + 0x0A)` | o campo que os one-liners escrevem (substitui o `mgr20_lo16`) |
| `own_idx` | `vm_read32(child_h + 0x20)` | o índice no array do gestor; `-1` = nunca inserido |
| `hflags` | `vm_read16(child_h + 8)` | o bit `4` que `0x002545B0` respeita e o push não |
| `tab[low16]`, `tab[subtag]` | `vm_read32(0x00868D48 + idx*4)` | os dois gestores |
| `cursor_push` / `cursor_pop` | `(int8_t)vm_read8(tab[low16] + 0xC8)` / `… tab[subtag] + 0xC8` | **os dois cursores** — é a assimetria a olho |

| **A — o desserializador é a causa (previsão desta sessão)** | **B — não é** |
|---|---|
| **`low16 == 1` para a esmagadora maioria dos filhos**, com `subtag` variado | `low16` variado e **igual** ao `subtag` |
| `tab[low16]` é **sempre o mesmo gestor** (`tab[1]`) | `tab[low16] == tab[subtag]` sempre |
| **`cursor_push` de `tab[1]` sobe monotonicamente** (uma vez por nó) enquanto `cursor_pop` de `tab[subtag]` desce | os dois cursores são do mesmo gestor e fecham em `[-1, 32)` |
| `own_idx` cresce dentro de cada gestor, com saltos (reciclagem) — **nunca igual ao `subtag` por construção** | `own_idx` acompanha o `subtag` ⇒ o achado 2 desta sessão está errado |
| ⇒ confirma o desenho `low16 = "quem me trata"` / `subtag = "o que eu sou"`; o alvo passa a ser o `hflags & 4` que o push não consulta | ⇒ a leitura de `0x00254C40` não se aplica a estes nós; reabrir de onde vêm |

**Nota para quem implementar (mantém-se da sessão anterior):** o cursor `+0xC8` **nasce a `0xFF` = −1**
(`0x0025271C`: `*(undefined1 *)(param_1 + 0x32) = 0xff`). O pop trata `cVar1 < 0` devolvendo
`iVar6 = 0`. **Um cursor a `-1` é "vazio", não é erro.**

**Nota nova:** o ctor-base põe `hflags = 0x20` (`sth r9,4(r11)` com `r9 = 32`, ex. `0x002902CC`), logo
o bit `4` **nasce limpo**. Um trace que o leia como "já visitado" à entrada dá falso positivo.

---

## O que esta sessão NÃO afirma

- **Não** afirma ter medido um único `w0` numa corrida. Tudo aqui é estático. `0x40030001` continua
  a ser **entrada** de outra sessão; o que esta sessão acrescenta é **um sítio no binário que produz
  exactamente esse padrão de bits**, e que é único no binário para essa forma.
- **Não** afirma ter provado a cadeia de dados `registo de 0x00254C40 → descritor do ctor-base →
  o objecto com w0 = 0x40030001`. Os *callers* de `0x00254C40` e dos ctors-base são todos
  indirectos (`--callers` dá `(nenhum)`). O que está provado é a **coincidência exacta e única do
  padrão de bits**, não o fluxo de dados. Fechar isso exige E1 ou um trace do loader.
- **Não** afirma que o jogo original tem um bug. Afirma que `0x0041F700` e `0x0041F924` usam campos
  diferentes do mesmo `w0` para indexar a mesma tabela, e que `0x00254C40` produz sistematicamente
  nós em que esses campos diferem. As duas coisas juntas são um mecanismo; um mecanismo não é uma
  observação.
- **Não** afirma que `tab[1]` ou `tab[3]` estão preenchidos numa corrida. A `tab` (`0x00868D48`)
  está em `.bss`, fora do `filesz` — continua inacessível offline.
- **Não** afirma ter triado os 614 `stw rX,0x20(rA)` **fora** da hierarquia de tipos. São de outras
  classes e irrelevantes para esta pergunta; ficam por triar por desenho, não por esquecimento.
- **Não** afirma que `0x002902E4` e `0x002A76EC` (os 2 sítios por triar dentro da hierarquia)
  sejam inócuos. Não foram resolvidos.
- **Não** refuta o invariante `low16(instância) == subtag(do seu gestor)` da sessão de
  2026-08-02 (a §*w0-semântica*). Ele continua de pé; o que esta sessão diz é que **o mecanismo
  proposto para o garantir não é o que a sessão seguinte julgou.**
- **Não** toca em pixels, shaders ou `SHADERSRC`.
- **Não** reabre nada da lista *"O que já está ELIMINADO por medição"* do ROADMAP.
- **Não** declara a parede fechada.

---

## Próximo experimento para quem retomar

1. **E1 + E3 acima.** Uma corrida decide A vs B. O discriminador é a **distribuição de `low16`**,
   não mais o `mgr20_lo16`.
2. **Fechar a cadeia de dados de `0x00254C40` (offline, médio custo).** A ancoragem já está feita
   (§ *A resposta (4)*): é `vt+0xA0` da classe `0x00515B08`. Falta achar **quem invoca `vt+0xA0`** —
   um varrimento de `lwz rX,0xA0(vptr)` sobre o texto, no estilo do T6 da sessão de 2026-08-02
   (420 sítios por 88 aliases de TOC). Se o invocador for o leitor do WAD, a ligação
   `stream → 0x40030001` fica provada **sem boot**.
3. **`0x00254250` merece a mesma atenção que `0x0041F700`.** É o *outro* walk da tabela (`vt+0xD0`
   da mesma classe), tem a jump table de 33 entradas por `low16`, verifica `low16 < 0x21` (o push
   não verifica nada) e faz a subida de hierarquia por `subtag`. Comparar as suas guardas com as
   do push é barato — e agora sabe-se que são métodos irmãos do mesmo objecto.
4. **Auditar o bit `hflags & 4`** — quem o põe (`0x0039D764`, `0x0041BF00`, `0x00254954` com `|= 3`),
   quem o limpa, e porque é que `0x002545B0` o respeita e `0x0041F700` não. Passa a ser o alvo
   principal se E1 der A.

---

## Anti-padrões: o que esta sessão recusou fazer

- **Não publicou o primeiro enquadramento.** Comecei a triar "escritores de `mgr[+0x20]`" com a
  moldura do briefing; foi a ancoragem das vtables (T4) que mostrou que `+0x20` é campo do objecto
  e que o one-liner escreve outro campo. O relatório é o segundo enquadramento, não o primeiro.
- **Não repetiu os números da sessão anterior por estarem escritos.** Re-mediu 654 e 140 por
  caminho próprio (bateram) e declarou a divergência em `li -1`/`li 0` (98/216 vs 93/53) em vez de
  a esconder.
- **Não chamou "bug do jogo" à assimetria push/pop.** Continua a ser um mecanismo por medir.
- **Não corrigiu** o índice do push nem do pop, não editou `0x00254C40`, não estampou nenhum
  `low16`. Nenhum gate foi ligado.
- **Não editou nenhum `ppu_recomp_*.cpp`**, não correu builds, não correu boots ⇒ sem `pkill`, sem
  órfãos, sem logs no git. Scripts descartáveis em `/tmp/re_mgr20/`; o EBOOT e os JSON do
  `ghidra_out/` nunca saíram do disco.
- **Não declarou a parede fechada.** Continua **aberta** — com a causa da divergência localizada
  ao par de instruções e o discriminador de E1 reescrito.
