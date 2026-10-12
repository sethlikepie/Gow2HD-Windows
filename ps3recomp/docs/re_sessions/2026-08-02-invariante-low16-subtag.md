# Sessão de RE — o invariante `low16 == subtag` — 2026-08-02

Sessão conduzida com [`docs/RE_PLAYBOOK_GOW2.md`](../RE_PLAYBOOK_GOW2.md). Continuação
directa de [`2026-08-02-w0-semantica-indexacao.md`](2026-08-02-w0-semantica-indexacao.md)
(que por sua vez corrigiu [`2026-08-02-wall-4-walk-de-tipos.md`](2026-08-02-wall-4-walk-de-tipos.md)).

**Veredicto em uma linha:** **existem tipos com `low16 != subtag`** — dois atestados, um
deles verificado ao byte no ELF que saiu em disco (`low16=3, subtag=1` em `0x002B1068`).
O invariante que a sessão anterior declarou provado (`low16(instância) == subtag(do seu
gestor)`) **sobrevive** e não é o que aqui cai; o que cai é a **igualdade dos dois campos
dentro da mesma palavra**, que é exactamente a condição de que o push e o pop precisam para
indexar o mesmo gestor. Encontrado ainda o mecanismo que os separa, e uma medição negativa
que o fecha: **nenhum sítio no binário deriva `mgr[0x20]` do `w0` do próprio gestor**.

**Zero builds, zero boots, zero escritas fora deste ficheiro.**

---

## Pergunta (uma frase)

> **Existe algum tipo cujo `low16 != subtag`?** Se existir, o push `0x0041F700` e o pop
> `0x0041F924` indexam gestores diferentes para o mesmo nó, e o cursor `+0xC8` sobe sem descer.

---

## Âncoras

| âncora | valor |
|---|---|
| **ELF** | `../gow2-recomp/EBOOT.ELF`, sha256 `23cfd435…b1163b` (igual ao `MANIFEST.json` do `ghidra_out`) |
| **EA guest** | `0x002B0E78` (o teste `(3,1)`), `0x0041EDE4` + `0x002501A8` (criadores `vt+0x58`), `0x0025271C` (ctor do gestor, vtable `0x00515AA0`), `0x004117B0`/`0x0041E8A4`/`0x0041C558`/`0x0042C644`/`0x0042EC60` (carimbo do `low16`), `0x0041F700` (push), `0x0041F924` (pop), `0x0041FA90` + `0x0041ECD8` (registo) |
| **símbolo Ghidra** | `.opd.FUN_002b0e78 size=832`, `.opd.FUN_0041ede4 size=324`, `.opd.FUN_002501a8 size=324` |
| **tabela de tipos** | `0x00868D48` (`.bss`, SH26 `NOBITS` `0x00576F00+0x442908`) — **fora do `filesz`**, inacessível offline |
| **jump table por `low16`** | **`0x0025431C`, exactamente 33 entradas (0..32)**, TOC `r2-0x221C` |
| **OPD** | `0x00519570..0x00539178`, 16 257 slots, 13 004 funções distintas, `toc=0x00541178` |
| **tag de log** | nenhuma (a parede continua sem instrumentação própria) |

---

## Sondas de CONTROLO (corridas ANTES de qualquer achado)

O playbook exige-o, e nesta sessão **matou o primeiro resultado que obtive** (§C1).

