# RE do consumidor de `'SMPD'` / `func_0043FF30` — Task 2 (pós-Stop, pré-WAD)

Análise **puramente estática**: leitura do lift (`recomp_macos_v2/ppu_recomp_0NN.cpp`,
gitignored, ~500k linhas/chunk — sempre `grep -n` + `sed -n`/`Read` com range, nunca o
ficheiro inteiro) **mais** leitura read-only do `EBOOT.ELF` (parse de `PT_LOAD` +
resolução de OPD/TOC, para decidir o conteúdo de dados estáticos que o C do lift não
mostra — só instruções, não o `.data` inicial). **Sem boot, sem build, sem sonda em
runtime.** Nenhuma escrita em memória guest, nenhum host `vm_write`. EAs de guest
reconstruídos do fluxo lifted e de offsets `gpr[2]+K` (TOC), nunca de `ra` do host
(PIE+trampolins tornam `ra` inútil — lição já registada em `2026-07-20-st620-3to0-static.md`).

Prerequisito (medido na Task 1, commit `77c9418`): com o EOS arm ligado, a FSM cascata
`st620` 3→…→11 e **auto-para em 11** via `func_002BFF88` (branch `st620==0xB`) →
`func_002C008C` → `func_002BFFE0`/`func_002BFFF4`. É exactamente aí que o broadcast
`'SMPD'` acontece. Este documento começa onde a Task 1 parou: **o que consome esse
broadcast, e chega a um open de WAD?**

---

## TL;DR — veredito

**G2.** O broadcast `'SMPD'` corre (3/3 sítios, bytes idênticos), é despachado com
sucesso (a classe está registada desde o early-boot, o gate de contagem passa), e
**chega a um listener real e especificamente registado — cujo handler é um no-op
deliberado** (`func_002D2BB8` @ `ppu_recomp_001.cpp:54292`, corpo = `{ return; }`).
Não há nenhuma chamada a loader de WAD em toda a cadeia Stop→broadcast→dispatch.

Isto não é "0 listeners" no sentido literal (existe UM listener registado nesse slot) —
é a outra metade da definição G2: "handler no-op". E como o binário guest (`EBOOT.ELF`)
é o mesmo PPC/PS3 independentemente do host, este no-op está igualmente presente no
caminho Windows/oráculo — o que implica **G4** como a explicação arquitectural de fundo
(o open real do WAD no Windows não pode vir deste canal SMPD; tem de ser outro sinal,
provavelmente um callback directo do vdec). G4 não foi directamente medido nesta task
(fora do escopo de `func_0043FF30`) — fica como pista forte para a Task 3.

---

## 1. Grafo Stop → broadcast → dispatch → (no-op)

```
func_002BFF88 (MovieStop)                    ppu_recomp_001.cpp:36141
 ├─ st620 ∈ {1..0xA} → loc_002C0048 (:36200) ─┐
 └─ st620 == 0xB     → func_002C008C          │   (ppu_recomp_001.cpp:36228)
                        ├─ obj+0x724!=0 → func_002BFFE0  (ppu_recomp_002.cpp:224556)
                        └─ obj+0x724==0 → func_002BFFF4  (ppu_recomp_002.cpp:224604)
                                                │
        TODOS os 3 ramos (loc_002C0048 / FFE0 / FFF4) fazem, byte-a-byte idêntico:
        r3='SMPD'(0x534D5044) r4=0x1C(28) r5..r9=0; vm_write32(obj+0x620,0); ─┘
                                                │
                                                ▼
                              func_0043FF30(fourcc,subIdx,p1..p5)   ppu_recomp_001.cpp:399660
                                                │
                          func_0043FCD8(fourcc) → nó da classe (r11) ou 0
                                    (ppu_recomp_001.cpp:399503; lookup em lista ligada)
                                                │
                        r11==0? ──sim──> func_00440028 → func_0043FD80(reason=3,...)
                          │no                              (log/assert "unhandled", sem WAD)
                          ▼
                *(r11+4) > subIdx(0x1C)? ──não──> func_00440018 → func_00440028 →
                          │sim                     func_0043FD80(reason=4,...) (idem, sem WAD)
                          ▼
              slot = *(r11 + 0x10 + subIdx*4)         (= *(r11+0x80) p/ subIdx=0x1C)
              ctr=*(slot+0); r2=*(slot+4); ps3_indirect_call(p1..p5=0,0,0,0,0)
                                                │
                                                ▼
                               func_002D2BB8  (ppu_recomp_001.cpp:54292-54294)
                               { return; }      <-- NO-OP. Fim da cadeia. Nenhum open de WAD.
```

