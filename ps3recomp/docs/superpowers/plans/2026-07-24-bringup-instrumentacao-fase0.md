# Plano de execução — Instrumentação comum + Fase 0 (bring-up do jogo recompilado)

> **Origem.** Este ficheiro decompõe em tarefas executáveis a secção
> **"Instrumentação comum"** e a **"Fase 0 — Baseline do jogo recompilado"** de
> [`docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md`](../../IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md)
> (linhas 90–156). O plano de origem é metodológico (Fases 0–10, checkpoints
> C1–C9) e não tem tarefas numeradas; as tarefas abaixo são a decomposição
> acordada com o autor do projecto. **As Fases 1–10 do plano de origem estão
> FORA do escopo deste ficheiro.**
>
> Branch: `bringup/instrumentacao-fase0`, a partir de `aea878d`.

## Porque esta fatia primeiro

O plano de origem é explícito (linha 154-156): *"Corrigir somente o harness de
reprodução. Não iniciar uma correção do runtime sem um baseline repetível."*
A instrumentação comum é a pré-condição de todas as fases seguintes — sem
formato de log único e sem parser, cada fase volta a inventar as suas próprias
sondas (o repositório já tem **64** `PS3_TRACE_*` ad-hoc, cada um com o seu
formato).

---

## Restrições globais (Global Constraints)

Estas restrições ligam **todas** as tarefas. Qualquer violação é defeito, mesmo
que a tarefa individual não a repita.

### G1 — Formato de linha canónico

Toda linha emitida pela camada nova é **exactamente**:

```text
[PS3T] <tb> | <thr> | <fn> | <subsys> | <event> | <data>
```

| Campo | Conteúdo | Ausente |
|---|---|---|
| `<tb>` | timestamp virtual: valor decimal de `ps3_timebase_now()` (`runtime/syscalls/sys_timer.c:39`), sem separadores de milhar | nunca |
| `<thr>` | thread guest, `0x%08X` | `-` |
| `<fn>` | função guest: nome, ou EA em `0x%08X` | `-` |
| `<subsys>` | um token de G2, maiúsculas | nunca |
| `<event>` | token curto, sem espaços e sem `\|` | nunca |
| `<data>` | pares `k=v` separados por espaço simples | `-` |

- O separador é **exactamente** ` | ` (espaço, pipe, espaço).
- **Nenhum campo pode conter o carácter `|`** — quem formatar `<data>` tem de
  garantir isso (substituir por `/` se surgir).
- O prefixo `[PS3T] ` (sentinela) é obrigatório e é o que permite ao parser
  separar estas linhas das 64 sondas ad-hoc já existentes no mesmo `stderr`.
  Esta sentinela é interpretação nossa do requisito "todos os logs devem
  conter …" do plano de origem (linha 105-109) e tem de ser documentada como tal.
- Uma linha = **uma** chamada de escrita (sem interleaving parcial entre threads).
- Destino: `stderr`. Não inventar `PS3_TRACE_FILE` nem outro destino.

### G2 — Os dez subsistemas e o seu gate

Exactamente estes dez tokens, ligados por exactamente estas variáveis:

| Token `<subsys>` | Variável | Requisito do plano de origem (linha) |
|---|---|---|
| `CHECKPOINTS` | `PS3_TRACE_CHECKPOINTS` | checkpoints de boot (94) |
| `MEMORY` | `PS3_TRACE_MEMORY` | acesso inválido, região, thread e função (95) |
| `HEAP` | `PS3_TRACE_HEAP` | alocação, split, free list e headers (96) |
| `THREADS` | `PS3_TRACE_THREADS` | criação, bloqueio, wakeup e término (97) |
| `SPURS` | `PS3_TRACE_SPURS` | workload, task, worker e fila (98) |
| `DMA` | `PS3_TRACE_DMA` | GET/PUT, tag, EA, LS e conclusão (99) |
| `FIOS` | `PS3_TRACE_FIOS` | operação, buffer, callback e estado (100) |
| `MEDIA` | `PS3_TRACE_MEDIA` | AU, PICOUT, PCMOUT e EOS (101) |
| `RSX` | `PS3_TRACE_RSX` | FIFO, draw, label, fence, flip e present (102) |
| `SNAPSHOT` | `PS3_TRACE_SNAPSHOT` | snapshots guest em checkpoints (103) |

- Ligado sse a variável existe e o primeiro carácter não é `\0` nem `'0'`.
- **Default OFF.** Com todas as variáveis por definir: zero bytes emitidos e
  zero alteração de comportamento observável (no-op no baseline). Isto é a
  regra 6 do `CLAUDE.md` e é critério de aceite de todas as tarefas 1–5.

### G3 — Não regredir os consumidores existentes

`PS3_TRACE_HEAP` e `PS3_TRACE_SPURS` **já existem** e já têm consumidores:

- `runtime/ppu/ppu_loader.cpp:826` — `heap_trace_on()`
- `runtime/syscalls/sys_event.c:15` — gate SPURS
- `runtime/ppu/ppu_hle.cpp:31` — gate SPURS

A camada nova lê as **mesmas** variáveis, em paralelo. **Não alterar, não
renomear e não remover** os gates antigos: ligar `PS3_TRACE_HEAP=1` passa a
produzir as linhas antigas **e** as canónicas. O parser só consome as canónicas
(pela sentinela `[PS3T]`).