| # | sonda | resultado |
|---|---|---|
| **C1** | **positiva, do emulador de imediatos** — o meu emulador tem de reproduzir o `puVar7[1] = 0x40010001` que o Ghidra decompila em `0x0041EDE4` | **Falhou 3 vezes** (0 sítios resolvidos) antes de ficar certo: perdia `r1` no prólogo, e tratava `rldicl rX,rX,0,32` (truncagem a 32 bits) como destruidora do valor. Só depois de dar `0x40010001` em `0x0041EE80` é que os números foram usados. **A primeira leitura — "0 violações" — era um artefacto do emulador e não foi publicada.** |
| **C2** | **negativa, dados** — procurar `(w & 0x0FFFFFFF) == 0x0030001` em todas as **189 898** palavras de dados do ficheiro | **0 ocorrências.** As 2 de `0x0010001` (`0x004FB7B0`, `0x004FB7D0`) foram **refutadas**: caem num bloco de permutação de bytes (`02030203 04050405 06070607 …`), não são descritores |
| **C3** | **negativa, forma** — as 235 palavras de dados com nibble de flags `0x4` e ambos os campos ≤ `0x20` | **todas refutadas**: são floats IEEE-754 (`0x40200000` = 2.5f, `0x40080000` = 2.125f). Nenhum descritor estático sobreviveu |
| **C4** | **cruzada, 4.º descodificador** — descodificar eu próprio, dos bytes do ELF, as máscaras do push e do pop | `0x0041F78C 540913BA rlwinm r9,r0,2,14,29` (**low16**·4) e `0x0041F9B0 540914BA rlwinm r9,r0,2,18,29` sobre `0x0041F9A8 rldicl r0,r0,48,52` (**subtag**·4). **Bate com os 3 descodificadores anteriores** |
| **C5** | **cruzada com o in-boot** — a jump table por `low16` em `0x0025431C` tem quantas entradas? | **exactamente 33 (0..32)**; a entrada 34 já é código (`0x002543A0` = o 1.º handler). Bate com as *26 entradas, tags 0..32* medidas in-boot e registadas no ROADMAP ✅ |
| **C6** | **negativa, tooling** — o `--grep` do `ghidra_lookup.py` faz `break` por função (achado da sessão anterior) | reconfirmado; **todas as contagens desta página vêm de varrimento próprio do ELF ou de `json.load(decompiled.json)` sem `break`** |

**C1 é a lição desta sessão**, e é a mesma da Fase 20 e da sessão anterior: o meu primeiro
número era `0 violações`, e estava errado por causa de duas instruções PPC mal modeladas.
Sem o controlo positivo, teria fechado a sessão com a conclusão **oposta** à verdadeira.

---

## Ferramentas usadas (comando + rc + o que devolveu)

Oráculo: `../gow2-recomp/ghidra_out` (13 143 funções, 13 096 com C). **Não regenerado.**
Ferramentas próprias em `/tmp/re_desc/` (descartáveis, nada commitado; o EBOOT nunca sai de lá).

| # | ferramenta | rc | devolveu |
|---|---|---|---|
| T1 | parser ELF64 PPC BE próprio (PH + SH + `vaddr→offset`) | 0 | PH0 `0x00010000+0x4FC7E0` (texto+rodata), PH1 `0x00510000+0x66EA4` `memsz=0x4A9808`; **SH26 `NOBITS` `0x00576F00+0x442908` contém `0x00868D48`** |
| T2 | varrimento de dados por assinatura de `w0` | 0 | ver C2/C3 — **nenhum descritor estático** |
| T3 | localizador de OPD (pares `(code, 0x00541178)`) | 0 | `0x00519570..0x00539178`; permitiu resolver a **vtable `0x00515AA0` slot a slot** |
| T4 | grafo de `bl` do binário inteiro | 0 | 41 245 sítios, 5 691 alvos; **45/98 ctors-base alcançados por `bl` directo, 69 sítios** |
| T5 | backscan simbólico de `r4` nesses 69 sítios | 0 | **0 constantes** — o descritor é sempre encadeado de um parâmetro acima |
| T6 | emulador linear de imediatos (regs + *spill slots* de `r1` + TOC), por prólogo | 0 | 140 sítios `oris rA,rS,0x4000`; **5 resolvidos**; só **2** são `w0` de objecto |
| T7 | extractor de `w0` literal nas 104 funções com `0xdeadbeef` | 0 | **2** com constante: `0x40010001` e `0x40040004` |
| T8 | varrimento de `stw rX,0x20(rA)` + backscan da fonte | 0 | **654 sítios; 0 alimentados por extracção de campo do `w0`** |
| T9 | leitor da jump table `0x0025431C` | 0 | 33 entradas, 4 handlers distintos (ver §*A tabela de 33*) |

---

## A resposta: **SIM, existem — pelo menos 2 tipos, e o mecanismo está identificado**

### Violação #1 — atestada ao byte no binário original

`0x002B0E78`, dentro de `.opd.FUN_002b0e78 size=832`, testa **os dois campos da mesma
palavra**, a partir do **mesmo registo `r9`**:

```
0x002B1068  5520043E   rlwinm r0,r9,0,16,31    ; r0 = w0 & 0xFFFF        (low16)
0x002B106C  2F800003   cmpwi  cr7,r0,3         ; low16 == 3
0x002B1074  79208522   rldicl r0,r9,48,52      ; r0 = (w0 >> 16) & 0xFFF (subtag)
0x002B1078  2F800001   cmpwi  cr7,r0,1         ; subtag == 1
```

C do Ghidra, para o mesmo sítio:

