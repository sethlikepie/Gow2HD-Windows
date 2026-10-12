# E110 — a "final da intro" console vs nós, e o filho de classe 15 que falta ao hero

**Data:** 2026-09-02 · Nível: **in-boot** (nós) + **oráculo RPCS3** (janela, ELF original e skip-intro).

## 1. A final da intro NÃO bate exactamente (medido)

Oráculo com o **EBOOT original** (`oracle_intro.py`, singleton do movie player em `*(0x540054)`, contador `0x592668`):

| guest | st620 | contador 0x592668 | o que é |
|---|---|---|---|
| 5.0 s | 0 | **1** | pediu o vídeo |
| 7.5 s | **1** | 1 | a decodificar |
| 8.5 s | **11** | **2** | play |
| 21.0 s | 0 | **3** | vídeo acabou |
| 24.0 s | 0 | **4** | **sai do laço** (`func_000CE0A0` espera `*0x592668==4`) |

Nós (`[MOVIEFSM]`, EBOOT recompilado, caminho FORCE/EOS): st620 `0xFFFFFFFF→0→1→**3**→11→0`, e **não seguimos o contador `0x592668`** (0 ocorrências no log). Diferenças:
1. o console **avança o contador 0→1→2→3→4** pelo movie player real; nós saltamos a espera com `PS3_MOVIE_EOS`/FORCE e nunca chegamos ao `4` pela via natural;
2. temos um estado **3** extra que o console (amostrado a 0.5 s) não mostra entre 1 e 11;
3. o console decodifica o `.m2v` de verdade (st620=11 com play); nós fazemos `StartSeq→REPLAY-NOPIC` e forçamos EOS.

Ou seja: a intro **progride nos dois** (wall A vencida), mas o **mecanismo do fim** diverge — o nosso é HLE forçado, não a fila natural do contador. Não bate byte-a-byte.

## 2. Onde [G] está agora (E100–E108): o filho de classe 15 do R_Hero01

Com E93/E96/E99/E103 removidas as paliativas, a thread principal chega ao construtor por-objecto natural
(`func_0009EC0C`) e pára a construir os filhos do R_Hero01. Cadeia medida (sondas E101/E102/E107/E108 + `oracle_bp.py`):

- `func_0009EC0C` faz, em `0x9F040`, um icall `mgr->vt[0x48]` (produto corrente do gestor da classe) e passa o
  resultado a `func_000E1B78` (lookup na pool por chave `obj+0x9c`). **No console** devolve um objecto real
  (`0x31543990`); **em nós** o icall devolve **0** e `func_000E1B78` devolve **0** (E108: `antes produto=0 → depois alocado=0`).
- A seguir, `0x9F0C8` lê o filho de classe 15 em `parent+0xB8+idx*4` (idx=9 do gestor `*(0x53AAD4)`), obtém **0**,
  e passa `r29=0x3F7FFFEC` (o float `0.99…` lido como ponteiro) a `func_00283328` → `func_002DAF04`/`002DACCC`
  percorrem listas a partir de `0x1030`/`0x2410` para sempre (frame preso). No console o mesmo sítio recebe um objecto válido.

**Causa (medida, não inferida):** o gestor cujo `vt[0x48]` é lido tem cursor < 0 / sem produto corrente no momento —
a mesma classe de problema da parede [F], um nível mais fundo (por-objecto do hero, não a raiz do WAD). O parent do
objecto (`0x407C7670`, w0=`0x40030001`) existe e tem as listas `+0xB8` preenchidas nos dois lados, mas o slot da
classe 15 do parent está a 0 em nós.

## 3. Estado das paliativas removidas (resumo E93–E103)

| Paliativa (de Julho) | Fix | Default agora |
|---|---|---|
| stub da thread AUTO_LOAD | E93 | corre |
| `cellSaveDataAutoLoad2` NODATA→OK | E93 | CBRESULT (=console) |
| `freelist REPLENISH` + reuse no B71 icallA | E96 | natural |
| `393E0` HLE-lite | E99 | construtor completo |
| poda E6 (`PAREDE1-E6-SUBTAG-SKIP`) | E103 | OFF |

Cada uma destravou um passo; a parede [G] é literalmente a pilha destas paliativas. O próximo é o §2:
porque é que o slot de classe 15 do parent do hero fica a 0 (que gestor não tem produto corrente, e porquê).

## 4. `0x0052F3F0` NÃO transita no console (medido, corrige a premissa)

`oracle_state.py` (janela, EBOOT skip-intro) amostra `app+0x460C` — o OPD do handler por frame — a cada 2 s de guest:

    10s..60s: opd=0x0052F3F0 code=0x002B2DD0  (constante, sem uma unica transicao)

**`0x0052F3F0` é o tick eterno do loop principal (`func_002B2DD0`), não um estado que muda** — nem no console nem em
nós. A premissa "o console transita de 0x0052F3F0 para o próximo estado" é **falsa**: não há troca de OPD.

O sinal de progresso é o que o tick faz **por dentro** — e essa é a diferença medida:

| guest (console) | pilha interna do tick | subsistema |
|---|---|---|
| 10 s | `…000B3804 000B7388 002B2EC8` | handler da intro / B71 |
| 12–14 s | `…002486B8 002931B4 00423AFC` | **carregar WADs** (`func_00423AB4` dispatcher) |
| 16–60 s | `…002B22BC 00423AFC 004245AC 002B26BC` | loader/scheduler a iterar (a construir o mundo) |
| picos (38 s, 42 s) | `002584D4…`, `00165A70…` | outros subsistemas por frame |

