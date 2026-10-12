# Phase 1: Emissões do lifter e teste-ouro do preâmbulo - Context

**Gathered:** 2026-07-26
**Status:** Ready for planning

<domain>
## Phase Boundary

Um lift gerado hoje por `tools/ppu_lifter.py` contém tudo que os scripts de build
consomem, e uma regressão nessas emissões é detectada por teste em vez de por link
quebrado horas depois.

**Dentro do escopo:** as três emissões que `games/gow2/build_macos.sh` compila
(`#include <stdlib.h>`, o ramo `#elif defined(__APPLE__)` do TLS de `g_trampoline_fn`,
e um `ppu_stubs.cpp` gerado); o teste-ouro do preâmbulo com prova por mutação; a
instrumentação de OOB dos quatro atômicos; o gate G4 de tokens proibidos.

**Fora do escopo, por declaração do marco:** `R_PermA`, caminho de present, menu,
pixels. Gate A continua RED como valor esperado.

</domain>

<decisions>
## Implementation Decisions

### Instrumentação dos quatro atômicos (critério 3)

- **D-01:** Instrumentar **inline** os quatro helpers `ppu_res_lwarx/stwcx/ldarx/stdcx`
  no `SOURCE_PREAMBLE` do lifter (`tools/ppu_lifter.py:307-341`), em vez de declarar a
  perda de instrumentação por escrito ou fundir com o par de `ppu_memory.h`.
  — **Reversibility:** reversible — é adição local ao preâmbulo emitido; remover volta
  ao estado atual sem migração nem contrato quebrado.

- **D-02:** A instrumentação chama **`ps3_mem_oob_report`**, não `vm_oob`.
  Fundamento medido nesta sessão: `vm_oob` é `static int` em
  `runtime/ppu/ppu_loader.cpp:399` — file-local, inacessível de outra unidade de
  tradução. Já `ps3_mem_oob_report` é declarado no header público
  `include/ps3_mem_trace.h` e definido em `runtime/trace/ps3_mem_trace.c`, e o
  `ppu_loader.cpp` já o chama — o símbolo portanto já está no link.
  O preâmbulo faz o seu próprio bounds check inline (self-contained, coerente com o
  princípio de design que o comentário em `:292-300` protege) e só chama o reporter.

- **D-03:** Seguir a assinatura já estabelecida pelos atômicos do runtime: `len` estático
  — **4** para `lwarx`/`stwcx`, **8** para `ldarx`/`stdcx`; `op="read"` para
  `lwarx`/`ldarx`, `op="write"` para `stwcx`/`stdcx`.
  **Correção de citação (pós-research):** o bloco a espelhar é a *implementação* em
  `runtime/ppu/ppu_loader.cpp:1691-1752` — ex.:
  `if (vm_oob(addr,4)) { ps3_mem_oob_report(addr, 4, "read", __builtin_return_address(0)); ctx->reserve_valid = 0; return 0; }`.
  A citação original `:452-465` aponta o comentário de *política* que justifica a
  decisão, não o código. Ambas são úteis; a de `:1691` é a que o plano deve copiar.

- **D-04:** Comportamento em OOB é **report-only, nunca abortar**. Não foi perguntado
  ao autor porque o contrato existente já decide: `ps3_mem_oob_report`
  (`runtime/trace/ps3_mem_trace.c:36-46`) retorna cedo quando
  `ps3_trace_on(PS3_TS_MEMORY)` está off e tem cap de emissão
  (`PS3_MEM_OOB_CANONICAL_CAP`). Isso satisfaz a regra 6 do `CLAUDE.md` (probes gated
  por env var, OFF por default) sem decisão nova.

### Fonte de verdade do `build_macos.sh` (critério 1)