```c
if (((*puVar7 & 0xffff) == 3) && ((*puVar7 >> 0x10 & 0xfff) == 1)) { … }
```

⇒ **o jogo procura activamente um objecto com `low16 = 3` e `subtag = 1`.** Um ramo que
chama `_opd_FUN_002bb064`, não um `assert`. *(offline-unit — 2 descodificadores)*

### Violação #2 — a medida in-boot, agora explicada

`w0 = 0x40030001` (parede 1, ROADMAP linha 200): `low16 = 1`, `subtag = 3`. **É a
transposta da #1.** Entrada de outra sessão; não a re-medi.

### Os únicos dois `w0` que o binário materializa são DIAGONAIS

Das **104** funções com a impressão digital `= 0xdeadbeef` (o campo `+0x14` de todo objecto
desta hierarquia), só **2** escrevem um `w0` literal — e são o *mesmo* método
(`vtable +0x58`, "criar objecto", chamado pelo registador `0x0041FA90`) de dois gestores:

| função | `w0` | subtag | low16 | |
|---|---|---|---|---|
| `0x0041EDE4` (gestor da vtable `0x00515AA0`) | `0x40010001` | `0x001` | `0x0001` | **diagonal** |
| `0x002501A8` (gestor irmão) | `0x40040004` | `0x004` | `0x0004` | **diagonal** |

Verificado ao byte em `0x0041EDE4`: `lis r0,1` → `ori r0,r0,1` (= `0x00010001`, a palavra
do descritor) → `rlwinm r9,r0,0,16,31` | `rlwinm r0,r0,0,4,15` → `or` → **`oris r0,r0,0x4000`**
→ `stw r0,0(r5)` com `r5 = obj+4`. É o ctor-base (`w0 = desc[0] & 0x0FFFFFFF | 0x40000000`)
compilado *inline*, com o descritor como temporário de pilha.

**Ou seja: o objecto nasce diagonal, e as duas violações acima são objectos que
DEIXARAM de o ser.**

### O mecanismo que os separa — e a medição negativa que o fecha

```
h+0x04 = w0        subtag := desc[0] bits 16..27   (do ctor, NUNCA reescrito)
                   low16  := desc[0] bits  0..15   (do ctor)  ->  DEPOIS REESCRITO
```

O `low16` é reescrito por uma família de *one-liners*, um por classe de gestor
(`0x004117B0`, `0x0041C558`, `0x0041E8A4`, `0x0042C644`, `0x0042EC60`), todos byte a byte
idênticos:

```
80030020   lwz  r0,0x20(r3)     ; r3 = o GESTOR   -> mgr[+0x20]
38600001   li   r3,1
B0040006   sth  r0,6(r4)        ; r4 = o NÓ       -> low16(nó) := (u16)mgr[+0x20]
4E800020   blr
```

E o slot da tabela é o **outro** número: `0x0041FA90` e `0x0041ECD8` fazem ambos
`tab[(mgr.w0 >> 14) & 0x3FFC] = mgr`, isto é **`tab[subtag(mgr.w0)]`**.

> **Medição decisiva (T8):** varridos os **654** `stw rX,0x20(rA)` do binário inteiro, com
> backscan da definição da fonte. **Zero** são alimentados por uma extracção de campo do
> `w0` (`rldicl …,48,52`, `rlwinm …,0,16,31`, `lhz …,2(…)`). *(offline-unit)*
>
> ⇒ **Nada no binário liga `mgr[0x20]` a `subtag(mgr.w0)`.** São dois números postos por
> mecanismos independentes. O `mgr[0x20]` nasce a `0xFFFFFFFF` (`0x0025271C`:
> `param_1[8] = 0xffffffff`, e 93 sítios `li rX,-1; stw rX,0x20(rA)` no binário).

**Consequência directa e suficiente:** basta que um objecto seja criado a partir de um
descritor de classe `A` e carimbado por um gestor `B ≠ A` para ficar com
`subtag = A`, `low16 = B`. Isso produz **exactamente** `(low16=3, subtag=1)` — a violação #1 —
e **exactamente** `(low16=1, subtag=3)` = `0x40030001` — a violação #2. As duas observações,
que pareciam contraditórias, saem do **mesmo** mecanismo. *(offline-unit)*

### A tabela de 33 — porque é que os dois campos vivem no MESMO espaço

`0x00254250` despacha por `low16` através de uma jump table em **`0x0025431C`** com
**exactamente 33 entradas (0..32)**; a entrada 34 já é o corpo do 1.º handler
(`0x0025431C + 33*4 = 0x002543A0`). Distribuição:

