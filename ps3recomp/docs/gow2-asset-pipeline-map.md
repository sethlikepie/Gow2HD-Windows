# GoW2 — Mapa do pipeline de asset primário (R_PermA) e ponto da quebra

Plano 3, Task 2. Rastreamento evidência-antes-de-hipótese do consumo do
`R_PermA.wad_ps3`. Fonte de evidência: `gow2_work/recomp_mid_v2/smoke_asset_pipeline.log`
(32317 linhas, PS3_TRACE_ASSET=1) + inspeção do WAD + código lifted.

## TL;DR

O `R_PermA.wad_ps3` é um **WAD NÃO-comprimido** (diretório de grupos de asset +
payload cru). NÃO passa por zlib/edgezlib. Logo `push2=0` e `HOSTINFL=0` são o
comportamento **correto**, não o bug — a tese de "deadlock produtor-consumidor no
dearchive/inflate" (H1/H2/H4 do brief) é **red herring para o R_PermA**.

A quebra real: o jogo faz stream linear do WAD (0 → 5.619.712 bytes = 27,9%) e
**para de pedir dados**. No exato ponto do freeze, o **alocador de free-list guest
(`func_00263178`) desreferencia uma palavra taggeada `0x80000000` da free-list COMO
ponteiro cru**, gerando um burst de escritas a uma arena não-committed `~0x84000000`
que o guard do vm dropa. A alocação corrompe/falha → o loader do WAD não consegue
colocar o próximo grupo de asset → o stream trava.

## A cadeia (open → stream → consumo → quebra)

```
[movieio] open 'R_PermA' -> movie_cache/R_PermA.wad_ps3 size=20169344 slot=0   (log 32055)
   │
   │  R_PermA.wad_ps3 = WAD GoW cru:
   │    header "WAD_R_Perm" + diretório de grupos nomeados:
   │      GFXX_ TXRX_ MATX_ LGTX_ FLPX_ CXT_ ANMX_ SCRX_ BHVX_ MDLX_ PRTX_
   │      CAMX_ EEPR_ SNDX_ COLX_ SHGX_ FX_ WYPX_ ... (offsets/sizes)
   │    + payload cru (bytes em 0x55C000 são dados de asset, NÃO deflate)
   ▼
async read op (op=0x008695D4)  ──HLE──► func_002B3D1C  (ppu_recomp_001.cpp:22876)
   │  lê IMEDIATO p/ ring guest em ~0x40080000 (janela ~147KB, wrappa),
   │  seta op+0x14=pos+n, op+0x8=0 (done), bit in-flight limpo.
   │  movie_io_pread (movie_hle.c) → serve do host, sempre completa.
   │  poll do pump = func_002B45D0 (reporta done na hora — NÃO é o gargalo).
   │
   │  reads observados (throttle #1-12, depois %40):
   │    #40 pos=1572864 want=131072 dst=0x40083D40  (log 32193)
   │    #80 pos=4341760 want=98304  dst=0x400A7D40  (log 32266)  ← ÚLTIMO read
   ▼
CONSUMIDOR = loader de grupos de asset do WAD (engine GoW, código guest)
   │  drena o ring, parseia grupos, dispara loaders por tipo (GFXX→shader/effect,
   │  TXRX→textura, MDLX→modelo, ...). Progrediu ~5,4MB (ring pequeno ⇒ consumo real).
   │  precisa ALOCAR memória p/ cada asset →
   ▼
ALOCADOR guest free-list  ── família func_00263040 / func_00263178 / func_00263B70
   │  (ppu_recomp_000.cpp:548032)  "megaprojeto de concorrência do pool de handles"
   │  já documentado na memória do projeto.
   │  free-list guarda `count|0x80000000` como TAG nos headers dos nós.
   │  BUG: uma palavra taggeada 0x80000000 é lida e usada como PONTEIRO cru
   │       (sem mascarar o bit) → base de arena = ~0x84000000.
   ▼
✗ QUEBRA: vm_write32 → EA 0x8400000F/0x84000013/0x84000017/0x8400001B/0x8400003B
   │  guard vm_uncommitted_guard (ppu_loader.cpp:284) → região reservada porém
   │  NÃO-committed (sys_memory só cobre 0x40000000..0x4B000000) → escrita DROPADA.
   │  ra=0x141301065 = dentro de func_00263178 (nm). 25 escritas em rajada,
   │  linhas 32225-32291, cercando o último read e o freeze; depois o cap de 40
   │  do log silencia (o spin provavelmente continua).
   ▼
free-list corrompida / alocação inválida → loader não coloca o próximo asset →
para de pedir reads → STREAM TRAVA em 27,9% → slot nunca fecha, jogo segue
renderizando (flips sobem) mas o asset primário nunca termina de carregar.
```