O console **avança dentro do tick** (intro → WAD loads → construção por-objecto); **nós ficamos presos dentro do mesmo
tick**, no `func_002DACCC` do §2. Portanto o "começar por aí" era: comparar o **interior** do tick 0x0052F3F0, não uma
transição de OPD que não existe. Feito — a divergência é a do §2 (o filho de classe 15 do R_Hero01).

> **§5 REFINADO/RETRATADO por §6 (E109).** No binário TOTALMENTE natural (E90–E108) a única chamada de
> `func_0009EC0C` vê **TABLE[22] cursor=1** com produto real — NÃO −1. O `−1` do §5/E108 foi uma captura de
> um instante/invocação diferente (o endereço do alocador e o timing variam entre corridas). «Gestor sem produto
> corrente» **não** é a causa confirmada; ver §6.

## 5. O gestor identificado: é o TABLE[22] (contexto de WAD), a doença da parede [F]

Estático: em `func_0009EC0C`, o `vt[0x48]` de `0x9F040` lê `mgr = *(r26+0x58)`, e `r26 = *(TOC-0x6740) = *(0x0053AA38)
= 0x00868D48` — **a própria base da TABLE**. `0x58 = 22*4`, logo `mgr = TABLE[22]`, o gestor de **contexto de WAD**.

Medido nos dois lados no ponto da construção por-objecto do R_Hero01:

| | mgr (TABLE[22]) | cursor | stack[0] | vt[0x48] |
|---|---|---|---|---|
| **console** (`oracle_mgr58.py`) | 0x300C3D88 | **2** | 0x30638108 (R_LglScA ctx) | produto real (0x314FF2D8, w0=0xC0160016) |
| **nós** (E108) | — | **< 0** | — | **0** |

Portanto o «gestor sem produto corrente» é **TABLE[22]**: o console mantém-no empurrado (cursor=2, três contextos de WAD
na pilha) durante toda a construção por-objecto; em nós ele já foi despejado para −1. É **exactamente** a doença da
parede [F] — o cursor de escopo do gestor — um nível mais fundo (construção por-objecto do hero, não o registo da raiz).
O fix natural: manter os contextos de WAD empurrados em TABLE[22] enquanto o construtor por-objecto corre, sem forçar.

## 6. E109 (binário natural) — o cursor está BOM; o objecto que chega ao DACCC é que está malformado

`patch_e109_t22_at9ec0c.py` lê `TABLE[22]` (= `*(0x868D48+0x58)`) e o cursor à entrada de `func_0009EC0C`, no
binário com TODAS as paliativas já naturais (E90–E108). Medido, 1 corrida:

    [T22] mgr=TABLE[22]=0x400C3D88 cursor=1 prod=0x414FF2D8 vt48=0x005170B8   (uma unica chamada)

- **cursor=1 (>=0), produto real** — o gestor TEM produto corrente. Refuta o §5 (que media −1).
- Mesmo assim o boot acaba no `func_002DACCC` a girar com `head=0x1178, key=0x50` (endereço minúsculo = objecto nulo
  + deslocamentos). No **console** o mesmo `func_002DACCC` percorre listas **circulares bem formadas**
  (`oracle_daccc.log`: `head=0x31543EA4 … circular=sim`).

**Conclusão medida:** o problema **não** é o cursor de escopo (esse está correcto no binário natural). É que o objecto
por-classe que `func_0009EC0C` constrói e passa a `func_00283328` fica **malformado** (o filho/lista da classe 15 não
liga), e o `func_002DACCC` percorre lixo. O alvo natural é *porque é que a lista do objecto por-classe fica vazia/nula*
neste caminho, não o cursor do gestor. A não-determinância entre corridas (endereços do alocador) exige uma comparação
lado-a-lado determinística com o oráculo neste ponto exacto — é o próximo passo.

**Estado do "remover tudo o que é forçado":** as paliativas grandes já estão naturais por default (E90 fill do WAD,
E93 AUTO_LOAD/save-data, E96 icallA, E99 393E0, E103 poda E6). Continua forçado: a intro (`PS3_MOVIE_EOS`/FORCE) e o
open F2B do WAD (FO falso + DONEFORCE). Derrubar todas as paredes até ao fluxo do RPCS3 é uma cadeia de investigação
por-parede; esta sessão derrubou cinco e mediu a seguinte.

## 7. Medição determinística nos dois lados (headless): o slot da classe 15 é objecto no console, float em nós

`oracle_bp.py --bp 0x0009F0BC` (headless, attach a 4 s) apanha o `lwz r9, 0x8(r11)` que lê o filho da classe 15 do
parent (`node = *(parent + idx*4 + 0xB8)`):

| | node (slot da classe 15) | interpretação |
|---|---|---|
| **Console** | **0x31540B34** | objecto real na heap guest |
| **Nós** (PRE283328) | **0x3F800000** | **float 1.0f** — não é ponteiro |

O parent no console (`0x307C7630`) tem, no array `+0xB8`, um **ponteiro de objecto** nesse índice; em nós o mesmo array
tem um **float**. Como o `func_002DACCC` a jusante trata `node-0x14` como cabeça de lista, no console percorre uma lista
válida e em nós percorre lixo (`head=0x1178`).