| handler | tags |
|---|---|
| `0x00254414` | **1** |
| `0x002543E0` | **4, 7, 20** |
| `0x00254460` | **9, 16, 17, 21, 25, 32** |
| `0x002543A0` (default) | os restantes 23 |

Os tags distinguidos batem com o grafo já conhecido (pai `1`; filhos
`3,4,9,15,16,17,18,20,21,23,25,32`): **`9,16,17,21,25,32` são todos filhos conhecidos e
partilham o mesmo handler**. E `0x002B0FB4 lhz r0,2(r30)` + `0x002B1008 cmpwi r0,32` põe o
mesmo limite `low16 ≤ 32` noutro sítio. Os valores comparados contra `subtag` no C
(`0,1,3,7,0xA,0xB,0xC`) caem todos dentro desse mesmo alcance.
⇒ **um só espaço de índice, 0..32; dois campos que o habitam; nada que os force a coincidir.**

---

## Qual dos dois lados usa o campo errado

À luz da semântica agora medida, **o push `0x0041F700`**:

| | guarda | índice da tabela | coerente? |
|---|---|---|---|
| **push** `0x0041F700` | `subtag(filho) != subtag(pai)` | **`tab[low16(filho)]`** | ❌ **guarda num campo, indexa no outro** |
| **pop** `0x0041F924` | `subtag(filho) != subtag(pai)` | `tab[subtag(filho)]` | ✅ mesmo campo dos dois lados |
| **7 irmãos** (`0x00256B1C`, `0x0039F4D8`, `0x0041CF6C`, `0x00420254`, `0x0042A660`, `0x0042CCE8`, `0x004302DC`) | `low16(filho) != subtag(dono)` | `tab[low16(filho)]` | ✅ mesmo campo dos dois lados |

As **7 instanciações irmãs** do mesmo *template* despacham para `tab[low16(filho)]->vt[0x40]`
— o **mesmo** slot que o push — mas guardam com `low16(filho) != subtag(dono)`. O push é o
**único** membro da família que compara `subtag(filho)` e depois indexa por `low16(filho)`.

**Não afirmo que isto seja um bug do jogo** (regra 13 do playbook). Afirmo o que está medido:
a assimetria é **interna a `0x0041F700`**, e a família inteira à volta dele é auto-consistente.
Se o push estivesse a executar sobre nós já diagonais, a assimetria seria invisível — que é
provavelmente o que acontece na consola.

---

## O que fica REFUTADO por medição

1. **"Os descritores estáticos estão na `.rodata` e enumeram-se offline"** (Q4 do *próximo
   experimento* da sessão anterior). **FALSO / estruturalmente impossível.** 189 898 palavras
   de dados varridas: 0 ocorrências de `0x0030001`; as 2 de `0x0010001` e as 235 candidatas
   com nibble `0x4` refutadas (permutação de bytes / floats IEEE). Dos 98 ctors-base, 45 têm
   `bl` directo (69 sítios) e **nenhum** com `r4` constante — o descritor é sempre encadeado.
   Só **2** dos 140 `oris …,0x4000` do binário têm o descritor materializado inline. **Os
   descritores vivem em memória de runtime, tal como a `tab`.** *(offline-unit)*
2. **"`0x40030001` já é, por si, uma violação do invariante da sessão anterior."** **FALSO.**
   O invariante dela é `low16(instância) == subtag(do seu gestor)`. `0x40030001` tem
   `low16 = 1` ⇒ despacha para `tab[1]`; o invariante só exige que o gestor registado no slot 1
   tenha `subtag = 1`. E há prova estática de que a família do slot 1 é diagonal: o seu
   `vt+0x58` (`0x0041EDE4`) fabrica objectos com `w0 = 0x40010001`. **O invariante da sessão
   anterior sobrevive intacto.** *(offline-unit)*
3. **"`low16 == subtag` dentro da mesma palavra"** — a igualdade de que o par push/pop
   precisa. **FALSO.** Violação atestada ao byte em `0x002B1068`–`0x002B1078`. *(offline-unit)*
4. **"`mgr[0x20]` é o `subtag` do gestor, logo os dois campos coincidem por construção."**
   **REFUTADO por varrimento exaustivo:** 654 escritas a `+0x20`, **zero** derivadas de um
   campo do `w0`. *(offline-unit)*
