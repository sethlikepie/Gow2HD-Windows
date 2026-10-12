# Plano de implementação: bring-up confiável do jogo recompilado

## Objetivo

Levar o jogo recompilado, especialmente o GoW2, de boot parcial até execução
estável, removendo as barreiras na ordem causal correta:

```text
memória/heap
  -> threads e scheduler
  -> SPURS/SPU/DMA
  -> FIOS e WADs
  -> mídia/EOS
  -> RSX/render/present
  -> input/menu/gameplay
  -> save/load/encerramento
```

Cada etapa deve ser executada por um agente com o mesmo ciclo:

```text
observar -> formular hipótese -> instrumentar -> implementar
    -> testar isoladamente -> executar checkpoint
    -> comparar resultado -> aceitar ou reverter
```

Não é permitido considerar uma etapa concluída apenas porque o processo avançou
mais alguns segundos. O avanço precisa ser acompanhado do efeito guest esperado.

---

## Regras de execução para o agente

### Antes de alterar código

O agente deve:

1. verificar o estado do worktree;
2. identificar o último checkpoint aprovado;
3. executar o caso de reprodução atual;
4. salvar log, duração, checkpoint alcançado e primeiro erro;
5. formular uma hipótese única e testável.

Uma tentativa não deve alterar simultaneamente memória, SPURS, FIOS e RSX.

### Para cada tentativa

O agente deve registrar:

```text
tentativa: N
checkpoint de origem:
barreira:
hipótese:
arquivos alterados:
instrumentação usada:
teste unitário:
teste de integração:
resultado esperado:
resultado observado:
primeiro evento divergente:
decisão: aceitar / ajustar / reverter / bloquear
```

### Política de repetição

- Se o teste falhar por compilação: corrigir antes de executar o jogo.
- Se falhar deterministicamente no mesmo evento: revisar a hipótese e ajustar.
- Se falhar em evento diferente: investigar race, timing ou corrupção anterior.
- Se três tentativas falharem pela mesma causa, parar a etapa e produzir um
  relatório de bloqueio com evidências.
- Nunca adicionar um hook que force uma FSM, EOS ou flag sem registrar a causa e
  criar um teste que prove o comportamento natural equivalente.
- Workarounds experimentais devem ser protegidos por flag e não podem ser o
  comportamento padrão.

### Resultado obrigatório de cada etapa

Cada etapa produz:

- código;
- teste novo ou ampliado;
- log de execução;
- checkpoint de aceite;
- relatório curto de causa/efeito;
- atualização deste plano ou do índice de progresso.

---

## Instrumentação comum

Antes das fases de correção, implementar ou padronizar:

- `PS3_TRACE_CHECKPOINTS=1` para checkpoints de boot;
- `PS3_TRACE_MEMORY=1` para acesso inválido, região, thread e função;
- `PS3_TRACE_HEAP=1` para alocação, split, free list e headers;
- `PS3_TRACE_THREADS=1` para criação, bloqueio, wakeup e término;
- `PS3_TRACE_SPURS=1` para workload, task, worker e fila;
- `PS3_TRACE_DMA=1` para GET/PUT, tag, EA, LS e conclusão;
- `PS3_TRACE_FIOS=1` para operação, buffer, callback e estado;
- `PS3_TRACE_MEDIA=1` para AU, PICOUT, PCMOUT e EOS;
- `PS3_TRACE_RSX=1` para FIFO, draw, label, fence, flip e present;
- `PS3_TRACE_SNAPSHOT=1` para snapshots guest em checkpoints.

Todos os logs devem conter:

```text
timestamp virtual | thread guest | função guest | subsistema | evento | dados
```

Criar um parser de logs que gere:

- primeiro erro;
- primeiro endereço inválido;
- última transição de estado;
- threads bloqueadas;
- operações DMA pendentes;
- operações FIOS pendentes;
- último flip/present;
- diferença em relação ao último run aprovado.

---

## Fase 0 — Baseline do jogo recompilado

### Objetivo

Fixar exatamente o estado atual antes de qualquer correção.

### Execução