Nenhum WAD-loader, nenhum `cellFs`/FIOS `open`, em qualquer ponto deste grafo.

---

## 2. `func_0043FF30` decodificado — resposta ao Step 1 do brief

**"Tabela de listeners? fila? chamada directa a loader de WAD?"** → **Tabela de
listeners** (um registo de "classes de mensagem" indexadas por fourcc, cada classe com
uma tabela fixa de slots indexados por sub-evento; **não** é fila — não há enfileiramento
nem processamento diferido, o dispatch é síncrono e imediato dentro da própria chamada; e
**não** é uma chamada directa a loader nenhum — é despacho indirecto genérico, reusado
para dezenas de fourccs/sub-eventos diferentes no jogo inteiro).

Corpo (`ppu_recomp_001.cpp:399660-399719`): copia os 7 argumentos (`r3..r9` →
`r31,r30,r26,r25,r29,r28,r27`), chama `func_0043FCD8(ctx)` (só usa `r3`=fourcc; os
outros 6 args recebidos por `func_0043FF30` **não** são passados ao lookup — só servem
para a fase de dispatch, mais abaixo). Se o lookup devolve 0 → `func_00440028`
(ramo "classe nunca registada"). Senão, `r0=*(r11+4)` ("contagem"/capacidade da classe);
gate `r0 > subIdx` (comparação **estritamente maior**, `cr&4`/GT); se falha →
`func_00440018` → `func_00440028` (ramo "classe existe mas sub-índice fora do
alcance registado"). Se passa, `slot = *(r11 + 0x10 + subIdx*4)` (stride 4 bytes/slot,
base do array em `+0x10`), depois `ctr=*(slot+0)`, `r2=*(slot+4)` (2 palavras = OPD PPC64
clássico: {code,toc}) e `ps3_indirect_call` — chamada indirecta ao handler, com
`r3..r7 = p1..p5` (os parâmetros originais do broadcast, aqui todos 0).

`func_0043FF30` é **extremamente reutilizado**: 60 call sites em 6 chunks diferentes
(`ppu_recomp_000/001/002/003/005`, grep completo), a maioria com fourccs diferentes de
`'SMPD'` — confirma que é infra-estrutura genérica do motor, não algo específico do
movie player. Existe um irmão estrutural idêntico, `func_0043FE30`
(`ppu_recomp_001.cpp:399605-399657`), usado 20x noutros sítios — mesmo mecanismo, só
difere nos argumentos fixos de chamada (nenhum dos 20 sítios usa `subIdx=0x1C`; os
valores vistos são `1, 0xB, 0xD, 0x1B` — nunca o slot do Stop).

### Cadeia "classe/sub-índice não encontrado" (`func_00440018`/`func_00440028`/`func_0043FD80`/`func_0043FDF0`)

