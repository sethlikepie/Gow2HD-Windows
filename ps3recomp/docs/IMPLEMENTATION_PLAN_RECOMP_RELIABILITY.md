# Plano de implementação: recompilação confiável e estado consistente

## Objetivo

Tornar a recompilação de jogos PS3 reproduzível, verificável e capaz de preservar
corretamente o estado PPU, SPU, DMA, threads, timers, callbacks, HLE e RSX.

O plano prioriza primeiro a observabilidade e a validação. Correções de fidelidade
que não tenham um teste reproduzível não devem ser incorporadas como solução
definitiva.

## Resultado esperado

Para cada jogo, o comando abaixo deve gerar um build completo ou falhar indicando
exatamente a etapa e a causa:

```text
ps3recomp build --elf EBOOT.ELF --config game.toml --out build/game
```

O resultado deverá conter:

- manifesto com hashes, versões e opções usadas;
- imagem ELF/PRX analisada e relocada;
- relatório de funções, imports, NIDs e cobertura PPU/SPU;
- fontes geradas e objetos compilados;
- testes de ABI, runtime e integração executados;
- relatório de pendências que impeçam declarar o build como confiável.

## Critérios globais de qualidade

Um jogo só poderá ser marcado como `recompiled` quando:

1. não houver instrução crítica desconhecida;
2. não houver import obrigatório sem resolução explícita;
3. todos os chunks passarem por compilação e link;
4. o loader validar segmentos, BSS, OPD, TOC e relocations;
5. os testes de estado e concorrência passarem;
6. o boot atingir o checkpoint definido pelo jogo sem acesso guest inválido não
   autorizado;
7. o resultado for reproduzível a partir do manifesto.

---

## Fase 0 — Baseline e infraestrutura de desenvolvimento

### Tarefas

- Registrar o estado atual do repositório e separar claramente runtime genérico,
  ferramentas e código específico de `games/gow2`.
- Criar `pyproject.toml` com versão mínima de Python, dependências e comandos de
  teste.
- Adicionar `tomli` para Python 3.9 ou elevar oficialmente a versão mínima para
  Python 3.11.
- Corrigir imports dos self-tests (`elf_symbols` e demais módulos locais) para
  funcionarem a partir da raiz do repositório.
- Remover dependências de `cmd /c`, `taskkill` e caminhos absolutos dos testes
  genéricos; separar testes específicos de Windows.
- Adicionar `CTest` ao CMake e fazer `PS3RECOMP_BUILD_TESTS` realmente registrar
  os testes disponíveis.
- Criar scripts portáveis `scripts/test_python.sh`, `scripts/test_runtime.sh` e
  `scripts/test_all.sh`.
- Configurar CI para macOS, Linux e Windows, com GCC/Clang/MSVC quando aplicável.

### Critérios de aceite

- Um comando instala dependências e executa todos os testes possíveis na
  plataforma.
- Testes incompatíveis com a plataforma são marcados como `SKIP`, não como
  falha misteriosa.
- `cmake -DPS3RECOMP_BUILD_TESTS=ON`, `cmake --build` e `ctest --output-on-failure`
  funcionam em todas as plataformas suportadas.

---

## Fase 1 — Pipeline único de recompilação

### Tarefas

- Criar `tools/ps3recomp_build.py` como orquestrador oficial.
- Integrar as etapas:
  1. identificação ELF/SELF/PRX;
  2. descriptografia opcional;
  3. parsing e validação ELF;
  4. relocação de PRX/ET_DYN;
  5. descoberta de funções PPU;
  6. extração e descoberta de funções SPU;
  7. análise de imports/exports/NIDs;
  8. lift PPU/SPU;
  9. geração de fontes auxiliares e tabela HLE;
  10. compilação incremental;
  11. link do host;
  12. testes e geração do relatório.
- Fazer `recomp_config.py` controlar de fato todas as opções do pipeline.
- Definir uma estrutura de diretórios de build estável:

```text
build/<game>/
  manifest.json
  input/
  analysis/
  relocated/
  lifted/ppu/
  lifted/spu/
  generated/
  objects/
  reports/
  bin/
  logs/
```

- Substituir os scripts de GoW2 por wrappers que chamem o orquestrador.
- Fazer todas as etapas falharem com código diferente e mensagem estruturada.
- Implementar cache por hash do input, configuração, ferramenta e dependências.

### Critérios de aceite