1. Gerar o jogo usando o pipeline atual.
2. Compilar todos os chunks.
3. Executar com timeout controlado.
4. Repetir pelo menos três vezes.
5. Registrar:
   - duração;
   - primeiro erro;
   - último checkpoint;
   - número de OOB;
   - imports não resolvidos;
   - instruções desconhecidas;
   - threads criadas;
   - jobs SPU executados;
   - WADs abertos;
   - frames apresentados.

### Critério de aceite

Existe um `baseline.json` e os três runs apresentam a mesma barreira ou uma
variação explicada.

### Se falhar

Corrigir somente o harness de reprodução. Não iniciar uma correção do runtime
sem um baseline repetível.

### Estado (2026-07-24) — CONCLUÍDA

Executada via `docs/superpowers/plans/2026-07-24-bringup-instrumentacao-fase0.md`
(Tasks 1–8). Instrumentação comum (dez `PS3_TRACE_*`), parser
(`tools/trace_parse.py`) e harness (`scripts/baseline_run.sh` +
`tools/baseline_report.py`) implementados e testados; 3 runs headless reais
de `boot_gow2` (`PS3_NO_RSX=1`, timeout 60s, kill por PID) executados nesta
data.

- Artefacto de aceite: [`docs/bringup/gow2/FASE0/baseline.json`](bringup/gow2/FASE0/baseline.json).
- Relatório completo (11 secções): [`docs/bringup/gow2/FASE0/attempt-1.md`](bringup/gow2/FASE0/attempt-1.md).
- **`barreira.igual_nos_runs = true`** nos 3 runs: todos pararam em
  `ultimo_checkpoint=FIRST_WAD_OPEN`, com o mesmo `primeiro_erro`
  (`MEMORY/OOB ea=0xFB788082 len=4 region=unmapped op=read`). Sem
  necessidade de `variacao_explicada`.
- Zero retries do flake conhecido ("vm commit (sys_memory window) failed")
  nesta leva — registado, não escondido, em `attempt-1.md`.
- `instrucoes_desconhecidas` fica `null` (grandeza de lift-time, não
  observável em run-time — motivo documentado no cabeçalho de
  `tools/baseline_report.py`); `frames_apresentados=0` nos 3 runs é medição
  real do backend `trace` forçado (headless), não um defeito.
- Próximo passo apontado por `attempt-1.md`: Fase 1, com o primeiro evento
  divergente candidato já identificado (o `MEMORY/OOB` em
  `_Z13func_00263554P11ppu_context` logo após `FIRST_WAD_OPEN`).

---

## Fase 1 — Integridade da memória guest e do heap

### Hipóteses a eliminar

- ponteiro inválido produzido por allocator;
- free list corrompida;
- bloco liberado sendo reutilizado incorretamente;
- região válida não comprometida;
- endianess incorreta em headers;
- tamanho ou alinhamento incorreto em DMA/IO;
- escrita inválida silenciosamente descartada.

### Implementação

- Adicionar modo `strict` e modo `diagnostic` de memória.
- Instrumentar cada alocação guest com endereço, tamanho, origem e estado.
- Validar headers e footers do allocator.
- Detectar double-free, overlap, split inválido e ponteiro fora da arena.
- Registrar o primeiro escritor que corrompeu um header.
- Corrigir o mapeamento das regiões heap, mmapper, TLS, stack e RSX.
- Suspender temporariamente hooks de `COMMIT_HIGH` e outros experimentos para
  diferenciar solução real de mascaramento.

### Testes

- teste unitário do allocator;
- alocação/liberação em múltiplas threads;
- split/coalesce;
- exaustão da arena;
- ponteiro inválido em modo strict;
- snapshot antes e depois da corrupção;
- boot do jogo com `PS3_TRACE_HEAP=1`.

### Checkpoint C1

```text
BOOT_MEMORY_OK
```

O boot não possui corrupção de header conhecida, não há escrita guest inválida
silenciosa e o endereço inválido pode ser atribuído a uma origem.

### Ciclo de falha