### G4 — Plataforma

macOS/arm64 é o alvo primário e é onde tudo tem de ser medido. Windows não pode
regredir. Proibido fora de `#ifdef _WIN32`: `<intrin.h>`, `Sleep(`,
`__declspec`, `GetModuleHandleA`. C11 para `.c`, C++20 para `.cpp`.

### G5 — Honestidade de aceite

Nunca forjar resultados. Todo aceite declara explicitamente o seu nível:
**in-boot** (observado num run do jogo), **offline-unit** (teste isolado) ou
**não-exercitado**. Um call site que nunca foi atingido num boot é
"não-exercitado" — nunca "funciona".

### G6 — Runs do jogo

- Workspace: `../gow2-recomp` (irmão deste repo) — tem `boot_gow2`, `EBOOT.ELF`,
  `extracted/`, `movie_cache/`.
- **Headless obrigatório**: `PS3_NO_RSX=1`. Sem backend Metal/D3D12.
- Timeout controlado; **morte sempre por PID** (`kill -TERM $PID`, depois
  `kill -9 $PID`). **NUNCA** `pkill -f boot_gow2` (o `CLAUDE.md` regista load 75
  por causa disso).
- `exit 124`/morte por sinal é normal — o boot corre em loop até timeout.

### G7 — Git

- Commits **em português**, sem `Co-Authored-By`, sem `Claude-Session`.
- `git add <ficheiro explícito>` — **nunca** `git add -A` nem `git add .`.
- **Nunca** commitar dados de jogo: `EBOOT.ELF`/`.BIN`, `*.psarc`, `*.self`,
  `extracted/`, `movie_cache/`, `spu_images/`, `*.m2v`, `*.wav`, `*.wad_ps3`,
  e **nenhum log de boot bruto** (podem conter dumps de memória do jogo).
- Não commitar ficheiros modificados por outras sessões que não sejam da tarefa.
- Não fazer `git push`.

### G8 — Nada de forçadores por default

Nenhum hook que force FSM, EOS ou flag pode ficar ligado por default. As
variáveis já existentes (`PS3_MOVIE_EOS`, `PS3_VDEC_FORCE_SEQDONE_MS`, …) não
são alteradas por este plano; o baseline regista quais estavam activas.

---

## Task 1 — Núcleo `ps3_trace`: formato único, gating e sentinela

### Objectivo

Criar a camada de emissão que todas as tarefas 2–5 usam, com o formato G1 e o
gating G2, custo zero quando desligada.

### Ficheiros

- **Criar** `include/ps3_trace.h` — cabeçalho público (o `include/` já é
  `target_include_directories(... PUBLIC)`, `CMakeLists.txt:183-185`, portanto
  fica visível a `libs/**` e ao build por-jogo do `ppu_recomp`).
- **Criar** `runtime/trace/ps3_trace.c` — implementação. **Esta localização é
  obrigatória**: `CMakeLists.txt:118-121` faz `GLOB_RECURSE runtime/*.c` e
  `CMakeLists.txt:129` exclui apenas `/tests/`, `/runtime/ppu/` e
  `/runtime/host/`. Um `runtime/trace/*.c` entra na `libps3recomp_runtime.a`
  sem editar o `CMakeLists.txt`; `runtime/host/` **não** entraria, e as
  chamadas a partir de `libs/**` (que estão na lib) ficariam por resolver.
- **Criar** `runtime/trace/tests/test_ps3_trace.c` — teste unitário.

### Requisitos

1. Enum público, nesta ordem exacta (o índice é usado pelo array de gates):

   ```c
   typedef enum {
       PS3_TS_CHECKPOINTS = 0,
       PS3_TS_MEMORY,
       PS3_TS_HEAP,
       PS3_TS_THREADS,
       PS3_TS_SPURS,
       PS3_TS_DMA,
       PS3_TS_FIOS,
       PS3_TS_MEDIA,
       PS3_TS_RSX,
       PS3_TS_SNAPSHOT,
       PS3_TS__COUNT
   } ps3_trace_subsys;
   ```

2. API pública mínima (não acrescentar mais superfície do que isto):

   ```c
   int      ps3_trace_on(ps3_trace_subsys s);                  /* 1 = ligado */
   void     ps3_trace_emit(ps3_trace_subsys s, const char* fn,
                           const char* event, const char* fmt, ...);
   void     ps3_trace_set_thread(uint32_t guest_tid);          /* TLS */
   uint32_t ps3_trace_thread(void);                            /* 0 = desconhecido */

   #define PS3_TRACE(s, fn, ev, ...) \
       do { if (ps3_trace_on(s)) ps3_trace_emit((s), (fn), (ev), __VA_ARGS__); } while (0)
   ```

   - `fn == NULL` → campo `-`. `ps3_trace_thread() == 0` → campo `-`.
   - `fmt == NULL` ou string vazia → `<data>` é `-`.
   - `ps3_trace_emit` tem de ter `__attribute__((format(printf, 4, 5)))` (ou
     equivalente sob `#ifdef __GNUC__`).