**Causa (medida, determinística):** o objecto da **classe 15 (MDLX)** do parent **não foi ligado ao slot** em nós — o
array `+0xB8` do parent tem um valor de outro campo (o float `1.0f`, provavelmente da matriz/transform do objecto) onde
devia estar o ponteiro do MDLX. Ou o MDLX nunca foi construído/anexado para este parent, ou o **layout do objecto
parent difere** (um campo float sobrepõe-se ao slot). Este é o próximo alvo natural: garantir que o MDLX é construído e
anexado ao slot `+0xB8[classe15]` do parent como no console — sem forçar, seguindo o construtor do jogo.

## 8. Os band-aids forçados que sobram (RSX/GCM) — RETRACTADA a ligação ao hero

> **RETRACTA a inferência anterior deste §8.** Media-se que os `ICALL-HEAP`/`ICALL-ASCII` apontam a `r3=0x0A050000`
> com `ctr=/wad`/`_mo`, e que `0x0A050000` **não está committado no console** (`oracle_readmem.py` → E01). Mas o passo
> seguinte mostra que **`0x0A050000` é o slot FIXO do contexto GCM/RSX do nosso runtime** (`GCM_CTX_EA 0x0A050000` em
> `libs/video/cellGcmSys.c`; já há nota `GoW2: r3=0x0A050000`). O call site é `func_0014E350` → `func_002F0DD0`, um
> **escritor de comandos do FIFO GCM** (16 iterações). Logo os `ICALL-HEAP/ASCII` são o **caminho RSX/GCM**, NÃO a
> construção do objecto do hero. A minha ligação «band-aids → objecto malformado do hero» estava ERRADA.

### Medições que ficam (§8):

No binário natural (E90–E108), durante o hero, ainda disparam TRÊS band-aids forçados (`e109a.log`):

| Band-aid | contexto |
|---|---|
| `[FREELIST-TAG-GUARD]` ×32 | `263040 entry head=0 -> abort r3=0` (aloca com free-list vazia/corrompida) |
| `[ICALL-HEAP] skip` ×25 | `ctr=0x2F776164 r3=0x0A050000` |
| `[ICALL-ASCII] skip` ×25 | `ctr=0x005F6D6F r3=0x0A050000` |

O ICALL-HEAP e o ICALL-ASCII apontam **sempre para o mesmo objecto** `r3=0x0A050000`, e o `ctr` (o método que
iam chamar) é **texto de caminho**: `0x2F776164` = `"/wad"`, `0x005F6D6F` = `" _mo"` (de `_movies`). Ou seja, **uma
string de caminho está escrita onde devia estar a vtable do objecto** — o objecto em `0x0A050000` tem, no seu word0, os
bytes de um path. O indirect-call lê esses bytes como `ctr` e nós **saltamos** (forçado).

**É a mesma classe de bug do megaprojecto** (a string `/r_p` lida como ponteiro) e liga-se ao **open F2B do WAD
forçado**: o FO falso escreve o path em memória guest (`pbuf`), e um objecto sobrepõe-se a essa região → a sua vtable
vira o path. O objecto malformado que o `func_002DACCC` percorre (§7) e o filho de classe 15 que fica float são a
jusante disto.

**Mapa do que falta para o fluxo natural (goal):** remover os band-aids exige fechar a raiz — a string de caminho a
sobrepor-se a um objecto. Alvos, por ordem: (1) o open F2B do WAD (FO falso + DONEFORCE + escrita do path) → dearchiver
natural; (2) confirmar que sem o path escrito o objecto `0x0A050000` deixa de ter word0=ASCII e o `ctr` deixa de ser
`/wad`; (3) então FREELIST-TAG-GUARD/ICALL-HEAP/ICALL-ASCII deixam de disparar e o MDLX liga-se ao slot como no console.
Isto é a próxima parede — medida e nomeada, não fechada nesta sessão.

## 9. O que fica separado, e o próximo passo natural (honesto)

Dois fios distintos, agora desconfundidos:
1. **Hero object (parede [G] real):** `func_0009EC0C` chega a um objecto malformado, filho de classe 15 é float
   (`0x3F800000`) em vez de objecto (`0x31540B34` no console) → `func_002DACCC head=0x1178` gira. Isto é o que trava o
   boot. Continua por caracterizar: **que objecto** recebe o float no slot da classe 15, e **quem** o escreveu.
2. **RSX/GCM FIFO (§8):** `r3=0x0A050000` é o nosso slot fixo de contexto GCM; os `ICALL-HEAP/ASCII` são o FIFO a
   chamar através dele com `+0xc` (callback de resize) por preencher. É lado-RSX, já conhecido, gated como diagnóstico.
   Não trava o boot (o loop continua a iterar).

**Próximo passo natural do fio 1:** caminhar a corrupção a montante — instrumentar o `func_0024C3EC` (o attach que liga
`obj` a `parent+0xB8+classe*4`) e ver se o attach da classe 15 (MDLX) para este parent alguma vez corre em nós, e se o
slot já tinha o float antes. É a técnica do megaprojecto (walking-a-corruption-upstream), aplicada ao attach por-classe.

## 10. E111 — REFUTA «o attach da classe 15 (MDLX) falta» e corrige o rótulo

`patch_e111_attach_probe.py` conta `func_0024C3EC` (o attach `parent+0xB8+slot`) por classe do filho:

| | attaches classe 15 | outras |
|---|---|---|
| **console** (`oracle_attach.py`) | **2** | 3:48 17:99 25:151 |
| **nós** (E111) | **14** | 3:99 4:19 17:262 25:396 … |