- Se o primeiro erro for um header corrompido, voltar ao primeiro escritor.
- Se for apenas região não comprometida, validar o mapa antes de alterar o
  allocator.
- Se aparecer um novo endereço inválido diferente, comparar com o baseline; não
  assumir que a correção piorou sem verificar se o erro anterior foi removido.

### Estado (2026-07-25) — PARCIAL, C1 `BOOT_MEMORY_OK` NÃO ATINGIDO

Executada via `docs/superpowers/plans/2026-07-25-fase1-desmascarar-memoria.md`
(Tasks 1–7). Âmbito desta ronda: "desmascarar" o que já estava a acontecer
silenciosamente no acesso a memória guest — canonicalizar dois guards
ad-hoc (`vm_uncommitted_guard`, `tblsize_guard`) no evento `MEMORY|SWALLOWED`
já usado por `MEMORY|OOB` (Fase 0), corrigir o mapa de regiões de `vm.h`
(`lv2_heap`/`sys_vm`/`rsx_local`/`tls` deixam de ler `unmapped`), registar em
`sys_memory.c` as ranges que cada `vm_commit` bem-sucedido comita, e medir
com E1 (máscaras ligadas)/E2 (cada máscara desligada uma de cada vez) o que
cada uma engole e o que muda sem ela. **Não** implementou validação de
allocator, modos `strict`/`diagnostic`, nem tocou no lifter — ver "O que
falta para C1" abaixo.

Terceira mudança de comportamento declarada (revisão final de branch, I4):
`sys_mmapper_allocate_address` (`runtime/syscalls/sys_memory.c:531-533`)
passou a devolver `CELL_ENOMEM` e a não escrever `addr_out` quando o
`vm_commit` falha — antes devolvia `CELL_OK` com `addr_out` a apontar para
memória nunca comitada. Mudança de contrato de sucesso da syscall, correcta,
mas que se soma às duas correcções de mapa/registo acima.

- Artefacto de aceite desta task (Task 7): [`docs/bringup/gow2/FASE1/
  baseline-pos-fase1.json`](bringup/gow2/FASE1/baseline-pos-fase1.json) — 3
  runs headless, mesma barreira da Fase 0 (`ultimo_checkpoint=FIRST_WAD_OPEN`,
  mesmo `primeiro_erro` normalizado `MEMORY|OOB ea=0xFB788082`). Comparação
  campo-a-campo com `docs/bringup/gow2/FASE0/baseline.json`: **nenhuma das
  dez métricas mudou fora do jitter que a própria Fase 0 já tinha
  documentado** (`jobs_spu`, `duracao_s` ±1-2s) — registar as ranges
  comitadas (Task 5) não muda o caminho medido, porque o `primeiro_erro`
  (`ea=0xFB788082`) é um **OOB** (`a+n > ppu_vm_size`): os doze sítios que
  emitem `MEMORY|OOB` (`ppu_loader.cpp:831,866,893,978,1192,1213,1300,1608`
  e os quatro atómicos em `:1657,1677,1692,1704`) retornam **antes** de
  `vm_uncommitted_guard` correr, portanto nunca chega a perguntar-se se o
  endereço está coberto por alguma range registada. Não é "estar fora de
  uma banda que o guard cobre": `vm_uncommitted_guard` não verifica banda
  nenhuma para decidir se intercepta — só olha se há uma range registada
  que cubra o endereço; o teste `0x80000000-0x9FFFFFFF` (`ppu_loader.cpp:636`)
  corre **depois** dessa decisão e só escolhe a sonda `PS3_COMMIT_HIGH` e o
  formato do log. Relatório
  completo (11 secções, incluindo o diagnóstico de um binário `boot_gow2`
  temporariamente linkado contra o engine partilhado errado, corrigido
  antes de aceitar a medição): [`docs/bringup/gow2/FASE1/
  attempt-1.md`](bringup/gow2/FASE1/attempt-1.md).