## O que NUNCA roda vs. o que roda no hot loop pós-trava

| Elo | Estado no freeze | Evidência |
|-----|------------------|-----------|
| canal ANY2ANY 32B de inflate (q=0x43008180, dir=3) | init só, **0 pushes** | `push2=0` |
| `lfq_host_inflate_try` (HOSTINFL) | **nunca dispara** (esperado — WAD é cru) | `HOSTINFL=0` |
| filas job 224B PPU2SPU (q=0x47CA9B80/0x47D59A80) | 41 pushes **só no BOOT** (linhas 397-1305), nada na janela | `push#` |
| task SPU edgezlib (fp=0x2A5C4E67A14505B8) | dispatch + "returned cleanly", **idle/vazia** | logs 32050/32308 |
| async read op (func_002B3D1C) | completa na hora, mas **não é mais chamado** após #80 | reads param |
| pump poll (func_002B45D0) | reporta done na hora — não é o gargalo | código |
| **alocador free-list (func_00263178)** | **spin escrevendo em arena 0x84000000 não-committed** | `[vm] UNCOMMITTED ... 0x8400000F ra=0x141301065` ×25 |

## Hipótese de gate (testável)

> **O stream do R_PermA trava em 5,6MB porque o alocador de free-list guest
> (`func_00263178` & cia.) desreferencia uma palavra da free-list taggeada com
> `0x80000000` como ponteiro cru, produzindo uma base de arena `~0x84000000` que
> a VM não tem committed; as escritas de metadados/asset são silenciosamente
> dropadas pelo guard, corrompendo a alocação e impedindo o loader do WAD de
> colocar o próximo grupo — o consumidor então para de pedir reads.**
> NÃO há relação com zlib/edgezlib (o WAD é cru; ANY2ANY/HOSTINFL corretamente
> silenciosos).

### Experimento discriminador (1 linha, gated) — `PS3_COMMIT_HIGH=1`

Guard modificado (`ppu_loader.cpp`) para, sob env, commitar on-demand a página
da banda alta (0x80000000..0x9FFFFFFF) e PERMITIR a escrita:
- **Se o stream passar de 5,6MB** ⇒ arena era válida-mas-não-committed ⇒ fix =
  commitar/registrar a região (ou corrigir a base para dentro do range real).
- **Se travar igual em 5,6MB** ⇒ 0x84000000 é ponteiro genuinamente corrompido ⇒
  fix = contabilidade do alocador (o megaprojeto do pool de handles).

### Resultado do experimento (executado, Task 2)

`PS3_COMMIT_HIGH=1` rodado (`recomp_mid_v2/commit_high.log`, exit=124 normal):
- 1× `[vm] COMMIT-HIGH 0x8400003B span=0x1000 ra=0x1413014DF` (ainda em func_00263178);
  ao commitar a página 0x84000000 inteira, todas as demais escritas 0x840000xx
  passaram a landar (0 linhas `UNCOMMITTED-HI` restantes).
- **R_PermA travou IDÊNTICO: preads=78, bytes_read=5.619.712 (27,9%).** Zero
  progresso adicional.

**Veredito:** deixar as escritas do alocador landarem em memória real NÃO destravou
o stream ⇒ `0x84000000` NÃO é arena válida-não-committed, é **ponteiro genuinamente
corrompido**. A raiz é a **lógica de contabilidade da free-list** (megaprojeto do
pool de handles: func_00263040/263178/263B70), não um fix de commit de memória.
Task 3 deve mirar o alocador, não a VM.

## Task 3 — diff semântico do alocador + causa raiz (H-D confirmada)

### O alocador é um heap de boundary-tags (estilo dlmalloc), não uma free-list simples