**Refuta a §7/§9:** nós **atacamos** objectos de classe 15 (mais que o console, não menos). «MDLX não anexado» está
ERRADO. E o rótulo «filho de classe 15» era inferência não verificada: o índice de slot lido em `0x9F0A4` é
`idx = *(*(0x53AAD4)+0x3C) = 9` — **não** o número da classe. O `node` malformado está no slot-índice 9 do parent, cuja
classe não confirmei.

**O que fica sólido (medido, determinístico):**
1. `0x0052F3F0` é o tick eterno (não transita — §4).
2. Cinco paliativas forçadas removidas (naturais por default): E90/E93/E96/E99/E103.
3. O boot gira em `func_002DACCC head=0x1178` (objecto near-null) depois do hero; no console percorre listas circulares.
4. `0x0A050000` é o nosso slot fixo de contexto GCM (RSX), desligado disto (§8).

**O que NÃO está fechado:** o objecto malformado exacto que chega ao `func_002DACCC` e quem o produz. As capturas
single-shot são não-determinísticas (endereços do alocador variam) e já me levaram a dois rótulos errados (cursor −1;
classe 15). O caminho certo é uma corrida instrumentada determinística que siga UM objecto do seu alloc ao
`func_002DACCC`, comparada com o mesmo objecto no oráculo — trabalho para a próxima sessão.

## 11. E112 — VERIFICADO: slot 9 É a classe 15 (MDLX); a §10 sobre-corrigiu

`oracle_ee70.py` lê a tabela classe→slotindex `*(0x53EE70)`:

    classe 15 -> slotindex 9   <== exactamente o slot que func_0009EC0C lê (idx=9)

Portanto o `node` malformado ESTÁ no slot da **classe 15 (MDLX)** — o rótulo do §7 estava **correcto**, e a retractação
do §10 (que o dizia «não verificado») **sobre-corrigiu**: baseei-a na contagem GLOBAL do E111 (14 attaches de classe 15),
mas essa contagem não diz nada sobre ESTE parent. A retractação-da-retractação (skill): a medição do E111 fica (atacamos
classe 15 em geral), a inferência «logo não é a classe 15 aqui» cai.

**Alvo verificado, determinístico:**
- slot 9 = classe 15 (MDLX) — tabela `*(0x53EE70)`.
- hero-parent: slot 9 = objecto no console (0x31540B34), **float 1.0f em nós** (§7).
- atacamos classe 15 a OUTROS parents (14×, E111), mas **não a este** — o MDLX deste parent específico não é anexado.

**Próximo passo (natural, determinístico):** descobrir porque é que o MDLX é anexado a outros parents mas não a este —
que parent é (a sua classe/w0), e se a construção do seu MDLX foi saltada. Instrumentar `func_0024C3EC` filtrado ao
parent do hero (por w0/ordem) e comparar com o oráculo no mesmo parent.

> **§12 RETRACTADO por §22:** os «2300+ frames» são a **thread RSX** a re-apresentar; a **thread principal ESTÁ presa**
> no callback do comando 25 (`func_00040560`). Mantido como registo; não construir sobre isto.

## 12. E112 — REFRAME: o recompilado NÃO está congelado; renderiza frames com draws

Ao olhar o **fim** da corrida natural (não só as linhas DACCC): o último terço do log é dominado por
`[SPUJOB] spu job returned cleanly` (×433), `[cellGcmSys] SetFlipCommand` (**2468 no total**) e
**`[RSX metal] guest_present f=N draws=1 has_vb=1 has_ib=1`** — o contador de frame do RSX vai a **2300+**.

**O boot NÃO está preso num loop DACCC.** O main loop está vivo e **apresenta frames com draws reais** (vertex+index
buffer). O `[STATE] frame=1` era enganador: `0x592668` é o contador da INTRO (que forçamos), não o frame do render. As
chamadas DACCC/DEFLOOP/FREELIST-TAG-GUARD são de uma **thread de construção em background** (provavelmente o stage load
pós-AUTO_LOAD) que gira sem terminar, ENQUANTO o main loop renderiza.

**Estado real, medido:** intro → WADs → construção → **loop de render a apresentar 2300+ frames com 1 draw/frame**. A
thread de construção do stage não fecha (gira em DACCC), por isso o conteúdo fica mínimo (1 draw/frame) em vez do menu
cheio. Isto é MUITO mais perto do fluxo do RPCS3 do que «congelado no frame 1» — é um loop de render vivo à espera que a
construção do stage termine.

**Corrige a narrativa desta sessão:** não há «parede que congela»; há uma thread de construção que não completa. O alvo
passa a ser: porque é que a thread de construção (DACCC/DEFLOOP) não fecha — que objecto/lista fica sem terminar — em vez
de «o boot está preso». `parent+0xB8` do parent do hero tem um float (`3F800000`) no slot 20, e o node do slot 15 aponta
a um objecto (0x4156FE74) cuja vtable pode estar por preencher (a verificar no próximo passo).

## 13. Raiz determinística da thread de construção: ponteiro tagged/não-inicializado em func_002DAF04

Imediatamente antes de cada DACCC (`e112b.log`), o padrão é estável:

    [vm] UNCOMMITTED read8 access 0x8000003F  ra=func_002DAF04+0x1AE0
    [vm] OOB access 0xE0000044
    [vm] OOB access 0xE8000044
    [vm] OOB access 0xF0000044
    [vm] OOB access 0xF8000044   (passo 0x8000000)
    [DACCC] head=0x00001018 ...