- **D-05:** O teste-ouro ancora em **`games/gow2/build_macos.sh`** (presente em CI e em
  clone limpo), **mais** um gate de sincronização que compara com
  `../gow2-recomp/build_macos.sh` e **pula** quando o irmão não existe.

  > ⚠ **DESVIO DA LETRA DESTA DECISÃO, medido durante o planejamento (2026-07-26).**
  > A forma literal — imprimir `SKIP:` — **envenena o teste inteiro** e não pode ser usada.
  > `CMakeLists.txt:298` define `PS3RECOMP_SKIP_REGEX` **desancorado** (escolha deliberada,
  > documentada em `:285-296`) e `:326-330` aplica-o a `py.ppu_lifter_emit`. Como a CI faz
  > checkout só de `ps3recomp` (`.github/workflows/ci.yml`, três jobs, zero checkout do
  > irmão), o gate imprimiria o marcador em **toda** execução e o CTest classificaria o
  > teste **inteiro** como *Skipped* — apagando da CI as quatro emissões fixadas, a
  > instrumentação OOB e o gate G4, e quebrando o critério 2. Pior: `scripts/test_python.sh:148`
  > usa `grep -E '^SKIP:'` **ancorado**, então os dois classificadores discordariam — um
  > diria Skipped, o outro PASS.
  > **Resolução:** o plano usa um marcador de pulo que não colide, preservando a *intenção*
  > da D-05/D-06 (pular, nunca falhar) em vez do token literal. O desvio está declarado no
  > `<objective>` do `01-03-PLAN.md` e é obrigatório como comentário no código-fonte.
  > Verificado de forma independente pelo `gsd-plan-checker`.
  > Alternativa descartada: um segundo alvo CTest sobre o mesmo arquivo — seria um terceiro
  > sítio de registro, exatamente o custo que a **D-08** existe para evitar.
  > **RATIFICADO pelo autor em 2026-07-26**, depois de lhe serem apresentados os três fatos
  > medidos e as duas alternativas (forma literal com segundo alvo CTest; ou tirar o gate de
  > sync do teste-ouro). A D-05 passa a valer pela intenção, não pelo token.
  Ancorar só no monorepo validaria uma cópia comprovadamente defasada; ancorar só no
  irmão deixaria o teste permanentemente em SKIP na CI, que é o único alvo bloqueante.
  — **Reversibility:** reversible.

- **D-06:** Enquanto a divergência conhecida existir, o gate de sync **avisa (WARN) e
  não falha**; vira FAIL numa fase posterior — naturalmente a Fase 2, cujo domínio é
  exatamente o `host_gow2_factory`. A Fase 1 não nasce vermelha por um débito que não
  é dela, e a sincronização de subtree não é arrastada para dentro de uma fase cujo
  domínio é emissão do lifter.

### Prova por mutação (critério 2)

- **D-07:** Mutação **automatizada em todo run**. O próprio teste remove, em memória,
  cada uma das três emissões do preâmbulo, reroda o assert e exige que ele falhe.
  Descartadas: fixtures fixadas (congelam e passam a testar formato morto quando o
  preâmbulo evolui) e alvo `--self-check` sob demanda (a prova vira ritual manual que
  ninguém re-executa, e o teste apodrece sem ninguém notar).
  Custo aceito: três passagens extras por execução — operações de string, não
  compilação, portanto irrelevantes frente ao harness C++ que o teste já roda.

### Onde vivem o teste e o gate G4 (critérios 2 e 4)

- **D-08:** Ambos entram em **`runtime/ppu/tests/test_ppu_lifter_emit.py`**, estendendo
  o arquivo existente em vez de criar irmãos. Motivo: já é golden de emissão, já está
  registrado nos **dois** lugares que precisam ficar em sincronia
  (`scripts/test_python.sh:110` e `CMakeLists.txt:327`, como `py.ppu_lifter_emit`), e
  o `TESTING.md` alerta que esse espelhamento é manual e fácil de esquecer. Custo
  aceito: o arquivo cresce e passa a misturar granularidade de instrução com a de
  arquivo.