- Medição das duas máscaras (E1/E2, in-boot): [`docs/bringup/gow2/FASE1/
  mascaras-e1-e2.md`](bringup/gow2/FASE1/mascaras-e1-e2.md).
  `uncommitted_guard` engoliu ≥4.194.304 eventos no E1 dedicado (limite
  inferior de um único experimento, não um valor por-run garantido — a
  re-medição da Task 7, sob o ambiente-padrão, só chegou ao marco
  `2097152`; banda `0x80000000-0x9FFFFFFF`, 100% `region=unmapped`) —
  desligá-lo faz o processo morrer por SIGBUS no mesmo ponto do log em que
  o run de controlo (sem crash) está nesse instante, mas isso é **n=1,
  mecanismo causal não confirmado por backtrace** (duas hipóteses
  alternativas em aberto: o próprio gate novo; um acesso a jusante do
  primeiro). `tblsize_guard` engole 1 evento por run
  no único EA exercitado (`0x00572BB0`, `region=main`, válido e comitado) —
  não é bug de mapeamento **para esse EA**, mas entre "lacuna de seeding do
  ambiente de recompilação" e "bug de lift em `func_0037600C`" essa medição
  **não discrimina**; os outros 5 EAs que o guard protege continuam
  **não-exercitados**, não "não acontecem".
- **C1 `BOOT_MEMORY_OK` NÃO foi atingido.** O `primeiro_erro` continua um
  `MEMORY|OOB` não diagnosticado (`ea=0xFB788082`), idêntico à Fase 0; não
  há boot limpo de memória. Falta explicitamente, para C1:
  - validação de headers e footers do allocator guest;
  - detecção de double-free, overlap e split inválido;
  - os modos `strict`/`diagnostic` de memória (pedidos pela secção
    "Implementação" acima, deliberadamente fora do âmbito desta ronda);
  - fechar o buraco das **~2110 escritas directas a `vm_base`** que
    contornam o funil hookado (`stvx/stvebx/stvehx/stvewx`=1184,
    `stwbrx/sthbrx`=922, `dcbz`=4, medidas no lift construído) — a
    contagem de `SWALLOWED`/`OOB` é por isso um **limite inferior**, nunca
    a contagem real de acessos inválidos;
  - as duas máscaras `type15_*` fora do repo do motor
    (`gow2-recomp/host_gow2_factory.cpp`) continuam não instrumentadas;
  - a mina do re-lift (o lift construído hoje usa os `ppu_lwarx` hookados,
    não os `ppu_res_*` mais recentes do lifter, que não reportam
    OOB/SWALLOWED) continua por resolver e bloqueia qualquer re-lift
    futuro sem apagar esta instrumentação.
- Achado de harness (não corrigido nesta ronda, registado em
  `attempt-1.md`): `scripts/baseline_run.sh` decide reconstruir
  `boot_gow2` comparando `mtime` de `libps3recomp_runtime.a` contra o
  binário — isso não detecta um `boot_gow2` linkado contra um
  `PS3_ENGINE_ROOT` diferente do worktree que está a medir (risco real:
  `../gow2-recomp` é um checkout partilhado por múltiplas sessões/branches
  do motor). O campo `commit_engine` do `baseline.json` **não teria
  apanhado este problema**: é lido de `git rev-parse HEAD` no worktree que
  corre o harness, não do binário efectivamente medido — este campo, neste
  JSON, não prova nada sobre o binário que foi medido. Follow-up
  registado: gravar o `PS3_ENGINE_ROOT` usado no último build ao lado do
  binário, ou verificar um símbolo-sentinela com `nm` antes de aceitar uma
  medição.
- Achado de ferramenta (não corrigido nesta ronda, registado em
  `attempt-1.md`): `tools/trace_parse.py --baseline <baseline.json>`
  **rebenta com `TypeError`** quando `<baseline.json>` é o schema agregado
  de `baseline_report.py` (`ps3recomp.baseline/1`, chaves `runs`/`env`/
  `barreira`) — a flag `--baseline` só está testada contra a saída de
  `trace_parse.py --json` de uma única execução, um schema diferente da
  medição agregada que a Fase 0/1 produzem.

---

#### Follow-ups da revisão final (não bloqueiam, mas são reais)