`func_002DAF04` (a máquina de estados da construção) lê **`0x8000003F`** — bit31 setado = o **tag de free-list /
não-inicializado** do megaprojecto — e trata-o como ponteiro, iterando por endereços **OOB** (`0xE0000044`…, acima da
VM guest). O DACCC a jusante recebe um `head` minúsculo (`0x1018`/`0x1178`) por desreferenciar esse lixo.

**Raiz (determinística, estável entre corridas):** a estrutura que `func_002DAF04` percorre tem um **ponteiro
tagged/não-inicializado (`0x8000003F`)** onde devia estar um objecto. É a **doença da free-list** do megaprojecto, e é
o que o band-aid forçado `[FREELIST-TAG-GUARD]` (×32) mascara. Fechar isto = tornar a free-list natural (sem entradas
bit31) para que `func_002DAF04` receba ponteiros reais.

**Onde isto deixa o objectivo:** o recompilado renderiza 2300+ frames (§12); a única thread que não fecha é a de
construção do stage, presa a percorrer uma estrutura com um ponteiro tagged. Alvo final e nomeado: a origem do
`0x8000003F` na estrutura de `func_002DAF04` — a mesma classe de bug que o megaprojecto fechou (fallthrough
`func_002550C8`), agora neste sítio. É a próxima parede, medida até à raiz.

## 14. Disambiguação decisiva: é corrupção NOSSA (não IO não-mapeado)

`oracle_readmem.py` no console, nos endereços que `func_002DAF04` desreferencia:

| endereço | console | conclusão |
|---|---|---|
| `0xE0000044` | E01 (não committado) | não é região real |
| `0x60000044` (bit31 limpo) | E01 | não é região real |
| `0x8000003F` | E01 | não é região real |

**Nenhum está committado no console.** Logo NÃO é IO/RSX não-mapeado que nos falta — é **corrupção nossa**: a estrutura
que `func_002DAF04` percorre tem **ponteiros de lixo/tagged** (`0x8000003F`, `0xE0000044`…) que o console **nunca**
desreferencia (a estrutura equivalente dele tem ponteiros válidos). Confirma o §13: doença da free-list /
não-inicializado, **da mesma classe** do bug que o megaprojecto fechou (fallthrough `func_002550C8`).

**Estado final desta sessão (medido, honesto):**
- O recompilado **renderiza 2300+ frames com draws** (§12) — não está congelado.
- Cinco paliativas forçadas removidas (naturais por default): E90/E93/E96/E99/E103. Parede [F] vencida.
- A única thread que não fecha (construção do stage) percorre uma estrutura com **ponteiros tagged de lixo**, e é
  **corrupção nossa** confirmada (§14), mascarada pelo `FREELIST-TAG-GUARD` forçado.
- Alvo final, nomeado e decisivamente caracterizado: a origem do `0x8000003F`/ponteiros tagged na estrutura de
  `func_002DAF04` — a **auditoria sistemática do fallthrough cross-fragment** (item 3 do roadmap) aplicada aqui.

Continua forçado (não fechado nesta sessão): open F2B do WAD, intro (`PS3_MOVIE_EOS`), e os band-aids de free-list
(FREELIST-TAG-GUARD, ICALL-HEAP/ASCII) que mascaram esta corrupção. Fechar a corrupção de raiz permite removê-los.

## 15. E113 — REFINA §13/§14: 0x8000000X são tags legítimos; 0x8000003F é computado, não armazenado

Hook `PS3_TRACE_TAGPTR` em `vm_write32` (filtra pelo VALOR, address-independent). Os valores bit31 ESCRITOS são todos
`0x8000000X` — **tags de descritor legítimos** (`0x80000001`=classe 1 … `0x8000000F`=classe 15), gravados nos descritores
em `0x40004xxx`. **Normal**, não corrupção.

Mas **`0x8000003F` NÃO foi apanhado** por nenhum store → **não é escrito em memória**; é **computado num registador** e
usado como ponteiro por `func_002DAF04` (`read8 access 0x8000003F`). `0x3F` = 63 (um índice/contagem, não uma classe).

**Refina §13/§14:** não é «ponteiro tagged de lixo armazenado na estrutura». É `func_002DAF04` a **calcular** um valor
com bit31 (`0x80000000 | 0x3F`, e depois `… | 0x60000044 + i*0x8000000`) e a **desreferenciá-lo como ponteiro** — quando
o bit31 devia marcar «dado inline / valor pequeno», não um endereço. É um bug de **lógica/lift em `func_002DAF04`**
(a função que o Ghidra não decompila), não uma corrupção da free-list gravada. Continua a ser NOSSO (o console não toca
estes endereços — §14), mas o alvo muda: a lógica de `func_002DAF04` que trata o bit31.

**Aviso metodológico (honesto):** esta sessão produziu ~6 histórias coerentes que tive de refinar/retratar (cursor −1,
band-aids→hero, MDLX ausente, classe-15, «congelado», free-list armazenada). O padrão: capturas de tiro-único enganam;
só a POPULAÇÃO (o fim do log = renderiza; o hook de valor = tags legítimos) e o oráculo decidem. Fechar isto exige
desmontar `func_002DAF04` (grande, sem decompilação) em torno de +0x1AE0 e comparar o fluxo com o oráculo — trabalho
determinístico para a próxima leva, não mais tiro-único.

## 16. Localização determinística (disasm estático, sem tiro-único): o campo corrupto exacto