- Um jogo pode ser reconstruído sem executar comandos manuais intermediários.
- Reexecutar o mesmo build reutiliza etapas inalteradas.
- Um erro de relocação, função, NID, cobertura ou compilação impede o link.
- O manifesto permite reproduzir exatamente o build.

---

## Fase 2 — Validação ELF, PRX, OPD, TOC e relocations

### Tarefas

- Unificar `elf_parser.py`, `ppu_loader.py` e `prx_relocate.py` numa API comum.
- Validar limites de todos os headers, segmentos, offsets e tamanhos antes de ler.
- Validar sobreposição de segmentos e regiões inválidas.
- Implementar relocação de PRX/ET_DYN como etapa oficial, não como operação manual.
- Registrar no manifesto cada relocation aplicada, ignorada ou rejeitada.
- Validar tipos de relocation não suportados como erro explícito.
- Validar OPD:
  - alinhamento;
  - endereço de código dentro de segmento executável;
  - TOC dentro de região válida;
  - duplicidade e inconsistência de descritores.
- Validar a entrada `e_entry` e a resolução `OPD -> code/toc`.
- Adicionar testes sintéticos para ELF EXEC, PRX, BSS, TLS, OPD e relocations.

### Critérios de aceite

- EBOOTs fixos e PRXs relocáveis passam pelo mesmo pipeline.
- ELF truncado, OPD inválido e relocation desconhecida falham antes do lift.
- O relatório mostra a cobertura e o motivo de cada relocation não aplicada.

---

## Fase 3 — Lifter com contrato de cobertura

### Tarefas

- Alterar o PPU lifter para registrar cada instrução traduzida, aproximada e
  desconhecida.
- Alterar o SPU lifter com a mesma taxonomia.
- Remover `TODO` silencioso como comportamento padrão.
- Introduzir políticas de cobertura no TOML:

```toml
[coverage]
unknown_ppu = "error"
unknown_spu = "error"
approximate = "warning"
allow_noop = []
```

- Para cada aproximação, registrar:
  - opcode;
  - endereço guest;
  - função;
  - efeito omitido;
  - impacto esperado;
  - justificativa da permissão.
- Fazer a saída do lifter ser determinística, inclusive em modo paralelo.
- Validar labels, gotos, trampolines e entradas de função em cada chunk.
- Criar teste de compilação sintática automático para todos os chunks.
- Separar dados embutidos em `.text` de instruções SPU sempre que possível.
- Adicionar testes dourados para instruções PPU/SPU e para CFGs difíceis.

### Critérios de aceite

- Nenhuma instrução desconhecida passa despercebida.
- O mesmo input produz fontes equivalentes independentemente de `-j`.
- O build reporta exatamente quais aproximações foram aceitas.
- Falhas de label, target ou trampoline são detectadas antes do compilador host.

---

## Fase 4 — Runtime de memória com modos de segurança

### Tarefas

- Criar uma API única para memória guest usada por PPU, HLE, syscalls, SPU e RSX.
- Eliminar cópias divergentes de `vm_read*` e `vm_write*`.
- Implementar os modos:

```text
strict       acesso inválido encerra com diagnóstico completo
diagnostic   registra e retorna erro controlado
compat       comportamento tolerante legado
```

- Registrar thread guest, função, endereço, tamanho, tipo de acesso e call chain.
- Implementar tabela dinâmica de regiões comprometidas, sem limite artificial de
  poucas regiões.
- Validar alinhamento quando a instrução exigir alinhamento.
- Diferenciar erro guest recuperável de corrupção do runtime.
- Adicionar guard pages quando o host permitir.
- Criar testes de endianess, BSS, TLS, stack, RSX memory e acessos cruzados.

### Critérios de aceite

- Um ponteiro inválido nunca causa corrupção silenciosa no modo `strict`.
- O relatório identifica a primeira origem conhecida do endereço inválido.
- HLE e código recompilado observam as mesmas regras de memória.

---

## Fase 5 — Estado PPU e ABI

### Tarefas

- Revisar `ppu_context` contra o estado arquitetural usado pelo código gerado.
- Garantir preservação de GPR, FPR, VMX, CR, LR, CTR, XER, FPSCR, VSCR, VRSAVE,
  CIA, TLS, reserva atômica e identificador de thread.
- Tornar `lwarx/stwcx` dependente do contexto da thread guest, não de estado global.
- Criar APIs explícitas para salvar/restaurar contexto em:
  - syscall;
  - callback;
  - HLE;
  - thread switch;
  - trampoline;
  - exceção controlada.