`func_00440028` (`:399729-399744`) só chama `func_0043FD80` e devolve 0.
`func_0043FD80` (`:399556-399583`) tenta um "handler default" global em
`*(TOC-0x4E4)`; se nulo, cai em `func_0043FDF0` (`:399586-399603`), que tenta um
"handler fallback" em `*(TOC-0x4DC)`; se também nulo, chama `func_004463C8` com uma
format-string de `*(TOC-0x4D8)` + `(reason, fourcc, param, 0)` — assinatura clássica de
diagnóstico/assert de "mensagem sem handler". **Confirmado por leitura estática do ELF**
(secção 4): ambos os globais `TOC-0x4E4` e `TOC-0x4DC` valem 0 no binário como
distribuído — esta cadeia, quando exercida por qualquer fourcc sem classe registada,
resolve sempre no log/assert, nunca num WAD-loader. (Não é o caminho do `'SMPD'`/Stop —
a classe SMPD **está** registada — mas prova que não há um "fallback genérico para abrir
WAD" escondido aqui.)

---

## 3. Os 3 pontos de broadcast do Stop (confirmação byte-a-byte)

| Sítio | Ficheiro:linha | Ramo FSM | r3 | r4(subIdx) | r5..r9 |
|-------|-----------------|----------|----|------------|--------|
| `func_002BFF88`, `loc_002C0048` | `ppu_recomp_001.cpp:36201-36219` | `st620` 1..0xA | `'SMPD'` | `0x1C` | 0,0,0,0,0 |
| `func_002BFFE0` | `ppu_recomp_002.cpp:224585-224595` | `st620==0xB`, `obj+0x724!=0` | `'SMPD'` | `0x1C` | 0,0,0,0,0 |
| `func_002BFFF4` | `ppu_recomp_002.cpp:224628-224638` | `st620==0xB`, `obj+0x724==0` | `'SMPD'` | `0x1C` | 0,0,0,0,0 |

Os 3 sítios escrevem `st620=0` **antes** de chamar `func_0043FF30` (não depois — a
ordem já não importa para o resultado do dispatch, que não toca `st620`). O sítio
realmente exercitado no cenário medido pela Task 1 (cascata até 11, self-stop via
`st620==0xB`) é o par `func_002BFFE0`/`func_002BFFF4` — mas como os 3 são idênticos em
argumentos e ambos os ramos de `func_002C008C` levam ao mesmo `func_0043FF30`, o
resultado do dispatch (secção seguinte) é o mesmo para qualquer um dos 3.

(Nota: `patch_smpd_probe.py` do Task 1, Site C, já tinha identificado esta chamada e os
valores `'SMPD'`/`0x1C` no ramo `loc_002C0048`, por leitura estática independente — bate
certo com a RE aqui, é uma segunda confirmação, não uma repetição às cegas.)

---

## 4. Por que a classe `'SMPD'` está registada (e o gate de contagem passa)

`func_0043FCD8` (`ppu_recomp_001.cpp:399503-399513`) percorre uma lista ligada simples:
`headSlot = *(TOC-0x4E8)` (indirecção: este é o endereço de uma variável estática que
**aponta** para a lista, não a lista em si); `head = *(headSlot+0)`; se `head==0` devolve
0; senão compara `head->campo0(fourcc)` com o argumento, senão continua via
`func_0043FD0C` (`:399516-399523`, campo `+8` = `next`).

**Não existe, em nenhum dos 31 chunks, nenhuma escrita a `[TOC-0x4E8]`** (grep exaustivo
de `gpr[2] + -0x4E8)`, 3 ocorrências no total, todas leituras dentro deste cluster de
funções). A lista só cresce através de `func_00440150` (`ppu_recomp_001.cpp:399823-399848`,
"RegisterClass(node)": faz o mesmo lookup por fourcc; se ainda não existe, tail-call para
`func_004401B8` `:399851-399872`, que percorre até ao fim da lista e liga
`lastNode->next = newNode`). **`func_00440150` tem exactamente UM caller em todo o
lift**: `func_0025C2C0` (`ppu_recomp_000.cpp:541627-541679`), que regista o nó estático
de `'SMPD'` (via `*(TOC-0x216C)`) e, a seguir, já envia `subIdx=1` (um "anúncio de
init", não o slot do Stop) através de `func_0043FE30`.

Cadeia de chamadores de `func_0025C2C0` (também únicos em cada nível, confirmado por
grep):

```
func_0025C2C0                              ppu_recomp_000.cpp:541627  (regista + subIdx=1)
  ← func_002B5508                          ppu_recomp_001.cpp:24787   (wrapper trivial, owner=0)
    ← func_002B2E74                        ppu_recomp_001.cpp:21933   (~11 chamadas de init em linha, sem condição)
      ← func_0025C838                      ppu_recomp_000.cpp:542021  (~9 chamadas de init em linha, sem condição)
        ← ppu_recomp_000.cpp:349  (dentro de func_00010354 @ :248, guest EA ~0x10354 -- muito perto do entry point)
        ← ppu_recomp_004.cpp:1502, :1537   (2 outros sítios)
```

`func_00010354` fica a um punhado de instruções do início do binário (`0x10354`); os
dois dispatchers intermédios (`func_002B2E74`, `func_0025C838`) não têm NENHUM `if`
antes de chamar a sequência — são listas planas de "InitSubsystem()". Isto é
arquitecturalmente equivalente a "corre sempre, muito cedo no boot, antes de qualquer
lógica de filme". Não medi isto por sonda (não é preciso: não há nenhum ramo condicional
em todo o caminho que pudesse desviar da chamada), mas fica registado como inferência de
controlo-de-fluxo, não como facto medido em runtime.

---

## 5. Leitura estática do `EBOOT.ELF` (read-only, sem boot) — resolve o conteúdo de dados

O C do lift mostra só instruções; o valor inicial de `.data`/`.bss` (o "count" da classe,
o conteúdo de cada slot) não aparece em nenhum `grep` de `ppu_recomp_*.cpp` — só existe
no binário. Li o `EBOOT.ELF` local (presente no repo, 5.6 MB, ELF64 BE PPC, `PT_LOAD`
×2) com um script Python read-only (mapeamento `vaddr→file offset` via program headers,
igual ao `be16/be32/be64` + loop de `PT_LOAD` que `ps3recomp/runtime/ppu/ppu_loader.cpp`
já faz) e confirmações pontuais por `xxd -s OFFSET -l 8` (leituras de 8 bytes, nunca o
segmento inteiro). **Isto não é boot nem execução — é leitura de ficheiro, mesma
categoria de "estático" que ler o lift.**

- TOC (`r2`) resolvido via OPD do entry point (`e_entry=0x00519570` → `code=0x00010230
  toc=0x00541178`). Todo este cluster de funções (`func_0043FCD8`…`func_004402xx`)
  partilha este TOC (chamadas directas `bl`, sem recarregar `r2` — só os `ps3_indirect_call`
  poupam/restauram `r2`, precisamente porque só esses podem cruzar módulo).
- `*(TOC-0x4E8)` = `0x00540C90` → conteúdo estático `0x00882788` (endereço em BSS,
  fora do `filesz` do 2º `PT_LOAD`) → `*(0x00882788)` = `0x00000000`. **A lista está
  vazia na imagem inicial** — confirma que o registo é mesmo dinâmico (secção 4), não uma
  tabela estática pré-ligada.
- `*(TOC-0x216C)` = `0x0053F00C` → conteúdo estático `0x00573874` = endereço do nó da
  classe `'SMPD'`. Esse endereço **cai na região `.data` com ficheiro** (2º `PT_LOAD`,
  `vaddr=0x510000 filesz=0x66EA4`), logo o seu conteúdo inicial é lido directamente do
  ficheiro:
  - `+0x00 = 0x534D5044` (**`'SMPD'`** — fourcc do próprio nó, cravado em link-time).
  - `+0x04 = 0x00000023` (35 — o campo "count"/capacidade lido por `func_0043FF30`).
  - `+0x08 = 0x00000000` (`next`, ainda por ligar — bate com a secção 4).
  - Tabela de slots a partir de `+0x10`, stride 4: sondei os índices `0,1,9,0xB,0xD,0x1B,
    0x1C,0x1D,0x1F` — **todos não-nulos**. Como `35 > 0x1C(28)`, o gate de
    `func_0043FF30` para o Stop **passa** — a chamada indirecta acontece de facto.
  - **Slot `0x1C` (`+0x80` = EA `0x005738F4`) = `0x00531110`.**
- OPD em `0x00531110` (`xxd -s 0x521110 -l 8 EBOOT.ELF`) → `code=0x002D2BB8
  toc=0x00541178` → **`func_002D2BB8` (`ppu_recomp_001.cpp:54292-54294`)**, corpo lido
  directamente do lift: `void func_002D2BB8(ppu_context* ctx) { return; }`. Sem
  `trampoline`, sem escrita, sem chamada — **no-op literal**.

### Cross-checks de integridade (para excluir erro de offset/TOC)

1. O fourcc lido do nó (`0x534D5044`) bate **exactamente** com a constante `'SMPD'`
   derivada independentemente do lift (`(0x534D<<16)|0x5044`) — coincidência
   estatisticamente impossível se o TOC ou os offsets estivessem errados.
2. Todo slot lido resolve a uma OPD cuja 2ª palavra é sempre `0x00541178` — o mesmo TOC
   global, em todas as 4 leituras independentes (slots `1, 0xD, 0x1B, 0x1C`).
3. Os slots vizinhos **não** são no-ops: `slot 1 → func_002D53AC`
   (`ppu_recomp_001.cpp:56809`, 46 linhas), `slot 0xD → func_002D5004`
   (`:56666`, 95 linhas), `slot 0x1B → func_002D4714` (`:56123`, 22 linhas) — todos com
   corpo substancial. **Só o slot do Stop (`0x1C`) é vazio** — não é um artefacto de
   "a tabela toda está por preencher"/erro sistemático de leitura; é uma escolha
   específica desse sub-evento.
4. `func_002D2BB8` só aparece uma vez em todo o lift — como entrada na tabela de funções
   (`ppu_recomp_030.cpp:491264`, `{ 0x002D2BB8ULL, func_002D2BB8, "func_002D2BB8" }`),
   nunca chamada directamente (`bl`) por ninguém — consistente com ser invocada
   exclusivamente por despacho indirecto (a única "porta de entrada" é mesmo a OPD no
   slot 0x1C).

---

## 6. Por que não usei a sonda opcional

O brief prioriza RE estática e só pede a sonda dinâmica "se a análise estática não
conseguir decidir a classe do gap". A leitura do ELF acima **decide** a questão que a
sonda visava responder (a classe está registada? o gate de contagem passa? o slot tem
handler?) com uma resposta mais forte do que uma sonda conseguiria dar num único boot:
não é "neste boot em particular o valor lido foi X" — é o **valor cravado no binário**
(link-time constant), correlacionado por 4 cross-checks independentes (secção 5). Um log
em runtime só re-confirmaria o mesmo número já lido directamente do ficheiro, com o custo
extra de build+boot (e o risco de processos presos que o brief pede para evitar). Por
isso não criei `patch_*.py` nenhum nesta task — zero mudanças em `recomp_mid_v2/`,
zero boots.

(Registo para quem revir: o coordenador sugeriu a sonda a meio da task, antes de ver
este resultado; decidi não a aplicar por já ter a resposta com maior confiança por via
estática — ver critério "Prefer STATIC RE first" do próprio brief. Se alguém preferir a
confirmação em runtime mesmo assim, o esqueleto fica fácil: log no entry de
`func_0043FF30` com `r3`(fourcc), `r4`(subIdx) e o valor de `r11` pós-`func_0043FCD8`,
gate `PS3_TRACE_SMPDC`, modelado em `patch_smpd_probe.py`.)

---

## 7. Comparação com o caminho Windows/oráculo

Fonte: `ps3recomp/docs/superpowers/plans/2026-07-14-gow2-intro-movie-fsm-wads.md` +
`gow2-recomp/recomp_mid_v2/bt_intro_wads.sh` (script "congelado" do recipe que já abriu
os WADs historicamente, plataforma Windows/`boot_v2_new.exe`).

| Evento | Windows/oráculo (doc + `bt_intro_wads.sh`) | Mac recomp (medido/RE) |
|--------|---------------------------------------------|--------------------------|
| EOS / done | `FORCE SEQDONE` (vdec, 8s) + `[MOVIEEOS] func_0045B2A8 -> 1 (state-3 gate)` | arm time-based + read-hook em `obj+0x744` (Task 1) |
| Stop / SMPD | **não aparece em nenhum critério de aceite do script** (greps: `FORCE SEQDONE`, `state-3 gate`, `open 'R_LglScA'`, `open 'R_PermA'`, `Invalid shader`, `spu1_miss`, `flips` — zero menções a `SMPD`/`STOP`) | `[STOP] SMPD` sim, medido 3/3 (Task 1); dispatch confirmado por RE, chega a no-op (esta task) |
| Open `R_LglScA` | sim (`r_lgl>=1` no critério de aceite) | não (0/6 boots, Task 1) |
| Gap | O script de referência nunca precisou de rastrear `SMPD` para provar que os WADs abriam — o sinal que ele mede é `state-3 gate`/`FORCE SEQDONE`, não Stop. | O único canal que a Task 1 conseguiu chegar a medir (`[STOP] SMPD`) **não é** o canal que abre WAD — RE prova que o seu handler é no-op. |

Como o `EBOOT.ELF` é o mesmo binário guest PPC independentemente do host (Windows vs
macOS só mudam o runtime/recomp à volta, não o código do jogo), `func_002D2BB8` é
no-op **também** no caminho Windows/oráculo. Logo o mecanismo que abre `R_LglScA`
naquele recipe **não pode** ser este broadcast SMPD — tem de ser outro sinal, mais
provavelmente ligado directamente ao `state-3 gate` (`func_0045B2A8`, já mapeado em
`2026-07-20-st620-3to0-static.md` como dependente de `*(resolve(obj+0x720)+0x1B8)`,
o estado da sessão de decode) ou a um callback OPD do próprio `cellVdec` no SEQDONE —
**não segui essa cadeia nesta task** (está fora do escopo de `func_0043FF30`; é
território do gate/estado-3, já parcialmente mapeado por outra nota).

---

## 8. Classificação do gap

| Classe | Aplica? | Evidência |
|--------|---------|-----------|
| G1 (`'SMPD'` nunca broadcast) | **Não** | 3/3 sítios no lift, todos alcançáveis pelo ramo `st620==0xB` que a Task 1 mediu como exercitado; Task 1 já tinha `[STOP] SMPD` medido em runtime |
| **G2 (broadcast corre; listener existe mas é no-op)** | **SIM — classificação primária, medida** | Cadeia completa Stop→`func_0043FF30`→`func_0043FCD8`→nó `'SMPD'` registado (early-boot)→gate de contagem passa (35>28)→slot 0x1C→`func_002D2BB8`= `{ return; }`. Todos os elos com EA/linha citados; nó de dados cruzado 4x (secção 5.4) |
| G3 (listener corre; open falha em VFS/psarc/path) | Não avaliável — não há open nenhum a falhar, porque não há open | — |
| G4 (Windows usa outro sinal; SMPD irrelevante para WAD) | **Implicação forte, não medida directamente nesta task** | Mesmo binário guest ⇒ mesmo no-op no Windows/oráculo; script de referência nunca mediu SMPD; aponta para `func_0045B2A8`/estado da sessão vdec como o sinal real — fica para a Task 3 confirmar com RE dedicada a esse lado |

**Não abrir/ligar um listener HLE em `func_002D2BB8`** (a tentação óbvia de G2): seria
forjar comportamento — o contrato real deste slot **é** ser vazio; substituí-lo por um
handler que abre WAD divergiria do jogo real (que também não abre WAD por aqui, no
Windows). O trabalho a seguir é RE de quem devia abrir `R_LglScA`/`R_PermA` — que aponta
para o gate `func_0045B2A8`/sessão de decode (`obj+0x720`), não para este dispatcher de
mensagens.

---

## 9. Regras honradas

- Nenhuma escrita em memória guest, nenhum `vm_write` do host: só leituras (lift +
  `EBOOT.ELF` + `xxd`).
- Nenhum boot, nenhum build, nenhuma sonda nova em `recomp_mid_v2/` — decisão justificada
  na secção 6.
- Leituras sempre limitadas (`grep -n` para localizar, `sed -n`/`Read` com range, `xxd -s
  OFFSET -l N` com N pequeno) — nunca um chunk nem um segmento ELF inteiro para dentro do
  contexto; os scripts Python usados para o parse do ELF correm num subprocesso e só
  devolvem resumos pequenos (~30-90 linhas), nunca um dump bruto.
- `patch_st3_probe.py` e `patch_smpd_probe.py` (Task 1) não tocados.
- Não toquei `build_macos.sh`, `movie_eos_arm.c`, `smoke_perf_macos.sh`, `boot_gow2_O1`
  (ficheiros de outras sessões, working tree).