5. **"O `w0` nasce sempre diagonal, logo nunca diverge."** Meia-verdade: nasce diagonal
   (`0x40010001`, `0x40040004`), mas o `low16` é **reescrito depois** por
   `sth r0,6(r4)` a partir de `mgr[+0x20]`. A diagonalidade **não é preservada**. *(offline-unit)*

---

## Veredicto: **ABERTO — mas com o alvo mudado e reduzido a um só número**

A parede 4 continua **aberta**. O que mudou:

- a pergunta *"qual dos dois campos é o certo?"* está **respondida**: são ambos legítimos,
  e o defeito não é de indexação mas de **coerência entre um campo herdado do descritor e
  um campo carimbado pelo gestor**;
- o alvo deixou de ser *"o bit `hflags & 4`"* (o caminho B da sessão anterior) e passa a ser
  **`mgr[+0x20]`**: quem lhe escreve o valor real, e se esse valor é ou não o slot em que o
  gestor se registou;
- a Q3/Q4 offline (*enumerar os descritores estáticos*) está **fechada como impossível** —
  poupa a próxima sessão de a repetir.

---

## O que esta sessão NÃO afirma

- **Não** afirma ter medido um único `w0` numa corrida. Tudo aqui é estático. `0x40030001`
  continua a ser **entrada** de outra sessão, aceite como dado e não re-medida.
- **Não** afirma ter enumerado *todos* os tipos. Enumerou os **2** cujo `w0` o binário
  materializa e **provou que os restantes são inacessíveis offline**. "26 entradas, tags 0..32"
  continua a ser uma medição in-boot de outra sessão; o que esta sessão acrescenta é a
  confirmação **estática e independente** do alcance `0..32` (jump table de 33 entradas).
- **Não** afirma que o jogo original tem um bug. Afirma que `0x0041F700` é o **único** membro
  da sua família que guarda por `subtag` e indexa por `low16`, e que nada no binário força
  `low16 == subtag`. Um jogo que saiu em disco não fazia 27 M de iterações no boot — o mais
  provável continua a ser que a **nossa** corrida esteja a alimentar o walk com nós que a
  consola nunca lhe entregaria.
- **Não** identifica quem escreve o valor real de `mgr[+0x20]`. Provou só o que **não** o
  escreve (nenhuma extracção do `w0`). Os candidatos não-constantes ficam por triar.
- **Não** afirma que `tab[3]` está preenchido, nem que `tab[low16]` e `tab[subtag]` são
  simultaneamente não-nulos numa corrida. A `tab` está em `.bss` — continua inacessível offline.
- **Não** afirma que o bit `hflags & 4` deixou de importar; afirma que deixou de ser o
  **próximo** alvo, porque o caminho A (invariante partido) ganhou evidência estática.
- **Não** toca em pixels, shaders ou `SHADERSRC`.
- **Não** reabre nada da lista *"O que já está ELIMINADO por medição"* do ROADMAP.

---

## O que a sonda E1 ainda precisa de medir

A sonda **continua necessária**, mas o discriminador central **mudou**. O
`mgr_subtag == low16` da sessão anterior deixou de ser o teste interessante (é o invariante
que sobrevive); o teste passa a ser sobre o **próprio nó** e sobre `mgr[+0x20]`.

`PS3_TYPEWALK_TRACE=1`, OFF por default, no push `0x0041F700`, ~200 linhas. Por cada filho:

| campo | expressão | porquê |
|---|---|---|
| `w0` | `vm_read32(child_h + 4)` | a palavra crua |
| `low16` / `subtag` | `w0 & 0xFFFF` / `(w0>>16)&0xFFF` | **o discriminador**: contar quantos filhos têm `low16 != subtag` |
| `flags` | `w0 >> 28` | `0x4` = ctor-base puro; `0x8`/`0xC` = passou pelo clone `0x00252ED8` |
| `tab[low16]`, `tab[subtag]` | `vm_read32(0x00868D48 + idx*4)` | os dois gestores do push e do pop |
| `mgr20_lo16` | `(u16)vm_read32(tab[low16] + 0x20)` | **novo e central**: o carimbo tem de bater com o `low16` observado |
| `mgr_subtag` | `(vm_read32(tab[low16] + 4) >> 16) & 0xFFF` | o invariante da sessão anterior, que deve continuar de pé |
| `cursor_push` / `cursor_pop` | `(int8_t)vm_read8(tab[low16] + 0xC8)` / `… tab[subtag] + 0xC8` | **os dois cursores, não um** — é a assimetria a olho |
| `hflags` | `vm_read16(child_h + 8)` | o bit `4` que `func_002545B0` respeita e o push não |