- Implementar wrappers tipados para ponteiros guest/host.
- Proibir que o dispatcher genérico passe ponteiros guest diretamente para C host.
- Criar testes de ABI para argumentos, retorno, ponteiros, structs big-endian,
  floats, vetores e callbacks.

### Critérios de aceite

- Todo handler HLE declara explicitamente seu contrato de argumentos e retorno.
- Testes detectam corrupção de TOC, LR, CTR, CR ou reserva atômica.
- Callbacks entre host e guest preservam o contexto completo.

---

## Fase 6 — Scheduler, threads e máquina de estados

### Tarefas

- Definir um objeto de thread guest contendo contexto PPU, TLS, estado de bloqueio,
  prioridade, deadline e causa de espera.
- Implementar scheduler cooperativo para pontos de bloqueio conhecidos.
- Substituir gradualmente o giant lock global por locks por subsistema:
  - memória;
  - HLE;
  - filesystem;
  - áudio;
  - RSX;
  - SPURS;
  - tabela de threads.
- Manter o giant lock como modo de compatibilidade temporário e instrumentado.
- Criar política explícita de release/reacquire em syscalls bloqueantes.
- Implementar detecção de deadlock, starvation e thread que não progride.
- Registrar transições de estado com sequência monotônica e timestamp virtual.
- Adicionar snapshots de threads, locks, eventos e regiões relevantes.

### Critérios de aceite

- O modo determinístico reproduz a mesma sequência de eventos em execuções
  repetidas.
- Um deadlock gera dump com threads, locks, wait sites e último progresso.
- O jogo não depende de `Sleep(0)` ou timing acidental para progredir.
- O giant lock pode ser desativado em testes sem corrupção imediata.

---

## Fase 7 — Tempo, eventos e callbacks

### Tarefas

- Criar um relógio guest virtual com conversão para o timebase PS3.
- Permitir avançar o relógio por evento em testes.
- Separar `sys_time_get_current_time` do `mftb` e documentar ambos.
- Implementar deadlines, timers e timeout sem depender diretamente do relógio host.
- Centralizar filas de eventos, event flags, semáforos, mutexes e condições.
- Testar spurious wakeups, timeout, cancelamento, destroy durante espera e reuso
  de handles.
- Instrumentar callbacks com origem, destino, contexto e ordem de entrega.

### Critérios de aceite

- FSMs dependentes de tempo podem ser executadas instantaneamente em testes.
- Timeout e wakeup são reproduzíveis.
- Callbacks atrasados ou duplicados são detectados automaticamente.

---

## Fase 8 — SPU, SPURS, DMA e sincronização

### Tarefas

- Definir uma fila MFC real por contexto SPU, mesmo que o backend execute
  inicialmente de forma síncrona.
- Modelar estados de tag: pendente, executando, concluído e erro.
- Implementar testes para GET/PUT, listas, barreiras, sync e comandos atômicos.
- Corrigir e testar semântica de GETLLAR, PUTLLC, PUTLLUC e reservas.
- Validar endianess e alinhamento de LS, EA e listas DMA.
- Integrar `image_id` com carregamento de kernel, policy module e job.
- Definir contrato de retorno de jobs, `stop_code`, halt e branch não resolvido.
- Integrar fallback SPU e código levantado numa política declarativa por imagem.
- Testar mailbox blocking, sinais, event status, decrementer e preempção.
- Adicionar testes com múltiplos workers e ThreadSanitizer.

### Critérios de aceite

- Jobs SPU produzem os mesmos efeitos guest com fallback ou lift equivalente.
- Nenhum job fica indistinguível entre “terminou”, “bloqueou” e “crashou”.
- As filas SPURS não dependem de ordem acidental de threads host.

---

## Fase 9 — HLE e contratos de módulos

### Tarefas

- Alterar `MODULE_STATUS.md` para separar ABI, comportamento parcial e stub.
- Gerar matriz de NIDs exigidos por cada jogo.
- Fazer import obrigatório sem handler falhar o build, salvo override explícito.
- Criar registro tipado de assinaturas HLE.
- Implementar conversão guest/host para ponteiros e estruturas em cada API.
- Priorizar módulos pelo uso real do jogo, não pela quantidade de funções.
- Completar primeiro os caminhos críticos:
  - filesystem;
  - threads e sincronização;
  - SPURS;
  - vídeo/media;
  - input;
  - áudio;
  - save data.
- Para módulos de mídia, integrar backend real e manter fallback claramente
  identificado.

### Critérios de aceite