- **D-09:** O gate G4 varre o **`SOURCE_PREAMBLE` e as strings de emissão de
  `tools/ppu_lifter.py`** — tudo que o lifter *pode* emitir. Roda em CI sem exigir
  EBOOT nem lift, e é onde os tokens proibidos (`<intrin.h>`, `Sleep(`, `__declspec`,
  `GetModuleHandleA` fora de `#ifdef _WIN32`) de facto nascem.
  **Limitação declarada:** não pega token injetado por um `patch_*.py`. Essa lacuna é
  consciente, não silenciosa.
  **ARMADILHA descoberta na pesquisa — o gate NÃO pode exigir `#ifdef _WIN32`.** As três
  ocorrências de token proibido hoje no lifter estão guardadas por **`_MSC_VER`**, não
  por `_WIN32`: `tools/ppu_lifter.py:66` (`__declspec(align(16))` sob `#ifdef _MSC_VER`),
  `:236-237` (`#include <intrin.h>` sob `#ifdef _MSC_VER`) e `:377-378`
  (`__declspec(thread)` sob `#if defined(_MSC_VER)`). Um gate que só aceite `_WIN32`
  falha no dia um contra código que já está correto. A guarda aceita tem de cobrir
  `_MSC_VER` **e** `_WIN32`. O critério 4 do ROADMAP foi corrigido de acordo.

### Quarta emissão: contrato `ppu_timebase_now`

- **D-10:** O teste-ouro fixa **quatro** emissões, não três. A quarta é o contrato
  `ppu_timebase_now`: a declaração `extern "C" uint64_t ppu_timebase_now(void);` no
  preâmbulo (`tools/ppu_lifter.py:230,232`) e a emissão de `mftb`/`mftbu` como
  `ppu_timebase_now()` (`:2127`) têm de casar com um definidor versionado no runtime
  (`runtime/syscalls/sys_timer.c`).
  Fundamento: o commit `e441f37` provou a mesma classe de falha por link quebrado
  (`Undefined symbols: _ppu_timebase_now`, 7+ sítios, num lift limpo). Já corrigido por
  wrapper, mas sem teste nada impede que volte a divergir.
  **Não renomear no lifter.** `ppu_*` é o contrato do lifter upstream (`sp00nznet`);
  renomear criaria conflito a cada merge. A cola é o wrapper de uma linha no runtime.
  — **Reversibility:** reversible — é mais um assert no golden.
  — Escopo expandido por decisão explícita do autor em 2026-07-26; `ROADMAP.md`
  critério 1 e `REQUIREMENTS.md` `REQ-RDY0-1` atualizados na mesma leva.

### Claude's Discretion

- Forma exata do bounds check inline no preâmbulo (comparação contra o tamanho do
  espaço de endereçamento guest) — desde que não dependa de `vm_oob`.
- Estrutura interna da rotina de mutação dentro do teste (helper, decorator ou loop).
- Nomes de funções e organização das novas seções dentro de
  `test_ppu_lifter_emit.py`.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Planejamento do marco
- `.planning/ROADMAP.md` §"Phase 1" — meta, 4 critérios de sucesso, e a nota de
  realidade medida em 2026-07-26.
- `.planning/REQUIREMENTS.md` — `REQ-RDY0-1` e as três cifras conflitantes do
  inventário de host órfão (1603 / 1662 / 1793).
- `.planning/STATE.md` — bloqueadores do Marco 1, incluindo a mina do re-lift.
- `.planning/INGEST-CONFLICTS.md` — 5 WARNINGS / 16 INFO; nada bloqueante para esta fase.

### Alvos de código desta fase
- `tools/ppu_lifter.py:146` — emissão de `#include <stdlib.h>` (**já existe**).
- `tools/ppu_lifter.py:378-379` — ramo `#elif defined(__APPLE__)` do TLS de
  `g_trampoline_fn` (**já existe**).
- `tools/ppu_lifter.py:288-345` — comentário de design dos `ppu_res_*` e as quatro
  definições. Explica por que não foram fundidos com `ppu_memory.h`.