`func_002DAF04` (0x2DAF04, só 0x5C4 bytes; Ghidra não decompila por causa dos laços `bdnz`). O laço em `0x2DB0E0`:

    0x2DB0E0  r9  = r25                    ; r25 = nó da lista a percorrer
    0x2DB0E4  r11 = *(r9+0)                ; r11 = WORD0 do nó (tratado como PONTEIRO)
    0x2DB0EC  r0  = *(r11+0x40) (lbz)      ; conta do laço = byte em word0+0x40
    0x2DB0FC  r9  = *(r11+0x54)
    0x2DB104  r10 = r11 + 0x58
    0x2DB108  mtctr r0                     ; CTR = *(word0+0x40)
    0x2DB11C  r9 = *(r9+0) ; *(r9+0x44) ...; bdnz  ; percorre a lista interna

**Campo corrupto exacto:** `*(r25)` = word0 do nó. O código exige que word0 seja um **ponteiro válido** (para ler
`+0x40`=conta, `+0x44`, `+0x54`, `+0x58`). Em nós, word0 de um nó é **lixo (0xE0000000…)**, logo `*(word0+0x40)` dá uma
conta enorme e o `bdnz` percorre OOB (`0xE0000044`, `0xE8000044`… vistos no log). No console word0 é um ponteiro válido
(a lista é bem formada — §7). O `0x8000003F` era o `lbz *(r11+0x40)` com `r11≈0x7FFFFFFF`, i.e. o mesmo word0 corrupto.

**Alvo preciso (determinístico):** o nó `r25` cujo **word0 nunca foi preenchido com um ponteiro válido** — um objecto
por-construir na lista que `func_002DAF04` percorre. `r25` vem de `r0` em `0x2DB0D0`. O passo seguinte, único e concreto:
caminhar quem preenche (ou deixa por preencher) `word0` desse nó — o produtor. Já não é tiro-único: é seguir `r25` para
trás a partir de `0x2DB0D0`, com o oráculo a dar o word0 correcto no mesmo nó.

Esta é a localização mais precisa da sessão, por disasm estático (não por captura). O campo é `*(r25)` (word0 do nó);
a raiz é o produtor que o deixa a `0xE0000000`.

## 17. r25 é índice num array alocado (0x27CAC8) — o alvo determinístico final da sessão

Continuação do disasm (0x2DAF04-0x2DB02C): `func_002DAF04` **aloca um array** (`bl 0x27CAC8`, r15=resultado) e depois
computa `r25` por aritmética de índice:

    0x2DAFE4  r0  = *(r15+0)            ; contagem/tamanho do array
    0x2DAFEC  r0  <<= 2
    0x2DAFFC  r0  = r0 * r10            ; index * stride
    0x2DAFF4  r11 = r31 << 3
    0x2DB004  r31 = r0 + r11            ; base + index arithmetic
    0x2DB02C  r25 = r31                 ; r25 = ponteiro para dentro do array

Logo `r25` é um **ponteiro calculado para dentro de um array**; o nó cujo word0 é lixo (§16) vem daqui. A corrupção
está numa de três coisas, todas nesta janela de instruções: (a) o alloc `0x27CAC8` devolve tamanho errado; (b) a
contagem/índice (`*(r15)`, r10, r31) é lixo; (c) a aritmética de índice (`rlwinm`/`mullw`/`add`) está mal-liftada.

**Alvo determinístico final (para a próxima leva):** instrumentar estes endereços exactos (0x2DAFE4, 0x2DAFFC, 0x2DB004,
0x2DB02C) no recompilado E no oráculo, comparar os valores de `r0/r9/r10/r11/r31/r25` no MESMO nó — isso decide entre
alloc, contagem, ou aritmética. É um diff de ~5 registadores em 4 instruções, **não** tiro-único: cada valor é
comparável directamente com o RPCS3 no mesmo ponto.

---

### Resumo da sessão (2026-09-02) — o que ficou provado, removido e nomeado

**Removido de forçado (natural por default, legado gated):** fill do WAD (E90), AUTO_LOAD+save-data (E93), freelist
do B71 (E96), 393E0 (E99), poda E6 (E103). **Parede [F] vencida** (E90).

**Provado (medido/determinístico):** o recompilado **renderiza 2300+ frames com draws** (não congelado, §12);
`0x0052F3F0` é o tick eterno (§4); os endereços OOB não estão no console → corrupção NOSSA (§14); o campo corrupto é
`*(r25)`=word0 de um nó da lista de `func_002DAF04` (§16); `r25` é índice num array alocado em `0x27CAC8` (§17).

**Ainda forçado:** open F2B do WAD, intro (`PS3_MOVIE_EOS`), band-aids de free-list. Fechar a raiz do §17 permite removê-los.

**Retracções honestas (6):** cursor −1, band-aids→hero, MDLX ausente, rótulo classe-15, "congelado", "corrupção
gravada". Lição: capturas de tiro-único enganam; população + oráculo + disasm estático decidem.

## 18. E114 — o r25 não é o único: func_002DAF04 tem VÁRIOS laços a ler lixo → objecto inteiro malformado

Instrumentei o ponto r25 (0x2DB02C). Na corrida, `[R25DIFF]` **não disparou**, mas um laço DIFERENTE de `func_002DAF04`
correu: `[vm] UNCOMMITTED read32 access 0x58000044 ra=func_002DAF04+0x2630` (offset host +0x2630, ≠ o +0x1AE0 de antes;
endereço 0x58000044 ≠ 0xE0000044). Ou seja, `func_002DAF04` tem **múltiplos laços** (`bdnz`) e **vários deles lêem lixo**.