A família `func_00263040` (traversal/aloca) → `func_00263160` → `func_00263178`
(split com alinhamento) forma **UMA função guest** `0x263040..0x2632AC` que o
recompilador partiu em basic-blocks encadeados por trampolim. Layout do nó:
`[+0]` header = `tag(0x80000000) | (count&0x3F)<<26 | idx`; `[+4]` = next-link da
free-list; `[+8]` = campo aux; e **footers/boundary-tags** stampados nos limites do
split. `func_00263040` lê `[node+4]` como next e mascara o header com
`rlwinm r5,r0,0,6,31` (= `& 0x03FFFFFF`, tira tag+count). `func_00263178` no split
escreve o mesmo header tagueado em **três** endereços — `[r3+0]` (base),
`[r6+0]=r30+r8` e `[r5+0]` (footer) — e no ENTRY lê `r28=[node+4]` e o usa **cru como
ponteiro** (`stw r11,8(r4)` com `r4=zext(r28)`).

### Diff semântico (PPC EBOOT.ELF vs C liftado) — FIEL, instrução-a-instrução

`func_00263178` (0x263178–0x2632AC) e `func_00263040` (0x263040–0x263160) conferidos
opcode a opcode contra `ppu_recomp_000.cpp` (linhas 547942/548032). **Nenhuma
divergência.** `rlwinm/oris/srawi/rldicl/divwu/add/subf/stw/lwz` todos batem;
sign-extension (`srawi`→`(int32)>>`, `rldicl`→zext) correta; máscaras corretas.
**NÃO há lwarx/stwcx em lugar nenhum** — o alocador não é lock-free/CAS.
⇒ **H-A (máscara perdida), H-B (CAS infiel) e H-C (sign-ext) REFUTADAS** no site do
crash e na família inteira. O `r28=0x84000007` usado como ponteiro é **exatamente o
que o hardware faria** — ele confia que `[node+4]` é um ponteiro limpo; o valor já
entra corrompido (**H-D**).

### Evidência host→guest (probe PS3_TRACE_FREELIST em vm_read32/vm_write32)

Cross-referência dos slots `[node+4]` que voltam tagueados (`[FLREAD]`) contra quem
escreveu valor-header ali (`[FLWRITE]`), com host→guest via `nm` do exe:
- **Writer** do valor-header no slot corrompido = `func_00263178+0x8FB`
  (ra0 0x14130156D/587/5A1 = as 3 `stw` de boundary-tag) — a **própria** store legítima.
- **Reader/crasher** = `func_00263178+0x3F3` (ra 0x141301065, o fault da Task 2) e a
  família de heap-walk `func_00262938`/`func_002629E0` (ra 0x1412FExxx).
- Fault write `0x8400000F` = `r28+8` com `r28=[node+4]=0x84000007`
  (= `0x80000000|1<<26|7`, um header count=1 idx=7).

### Causa raiz (1 frase)