- `runtime/ppu/tests/test_ppu_lifter_emit.py` — 598 linhas; o golden de emissão a estender.

### Contratos de instrumentação
- `include/ps3_mem_trace.h:54` — contrato de `ps3_mem_oob_report`.
- `runtime/trace/ps3_mem_trace.c:36-46` — implementação; report-only, gated, com cap.
- `runtime/ppu/ppu_loader.cpp:399` — `vm_oob`, **`static`** (não utilizável do preâmbulo).
- `runtime/ppu/ppu_loader.cpp:452-465` — precedente dos atômicos do runtime: `len`
  estático 4/8 e `op="read"/"write"`. É a assinatura a espelhar.

### Contrato `ppu_timebase_now` (quarta emissão candidata — ver `<specifics>`)
- `runtime/syscalls/sys_timer.c` — wrapper `ppu_timebase_now` → `ps3_timebase_now`,
  adicionado no commit `e441f37`. Sem isto, um lift limpo não linka.
- `tools/ppu_lifter.py` ~230 — declaração `extern "C" uint64_t ppu_timebase_now(void);`
  no preâmbulo.
- `tools/ppu_lifter.py` ~2127 — emissão de `mftb`/`mftbu` como `ppu_timebase_now()`.
- `tools/lift_selftest.py` — assume o mesmo contrato.

### Build e registro de teste
- `games/gow2/build_macos.sh:47` — o abort `no lift output in $LIFT`.
- `games/gow2/build_macos.sh:92,106,192` — os três sítios que compilam `ppu_stubs.cpp`.
- `scripts/test_python.sh:110` — registro do teste na lista `TESTS`.
- `CMakeLists.txt:327` — registro CTest como `py.ppu_lifter_emit`.
- `CMakeLists.txt:296-330` — convenção `SKIP_REGULAR_EXPRESSION` desancorada.
- `.github/workflows/ci.yml` — macOS bloqueante; Linux e Windows `continue-on-error`.

### Mapa de codebase (fatos verificados, não intenção)
- `.planning/codebase/TESTING.md` — armadilha do `-DNDEBUG` em Release, convenção SKIP,
  e a regra de espelhamento entre `test_python.sh` e o CTest.
- `.planning/codebase/CONVENTIONS.md` — padrão `PS3_*` de env-gate cacheado e a
  disciplina de portabilidade `#ifdef`.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **`runtime/ppu/tests/test_ppu_lifter_emit.py`** (598 linhas): golden de emissão
  *por instrução* que codifica a palavra PowerPC real, decodifica com o decoder de
  produção, traduz com `PPULifter._translate_op`, compara com golden exato, e ainda
  compila e roda o C emitido contra os headers reais. O preâmbulo é a mesma família,
  granularidade de arquivo — daí a decisão D-08 de estender em vez de duplicar infra.
- **`ps3_mem_oob_report`**: reporter já pronto, público, já linkado. Não é preciso
  criar mecanismo de instrumentação novo.

### Established Patterns
- **Golden exato**: mudança na saída do lifter falha alto em vez de derivar em
  silêncio. O teste do preâmbulo deve seguir isso, não asserção por substring frouxa.
- **Convenção SKIP**: exit 0 + linha contendo `SKIP:`. Precedência deliberada — exit
  code é avaliado PRIMEIRO, um exit≠0 é sempre FAIL mesmo com `SKIP:` no stdout. O
  gate de sync (D-05) depende disso para pular quando o irmão não existe.
- **Espelhamento duplo de registro**: `scripts/test_python.sh` e `CMakeLists.txt`
  precisam concordar. Estender um teste já registrado evita esse risco por construção.
- **Env-gate cacheado `PS3_*`**: `static int on=-1; if(on<0){...}` — se a
  instrumentação precisar de gate próprio, seguir esta forma.

### Integration Points
- `SOURCE_PREAMBLE` de `tools/ppu_lifter.py` — onde as três emissões e os quatro
  `ppu_res_*` vivem.