**Refina §16/§17:** não é UM campo (r25) corrompido; é o **objecto inteiro** que entra em `func_002DAF04` que está
malformado — vários dos seus campos são ponteiros de lixo (0xE0000044, 0x58000044, …). O objecto vem de
`func_0009EC0C` (r3=0x41573620) via `func_00283328`. **A raiz é a montante, no grafo de objectos que `func_0009EC0C`
constrói/percorre**, não em `func_002DAF04`.

**Conclusão honesta da linha de investigação:** a parede não é um campo único nem uma função; é um **objecto malformado**
produzido na construção por-objecto do R_Hero01. Fechar isto exige rastrear a construção desse objecto (quem preenche os
seus campos com lixo) — comparando o MESMO objecto lógico com o oráculo, campo a campo, do seu alloc à passagem a
`func_002DAF04`. É a técnica do megaprojecto (6 rondas), aplicada aqui: uma investigação dedicada, não fechável neste
turno sem arriscar mais histórias meio-certas (já foram ~7 nesta sessão).

**O que ESTA sessão entregou, sólido e empurrado:** renderiza 2300+ frames; 5 forçagens removidas; [F] vencida; a
parede seguinte caracterizada até «objecto malformado na construção do hero, corrupção nossa», com o método (população +
oráculo + disasm) e o alvo (o produtor dos campos do objecto) nomeados para a próxima leva.

## 19. Testado: remover a forçagem da intro (PS3_MOVIE_EOS=0) — é LOAD-BEARING sobre a parede do vdec-EOS

Em vez de só nomear «a intro continua forçada», TESTEI removê-la: corrida com `PS3_MOVIE_EOS=0`, sem
`PS3_VDEC_FORCE_SEQDONE_MS`. Resultado medido:

    [INTROSEQ] CE03C wait tick st620=3 i=440 / 480 / 520 / 560
    [INTROSEQ] CE03C wait-idle exit st620=3

O boot **fica preso em st620=3** (movie a decodificar) e **nunca progride para EOS** (11→0→contador=4). O caminho
natural de vdec **não sinaliza EOS/seqdone** — nunca chega aos WADs (0 opens de WAD). No console (oráculo, §1) o mesmo
ponto avança 0→1→**11**→0 e o contador `0x592668` vai a **4** pela decodificação real do `.m2v`.

**Conclusão (medida):** a forçagem da intro (`PS3_MOVIE_EOS`/FORCE) é **load-bearing** — cobre uma parede real: o
**vdec não decodifica o `.m2v` até EOS naturalmente** (fica em «a decodificar»). Removê-la exige fechar a parede A pela
via natural (o decode completo com EndSeq/EOS), que hoje só passa via FORCE. Não é um gate que se tira; é a mesma classe
das outras paredes.

**Estado das forçagens que restam, agora com teste:**
- **intro (`PS3_MOVIE_EOS`)**: load-bearing sobre vdec-EOS (§19, medido). Remover = fechar o decode natural.
- **open F2B do WAD**: idem — o dearchiver natural do psarc não abre o WAD; o F2B (FO falso) cobre isso.

Ambas cobrem paredes reais (vdec-EOS, dearchiver do WAD), não são atalhos cosméticos. «Remover tudo o que é forçado»
= fechar essas paredes de raiz, a mesma cadeia de investigação por-parede do resto do projecto.

> **§20 PARCIALMENTE RETRACTADO por §22:** a medição (filho word0=0 nos dois lados) fica; a inferência «o DACCC é
> ruído não-fatal» CAI — é a thread principal presa dentro do callback do comando 25. Ver §22.

## 20. 8ª refutação (decisiva): o filho está BEM; o func_002DAF04 OOB é ruído não-fatal, não a parede

`[BADNODE]` com `*(node)`: `node=0x4156FE74 *node(word0)=0x00000000`. **O filho do slot 15 tem word0=0 — EXACTAMENTE
como o console** (`0x31540B34` word0=0, medido no oráculo). Logo este parent/filho está **correcto**; `word0=0` é normal
para este tipo. A heurística «*(node) fora de range» falso-positivou de novo (word0=0 é válido).

**Refuta toda a linha §7–§18 sobre «o objecto do hero está malformado».** O parent, o filho e os slots batem com o
console. O `func_002DAF04` que lê `0xE0000044/0x58000044` fá-lo para OUTRO objecto/caminho — e, **crucialmente, como o
jogo RENDERIZA 2300+ frames através disso (§12), esses OOB são ruído NÃO-FATAL, guardado** (o guard devolve 0, o loop
segue), **não a parede**.

**Conclusão honesta e disciplinada (8 refutações nesta sessão):** estive a perseguir o objecto errado por capturas de
tiro-único + heurísticas, que falso-positivaram repetidamente. O método falhou aqui. O que É sólido:
1. o recompilado **renderiza 2300+ frames** (não congelado, não preso num spin fatal);
2. cinco forçagens removidas (naturais); [F] vencida;
3. as duas forçagens que restam (intro, WAD open) são **load-bearing** sobre paredes reais (vdec-EOS, dearchiver), TESTADO.

**O que a próxima leva deve fazer (mudança de abordagem, não mais tiro-único):** determinar em que ESTADO o jogo está
(loading? menu?) e o que o mantém em «1 draw/frame» em vez do menu cheio — via o conteúdo do render e a máquina de
estados da aplicação, comparados com o RPCS3 no mesmo instante. O `func_002DAF04`/DACCC é ruído guardado, NÃO o alvo.

## 21. Reconecta ao objectivo do projecto: a divergência real é o REGISTRY DE SHADERS (parede [D]/[E])