3. **Gating barato**: cache estática por subsistema resolvida uma vez
   (`getenv` no primeiro uso), sem `getenv` no caminho quente. `ps3_trace_on`
   de um subsistema desligado não pode escrever nada nem alocar.

4. **Timestamp**: `ps3_timebase_now()` — declarado/definido em
   `runtime/syscalls/sys_timer.c:39`. Declarar `extern uint64_t
   ps3_timebase_now(void);` no `.c` se não houver cabeçalho que o exponha
   (verificar antes de duplicar a declaração).

5. **Thread guest**: armazenada em TLS (`_Thread_local` C11; em Apple/clang
   está disponível). `ps3_trace_set_thread` é chamado pelas tarefas seguintes;
   aqui só existe o mecanismo.

6. **Atomicidade de linha**: formatar para um buffer de pilha (máx. 1024 bytes,
   truncar com `…` se exceder) e emitir com **uma** chamada `fwrite` seguida de
   `fflush(stderr)`. Sem `fprintf` múltiplos por linha.

7. **Sanitização**: qualquer `|` produzido em `<fn>`, `<event>` ou `<data>` é
   substituído por `/` antes de emitir. `\n` e `\r` são substituídos por espaço.

8. `ps3_trace_subsys_name(s)` interno (ou público se o teste precisar) devolve
   o token exacto de G2.

### Testes (offline-unit)

`runtime/trace/tests/test_ps3_trace.c`, seguindo a convenção já existente em
`runtime/spu/tests/` (ficheiro `test_*.c` com `main()`, comando de compilação
documentado no cabeçalho do ficheiro; **não** introduzir framework novo).
Casos obrigatórios:

- **T1.1** Todas as vars por definir → `ps3_trace_on(s) == 0` para os 10, e um
  `PS3_TRACE(...)` de cada subsistema produz **0 bytes** no `stderr` capturado.
- **T1.2** `PS3_TRACE_RSX=1` → a linha emitida casa exactamente com
  `^\[PS3T\] [0-9]+ \| (0x[0-9A-F]{8}|-) \| [^|]* \| RSX \| [^| ]+ \| .*$`.
- **T1.3** Ligar `PS3_TRACE_HEAP` **não** liga nenhum dos outros nove.
- **T1.4** `fn=NULL`, thread não definida e `fmt=NULL` produzem `-` nos três campos.
- **T1.5** `|` em `<data>` sai como `/`; `\n` sai como espaço; a linha continua
  a ter exactamente 5 ocorrências de ` | `.
- **T1.6** `ps3_trace_set_thread(0x12345678)` reflecte-se em `<thr>` como
  `0x12345678`, e uma segunda thread não vê o valor da primeira.
- **T1.7** Os 10 tokens de `<subsys>` são exactamente os de G2 (comparação
  string a string contra um array literal no teste).

### Aceite

- `cmake -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release . && cmake --build
  build-macos -j$(sysctl -n hw.ncpu)` produz `libps3recomp_runtime.a` e
  `nm build-macos/libps3recomp_runtime.a | grep ps3_trace_emit` mostra o
  símbolo definido (`T`).
- O teste corre e imprime `0 failed` (ou equivalente da convenção usada).
- Nenhum ficheiro existente foi modificado excepto, se necessário, nada — esta
  tarefa é aditiva.

### Fora de escopo

Não instrumentar nenhum call site nesta tarefa. Não tocar em `CMakeLists.txt`
(se for mesmo necessário, justificar no relatório).

---

## Task 2 — Subsistemas `CHECKPOINTS` e `SNAPSHOT`

### Objectivo

Marcar a espinha do boot com checkpoints ordenados e emitir um snapshot do
estado guest em cada checkpoint quando pedido. "Último checkpoint" é uma das
dez métricas obrigatórias da Fase 0 (plano de origem, linha 140).

### Requisitos

1. Lista fechada de checkpoints, cada um emitido **no máximo uma vez** por run,
   com `<event>` exactamente igual ao nome:

   | `<event>` | Sítio esperado |
   |---|---|
   | `ELF_LOADED` | fim do carregamento do ELF guest |
   | `VM_READY` | memória guest mapeada/comprometida |
   | `TLS_INIT` | TLS guest inicializado |
   | `GUEST_ENTRY` | primeira instrução guest do entry point |
   | `FIRST_THREAD` | primeiro `sys_ppu_thread_create` bem sucedido (`runtime/syscalls/sys_ppu_thread.c:140`) |
   | `FIRST_FS_OPEN` | primeiro `cellFsOpen` bem sucedido (`libs/filesystem/cellFs.c:318`) |
   | `FIRST_WAD_OPEN` | primeiro open de WAD (`movie_io_open` / caminho `R_LglScA`/`R_PermA`) |
   | `FIRST_SPU_JOB` | primeiro job SPU despachado |
   | `FIRST_DRAW` | primeiro draw submetido ao backend RSX |
   | `FIRST_FLIP` | primeiro `cellGcmSetFlipCommand` (`libs/video/cellGcmSys.c:909`) |
   | `FIRST_PRESENT` | primeiro present concluído no backend host |
   | `MOVIE_EOS` | EOS da intro observado |
   | `SHUTDOWN` | encerramento ordenado, se ocorrer |

   `<data>` leva contexto útil (`path=…`, `tid=…`, `ea=0x…`, `buf=…`).