- O relatório mostra NID, nome, assinatura, handler e nível de fidelidade.
- Um stub usado em caminho crítico gera warning/falha configurável.
- Testes de integração exercitam handlers reais, não apenas mocks.

---

## Fase 10 — RSX, apresentação e recursos multimídia

### Tarefas

- Separar command processor RSX, state tracker e backend host.
- Criar testes de replay determinísticos para FIFO, shaders, texturas, render
  targets, flips e sincronização.
- Garantir que o backend não altere estado guest de forma invisível.
- Implementar política de apresentação por frame, sem hooks específicos do jogo
  no runtime genérico.
- Completar ou declarar explicitamente as limitações de VDEC/ADEC, VPOST e
  codecs.
- Integrar arquivos de vídeo e áudio através de uma camada host substituível.

### Critérios de aceite

- Um frame replayado produz resultado estável nos backends suportados.
- O jogo não precisa de injeções específicas para avançar sua FSM normal.
- Falhas de shader, textura ou apresentação apontam para o comando guest original.

---

## Fase 11 — Migração do GoW2

### Tarefas

- Converter `games/gow2/*.sh` para configuração declarativa e comandos do
  orquestrador.
- Classificar cada hook atual como:
  - correção genérica do runtime;
  - fallback de módulo;
  - workaround específico do jogo;
  - diagnóstico temporário.
- Remover gradualmente hooks de “forçar FSM”, EOS e estados sintéticos quando o
  caminho natural estiver implementado.
- Manter os probes de diagnóstico, mas fora do caminho normal de execução.
- Criar checkpoints de aceite:
  1. boot;
  2. intro;
  3. abertura de WAD;
  4. apresentação do primeiro frame;
  5. menu;
  6. input;
  7. gameplay;
  8. save/load;
  9. encerramento.
- Guardar logs e snapshots por checkpoint.

### Critérios de aceite

- O GoW2 recompila e inicia pelo pipeline comum.
- Workarounds restantes estão documentados e isolados.
- Cada checkpoint possui teste automatizado ou replay reproduzível.

---

## Fase 12 — Performance e remoção de dívida técnica

### Tarefas

- Medir tempo de parsing, descoberta, lift, compilação e link.
- Otimizar somente depois dos gates de correção.
- Melhorar paralelismo do lift e compilação sem alterar a saída semântica.
- Substituir tabelas lineares de dispatch por estruturas escaláveis quando
  necessário.
- Remover diagnósticos hardcoded de endereços de jogos do runtime genérico.
- Remover código morto, scripts duplicados e documentação desatualizada.
- Publicar uma matriz de compatibilidade por jogo e por plataforma.

### Critérios de aceite

- O build incremental é mensuravelmente mais rápido que o build limpo.
- Sanitizers passam nos testes de runtime.
- O runtime genérico não contém endereços ou FSMs específicas de um jogo.

---

## Ordem recomendada de execução

```text
Fase 0
  ↓
Fase 1 → Fase 2 → Fase 3
                    ↓
             Fase 4 → Fase 5
                         ↓
                  Fase 6 → Fase 7
                         ↓
                  Fase 8 → Fase 9 → Fase 10
                                      ↓
                                  Fase 11
                                      ↓
                                  Fase 12
```

Não iniciar a substituição do giant lock, nem completar HLEs de forma ampla,
antes de existir o baseline de testes, o manifesto de build e o coverage gate.

## Primeiros entregáveis sugeridos

### Entrega 1

- Fase 0 completa;
- `CTest` funcionando;
- testes Python reproduzíveis;
- CI inicial;
- relatório de baseline.

### Entrega 2

- orquestrador único;
- manifesto;
- relocação integrada;
- validação ELF/OPD/TOC;
- build incremental.

### Entrega 3

- coverage gate PPU/SPU;
- modo strict/diagnostic de memória;
- validação de chunks e imports;
- relatório de fidelidade.

### Entrega 4

- contexto PPU completo;
- scheduler determinístico;
- snapshots e dumps de deadlock;
- testes de threads, timers e callbacks.

### Entrega 5

- SPURS/DMA robustos;
- contratos HLE tipados;
- replay RSX/media;
- migração do GoW2 para o pipeline comum.

## Definição de pronto

Uma correção só será considerada concluída quando tiver:

- implementação;
- teste unitário ou de integração;
- log/diagnóstico útil em caso de falha;
- documentação atualizada;
- compatibilidade verificada nas plataformas relevantes;
- nenhuma regressão nos checkpoints já aprovados.