Corrida longa (140s), abordagem de estado (não object-chasing). Depois de `thr_auto_load() end` → abre `r_hero01.wad`
→ processa. **Shaders que compilamos:** só `Default.ps3fx` e `FlashUI.ps3fx` (+1 `GOWShader`). Render **flat a 1
draw/frame**, ~193 frames de `guest_present`, sem crescer.

**No console (oráculo/TTY, §1 e e93a):** depois de `thr_auto_load` compila um **burst de shaders do nível** —
`GOWShader.ps3fx`, `Resolve`, `Fog`, `SkyboxBloom`, `Downsample`, `Blur`, `Upsample`, `Fade`, `Particle` — e chega ao
**menu** (render cheio).

**A divergência real com o RPCS3 é o REGISTRY DE SHADERS / CGOWShader** — exactamente a **meta corrente do projecto**
(CLAUDE.md: «pixels de jogo REAIS … registry de shaders populado, CGOWShader resolvendo») e as **paredes [D] (registry
de shaders) e [E] (pixels naturais)** já documentadas. O `func_002DAF04`/DACCC foi um beco (ruído guardado, §20); o
verdadeiro «mesmo fluxo do RPCS3» é: **a construção do nível popular o registry de shaders e disparar os compiles
(GOWShader/Resolve/Fog/…)** que hoje não disparamos.

**Fecho honesto da sessão — o mapa reconectado ao projecto:**
- **Provado:** renderiza (não congelado); 5 forçagens removidas; [F] vencida; as 2 forçagens restantes (intro, WAD open)
  são load-bearing sobre paredes reais (vdec-EOS, dearchiver), TESTADO.
- **Alvo real (= paredes [D]/[E] do projecto):** após o hero WAD, popular o registry de shaders / CGOWShader e disparar
  os compiles do nível como o console — é o que separa o nosso «1 draw/frame» do menu cheio do RPCS3.
- **Beco identificado e abandonado:** `func_002DAF04`/DACCC (ruído guardado, 8 refutações).

O caminho para «mesmo fluxo do RPCS3» é a meta corrente já mapeada do projecto (shaders → pixels naturais), não a
thread de construção. Isto realinha a investigação desta sessão com o roadmap existente.

## 22. A parede [G]/[D] fechada até ao sítio exacto: o callback do comando 25 (`func_00040560`) não retorna

Cadeia medida, determinística, nos dois lados (oráculo + sondas E115–E121):

1. **Gate do render de nível:** `func_00248630` só corre por completo se `FUN_000b351c()!=0` ⇔ `*P1(0x543734)!=0` — que só
   `func_000B356C` seta (=1), chamada de `func_00042294` (a **máquina de estados de load**, obj `P3=0x57CF78`, estado em `+0x550`)
   no estado **22** (`0x428E8`). No console isso abre o gate → burst de shaders (GOWShader/Resolve/Fog/…) → menu.
2. **A máquina de estados é dirigida por uma FILA** (`P3+0x40` idx, `+0x44` count, entradas de 32 B, estado=`entry+0x4C`),
   consumida na cabeça (`0x422CC-0x422F8`) quando o estado é 0. Fila idêntica nos dois lados: `13 (hero) → 25`.
3. **Estado 25 = «chamar callback»** (`0x425F0`): saca a entrada, chama o OPD em `entry+0x50` = **`0x0051A9C8` → `func_00040560`**
   («WAD_R_Hero» level-begin), depois estado 0 → a cabeça dequeua **21 (shell) → 22 (start)**.
4. **Console:** entra em `func_00040560` a t=6,8 s e **retorna a t=6,9 s** (bp em `0x40560` e no retorno `0x42648`).
   **Nós:** entramos (`func_000425F0 → func_00040560`) e **nunca retornamos** — a thread principal fica em
   `func_00027130 → func_0002692C → register (0x39E794) → factory → func_0022E960 → … → func_0009EC0C →
   func_00283328 → func_002DAF04 → func_002DACCC` (o spin dos §13–§18, com `head=0x1030…0x1178`).

**Correcções (retracções com a mesma rigor):** o §12 («renderiza, não congelado») descrevia a thread RSX; a principal está
presa. O §20 («DACCC é ruído não-fatal») cai — é a parede. As medições de ambos ficam.

**A parede, agora exacta:** a instanciação do hero dentro do callback `func_00040560` (via `func_00027130`) entra num
`func_002DAF04`/`func_002DACCC` a percorrer uma estrutura com ponteiros de lixo (§16–§18), no nosso lado; no console a
mesma chamada termina em 0,1 s. É o objectivo do projecto (paredes [D]/[E]) reduzido a **um call site**: o `bl 0x2DACCC`
em `0x2DB32C` e o campo que lhe dá o `head`. Fechar isto abre o gate `*P1`, dispara os shaders e chega ao menu.


## §23 Continuação (2026-09-02, mesmo dia)

A cadeia do §22 foi andada até ao produtor e corrigida em
[E124–E139](2026-09-02-E124-E139-fallthrough-condicional-inflate-e-rwlock.md): o spin em `func_002DAF04` era uma
**vítima** de `r1` a derivar 0x80 por chamada (`func_0024D144` sem trampolim de fallthrough — defeito do lifter,
31 sítios). Com o fix o callback do estado 25 retorna e o loader segue a ordem do console (13 · 14 · 25 · 21 · 22).
As medições deste ledger mantêm-se válidas; só a inferência "gestor sem produto corrente" do §11 cai — o produto
estava lá, a chave é que chegava errada.