1. **Censo de escritores incompleto no comentário.** `ppu_register_committed_range`
   diz "5 escritores"; os call sites reais são 11 — 5 em `runtime/syscalls/sys_memory.c`,
   4 em `runtime/ppu/tests/boot_main.cpp` (arranque, single-thread), 1 em
   `runtime/ppu/ppu_loader.cpp` dentro do próprio guard (só com `PS3_COMMIT_HIGH=1`)
   e 1 em `libs/video/rsx_d3d12_backend.c` (thread RSX, Windows). O lock é global e
   cobre-os todos; o que ficou curto foi a contagem. O do D3D12 é um segundo escritor
   concorrente genuíno no Windows, que o fix passou a proteger sem que ninguém o
   tivesse planeado.

2. **`fprintf`+`fflush` correm dentro do spinlock** (no máximo ~9 vezes por boot:
   8 registos + 1 aviso). `runtime/syscalls/sys_memory.c` enuncia o princípio
   contrário — I/O dentro de lock tem custo de latência real mesmo sem risco de
   deadlock. Aqui é raro e limitado; só latência, mas está registado.

3. **Órfão de lock teórico.** O handler de sinais fatais do SPU
   (`runtime/spu/spu_workload.c`) faz `siglongjmp` quando o landing está armado.
   Uma thread de job SPU só alcança `ppu_register_committed_range` com
   `PS3_COMMIT_HIGH=1` (sonda, OFF por omissão), e a mesma janela já existia para
   os locks do stdio. **Forma de risco pré-existente, não introduzida pela Fase 1.**

## Fase 2 — Estado PPU, TLS e ABI

### Objetivo

Garantir que uma função recompilada preserva o estado necessário para a próxima
função e para cada thread guest.

### Implementação

- Validar GPR, FPR, VMX, CR, LR, CTR, XER, FPSCR, VSCR, VRSAVE, CIA e TOC.
- Validar `lwarx/stwcx` por contexto de thread.
- Validar stack frame, back-chain e alinhamento.
- Criar wrappers tipados para ponteiros guest.
- Comparar argumentos e retorno de HLE contra o ABI guest.
- Testar `bl`, `blr`, `blrl`, `bctrl`, `bctr`, tail calls e trampolines.

### Testes

- testes dourados do lifter;
- teste de cada forma de chamada indireta;
- teste de preservação de CTR/LR/TOC;
- teste de struct big-endian;
- callback host -> guest -> host;
- compilação sintática de todos os chunks.

### Checkpoint C2

```text
PPU_STATE_OK
```

A execução passa pelo mesmo conjunto de chamadas sem corrupção de contexto,
labels ausentes ou retorno para endereço incorreto.

### Ciclo de falha

Se o resultado mudar entre runs, desabilitar otimizações e paralelismo, ativar
snapshot de contexto e comparar a primeira divergência de registradores.

---

## Fase 3 — Threads PPU e scheduler

### Objetivo

Remover bloqueios falsos e garantir progresso determinístico entre threads.

### Implementação

- Registrar estado de cada thread: running, ready, blocked, sleeping, finished.
- Registrar motivo e objeto de espera.
- Garantir release/reacquire em cada syscall bloqueante.
- Criar modo de scheduler determinístico.
- Adicionar detecção de starvation e deadlock.
- Manter o giant lock apenas como opção de comparação.
- Comparar execução com giant lock e com locks por subsistema.

### Testes

- mutex, cond, semaphore e event flag;
- timeout e wakeup;
- múltiplos workers;
- join/detach;
- destroy/reuse de handles;
- teste sob ThreadSanitizer;
- repetição do boot com scheduler determinístico.

### Checkpoint C3

```text
THREADS_PROGRESS
```

Todas as threads críticas alternam progresso; não existe thread esperando sem
motivo registrado; a ordem de eventos é reproduzível.

### Ciclo de falha

- Se tudo parar, gerar dump de locks e voltar à última aquisição.
- Se somente uma thread morrer, preservar seu contexto e causa de término.
- Se o giant lock funcionar mas o modo concorrente falhar, a causa é race e não
  deve ser resolvida aumentando sleeps.