2. **Localizar os sítios reais antes de escrever**. Os anchors da tabela foram
   verificados para `sys_ppu_thread_create`, `cellFsOpen` e
   `cellGcmSetFlipCommand`; os restantes têm de ser encontrados e o
   `ficheiro:linha` escolhido **registado no relatório**. Um checkpoint sem
   sítio natural identificável é declarado **não-exercitado/sem sítio** no
   relatório — **não** inventar um sítio artificial só para o emitir.

3. Alguns sítios estão em `runtime/ppu/ppu_loader.cpp` (2824 linhas), que é
   **código nosso protegido** (`CLAUDE.md`: nunca sobrescrever com upstream).
   Editar cirurgicamente, nunca reformatar. Ficheiros `ppu_recomp_*.cpp`
   gerados estão **fora de escopo** (são gitignored).

4. `SNAPSHOT`: quando `PS3_TRACE_SNAPSHOT=1`, **cada** checkpoint emitido é
   imediatamente seguido de uma linha `SNAPSHOT` com `<event>` igual ao nome do
   checkpoint e `<data>` com pelo menos: `tb=`, `threads=` (nº de threads guest
   vivas), `heap_used=`, `last_flip=` (contador de flips até ao momento). Se
   algum destes contadores não existir no runtime, usar o valor real disponível
   e declarar no relatório qual foi omitido e porquê — **não** inventar zeros
   silenciosos.

5. `SNAPSHOT` desligado (default) → nenhuma linha extra. `CHECKPOINTS`
   desligado → nem checkpoint nem snapshot.

6. Ligar `ps3_trace_set_thread()` no ponto onde uma thread guest começa a
   executar, para que `<thr>` deixe de ser `-` no resto das tarefas.

### Testes

- **Offline-unit**: teste que exercita o helper "emitir uma vez" (once-guard) —
  duas chamadas ao mesmo checkpoint produzem uma só linha; checkpoints
  diferentes produzem linhas diferentes; com `CHECKPOINTS` off, zero bytes.
- **In-boot**: um run headless de ≤40 s com
  `PS3_TRACE_CHECKPOINTS=1 PS3_TRACE_SNAPSHOT=1` sobre `../gow2-recomp`
  (ver G6 e o `env_gow2.sh` de lá). Registar no relatório **quais** checkpoints
  apareceram, por que ordem, e quais não apareceram.

### Aceite

- Build macOS verde.
- Run headless produz ≥ `ELF_LOADED`, `VM_READY`, `GUEST_ENTRY` e
  `FIRST_THREAD` no formato G1 (colar as linhas reais no relatório).
- Baseline (todas as vars off) idêntico ao anterior: mesmo comportamento, zero
  linhas `[PS3T]`.

---

## Task 3 — Subsistemas `MEMORY` e `HEAP`

### Objectivo

Instrumentar acesso inválido de memória e o allocator guest, os dois insumos
da Fase 1 do plano de origem e duas métricas do baseline (nº de OOB, primeiro
endereço inválido).

### Requisitos

1. `MEMORY` — o plano de origem pede "acesso inválido, região, thread e função"
   (linha 95). Sítio já existente e verificado: `runtime/ppu/ppu_loader.cpp:393`
   (`[vm] OOB access 0x%08X (+%u)`), e o contexto extra em
   `ppu_loader.cpp:407` (`[OOBRA] @0x%08X`). Emitir, em paralelo (sem remover a
   linha antiga):

   ```text
   [PS3T] <tb> | <thr> | <fn> | MEMORY | OOB | ea=0x%08X len=%u region=<nome> op=<read|write>
   ```

   `region` é o nome da região guest que **deveria** conter o EA (ou
   `unmapped`). Se o runtime não tiver um mapa de regiões consultável, derivar
   do que existe em `runtime/memory/vm.h` (309 linhas) e declarar a fonte no
   relatório.

   Nota: `ppu_loader.cpp:393` tem um limitador `logged < 40`. A linha canónica
   **não** deve herdar esse limite silenciosamente; se um limite for necessário
   por volume, tem de ser explícito, documentado e emitir um evento
   `OOB_TRUNCATED` com a contagem total quando corta.

2. `HEAP` — "alocação, split, free list e headers" (linha 96). Eventos
   obrigatórios: `ALLOC`, `FREE`, `SPLIT`, `COALESCE`, `FREELIST`, `HEADER`.
   `<data>` com `ea=`, `size=`, `align=`, `arena=` conforme aplicável.
   Localizar o allocator guest real (o `CLAUDE.md` descreve free-list, cursor,
   magic e arena — a região à volta de `func_002550C8` é histórico do bug já
   corrigido) e instrumentar os pontos existentes, **sem** alterar a lógica de
   alocação. Se o allocator for código guest liftado (não host), então o que se
   instrumenta é o lado host que o observa; declarar isso claramente no
   relatório em vez de forçar.

3. Não alterar `heap_trace_on()` (`ppu_loader.cpp:826`) nem o comportamento das
   linhas antigas — G3.

### Testes

- **Offline-unit**: teste que dispara o caminho `MEMORY/OOB` com um EA fora do
  mapa e verifica o formato G1 e os campos `ea`/`len`/`region`; e um teste de
  `HEAP` para os eventos que forem instrumentáveis fora do boot.