- `scripts/test_python.sh` + `CMakeLists.txt` — já cobertos pelo registro existente.
- `.github/workflows/ci.yml` — sem alteração: a CI chama os dois scripts e não
  reimplementa descoberta de testes.

</code_context>

<specifics>
## Specific Ideas

- **Reverificar antes de "restaurar".** Medido em 2026-07-26: `#include <stdlib.h>` e
  o ramo `__APPLE__` **já existem** no lifter. Só `ppu_stubs` está genuinamente
  ausente (`grep ppu_stubs tools/ppu_lifter.py` vazio). O plano RDY-0 afirma que o
  commit `11a1c3c` apagou as três — isso está desatualizado. O plano da fase não deve
  gastar trabalho reimplementando duas coisas que existem.

- **Divergência viva do build.** `../gow2-recomp/build_macos.sh` tem um bloco de 18
  linhas para `host_gow2_factory` (commit `1f651aa`) ausente em
  `games/gow2/build_macos.sh`. O subtree está atrás do checkout onde o build roda.
  É o que motiva D-05 e D-06.

- **Cifra não verificável.** O plano cita 5277 chamadas `ppu_lwarx` no lift de
  produção. Não é verificável neste repo: `games/gow2/recomp_mid_v2/ppu_recomp_*.cpp`
  é gitignored e ausente. Tratar como não confirmada; se a fase precisar do número,
  medir no checkout irmão e registrar a medição.

- **QUARTA emissão candidata, descoberta durante esta sessão: `ppu_timebase_now`.**
  O commit `e441f37` (autor do projeto, 2026-07-26 01:45, sessão paralela) documenta
  exatamente a classe de defeito que esta fase existe para pegar: o lifter declara
  `extern "C" uint64_t ppu_timebase_now(void);` no preâmbulo e emite `mftb`/`mftbu`
  como `ppu_timebase_now()` (`tools/ppu_lifter.py` ~230 e ~2127), e o
  `lift_selftest.py` assume o mesmo contrato — mas o runtime só definia
  `ps3_timebase_now()`. **Um lift limpo nunca linkava** (`Undefined symbols: _ppu_timebase_now`,
  7+ sítios). O lift de produção escondia a divergência com um `extern` escrito à mão
  e os `mftb` reescritos — zero ocorrências de `ppu_timebase_now` no lift de produção,
  ou seja, mais um órfão sem escritor versionado. Já corrigido em
  `runtime/syscalls/sys_timer.c` como wrapper de uma linha (deliberadamente **não**
  renomeado no lifter: `ppu_*` é o contrato do lifter upstream `sp00nznet` e renomear
  criaria conflito a cada merge).
  **RESOLVIDO — está no escopo.** O autor decidiu em 2026-07-26 incluir esta quarta
  emissão na Fase 1; ver **D-10** abaixo. `ROADMAP.md` (critério 1) e `REQUIREMENTS.md`
  (`REQ-RDY0-1`) foram atualizados de acordo.

</specifics>

<deferred>
## Deferred Ideas

- **Promover o gate de sync do `build_macos.sh` para FAIL** — Fase 2, cujo domínio é
  justamente o `host_gow2_factory` que causa a divergência atual (D-06).
- **Varredura G4 sobre a saída de um lift real**, cobrindo tokens injetados por
  `patch_*.py` — fora da Fase 1 porque exige EBOOT e lift; candidata natural à Fase 3
  ou 4, quando o re-lift já existir.
- **Fechar as três cifras do inventário de host órfão** (1603 / 1662 / 1793) e fazer o
  `verify_lift_baseline.sh` cobrir `preamble_pure.cpp` — Fase 2, registrado como
  bloqueador do Marco 1 no `STATE.md`.
- **`git subtree pull` do gow2-recomp** para eliminar a divergência — operação de
  manutenção, não trabalho de lifter; fora do domínio da Fase 1.

</deferred>

---

*Phase: 1-Emissões do lifter e teste-ouro do preâmbulo*
*Context gathered: 2026-07-26*