**O que a sonda tem de decidir agora:**

| **A — a assimetria é real nesta corrida** | **B — não é** |
|---|---|
| aparecem filhos com `low16 != subtag` | **100%** dos filhos são diagonais |
| `tab[low16] != tab[subtag]`, ambos não-nulos, para ≥1 filho | `tab[low16] == tab[subtag]` sempre |
| **`cursor_push` sobe monotonicamente enquanto `cursor_pop` de outro gestor desce** | os dois cursores são do mesmo gestor e fecham em `[-1, 32)` |
| `mgr20_lo16 == low16` (o carimbo é coerente) mas `subtag` é de outra classe ⇒ o fix é **a montante**: quem carimbou o nó com um gestor que não é o da sua classe | ⇒ a explosão é combinatória; volta a valer o alvo `hflags & 4` |

**Nota importante para quem implementar:** o cursor `+0xC8` **nasce a `0xFF` = −1**, não a 0
(`0x0025271C`: `*(undefined1 *)(param_1 + 0x32) = 0xff`, byte `0xC8`). O pop já trata
`cVar1 < 0` devolvendo `iVar6 = 0`. **Um cursor a `-1` é "vazio", não é erro** — a sessão
anterior não tinha isto, e um trace que o leia como erro dá falso positivo.

**E3 — sonda de CONTROLO, obrigatória e primeiro.** Continua sem tag de log. Uma contagem de
entradas em `0x0041F700` que **tem** de aparecer, senão um silêncio não distingue "não passou
aqui" de "a sonda não foi compilada". Foi assim que as Fases 7–10 mediram uma string
inexistente — e foi assim que, **nesta** sessão, o primeiro resultado ("0 violações") quase
virou facto.

---

## Próximo experimento para quem retomar

1. **E1 acima, com E3 primeiro.** Uma corrida decide A vs B, e agora mede os **dois** cursores.
2. **Triar os escritores de `mgr[+0x20]` (offline, barato, ~1 h).** Dos 654 `stw rX,0x20(rA)`,
   ficam de fora os 93 `li -1` do ctor e os 53 `li 0`. Restam ~500, dos quais os candidatos
   por região do sistema de tipos já estão listados no varrimento T8. Procurar o que escreve o
   índice real do gestor: se for um **contador global de registo** (ordem de registo), então
   `mgr[0x20] != subtag(mgr.w0)` por construção e a parede fecha-se estaticamente.
3. **Auditar o único membro assimétrico.** Comparar `0x0041F700` instrução a instrução com
   `0x0041CF6C:139` (o irmão que guarda por `low16`). Se a diferença for uma só instrução
   (`rlwinm …,0,16,31` vs `rldicl …,48,52` na guarda), o achado passa a ser bem localizado —
   **mas o fix continua a ser a montante, nunca divergir do PPC no push** (regra 13).
4. **Só se A falhar:** voltar ao bit `hflags & 4` — quem o põe, quem o limpa, e porque é que
   `func_002545B0` o respeita e `func_0041F700` não.

---

## Anti-padrões: o que esta sessão recusou fazer

- **Não publicou o primeiro resultado.** O emulador dava "0 violações" e estava errado; só
  passou a valer depois de reproduzir `0x40010001` no sítio onde o Ghidra o decompila.
- Não chamou "tabela de descritores" aos 2 sítios de `0x0010001` nem às 235 palavras com
  nibble `0x4` — foram refutados (permutação de bytes, floats) antes de entrarem em qualquer
  contagem.
- Não "corrigiu" a guarda do push nem o seu índice. Sem E1 isso seria divergir do PPC original
  com base em inferência estática.
- Não declarou o invariante da sessão anterior derrubado — ele **sobrevive**; o que cai é uma
  igualdade **diferente**, e a página di-lo explicitamente para o ledger não inflacionar.
- Não editou nenhum `ppu_recomp_*.cpp`, não correu builds, não tocou em
  `../gow2-recomp/recomp_macos_v3_e2e*` (outra sessão a liftar em paralelo).
- Não correu boots ⇒ sem `pkill`, sem órfãos, sem logs no git. Scripts descartáveis em
  `/tmp/re_desc/`, nada commitado. O EBOOT e os JSON do `ghidra_out/` nunca saíram do disco.
- Não declarou a parede fechada. Continua **aberta** — com um alvo mais estreito.