- **In-boot**: run headless com `PS3_TRACE_MEMORY=1` e outro com
  `PS3_TRACE_HEAP=1`; colar amostras reais no relatório e reportar a contagem
  de linhas de cada.

### Aceite

- Build verde; baseline (vars off) sem alteração de comportamento nem output.
- Pelo menos o evento `MEMORY/OOB` provado **in-boot** com linha real colada.
- Todo evento `HEAP` não atingido num boot é declarado **não-exercitado**.

---

## Task 4 — Subsistemas `THREADS`, `SPURS` e `DMA`

### Objectivo

Tornar visível o progresso de concorrência: quem foi criado, quem bloqueou, quem
acordou, que jobs SPU correram e que transferências DMA ficaram pendentes.
Alimenta duas métricas do baseline (threads criadas, jobs SPU executados).

### Requisitos

1. `THREADS` — "criação, bloqueio, wakeup e término" (linha 97). Eventos:
   `CREATE`, `START`, `BLOCK`, `WAKE`, `EXIT`, `JOIN`. `<data>` obrigatório:
   `tid=`, e para `BLOCK` também `on=<mutex|cond|sema|eventflag|timer|…>` e
   `obj=0x%08X` (o objecto de espera). Sítios: `runtime/syscalls/sys_ppu_thread.c`
   (`sys_ppu_thread_create` em `:140`), `sys_mutex.c`, `sys_cond.c`,
   `sys_semaphore.c`, `sys_event.c`, `sys_rwlock.c`.
   Todo `BLOCK` tem de ter motivo e objecto registados — é requisito explícito
   do checkpoint C3 do plano de origem ("não existe thread esperando sem motivo
   registrado", linha 285-286).

2. `SPURS` — "workload, task, worker e fila" (linha 98). Eventos: `WORKLOAD_ADD`,
   `TASK_CREATE`, `TASK_START`, `TASK_END`, `WORKER_IDLE`, `QUEUE_PUSH`,
   `QUEUE_POP`. Sítios em `libs/spurs/` (`cellSpurs.c`, `cellSpursJq.c`,
   `cellSpursLFQueue.c`, `cellSpursTaskset*`).
   **G3**: não mexer nos gates `PS3_TRACE_SPURS` existentes
   (`runtime/syscalls/sys_event.c:15`, `runtime/ppu/ppu_hle.cpp:31`).

3. `DMA` — "GET/PUT, tag, EA, LS e conclusão" (linha 99). Eventos: `GET`, `PUT`,
   `LIST`, `TAG_WAIT`, `DONE`. Sítios no motor MFC do SPU (`runtime/spu/`, ver
   `spu_dma.h`/`mfc_engine` usado em `runtime/spu/tests/`).

   **Campos obrigatórios de `<data>`, por evento** (emenda de 2026-07-24, decidida
   pelo autor do projecto durante a execução da Task 4):

   | Evento | `<data>` obrigatório |
   |---|---|
   | `GET`, `PUT`, `LIST`, `DONE` | `ea=0x%08X`, `lsa=0x%05X`, `size=`, `tag=%u` |
   | `TAG_WAIT` | `mask=0x%08X`, `mode=%u`, `result=0x%08X` |

   Porquê a excepção: uma espera por máscara de tags não tem um EA, um LSA nem um
   tamanho únicos — espera por várias transferências de uma vez. Preencher
   `ea=0x00000000 size=0` seria inventar campos, o que a regra 4 do `CLAUDE.md`
   proíbe ("NUNCA forjar resultados"). O contrato honesto por evento vale mais ao
   parser da Task 6 do que um conjunto de campos uniforme onde metade nunca
   transporta informação.

   **`TAG_WAIT` não usa a chave `tag=` — usa `mask=`.** A assinatura real é
   `mfc_tag_wait(mfc, tag_mask, update_type)`: existe um único valor, que é uma
   *bitmask* de até 32 tags, mais o modo de update. Reutilizar `tag=` para essa
   máscara seria dar a mesma chave a dois domínios incompatíveis — `GET`/`PUT`
   emitem `tag=%u` (inteiro pequeno, um id) e a máscara é `0x%08X` — e um parser
   que emparelhasse transferências por `tag=` emparelharia mal. `mode=` é o
   `update_type` (IMMEDIATE/ANY/ALL) e `result=` é a máscara já completada.

   (Correcção de 2026-07-24: a primeira versão desta emenda listava `tag=` **e**
   `mask=` para o `TAG_WAIT`, o que inventava um segundo valor que não existe na
   função. Erro de quem escreveu a emenda, não do implementer.)

4. Emissão só sob o gate respectivo; nenhum dos três pode ligar outro.

### Testes

- **Offline-unit**: os testes SPU do repositório já constroem um `mfc_engine`
  (`runtime/spu/tests/`) — acrescentar um teste que corre uma transferência
  GET+PUT com `PS3_TRACE_DMA=1` e verifica as linhas `GET`/`PUT`/`DONE` com
  `ea`/`lsa`/`size`/`tag` correctos, e que com o gate off não sai nada.
- **Offline-unit**: teste de `THREADS` que cobre `CREATE` + `BLOCK` (com `on=` e
  `obj=`) + `WAKE` num mutex ou semáforo do runtime.
- **In-boot**: run headless com `PS3_TRACE_THREADS=1 PS3_TRACE_SPURS=1
  PS3_TRACE_DMA=1`; contar threads criadas e jobs SPU e colar amostras.
- Correr a suite SPU existente (`runtime/spu/tests/run_tests.sh`) e reportar o
  resultado — não pode regredir.

### Aceite

- Build verde; suite SPU sem novas falhas (colar a linha `Results:`).
- `THREADS/CREATE` e pelo menos um `BLOCK` com motivo+objecto provados
  **in-boot**.
- Eventos não atingidos declarados **não-exercitado**.

---

## Task 5 — Subsistemas `FIOS`, `MEDIA` e `RSX`

### Objectivo

Fechar os três subsistemas do caminho de dados e de imagem. Alimenta três
métricas do baseline (WADs abertos, frames apresentados) e o "último
flip/present" do parser.

### Requisitos

1. `FIOS` — "operação, buffer, callback e estado" (linha 100). Eventos:
   `OP_BEGIN`, `READ`, `PARTIAL`, `CALLBACK`, `CANCEL`, `OP_END`. `<data>`:
   `op=`, `path=`, `off=`, `len=`, `buf=0x%08X`, `state=`. Sítios:
   `libs/filesystem/cellFs.c` (`cellFsOpen` em `:318`) e o caminho
   `movie_io_*` em `libs/video/movie_hle.c` (é por onde os WADs `R_LglScA` /
   `R_PermA` passam — ver `CLAUDE.md`, paredes B e C).
   O evento de abertura de WAD tem de ser distinguível (ex.: `<data>` com
   `wad=1`), porque "WADs abertos" é métrica do baseline.

2. `MEDIA` — "AU, PICOUT, PCMOUT e EOS" (linha 101). Eventos: `AU_IN`,
   `PICOUT`, `PCMOUT`, `AUDONE`, `SEQDONE`, `EOS`. Sítios: `libs/codec/cellVdec.c`
   (o `SEQDONE` single-shot está em `:55` e `:160`) e `libs/codec/cellAdec.c:93`.
   **G8**: não alterar a semântica de `PS3_VDEC_FORCE_SEQDONE_MS` nem de
   `PS3_MOVIE_EOS`; a instrumentação tem de distinguir no `<data>` se o
   `SEQDONE`/`EOS` foi **natural** ou **forçado** (`src=natural|forced`) — essa
   distinção é exactamente o que a Fase 6 do plano de origem vai precisar.

3. `RSX` — "FIFO, draw, label, fence, flip e present" (linha 102). Eventos:
   `FIFO_KICK`, `DRAW`, `LABEL`, `FENCE`, `FLIP_REQ`, `FLIP_DONE`, `PRESENT`.
   O plano de origem exige separar `flip requested`, `flip complete` e
   `frame presented` (linha 447) — daí os três eventos distintos. Sítios:
   `libs/video/cellGcmSys.c` (`cellGcmSetFlipCommand` em `:909`,
   `_cellGcmSetFlipCommand` em `:1618`) e o backend host.
   Em headless (`PS3_NO_RSX=1`) o `PRESENT` pode nunca ocorrer — isso é um
   facto a registar, não um bug a contornar.

### Testes

- **Offline-unit**: teste de `MEDIA` que verifica `src=natural` vs `src=forced`
  no `SEQDONE`; teste de `FIOS` que verifica `wad=1` no open de WAD e o formato
  dos campos.
- **In-boot**: run headless com `PS3_TRACE_FIOS=1 PS3_TRACE_MEDIA=1
  PS3_TRACE_RSX=1`. Colar as linhas de open dos WADs (`R_LglScA`, `R_PermA`) e
  do EOS da intro. Registar se houve `FLIP_REQ`/`FLIP_DONE`/`PRESENT`.

### Aceite

- Build verde; baseline (vars off) sem alteração.
- Open de pelo menos um WAD provado **in-boot** com linha real colada.
- Estado de `PRESENT` em headless declarado honestamente (**não-exercitado** se
  não ocorrer).

---

## Task 6 — Parser de logs `tools/trace_parse.py`

### Objectivo

Produzir, a partir de um log de run, exactamente as oito derivações que o plano
de origem exige (linhas 111-120).

### Requisitos

1. **Criar** `tools/trace_parse.py`. Python ≥ 3.11 (é o mínimo já fixado em
   `pyproject.toml`, commit `0869c75`). Só stdlib — o `pyproject.toml` regista
   `dev = []` medido; não introduzir dependências.

2. CLI:

   ```text
   python3 tools/trace_parse.py LOG [--json] [--baseline BASELINE.json]
   ```

   - sem `--json`: resumo humano legível;
   - com `--json`: um objecto JSON em `stdout`, sem mais nada;
   - `--baseline`: activa a oitava derivação (diff).

3. As oito derivações, com estas chaves JSON exactas:

   | Chave | Conteúdo |
   |---|---|
   | `primeiro_erro` | primeira linha de erro do log (canónica ou ad-hoc), com `tb`, texto e nº de linha |
   | `primeiro_ea_invalido` | primeiro `MEMORY/OOB`: `ea`, `tb`, `thr`, `fn` |
   | `ultima_transicao_estado` | último `CHECKPOINTS` emitido (nome + `tb`) |
   | `threads_bloqueadas` | threads com `BLOCK` sem `WAKE`/`EXIT` posterior: `tid`, `on`, `obj`, `tb` |
   | `dma_pendentes` | `GET`/`PUT` sem `DONE` do mesmo `tag`: `tag`, `ea`, `lsa`, `size` |
   | `fios_pendentes` | `OP_BEGIN` sem `OP_END` correspondente: `op`, `path`, `state` |
   | `ultimo_flip_present` | último `FLIP_REQ`, `FLIP_DONE` e `PRESENT` (os três, separados) |
   | `diff_baseline` | diferenças face ao run aprovado (`null` sem `--baseline`) |

4. O parser consome **linhas canónicas** (sentinela `[PS3T]`, G1). Linhas
   ad-hoc só são usadas para `primeiro_erro` (heurística documentada no
   cabeçalho do ficheiro: que padrões conta como erro e porquê).

5. Robustez: linha malformada não pode fazer o parser rebentar — conta para um
   `linhas_ignoradas` no output. Log de 100 MB tem de correr sem carregar tudo
   em memória (streaming linha a linha).

6. Emparelhamento por `tag` (DMA) e por `op` (FIOS) tem de tolerar reutilização
   de tag: um `GET` novo com tag já pendente fecha o anterior como
   `reutilizado` em vez de duplicar entradas.

### Testes

`tools/tests/test_trace_parse.py` (ou a convenção `test_*.py` já usada em
`tools/` — verificar e seguir). Fixtures sintéticas em texto, escritas pelo
próprio teste (nada de logs de jogo commitados — G7). Casos:

- **T6.1** log vazio → todas as chaves presentes, valores nulos/vazios, sem excepção.
- **T6.2** `MEMORY/OOB` × 3 → `primeiro_ea_invalido` é o de menor `tb`.
- **T6.3** `BLOCK` sem `WAKE` → aparece em `threads_bloqueadas`; com `WAKE`
  posterior → não aparece.
- **T6.4** `GET tag=3` sem `DONE tag=3` → pendente; com `DONE` → não pendente.
- **T6.5** reutilização de `tag=3` sem `DONE` → uma entrada marcada `reutilizado`.
- **T6.6** `FLIP_REQ` sem `FLIP_DONE` → `ultimo_flip_present.flip_done` é nulo.
- **T6.7** linha truncada a meio e linha sem sentinela → `linhas_ignoradas` ≥ 1
  e o resto do parse continua correcto.
- **T6.8** `--baseline` com um run igual → `diff_baseline` vazio; com um run que
  perdeu um checkpoint → diff aponta esse checkpoint.
- **T6.9** `--json` produz JSON válido e **nada** mais em `stdout`.

### Aceite

- Todos os testes passam com o Python do `.venv` (`python3 -m pytest` ou o
  runner da convenção do repo — reportar o comando exacto e a saída).
- Correr o parser sobre um log real produzido na Task 5 e colar o resumo no
  relatório (**in-boot** para o parser).

---

## Task 7 — Harness de baseline

### Objectivo

Automatizar os passos 1–5 da Fase 0 (plano de origem, linhas 130-146): gerar,
compilar, executar com timeout, repetir ≥3 vezes e registar as dez métricas.

### Requisitos

1. **Criar** `scripts/baseline_run.sh`:
   - `set -euo pipefail`, portável a `bash` do macOS (3.2) — nada de `mapfile`
     nem de `${var,,}`;
   - `usage: baseline_run.sh [--runs N] [--timeout S] [--out DIR]`, com
     `N=3` e `S=60` por default;
   - **Passo 1-2 (gerar + compilar)**: verificar se `../gow2-recomp/boot_gow2`
     está mais recente do que as fontes de que depende; se não estiver,
     correr o build de lá (`build_macos.sh`) e **medir a duração**. Se uma
     regeneração completa (re-lift de todos os chunks) for necessária e
     exceder 45 min, **não** a executar: registar o custo medido/estimado e
     prosseguir com o binário existente, declarando a divergência no relatório.
     Registar sempre a proveniência do binário: caminho, `sha256`, mtime e o
     `git rev-parse HEAD` de ambos os repos;
   - **Passo 3-4**: N runs headless (G6): `PS3_NO_RSX=1`, todos os dez
     `PS3_TRACE_*` ligados, timeout `S`, morte por PID (`kill -TERM`, esperar,
     `kill -9`), **nunca** `pkill`;
   - carregar o ambiente canónico de `../gow2-recomp/env_gow2.sh` e registar
     **exactamente** que variáveis ficaram activas (incluindo forçadores como
     `PS3_MOVIE_EOS`/`PS3_VDEC_FORCE_SEQDONE_MS`) — G8;
   - logs para `--out DIR` (default: directório temporário fora do repo). **Os
     logs não são commitados** (G7).

2. **Criar** `tools/baseline_report.py` — consome os logs dos N runs (usando
   `tools/trace_parse.py` como módulo, não por `subprocess`) e escreve
   `baseline.json` com este schema exacto:

   ```json
   {
     "schema": "ps3recomp.baseline/1",
     "jogo": "gow2",
     "gerado_em": "<ISO-8601>",
     "commit_engine": "<sha>",
     "commit_jogo": "<sha>",
     "binario": { "path": "...", "sha256": "...", "mtime": "..." },
     "env": { "<VAR>": "<valor>" },
     "runs": [
       {
         "idx": 1,
         "duracao_s": 0.0,
         "primeiro_erro": null,
         "ultimo_checkpoint": null,
         "oob": 0,
         "imports_nao_resolvidos": 0,
         "instrucoes_desconhecidas": 0,
         "threads_criadas": 0,
         "jobs_spu": 0,
         "wads_abertos": 0,
         "frames_apresentados": 0
       }
     ],
     "barreira": {
       "igual_nos_runs": true,
       "descricao": "...",
       "variacao_explicada": null
     }
   }
   ```

   As dez métricas por run são exactamente as dez do plano de origem
   (linhas 137-146). Métrica cuja fonte não exista no runtime actual (candidato
   provável: `instrucoes_desconhecidas`, que é grandeza de *lift-time* e não
   de run-time) leva `null` — **nunca** `0` fabricado — e o motivo fica
   documentado no cabeçalho do `baseline_report.py`.

3. `barreira.igual_nos_runs` é `true` sse os N runs pararam no mesmo
   `ultimo_checkpoint` **e** com o mesmo `primeiro_erro` (comparado por forma
   normalizada, não por texto exacto com endereços). Quando `false`,
   `variacao_explicada` tem de ser preenchido por quem corre — o campo não pode
   ficar `null` com `igual_nos_runs: false`.

### Testes (offline-unit)

- `tools/tests/test_baseline_report.py`: fixtures de 3 logs sintéticos →
  `baseline.json` com o schema exacto; caso "mesma barreira" → `igual_nos_runs:
  true`; caso "checkpoints diferentes" → `false`; métrica sem fonte → `null`
  (nunca `0`); JSON validado contra as chaves obrigatórias.
- `scripts/baseline_run.sh --help` funciona sem `../gow2-recomp` presente
  (falha limpa e explícita se o workspace faltar, não stack trace).

### Aceite

- Testes verdes (colar comando + saída).
- `bash -n scripts/baseline_run.sh` limpo e `shellcheck` (se disponível) sem
  erros de severidade `error`.
- **Não** correr ainda os 3 runs formais — isso é a Task 8. Um run curto de
  fumo para validar o harness é permitido e deve ser reportado.

---

## Task 8 — Execução do baseline e relatório da Fase 0

### Objectivo

Produzir o artefacto de aceite da Fase 0: `baseline.json` real, medido, com os
três runs, e o relatório no formato obrigatório do plano de origem.

### Requisitos

1. Correr `scripts/baseline_run.sh --runs 3` (G6: headless, timeout, kill por
   PID). Se um run abortar por causa da flakiness conhecida ("vm commit
   (sys_memory window) failed" — `CLAUDE.md`), repetir esse run e **registar**
   quantas repetições foram precisas; não esconder.

2. Produzir `docs/bringup/gow2/FASE0/baseline.json` (o artefacto de aceite) —
   **verificar antes de commitar** que não contém dados do jogo, caminhos
   absolutos da máquina do autor, nem excertos de memória guest; se contiver,
   sanear (caminhos relativos ao repo) e dizê-lo no relatório.

3. Produzir `docs/bringup/gow2/FASE0/attempt-1.md` no formato **exacto** do
   plano de origem (linhas 573-597): `# Checkpoint: …`, `## Hipótese`,
   `## Baseline`, `## Alteração`, `## Testes unitários`,
   `## Teste de integração`, `## Resultado esperado`, `## Resultado observado`,
   `## Primeiro evento divergente`, `## Evidências`, `## Decisão`,
   `## Próxima tentativa`.
   O relatório tem de responder explicitamente ao critério de aceite da Fase 0
   (linha 150-152): *"Existe um `baseline.json` e os três runs apresentam a
   mesma barreira ou uma variação explicada."*

4. Actualizar a secção Fase 0 de
   `docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md` com o estado real
   (concluída / bloqueada) e um ponteiro para os artefactos — o plano de origem
   exige "atualização deste plano ou do índice de progresso" (linha 86).

5. Nas evidências, cada afirmação declara o seu nível (G5) e cola a linha real
   do log ou o comando + saída. Nenhuma métrica pode ser inferida.

### Aceite

- `docs/bringup/gow2/FASE0/baseline.json` existe, valida contra o schema da
  Task 7 e tem 3 entradas em `runs` com valores **medidos**.
- `barreira.igual_nos_runs` está preenchido e, se `false`,
  `variacao_explicada` também.
- `attempt-1.md` tem as 11 secções do modelo, na ordem do modelo.
- Nenhum dado de jogo nem log bruto foi commitado (G7).

---

## Nota sobre o que este plano **não** faz

- Não corrige nada no runtime. Instrumentação é aditiva e gated; qualquer fix
  encontrado pelo caminho é registado, não aplicado (é matéria da Fase 1+).
- Não toca no lifter, no allocator, no scheduler nem no backend RSX.
- Não altera o comportamento default de nenhuma variável já existente.
- Não faz `git push`.