O heap de boundary-tags desincroniza: um footer/header tagueado (`0x84xxxxxx`) ocupa o
slot `[node+4]` que o alocador depois lê como next-pointer e desreferencia cru — e
como **todo o código do alocador está fielmente liftado** (sem instrução divergente),
o desalinhamento é **estado de heap acumulado a montante** (o "megaprojeto do pool de
handles"), não um bug pontual de tradução → **não há fix fiel de ponto único**;
mascarar o tag no site é PROIBIDO e já provado inútil (COMMIT_HIGH).

### Próximo experimento (Task 4+)

Instrumentar o caminho de FREE/coalesce (`func_00263318` seta bit-0 in-use;
`func_00263400`/`263540` coalescem) e a INICIALIZAÇÃO da arena: a hipótese mais
provável é que a **base/tamanho do heap** na nossa emulação difere do PS3 real,
fazendo o cálculo de padding de alinhamento (`r31=(align-(node+4)%align)/4` em
func_00263040) produzir um **bloco-resto pequeno demais** (< header+next+prev+footer =
4 words) num split; ao ser encadeado, seu `[+4]` colide com um boundary-tag. Traçar,
com PS3_TRACE_FREELIST, a PRIMEIRA vez que um `[node+4]` recebe um tag em vez de um
ponteiro (o slot ainda-válido→corrompido), e comparar a base da arena com RPCS3.

## Task 3b — captura da PRIMEIRA corrupção (probe PS3_TRACE_FLCORRUPT/FLNULL)

Probe novo em `vm_write32` (gated `PS3_TRACE_FLCORRUPT`): detecta o momento em que
uma tag de boundary é gravada num endereço near-null (`node~0`) — o sintoma raiz — e
dumpa o descritor do **pool de chunks** em `[TOC-0x20D8]=[0x53F0A0]`. Harness:
`gow2_work/recomp_mid_v2/run_flcorrupt.sh`.

### O ADT do pool de chunks (decodificado instrução-a-instrução)

Descritor global em `0x008370A8` (`[0x53F0A0]` = ponteiro): `[+0]`=arr (0x008370BC,
inline), `[+4]`=idx (0x008370AC), `[+8]`=count (128), `[+0x10]`=1º chunk. É uma
**pilha que cresce para BAIXO** (itens vivem em `arr[idx..127]`; `arr[0..idx-1]`=0):
- `func_00262C64` PUSH: `idx-- ; [desc+4]=idx (publica) ; arr[idx]=chunk (dado)`.
- `func_00262C8C` POP: `r3=arr[idx] ; idx++`.
- `func_00262808` PEEK: `if(count==idx) return 0; else return arr[idx]`.
- `func_00262CC4` INIT: malloc(16384); idx=count=128 (vazio).
- `func_00263A50`(push)/`func_00263A78`(pop)/`func_00263878` também mexem em idx.

### O que a Task 3b REFUTOU (com evidência)

1. **Pool NÃO retorna 0** (tese "pool esgotado" da Task 3 next-step): no instante do
   crash `idx=126, count=128, arr[idx]=0x40686298` (ponteiro VÁLIDO). O peek entrega
   um chunk bom.
2. **NÃO é race cross-thread**: os 400 writes de idx saem todos de **uma única tid**
   (single-thread). A ordem "publish-before-data" do PUSH é inofensiva sem
   concorrência.
3. **NÃO há OOM a montante**: `allocate_from_container(cid=1, 0x100000)` devolve
   0x4B000000 repetido (reuso de bloco liberado) — nenhum ENOMEM.

### O mecanismo real da primeira corrupção

`func_002550E8` sub-aloca de um chunk do pool via `func_002637D8` (= `peek()` +
tail-call `func_00263040(arena, size, align=8)`). **`func_00263040` (o alocador de
boundary-tags, provado FIEL na Task 3) retorna `node=0` a partir de uma arena VÁLIDA
(o chunk `0x40686298`) cuja free-list INTERNA esgotou/corrompeu.** `func_002550E8`
NÃO faz null-check do retorno (e é fiel — o guest também não faz, porque no PS3 real
essa sub-alloc nunca falha aqui) → `memcpy`/stamps do `func_00263178` caem no endereço
guest `0` (offsets 0/2/4/0x12/0x14/0x16/0x18) → `[node+4]=0x84000007` depois lido como
next-link → deref cru → write 0x8400000F dropado → desync → R_PermA trava em
5.619.712. Sinal de vida do wedge: `func_00263A50`/`func_00263A78` fazem **spin
`idx 127↔126`** (push/pop alternado infinito = o alocador retentando de uma arena
exaurida).

### Regress delimitado (próximo passo, Task 4)

A corrupção NÃO é o pool nem o alocador (ambos fiéis/saudáveis) — é o **estado da
free-list de UM chunk específico do pool** (`0x40686298`, classe ~16KB). O regress
está agora **cercado a um único chunk**, não ao heap inteiro. Traçar a free-list
desse chunk do nascimento (push por `func_00262C64`) até a exaustão: instrumentar a
travessia de `func_00263040` para logar a 1ª vez que o walk daquele chunk alcança um
link nulo/tagueado, e comparar o TAMANHO/init do chunk com RPCS3 (resíduo de H-E: o
tamanho do chunk do pool pode divergir do console, esgotando a free-list cedo demais).

## Task 3c — a arena do peek é um CURSOR não-inicializado, não uma arena válida

Auditoria + medição fecharam mais dois níveis. Probes novos (gated, off por
default) em `ppu_loader.cpp`: `PS3_TRACE_POOLCNT` (conta writes na região host de
ALLOC vs FREE + push/pop do pool), walk da free-list do chunk do topo no dump
`FLNULL`, e `PS3_TRACE_BADPUSH` (captura push de ponteiro sem magic no array do
pool). Harness: `recomp_mid_v2/run_poolcnt.sh`.

### H-H (nossos circuit-breakers) — REFUTADA
Auditoria opcode-a-opcode de `func_00263040/160/178` (walk+split) e das funções
PUSH/POP/PEEK/INIT: **nenhum circuit-breaker injetado**; o código liftado está limpo.
O `tblsize_guard` (ppu_loader.cpp) só bloqueia zero-writes a 6 endereços estáticos
(`0x572BBx`, `0x53FBF0`, `0x53FF4C`, `0x53D6AC`) — **nenhum** no range do heap
(`0x40xxxxxx`) nem no descritor do pool (`0x8370xx`). O único guard que morde a
banda alta é `vm_uncommitted_guard`, que é DOWNSTREAM da corrupção (dropa a escrita
já-corrompida em `0x84000000`), não a causa.

### Medição (POOLCNT / BADPUSH)
- `alloc_w=25718` vs `free_w=1294` no run inteiro → **os FREEs acontecem** (não é
  leak puro H-K), mas alloc domina ~20:1. O alocador **NÃO fica em spin** (só ~25 mil
  writes no run todo): a corrupção é **one-shot**, não livelock.
- `push=33 pop=32`, `idx=126` (não 128) → o **refill do pool FUNCIONA** (H-J
  refutada); há chunk disponível.

### O mecanismo real (walk da free-list do chunk do topo)
No freeze, `peek()=arr[idx=126]`. Comparando os dois slots do pool:
- **`arr[127]=0x40004020` = a arena REAL e SAUDÁVEL**: `[+0]=0xC0DE1111` (magic da
  arena, escrito por `func_00263878`), `[+4]=free-head válido`, **7 blocos livres,
  ~18 MB livres**. Se o alocador tivesse pego este slot, teria sucesso.
- **`arr[126]=0x40686288` = um CURSOR não-inicializado**: `[+0]=0x00000000` (SEM
  magic), `[+4]=0xFFFFFFFF`. E `0x40686288` está DENTRO da arena de `0x40004020`
  (~18 MB), no **frontier de alocação** (≈ o free-node `0x4068629C`). Não é uma arena
  separada — é um ponteiro interior.

`func_00263878` é o CRIADOR de arena: escreve `0xC0DE1111` em `[arena+0]` e monta a
free-list (`[arena+4]=arena+0x30`). O cursor em `arr[126]` **não passou por ele** →
sem magic, sem free-list. `func_00263040` **não valida o magic** (fiel — no console
esse slot nunca é uma não-arena aqui): ele lê `head=[arena+4]=0xFFFFFFFF`, cujo
empty-check é `head==arena` (não reconhece `0xFFFFFFFF`), então em vez de retornar 0
limpo **caminha em memória oob→near-null** e "acha" um bloco fictício em `~0`,
stampando boundary-tags no endereço 0 → corrupção → o loader do WAD trava.

### Culpado identificado (cadeia com ra host, nm ASLR-off)
`BADPUSH` fixou o writer: **`func_002550E8` (o consumidor) → `func_002639F8`
(dispatch push/pop) → `func_00263A50` (PUSH, ra0 `0x142D5530E`)** escreve
`arr[126] = r3`, onde `r3 = [[TOC-0x2218]]` = o global "arena/escopo corrente". Esse
push oscila `idx 127↔126` (o "spin" que a Task 3b viu) — um alocador de escopo:
empurra o escopo, sub-aloca dele, faz pop. Os valores empurrados **crescem
monotonicamente** (0x40682BE8 → 0x406854E0 → 0x40686288 → …) acompanhando o frontier
da arena `0x40004020`. O escopo empurrado **deveria ser uma arena inicializada**;
`[[TOC-0x2218]]` está apontando para uma região **não-inicializada** (frontier cru).

### Regress delimitado (próximo passo real, Task 4)
NÃO aplicar fix no site do `func_00263040` (magic/null-check = máscara cosmética,
PROIBIDA — e o guest genuinamente não checa). A correção fiel exige achar por que
`[[TOC-0x2218]]` (a "arena/escopo corrente") guarda um ponteiro-frontier
não-inicializado em vez de uma arena com magic. Traçar os WRITERS de `[TOC-0x2218]`
(`PS3_WATCH_W32` no endereço do global) do nascimento até o freeze e comparar com o
momento em que `func_00263878` cria/registra a arena: a hipótese é que a
inicialização/registro de uma sub-arena (ou o avanço do cursor de escopo) diverge do
console por um valor de tamanho/contagem semeado errado no nosso ambiente — a MESMA
classe de bug dos "loops runaway com bound errado" já documentada no `tblsize_guard`.

## Task 3d — o "escopo corrente" é o TOPO do array-pool; empurram um bloco de DADOS como arena

Probe novo (gated, off default) em `ppu_loader.cpp`: `PS3_TRACE_SCOPE` — (1) WATCH do
global `[TOC-0x2218]=0x53EF60` (ra0/1/2+tid+header do valor) e do cursor `[P+0]`, e
(2) log de TODA estampa de magic `0xC0DE1111` (endereço+ra). Harness:
`recomp_mid_v2/run_scope.sh`.

### O que o global de escopo realmente é
`P = *(0x53EF60) = 0x008372BC = &arr[128]`. Ou seja, o "escopo corrente" NÃO é uma
arena — é um ponteiro para o **topo do próprio array de ponteiros do alocador**
(descritor do pool em `0x8370A8`: `[+0]=arr=0x8370BC`, `[+4]=idx` em `0x8370AC`,
`[+8]=cap=0x80`; `arr` é inline no descritor, inicializado por `func_00262CC4`).
`[P+0]=arr[128]=0x40004020` (arena-mãe, com magic) — **constante, nunca sofre store**
(watch de `[P+0]` deu 0 escritas). O pool é uma **pilha que cresce para baixo** a
partir de `arr[128]`; marcas de escopo vão em `arr[127]`, `arr[126]`, ...

### O PEEK e o INIT, identificados
- `func_00262808` (PEEK) retorna incondicionalmente `arr[idx]` (idx=`[0x8370AC]`); é o
  que `func_002637D8`→`func_00263040` usam como "arena corrente" para alocar.
- `func_00263878` é o ÚNICO estampador de magic (INIT de arena real). No run ele rodou
  para **exatamente 6 arenas** — `0x40004020/0x43004020/0x46C04020` (por `func_0025C544`
  ×3), `0x4003ED08` (`func_002AC364`), `0x41803D18` (`func_00041D5C`), `0x42F83D48`
  (`func_002BBA64`). **Nenhuma** em `0x4068xxxx`.
- (Correção ao brief da Task 3d: `func_00262CC4` NÃO estampa magic — ele inicializa o
  DESCRITOR do pool, `malloc` de 0x4000 + `arr`/cap. Quem estampa é `func_00263878`.)
- `func_0026258C` (o "gate de validade" no início de `func_00263040`) **não lê o magic**:
  é um teste de identidade/faixa contra `*(TOC-0x20DC)+0`. Fiel — no console o topo do
  pool nunca é uma não-arena aqui, então não há por que checar magic.

### O elo final (Q1/Q2 respondidos)
`BADPUSH` fixou o push do valor cru: `func_002639F8` (via `func_002550E8+0x67`) escreve
`arr[126]=r3`, com `r3 = 0x4068xxxx`. Rastreando a montante (fluxo de trampolim, pois o
host-stack `ra2` é o dispatcher e muda entre runs): a rotina `func_002550C8/E8`
**aloca um bloco da arena-mãe** (`func_002637D8`), inicializa registros de 16 bytes com
`-1` (4 shorts por entrada → tabela de handles/hash "vazia"), faz **memcpy** do conteúdo
antigo (`func_0036BA6C`, que retorna o dst) e então **registra esse bloco como a arena
de alocação corrente** empurrando-o no pool. O header do bloco é DADO
(`[+0]=0x200 [+4]=0x1FFFF`, ou `[+0]=0 [+4]=0xFFFFFFFF` na fronteira não-escrita), NÃO um
header de arena. Depois o PEEK devolve `arr[126]`=esse bloco e `func_00263040` lê
`[bloco+4]=0xFFFFFFFF` como free-head → caminha oob → escrita `0x84000007` → o loader do
WAD trava em 5,6MB (a corrupção das Tasks 2/3b).

### Gap nomeado (próximo passo real, Task 3e)
Esta é a **rotina de crescimento da tabela de handles** (o "megaprojeto de concorrência"
já documentado). O gap: ela registra um **bloco de dados** como arena de alocação
**sem rodar o INIT de arena** (`func_00263878`) sobre ele — e `func_00263878` não aparece
em nenhum ramo dessa rotina. No console o topo do pool tem de ser uma arena válida; aqui
o bloco cresce mas nunca é arena-inicializado. Provável causa: um ramo/contagem a
montante (candidatos: contagem de registros `r27`, tamanho `r26`, ou um flag
"precisa-init" no objeto da tabela em `r28/r30`) diverge e pula o INIT. **NÃO** aplicar
magic/null-check no site (máscara PROIBIDA, já provado inútil na Task 2). Task 3e: traçar
o ancestral que monta `r28/r30` no cluster `func_002550C8` e o ramo que deveria levar ao
arena-init do bloco crescido.

`bytes_read` do R_PermA permanece **5.619.712** (probes gated off por default = no-op no
baseline; zero mascaramento).

## Task 3e — o ramo REDECODIFICADO: não é "arena-init pulado no grow", é `[obj+0xC]` cru

A premissa da Task 3d ("o grow deveria chamar `func_00263878`") estava **errada**. O grow
**nunca cria arena** — ele **herda** uma. Disasm completo de `func_00254C44` (construtor
type==1) + `func_002550C8` (grow) + `func_002639F8`/`A50`/`A70`/`A78`/`A80` (dispatch
push/pop/replace) + `func_00262C64`/`C8C` (push/pop plano) + `func_00263878` (arena-init),
tudo conferido opcode-a-opcode contra o C liftado.

### A cadeia real (construtor → grow → alloc)
- **Construtor `func_00254C44` (type==1):** parseia um record, aloca objeto de 64 bytes
  (`func_00263B70`), `r28 = obj+32`, `r27 = [record+4]` (contagem). Em **`0x254E64`** faz
  **`[obj+0xC] = PEEK()`** (`func_00262808` = `arr[idx]` do pool de escopo) — ou seja, a
  **arena dedicada do objeto é o ESCOPO CORRENTE no instante da construção**. `cr4 =
  (r27==0)` em `0x254E58`; se `count != 0` cai no grow (`0x2550C8`).
- **Grow `func_002550C8`:** `r25 = PEEK()` (salva escopo). Lê `r3 = [r28+0xC]`; se `!=0`
  empurra `[r28+0xC]` como escopo corrente (`func_002639F8`), **se `==0` cai em `0x2552FC`
  que empurra `[[TOC-0x2218]+0]` = `arr[128]` = a arena-MÃE `0x40004020` (magic)**. Aloca a
  nova tabela (`count*16`) do escopo corrente (`func_002637D8`→`func_00263040`), inicializa
  os registros de 16B com `-1`, `memcpy` do antigo (`func_0036BA6C`), guarda em `[r28+8]`, e
  **restaura** o topo empurrando `r25` de volta. **É balanceado (push+restore).**

### As 3 hipóteses do brief, resolvidas
- **H-L (pop/restore ausente) — REFUTADA.** O grow restaura `r25` (2ª dispatch,
  `func_002550E8+0x67`... na verdade a 1ª dispatch; ver abaixo). O par push/restore existe e
  roda; não há pop faltante.
- **H-N (stub/indireto/lift errado) — REFUTADA.** Dispatch, arena-init, alocador
  (`func_00263040/160/178`), push/pop plano e peek: **todos fiéis** (branches, `mcrf`,
  máscaras, sign-ext conferem). Os alvos de trampolim `func_00263A50/A70` existem e estão
  corretos.
- **H-M (valor divergente) — CONFIRMADA, com evidência dura.** Probe novo `PS3_TRACE_ARRSEQ`
  (loga TODO write a `arr[116..128]` com magic+idx+ra) + `PS3_TRACE_RAWFIELD`. O escopo é
  **quase sempre a arena-mãe `0x40004020` (magic, saudável, free-head avançando)**; a pilha
  **nunca desce abaixo de idx=125** (arr[125] sempre `0x43004020` magic). A ÚNICA fonte de
  no-magic no topo: **a 1ª dispatch do grow empurrando `[r28+0xC]` = `0x4068xxxx`** — um
  bloco de DADOS cru (`[+0]=0/0x200`, `[+4]=0xFFFFFFFF/0x1FFFF`), **não** uma arena. Confirmado
  por exclusão: o valor empurrado (`0x40682BE8`) ≠ o valor anterior de `arr[126]` (`0x40004020`),
  logo é a 1ª dispatch (`[r28+0xC]`), não a restauração de `r25`. Então **`[r28+0xC]` guarda um
  bloco cru** e o grow aloca a tabela dele → `func_00263040` lê `[bloco+4]=0xFFFFFFFF` como
  free-head → anda oob → escrita `0x84000007`/`0x8400000F` dropada → freeze em 5,6 MB.

### O gap real (base-case, próximo passo)
`[r28+0xC] = PEEK()` no construtor. No console esse PEEK devolve **0** (→ fallback p/ a
arena-mãe, funciona) **ou** uma arena válida; no nosso ambiente devolve um **bloco cru**.
Duas sub-hipóteses restantes (precisam de probe DENTRO do liftado = rebuild de chunk):
(1) **janela transiente / reentrância:** uma construção aninhada roda DURANTE a janela de um
grow (entre o push de `[r28+0xC]` e a restauração), herdando o escopo cru — a ordem
de construção-vs-grow diverge do console (modelo trampolim/DRAIN); (2) **idx off-by-N:** a
pilha de escopo tem push(es) não-balanceado(s), então `PEEK` devolve `arr[idx]` cru em vez de
`0`/arena-mãe que o console veria. **Experimento seguinte:** probe no liftado `func_002550C8`/
`func_00254C84` dumpando `r28`/`[r28+0xC]`/`r25`/`idx` + call-stack estático do PRIMEIRO objeto
cujo `[obj+0xC]` é no-magic, e comparar a profundidade da pilha de escopo com RPCS3.
**PROIBIDO** (e inútil, provado na Task 2): estampar magic/null-check em `[r28+0xC]` ou no site
do alocador — o fix fiel destrava a fonte do escopo cru, não mascara o consumidor.

`bytes_read` do R_PermA permanece **5.619.712** (probes `PS3_TRACE_ARRSEQ`/`RAWFIELD` gated,
off por default = no-op no baseline; zero mascaramento).



## Task 3f — base-case capturado + FIX (fallthrough 2550C8→255178)

### Evento (probes no liftado, `PS3_TRACE_BASECASE=1`)
Padrão repetido 40×, tid único, **sem reentrância** (`grow_open=0`, `REENT=0`, `BC-CTOR=0`):

```
[BC-GROW-OPEN]  push=0x40004020 MAGIC  idx=126
[BC-XCHG] grow-push    rin=mother rout=mother idx 126→126  top=mother  r25=mother
[BC-RESTORE]           r25=mother r29=<alloc>   (r25 NÃO clobberado)
[BC-XCHG] grow-restore rin=mother rout=mother idx 126→126  top=mother
[BC-GROW-CLOSE]
[BC-PEEK]              v=<alloc>  (= free_head_antes+4)  ← escopo já corrompido
```

H-REENT e H-OFFN **refutadas**. O push/restore do grow é balanceado e deixa
`arr[126]=arena-mãe`. A corrupção acontece **depois** do CLOSE.

### Causa raiz (bug de lift / fragment split)
Epílogo de `func_002550C8` (chunk 000), após `memcpy` + check `[obj+4]==0`:

| | Alvo do fallthrough (count≠0) |
|--|--|
| **Errado (chunk 000)** | `func_002550E8` (0x2550E8) — reentra o corpo do grow |
| **Correto** | `func_00255178` (0x255178) — fase fill/next |
| **Já correto (chunk 002)** | cópia em `func_002550E8` → `func_00255178` |

No guest, 0x25516x **cai sequencialmente** em 0x255178 (endereço maior), nunca
volta a 0x2550E8 (endereço menor). O lifter emitiu o fallthrough cross-fragment
para o fragmento errado.

Com o alvo errado, `r3` ainda guarda o ponteiro da tabela recém-alocada →
`func_002550E8` faz `func_002639F8(r3)` e **empurra o bloco de dados cru** como
escopo corrente → alocações seguintes leem `[bloco+4]` como free-head → OOB
`0x8400000x` → stream do R_PermA trava em 5.619.712.

### Fix
`gow2_work/recomp_mid_v2/patch_fallthrough_2550c8.py` (idempotente) + rebuild
do chunk 000. Uma linha de trampoline: `2550E8` → `255178`.

### Aceite
| métrica | antes | depois |
|---------|-------|--------|
| `bytes_read` R_PermA | 5.619.712 | **20.169.344** (arquivo inteiro, preads=288, slot fecha) |
| `BC-PEEK` no-magic | 40/run | **0** |
| writes `0x8400000F` | dezenas | **0** |
| SHGX_ / ICGLdrShader | n/a (stream morto) | stream completo; CRC Invalid de `$/enginesupport/...` segue (Task 4/5) |



## Task 4 — type switch + OPD (partial registry path)

- **Jump table:** `func_002B11B8` switch was `TODO: .word` + TOC bctr → garbage CTR.
  Fixed by materializing offsets (base `0x2B1228`); `[TYDISP]` now fires.
- **OPD recovery:** vtable slots sometimes store code EA as if OPD → CTR=insn word
  `0x27182818`. `ps3_call_opd` / `ppu_opd_resolve` treat registered code EA as direct.
  `ICALL-BAD` → 0 after R_PermA.
- **SHADERSRC:** true stream count **N=0** (not N=-1); streams have `end>cur` but
  zero definition records → combination tables still empty.
- **ICGLdrShader:** still register-only (`[LDRSH]=0` post-WAD).