---

## Fase 4 — SPURS, SPU e DMA

### Objetivo

Fazer jobs SPU produzirem os mesmos efeitos guest esperados pelo PPU.

### Implementação

- Registrar cada imagem SPU e seu `image_id`.
- Validar carregamento de código e dados na local store.
- Integrar fallback e lift por configuração.
- Implementar fila MFC por contexto.
- Registrar GET/PUT, EA, LSA, tamanho, tag e conclusão.
- Corrigir comandos atômicos e reservas.
- Modelar mailbox blocking e sinais.
- Distinguir job concluído, bloqueado, halt e crash.
- Validar `stop_code` e retorno ao SPURS.

### Testes

- DMA GET/PUT básico;
- listas DMA;
- tags fora de ordem;
- barrier/sync;
- GETLLAR/PUTLLC;
- mailbox cheio/vazio;
- múltiplos jobs concorrentes;
- job lifted contra fallback com o mesmo fixture;
- SPU sanitizer/concurrency tests.

### Checkpoint C4

```text
SPU_EFFECTS_OK
```

Cada job crítico tem registro de entrada, efeitos guest, retorno e término. Não
há job pendente sem evento de progresso.

### Ciclo de falha

- Se PPU espera uma flag, identificar qual job deveria escrevê-la.
- Se a escrita DMA ocorreu, verificar endianess, EA e tamanho.
- Se não ocorreu, investigar dispatch, mailbox, tag e retorno do job.
- Só usar fallback depois de registrar o efeito que ele precisa reproduzir.

---

## Fase 5 — FIOS, filesystem e WADs

### Objetivo

Reproduzir o protocolo assíncrono de carregamento, não apenas disponibilizar o
arquivo no host.

### Implementação

- Modelar objeto de operação FIOS com estado explícito.
- Preservar leitura parcial, alinhamento, buffer e callback.
- Implementar ordem de conclusão e cancelamento.
- Validar path mapping e diferenças de case.
- Validar decompressor e tamanho de saída.
- Validar offsets, headers, tabelas e destino guest dos WADs.
- Associar completion de FIOS a thread/event flag/job responsável.

### Testes

- leitura síncrona e assíncrona;
- leitura parcial;
- callback único e repetido;
- cancelamento;
- WAD pequeno sintético;
- WAD real com hash da saída;
- teste de pressão com múltiplas operações;
- replay do carregamento.

### Checkpoint C5

```text
WAD_PIPELINE_OK
```

Os WADs esperados são abertos, lidos, descomprimidos, escritos no destino
correto e sinalizados na ordem natural. A FSM avança sem flag sintética.

### Ciclo de falha

- Se não houver `open`, verificar path e NID.
- Se houver `open` mas não houver conclusão, verificar FIOS/scheduler.
- Se concluir com dados errados, comparar bytes, tamanho, endianess e destino.
- Se dados corretos não forem consumidos, rastrear o leitor guest.

---

## Fase 6 — Mídia, vídeo e EOS

### Objetivo

Fazer a intro e demais vídeos avançarem por callbacks e dados reais.

### Implementação

- Implementar decode real conforme o formato usado pelo jogo.
- Modelar AU, sequência, PICOUT, PCMOUT, AUDONE, SEQDONE e EOS.
- Preservar timestamps e ordem entre áudio/vídeo.
- Integrar buffers guest e callbacks.
- Remover forçadores de EOS do caminho padrão.
- Manter overlay host somente como backend, não como substituto da FSM guest.

### Testes

- fixture de vídeo curto;
- fixture de áudio;
- sequência vazia;
- EOS natural;
- EOS após último frame;
- callback atrasado;
- repetição da intro com relógio virtual.

### Checkpoint C6

```text
INTRO_NATURAL_EOS
```

O estado da intro avança com EOS produzido pelo pipeline real e todos os
callbacks esperados foram observados.

### Ciclo de falha

Não alterar o valor da FSM diretamente. Localizar o primeiro evento ausente:
decode, buffer, callback, timestamp, EOS ou consumidor.

---

## Fase 7 — RSX, render e apresentação

### Objetivo

Separar claramente processamento guest, execução RSX e apresentação host.

### Implementação

- Instrumentar FIFO e comandos por frame.
- Validar labels, fences, timestamps e flip.
- Validar render targets, texturas, swizzle e formatos de vertex.
- Validar shaders e combinações de parâmetros.
- Implementar replay de command buffer.
- Garantir que present não altere estruturas guest sem evento correspondente.
- Separar `flip requested`, `flip complete` e `frame presented`.

### Testes

- FIFO sintético;
- draw mínimo;
- textura conhecida;
- render target;
- flip sem frame;
- fence pendente;
- replay determinístico de um frame;
- comparação de imagem quando possível.

### Checkpoint C7

```text
FIRST_FRAME_PRESENTED
```

O primeiro frame esperado é apresentado sem hook sintético de FSM e com
sequência completa de FIFO, draw, fence/flip e present.

### Ciclo de falha

- Se não houver draw, voltar para assets/estado guest.
- Se houver draw sem pixels, verificar shader, textura e render target.
- Se houver pixels sem present, verificar flip/fence/vblank.
- Se present ocorrer cedo demais, corrigir sincronização, não adicionar delay.

---

## Fase 8 — Input, menu e gameplay

### Objetivo

Validar que o jogo responde a eventos reais após o primeiro frame.

### Implementação

- Mapear pad host para estado guest estável.
- Preservar pressed, released, held e repeat.
- Integrar input com o scheduler e frame loop.
- Validar callbacks de sysutil/menu.
- Remover dependência de input sintético dos testes normais.

### Testes

- neutral state;
- botão pressionado por um frame;
- botão mantido;
- release;
- dois controles;
- input durante loading e durante present;
- replay de sequência de menu.

### Checkpoint C8

```text
MENU_INPUT_OK
```

O jogo chega ao menu, aceita uma sequência conhecida de input e muda de estado
de maneira reproduzível.

---

## Fase 9 — Save/load e encerramento

### Objetivo

Garantir que o jogo consegue completar um ciclo, não apenas iniciar.

### Implementação

- Validar paths, PARAM.SFO, callbacks e buffers de save data.
- Testar criação, leitura, alteração e remoção controlada.
- Implementar shutdown ordenado de threads, SPURS, áudio, vídeo e RSX.
- Validar retorno de funções e destruição de handles.

### Checkpoint C9

```text
SAVE_AND_EXIT_OK
```

O jogo salva, retorna ao menu ou encerra sem threads pendentes, DMA pendente,
handle inválido ou corrupção guest.

---

## Fase 10 — Validação final e regressão

### Execução

Para cada checkpoint aprovado:

1. executar três vezes em modo determinístico;
2. executar três vezes em modo normal;
3. comparar logs de eventos;
4. comparar snapshots de estado;
5. executar com sanitizers;
6. executar com e sem giant lock;
7. verificar ausência de hooks experimentais;
8. atualizar a matriz de compatibilidade.

### Critério de aceite final

```text
GAME_RECOMPILED_RUNTIME_OK
```

O jogo atinge todos os checkpoints definidos, os resultados são reproduzíveis,
nenhuma barreira depende de flag de força e os workarounds restantes estão
explicitamente classificados como específicos do jogo.

---

## Formato do relatório de cada tentativa

O agente deve criar um arquivo em:

```text
docs/bringup/<game>/<checkpoint>/attempt-<N>.md
```

Modelo:

```markdown
# Checkpoint: C5 — WAD pipeline

## Hipótese

## Baseline

## Alteração

## Testes unitários

## Teste de integração

## Resultado esperado

## Resultado observado

## Primeiro evento divergente

## Evidências

## Decisão

## Próxima tentativa
```

## Princípio de conclusão

O agente deve sempre corrigir o primeiro evento divergente conhecido. Não deve
corrigir a última barreira visível se houver evidência de corrupção, evento
perdido ou estado inválido anterior.
