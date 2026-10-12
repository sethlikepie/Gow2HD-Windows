# Checkpoint: Fase 1 — Re-medição do baseline pós-desmascaramento de memória

## Hipótese

A Task 5 do plano `2026-07-25-fase1-desmascarar-memoria` mudou comportamento
por omissão: `sys_memory.c` passou a chamar `ppu_register_committed_range()`
em cada `vm_commit` bem-sucedido, e `sys_mmapper_allocate_address` deixou de
devolver `CELL_OK` quando o commit falha (`fb11d4d`, `0a73dc1`, `3098ebd`).
Hipótese desta task: três runs headless do mesmo binário, sob o mesmo
ambiente da Fase 0, param na **mesma barreira** que `docs/bringup/gow2/
FASE0/baseline.json` (`ultimo_checkpoint=FIRST_WAD_OPEN`, mesmo
`primeiro_erro` normalizado) — ou seja, as ranges adicionalmente registadas
não afectam o caminho medido, porque o `primeiro_erro` desse caminho
(`ea=0xFB788082`) é um **OOB** (`a+n > ppu_vm_size`) — os doze sítios que
emitem `MEMORY|OOB` retornam **antes** de `uncommitted_guard` (ou
`tblsize_guard`) sequer correrem, logo nenhuma das duas máscaras desta task
chega a ser avaliada para este endereço, independentemente de banda. (Não
é "banda `0x8-0x9xxx`, assinatura de ponteiro-lixo do alocador" vs. banda
`0xF…`: essa banda só entra em jogo *dentro* de `uncommitted_guard`, depois
de decidir engolir, para escolher a sonda `PS3_COMMIT_HIGH`/o formato do
log — ver `ppu_loader.cpp:636` e `mascaras-e1-e2.md`.) Também não é o
tipo de endereço que `SYS_MEM_ALLOC_BASE..END` (`0x40000000-0x60000000`
— já confirmado por *facto de código*, não correlação, no relatório da
Task 6) cobre. Esta tentativa não corrige nada: só mede e compara.

## Baseline

`docs/bringup/gow2/FASE0/baseline.json` — 3 runs, `barreira.igual_nos_runs =
true`, `ultimo_checkpoint=FIRST_WAD_OPEN`, `primeiro_erro` = `MEMORY|OOB
ea=0xFB788082 len=4 region=unmapped op=read` em
`_Z13func_00263554P11ppu_context+0x1A8`, `oob=26`,
`imports_nao_resolvidos=5`, `threads_criadas=14`, `wads_abertos=2`,
`frames_apresentados=0`, `instrucoes_desconhecidas=null` (grandeza de
lift-time, nunca observável em run-time).

## Alteração

**Nenhuma no runtime.** Esta task não altera allocator, lifter, scheduler
nem backend RSX. O único artefacto de código tocado durante esta task foi
**um rebuild explícito do `boot_gow2`** (`cd ../gow2-recomp &&
PS3_ENGINE_ROOT=<este-worktree> ./build_macos.sh`) — necessário porque o
binário já presente no checkout partilhado `../gow2-recomp` (mtime
anterior, ver "Primeiro evento divergente" abaixo) tinha sido linkado sem a
camada canónica de instrumentação, e a régua de novidade (`RUNTIME_LIB -nt
BOOT_BIN`) de `scripts/baseline_run.sh` não detecta essa classe de
problema — ver essa secção para a análise completa. Isto não é "código
alterado por esta task": é a mesma reconstrução que `baseline_run.sh` já
faz sozinho quando decide que precisa de recompilar, disparada manualmente
porque o script, desta vez, decidiu (incorrectamente, ver abaixo) que não
precisava.

## Testes unitários

Nível: **offline-unit**. Não alterei nenhum teste nesta task; corri os
existentes para confirmar que o repositório continua verde antes de aceitar
a medição:

```text
$ bash runtime/spu/tests/run_tests.sh   # ultima linha:
Results: 21 passed, 0 failed
```

`tools/test_baseline_report.py` e `tools/test_trace_parse.py` não foram
re-corridos nesta task especificamente (não foram tocados desde a Task 6);
o **uso real** de `tools/trace_parse.py --baseline` contra
`docs/bringup/gow2/FASE0/baseline.json` (Passo 2 do brief) revelou uma
incompatibilidade de contrato não coberta por nenhum teste existente — ver
"Resultado observado".

## Teste de integração

Nível: **in-boot**. Comando real (harness da Fase 0, reutilizado tal como
está, G-"use-as, não reconstrua"):

```text
$ cd /Users/.../PESSOAL/ps3recomp-bringup
$ ./scripts/baseline_run.sh --runs 3 --out <OUT>
```

Duas execuções deste comando aconteceram nesta task — a primeira produziu
um resultado degenerado que foi diagnosticado e não usado como medição
final; a segunda (pós-rebuild) é a medição aceite. Ambas descritas em
detalhe em "Resultado observado".

## Resultado esperado

Critério informal desta task (não há um C-checkpoint próprio; a barreira
C1 `BOOT_MEMORY_OK` da Fase 1 continua em aberto — ver "Decisão"):
`baseline-pos-fase1.json` com 3 runs, dez métricas cada, e
`barreira.igual_nos_runs` decidido por identidade estável — igual ao
padrão da Fase 0 — mais uma comparação explícita com
`docs/bringup/gow2/FASE0/baseline.json` via `diff_baseline`
(`tools/trace_parse.py --baseline`, Passo 2 do brief).

## Resultado observado

### Tentativa 1 (descartada como medição, aceite como diagnóstico): binário linkado contra o engine errado

A primeira corrida de `./scripts/baseline_run.sh --runs 3 --out <OUT1>`
completou sem flake e sem violação de headless, mas produziu um
`baseline.json` **degenerado**: `ultimo_checkpoint: null` e
`primeiro_erro: null` nos 3 runs, `oob: 0`, `threads_criadas: 0`,
`wads_abertos: 0` — apesar do log bruto (250k-313k linhas) mostrar o boot a
progredir normalmente (ELF carregado, TLS, guest entry, dezenas de milhares
de linhas `[TRACE] sys_*`, e mesmo o evento `ea=0xFB788082` conhecido da
Fase 0, via a sonda ad-hoc `[vm] OOB access 0xFB788082 (+4)` em
`ppu_loader.cpp:403`). Ou seja: **o jogo corria, mas a camada canónica
`[PS3T]` inteira — CHECKPOINTS, MEMORY/OOB, MEMORY/SWALLOWED — estava
completamente muda** (zero linhas `[PS3T]` em qualquer um dos 3 logs).

Segui o princípio "suspeitar de nós mesmos primeiro" (`CLAUDE.md`) antes de
declarar uma regressão do jogo. Descartei por ordem, com evidência:

1. **Gating por env var errado?** Não — `env.txt` do harness confirma os 10
   `PS3_TRACE_*` a `1`, e `ps3_trace_on()` (`runtime/trace/ps3_trace.c:75`)
   usa a mesma função para MEMORY e CHECKPOINTS; um teste dirigido
   (`PS3_TRACE_CHECKPOINTS=1` isolado, 8s) confirmou zero linhas mesmo para
   `VM_READY`, que dispara nos primeiríssimos microssegundos de `vm_init()`.
2. **Lógica de `ps3_checkpoint_fire`/`ps3_trace_emit` quebrada?** Não —
   `nm build-macos/libps3recomp_runtime.a` mostra `ps3_checkpoint_fire`,
   `ps3_trace_on`, `ps3_mem_oob_report` definidos (`T`) correctamente
   dentro da biblioteca deste worktree.
3. **Então porque é que `boot_gow2` não os chama?** `nm
   ../gow2-recomp/boot_gow2` (o binário efectivamente usado) devolveu
   **zero ocorrências** de `ps3_checkpoint_fire`, `ps3_trace_on`,
   `ps3_trace_emit`, `ps3_mem_oob_report`, `ps3_mask_swallowed_report` — a
   API pública inteira da camada canónica (Fase 0 Tasks 1-3 + Fase 1 mask
   trace) estava **ausente do binário final**, apesar de presente na `.a`.
   Isto só é possível se o binário não tiver sido linkado contra esta `.a`.
4. **Causa raiz confirmada por leitura do harness do jogo**:
   `../gow2-recomp/build_macos.sh:26` resolve o engine por
   `PS3="${PS3_ENGINE_ROOT:-$HERE/../ps3recomp}"` — **na AUSÊNCIA da env
   var, o default é `../ps3recomp` (o checkout principal PARTILHADO), não
   este worktree**. `git worktree list` confirma que
   `/Users/.../PESSOAL/ps3recomp` está noutro branch inteiramente
   (`macos-arm64-port-f0-f2`, commit `151ee0c`, "ahead 22 behind 21" do
   `origin`), com várias worktrees-agente activas por baixo
   (`.claude/worktrees/agent-*`). O relatório da Task 6
   (`mascaras-e1-e2.md`) já tinha registado "durante o E2-b apareceu um
   segundo processo `boot_gow2` (PID 84355) que não é meu" — a mesma
   partilha de `../gow2-recomp` por múltiplas sessões. A explicação mais
   provável (não confirmada por log de outra sessão, que não tenho como
   inspeccionar): nalgum momento entre a Task 6 medir os seus E1/E2 (que
   *tinham* linhas `[PS3T]` reais — 108 em `f1_e1.log`) e o início desta
   task, `../gow2-recomp/boot_gow2` foi **reconstruído por outra coisa**
   sem `PS3_ENGINE_ROOT` correcto, sobrescrevendo o binário correcto com um
   linkado contra o engine partilhado errado — e
   `scripts/baseline_run.sh`'s critério de novidade (`$RUNTIME_LIB -nt
   $BOOT_BIN`, só compara *mtimes*, nunca proveniência/commit) não tem como
   detectar essa troca: o binário "errado" era mais novo que a `.a` deste
   worktree, logo passava por "actualizado".

**Isto é uma lacuna genuína do harness da Fase 0** (não coberta pelo brief
desta task, que manda "usar as ferramentas, não reconstruir") — registo-a
como achado, não a corrijo aqui (ver "Próxima tentativa"). O `commit_engine`
gravado no `baseline.json` degenerado (`ba42c67`) era, já agora, o valor
certo — porque é lido de `git -C "$ENGINE_ROOT" rev-parse HEAD` neste
worktree, não do binário; **o campo de proveniência do próprio harness não
teria apanhado o problema.**

### Tentativa 2 (aceite): rebuild explícito, verificado, depois re-medido

Reconstruí `boot_gow2` explicitamente:

```text
$ cd ../gow2-recomp
$ PS3_ENGINE_ROOT=/Users/.../PESSOAL/ps3recomp-bringup ./build_macos.sh
*** boot_gow2 built (LIFT_OPT=-O0 HOST_OPT=-O0) ***
```

Build limpo (`exit=0`), 0 erros. Verifiquei ANTES de confiar em qualquer
medição:

```text
$ nm boot_gow2 | grep -E ' T _ps3_(checkpoint_fire|trace_on|mem_oob_report|trace_emit|mask_swallowed_report)'
... T _ps3_checkpoint_fire
... T _ps3_mask_swallowed_report
... T _ps3_mem_oob_report
... T _ps3_trace_emit
... T _ps3_trace_on
```

e com um smoke de 10s confirmei os primeiros checkpoints reais
(`VM_READY` → `ELF_LOADED` → `TLS_INIT` → `GUEST_ENTRY` → `FIRST_SPU_JOB`
→ ... → `FIRST_DRAW`) antes de correr os 3 runs formais.

`./scripts/baseline_run.sh --runs 3 --out <OUT2>` (a mesma invocação,
binário agora correcto): **0 ocorrências de "vm commit (sys_memory window)
failed" nos 3 logs, `attempts=1 flaked_final=0` nos 3 `.meta` — zero
repetições necessárias por causa do flake**, tal como na Fase 0.
`headless_verified=1` nos 3 (banner `[boot] RSX backend=none (PS3_NO_RSX)`
+ `[rsx-bridge] trace backend active (no window)`, sem nenhum banner de
janela).

`boot_gow2` permaneceu com o mesmo `sha256` (`9ecb666...`) e `mtime` do
início ao fim das 3 corridas — confirmado depois de todas terminarem, para
excluir que outra sessão tivesse reconstruído o binário partilhado a meio
da medição (o mesmo risco que causou a Tentativa 1).

**Resumo real (in-boot), 3 runs:**

| run | duração | último checkpoint | oob | imports_nao_resolvidos | threads_criadas | jobs_spu | wads_abertos | frames_apresentados |
|---|---|---|---|---|---|---|---|---|
| 1 | 61.0s | `FIRST_WAD_OPEN` | 26 | 5 | 14 | 354 | 2 | 0 |
| 2 | 62.0s | `FIRST_WAD_OPEN` | 26 | 5 | 14 | 361 | 2 | 0 |
| 3 | 62.0s | `FIRST_WAD_OPEN` | 26 | 5 | 14 | 355 | 2 | 0 |

`barreira.igual_nos_runs = true`, mesmo `primeiro_erro` normalizado nos 3:
`MEMORY|OOB ea=0xFB788082 len=4 region=unmapped op=read` em
`_Z13func_00263554P11ppu_context+0x1A8`. Sequência de checkpoints
idêntica à Fase 0 nos 3 runs: `VM_READY → ELF_LOADED → TLS_INIT →
GUEST_ENTRY → FIRST_SPU_JOB → FIRST_THREAD → FIRST_FS_OPEN → FIRST_FLIP →
FIRST_DRAW → MOVIE_EOS → FIRST_WAD_OPEN` — **11/13 checkpoints possíveis**
(não 12): `SHUTDOWN` nunca dispara porque os 3 runs morrem por timeout, não
por saída ordeira; `FIRST_PRESENT` é **estruturalmente impossível** sob
`PS3_RSX_BACKEND=trace` (esse backend só regista
`.set_shader`/`.clear`/`.draw_arrays`/`.draw_indexed`, nunca `.present` —
o `if (b->present)` de `cellGcmSys.c:1120` nunca é verdadeiro), exactamente
como documentado em `FASE0/attempt-1.md`. Os dois WADs abrem na mesma
ordem: `r_lglsca.wad_ps3` (3072 bytes) primeiro, dispara `FIRST_WAD_OPEN`;
`r_perma.wad_ps3` (`len=20169344`, os mesmos 20MB da "Parede C vencida" do
`CLAUDE.md`) a seguir.

Os 5 NIDs não resolvidos são **exactamente os mesmos** da Fase 0
(`0x63FF6FF9`, `0xDB869F20`, `0x4692AB35`, `0xEFEB2679`, `0x4A5EAB63`,
confirmado por grep linha-a-linha no `run_1.log`).

### Comparação directa com `docs/bringup/gow2/FASE0/baseline.json`

Campo a campo (script Python ad-hoc, comparando os dois `baseline.json`,
schema `ps3recomp.baseline/1` — ver "Evidências" para o porquê de não usar
`trace_parse.py --baseline` aqui):

| métrica | Fase 0 (3 runs) | Fase 1 pós-medição (3 runs) | Diferença? |
|---|---|---|---|
| `ultimo_checkpoint` | `FIRST_WAD_OPEN` ×3 | `FIRST_WAD_OPEN` ×3 | **Não** |
| `primeiro_erro` (normalizado, sem `tb`) | `MEMORY\|OOB ea=0xFB788082 ...` ×3 | idêntico ×3 | **Não** |
| `oob` | 26, 26, 26 | 26, 26, 26 | **Não** |
| `imports_nao_resolvidos` | 5, 5, 5 | 5, 5, 5 | **Não** |
| `instrucoes_desconhecidas` | null, null, null | null, null, null | **Não** (grandeza de lift-time em ambas) |
| `threads_criadas` | 14, 14, 14 | 14, 14, 14 | **Não** |
| `wads_abertos` | 2, 2, 2 | 2, 2, 2 | **Não** |
| `frames_apresentados` | 0, 0, 0 | 0, 0, 0 | **Não** |
| `jobs_spu` | 355, 355, 357 | 354, 361, 355 | **Sim, jitter esperado** |
| `duracao_s` | 61.0, 62.0, 61.0 | 61.0, 62.0, 62.0 | **Sim, ±1s, jitter esperado** |

**As duas únicas diferenças (`jobs_spu`, `duracao_s`) são exactamente a
mesma classe de jitter que a própria Fase 0 já documentou como não-barreira**
(`FASE0/attempt-1.md`: "`jobs_spu` varia ligeiramente... jitter esperado de
quantos dispatch slices SPURS correm antes do kill por timeout"; a
`duracao_s` oscila ±1-2s à volta do timeout de 60s por granularidade do
polling de 1s em `baseline_run.sh`). **Nenhuma das dez métricas do schema
mudou de forma que indique uma barreira diferente.**

**Explicação directa (a que a hipótese previu):** as ranges que
`sys_memory.c` passou a registar via `ppu_register_committed_range()`
(Task 5) não mudam o caminho medido porque **o primeiro evento divergente
já era, antes e depois desta fase, um OOB** (`ea=0xFB788082`,
`a+n > ppu_vm_size`) — e os doze sítios que emitem `MEMORY|OOB`
(`ppu_loader.cpp:831,866,893,978,1192,1213,1300,1608` e os quatro atómicos
em `:1657,1677,1692,1704`) retornam **antes** de `uncommitted_guard` correr.
Não é uma questão de banda (`0x80000000-0x9FFFFFFF`, a assinatura de
ponteiro-lixo do alocador guest documentada em `ppu_loader.cpp:636`, vs.
`0xF…`): `uncommitted_guard` não filtra por banda para decidir se
intercepta — só olha se o endereço está coberto por uma range registada
(`vm_uncommitted()`, `ppu_loader.cpp:586-598`); o teste de banda em `:636`
corre **depois** dessa decisão, só para escolher a sonda `PS3_COMMIT_HIGH`
e o formato do log. O que de facto poupa `0xFB788082` de ambas as máscaras
desta task é o `return` do guard de OOB, que corre primeiro em todos os
call-sites — nem chega a perguntar-se se está coberto por
`SYS_MEM_ALLOC_BASE..END` (`0x40000000-0x60000000`) ou por qualquer range
registada. Isto confirma, com uma segunda medição independente, o
mesmo facto de código que a Task 6 já tinha estabelecido.

**Achado adicional (fora do schema de 10 métricas, mas directamente
relevante à Fase 1 — grep no log bruto, in-boot):** mesmo sob o ambiente
DEFAULT desta task (nenhum `PS3_MASK_OFF_*` definido — máscaras ligadas,
G2), o `uncommitted_guard` engoliu **80 linhas** (64 `SWALLOWED` completas
+ 16 `SWALLOWED_TRUNCATED`, último marco `count=2097152` = 2^21) e o
`tblsize_guard` engoliu **1 linha**, no mesmo EA (`0x00572BB0`) e mesma
função (`func_0037600C+0x28C`) que a Task 6 já tinha identificado — números
quase idênticos aos 81/1 da Task 6 (a diferença de 1 no `uncommitted_guard`
é o mesmo tipo de jitter de "quantos marcos de truncagem cabem antes do
timeout" já discutido acima). Isto **corrobora, sob o ambiente-padrão desta
task e não apenas sob o E1 dedicado da Task 6**, que as duas máscaras
continuam activas e a intervir da mesma forma.

**Veredictos da Task 6 (`mascaras-e1-e2.md`), citados aqui com as
qualificações que a revisão dessa task fixou — não re-fortalecidas:** o
`uncommitted_guard` foi caracterizado como escondendo corrupção real do
alocador guest, **mas** o crash observado ao desligá-lo (E2-a) é **n=1, com
o mecanismo causal não confirmado por backtrace** — há duas hipóteses
alternativas em aberto (o próprio gate novo nunca antes exercitado à
escala; ou um acesso a jusante do primeiro que o guard deixaria passar). O
`tblsize_guard` foi caracterizado como não sendo bug de mapeamento
**apenas para o único EA de facto exercitado** (`0x572BB0`) — entre
"lacuna de seeding do ambiente de recompilação" e "bug de lift em
`func_0037600C`" essa task **não discrimina**, e os outros 5 EAs que o
guard protege continuam **não-exercitados** (não "não acontecem"). Esta
re-medição não acrescenta informação nova a nenhuma dessas duas questões
em aberto — só confirma que os mesmos EAs/funções continuam a disparar sob
o ambiente-padrão.

## Primeiro evento divergente

Idêntico à Fase 0, nos 3 runs: a primeira `MEMORY/OOB`, leitura de 4 bytes
em `ea=0xFB788082` (`region=unmapped`), na função guest
`_Z13func_00263554P11ppu_context+0x1A8`, pouco depois de
`FIRST_WAD_OPEN`. Não investiguei mais fundo a causa raiz desse acesso
nesta task (fora de escopo — é trabalho de uma Fase 1+ dedicada à cadeia
causal, não a esta re-medição).

Dentro **desta task especificamente** (não do boot do jogo), o evento mais
divergente foi o diagnóstico da Tentativa 1 acima: um `baseline.json` com
`ultimo_checkpoint: null` que, à primeira vista, pareceria "o jogo parou de
funcionar" — mas que na realidade era "o binário não estava a chamar a
instrumentação", uma distinção que só ficou clara ao inspeccionar o log
bruto (que mostrava o boot a progredir normalmente via sondas ad-hoc) e ao
usar `nm` para confirmar a ausência dos símbolos canónicos no executável
final.

## Evidências

Nível de cada afirmação indicado entre parênteses.

- (**in-boot**) `baseline.json` da Tentativa 2, 3 runs reais, sanitizado em
  `docs/bringup/gow2/FASE1/baseline-pos-fase1.json`.
- (**offline-unit**) `nm` do `boot_gow2` da Tentativa 1 (sem os 5 símbolos
  canónicos) vs da Tentativa 2 (com os 5) — comando e saída colados acima;
  inspecção estática do binário, não observação de um boot a correr.
- (**offline-unit**) `../gow2-recomp/build_macos.sh:26`:
  `PS3="${PS3_ENGINE_ROOT:-$HERE/../ps3recomp}"` — linha real, lida por
  inspecção do ficheiro (leitura de texto, não execução).
- (**offline-unit**) `git worktree list` confirmando `/Users/.../PESSOAL/
  ps3recomp` noutro branch (`macos-arm64-port-f0-f2`) com worktrees-agente
  activas por baixo — comando de metadado do git, não do boot.
- (**in-boot**) `grep -c "vm commit (sys_memory window) failed"` = 0 nos 6
  logs totais desta task (3 da Tentativa 1 + 3 da Tentativa 2) — **0
  repetições** por causa do flake conhecido, em qualquer das duas
  tentativas.
- (**offline-unit**) `sha256`/`mtime` de `boot_gow2` idênticos antes e depois
  das 3 corridas da Tentativa 2 (`9ecb666...`, `2026-07-25T21:07:05Z`) —
  exclui reconstrução por outra sessão a meio da medição aceite; verificação
  de ficheiro no filesystem, não observação do boot em si.
- (**offline-unit / achado de ferramenta**) `tools/trace_parse.py
  --baseline docs/bringup/gow2/FASE0/baseline.json --json` (Passo 2 do
  brief, executado literalmente) **crasha**:
  ```
  TypeError: 'NoneType' object is not iterable
    File "tools/trace_parse.py", line 397, in _normalize_threads_bloqueadas
      return frozenset((e.get("tid"), e.get("on")) for e in entries)
  ```
  Causa: o contrato testado de `--baseline` (`tools/test_trace_parse.py`,
  `test_t6_8c_diff_baseline_via_json_no_disco`, docstring: "anterior de
  `--json`, lido de volta por `--baseline`") espera um dict no schema de
  **uma única execução** de `trace_parse.py --json` (com chaves
  `threads_bloqueadas`, `fios_pendentes`, etc.), não o schema **agregado**
  de `baseline_report.py` (`ps3recomp.baseline/1`, chaves `runs`/`env`/
  `barreira`) que `docs/bringup/gow2/FASE0/baseline.json` de facto é. O
  brief desta task (Passo 2) pede a combinação errada — nunca tinha sido
  exercitada antes porque a Fase 0 foi o primeiro baseline (nada para
  comparar). Não fiz o patch a `trace_parse.py` (fora do escopo desta task
  de medição/relatório, código partilhado sem cobertura de teste para o
  caso agregado); em vez disso, fiz a comparação directa campo-a-campo
  entre os dois `baseline.json` (secção "Resultado observado" acima, que
  responde à mesma pergunta que o Passo 2 pretendia) **e** corri
  `trace_parse.py --json` (sem `--baseline`) sobre os 3 `run_N.log` novos,
  para obter as oito derivações por si.
- (**in-boot**) `trace_parse.py --json` (sem baseline) sobre os 3 runs da
  Tentativa 2: `threads_bloqueadas` = 1 entrada (`tid=5 on=eventqueue`) nos
  3 runs; `fios_pendentes` = 4 entradas nos 3 (`gow2.psarc`, `smlogo_v2.m2v`,
  `r_lglsca.wad_ps3`, `r_perma.wad_ps3`, todas `state=open status=pendente`)
  — mesmos 4 ficheiros da Fase 0. **Ressalva de leitura obrigatória:** ambos
  os campos significam "aberto/à espera no momento em que o processo foi
  morto", não "vazado" — os logs são cortados por timeout (kill por PID,
  não saída ordeira), logo uma thread legitimamente à espera de um evento e
  um ficheiro legitimamente aberto nesse instante são indistinguíveis,
  neste parser, de uma fuga real; sem esta ressalva, os 4 ficheiros listados
  leriam-se erradamente como 4 fugas de handle. `dma_pendentes` = 1, 0, 1
  (mesmo padrão de jitter "0,0,1" que a Fase 0 documentou);
  `ultimo_flip_present.flip_done` não-nulo, `.present: null` nos 3;
  `linhas_ignoradas: 0` nos 3.
- (**in-boot**) contagem de máscaras no log-padrão (sem `PS3_MASK_OFF_*`):
  `uncommitted_guard` 80/80/80 linhas (mesma EA/função top da Task 6),
  `tblsize_guard` 1/1/1 linha (`ea=0x00572BB0`, `func_0037600C+0x28C`,
  igual à Task 6).
- (**in-boot**) os 5 NIDs não resolvidos idênticos à Fase 0, confirmados
  por grep linha-a-linha no `run_1.log` da Tentativa 2.
- (**offline-unit**) `bash runtime/spu/tests/run_tests.sh` → `Results: 21
  passed, 0 failed`, corrido antes de aceitar a medição.

**Sanitização de `baseline-pos-fase1.json` (obrigatório):** o ficheiro
bruto gerado pelo harness continha caminhos absolutos desta máquina em
`binario.path`, `env.PS3_MOVIE_CACHE` e `env.PS3_VFS_ROOT` (prefixo
`/Users/andrebrumcortezferreira/Documents/PESSOAL/...`). A cópia commitada
teve esse prefixo substituído por `../` (mesma convenção da Fase 0) — ex.:
`../gow2-recomp/boot_gow2`. Confirmado por
`grep -oE '/Users/[A-Za-z]+|EBOOT|\.psarc' baseline-pos-fase1.json` → sem
resultados. Nenhum campo contém dado de jogo nem excerto de memória guest
(os campos `texto` de `primeiro_erro` são linhas de instrumentação —
endereço/tamanho/região — não bytes do jogo). Os `run_N.log` brutos das duas
tentativas (~100-160MB cada, 6 no total) **não foram commitados** e
permanecem no directório de scratch fora dos dois repos, não apagados.

## Decisão

**Aceitar a medição da Tentativa 2 como o baseline pós-Fase 1**, com a
Tentativa 1 preservada como diagnóstico (não como número). Os três runs
apresentam a mesma barreira que a Fase 0, `barreira.igual_nos_runs = true`
sem necessidade de `variacao_explicada`, e a comparação campo-a-campo com
`docs/bringup/gow2/FASE0/baseline.json` não mostra nenhuma diferença fora
do jitter já documentado pela própria Fase 0.

**O C1 `BOOT_MEMORY_OK` da Fase 1 (`docs/IMPLEMENTATION_PLAN_GAME_
RUNTIME_BRINGUP.md`, linha ~218) NÃO foi atingido por esta task, nem era
suposto ser** — este entregável (Task 7 do plano
`2026-07-25-fase1-desmascarar-memoria`) é só o harness de re-medição; o
`primeiro_erro` continua um `OOB` não diagnosticado
(`ea=0xFB788082`), não um boot limpo. Falta, explicitamente, para C1:

- validação de headers e footers do allocator guest (o plano de origem
  pede-o na secção "Implementação" da Fase 1; nenhuma task desta fase
  implementou-o);
- detecção de double-free, overlap e split inválido;
- os modos `strict`/`diagnostic` de memória (o plano de Fase 1 desta task
  deliberadamente não os implementou — ver "O que este plano não faz" no
  plano de origem);
- fechar o buraco das **~2110 escritas directas a `vm_base`** que
  contornam o funil hookado (`stvx/stvebx/stvehx/stvewx` = 1184,
  `stwbrx/sthbrx` = 922, `dcbz` = 4 — medidas no lift construído, spec
  §7.1 do design de Fase 1) — a contagem de `SWALLOWED`/`OOB` continua,
  por isso, um **limite inferior**, nunca a contagem total real de acessos
  inválidos;
- as duas máscaras `type15_*` fora do repo do motor
  (`gow2-recomp/host_gow2_factory.cpp`) continuam **não instrumentadas**
  (spec §7.2);
- a mina do re-lift (spec §7.3) continua por resolver: o lift construído
  hoje usa os `ppu_lwarx` hookados (5277 chamadas), não os `ppu_res_*`
  mais recentes do lifter (que não reportam OOB/SWALLOWED) — um re-lift
  hoje apagaria silenciosamente parte desta instrumentação.

## Próxima tentativa

Duas frentes, sem ordem de prioridade imposta por esta task:

1. **Continuar a cadeia causal do `primeiro_erro`** (idêntico à Fase 0):
   investigar a origem do acesso `ea=0xFB788082` em `func_00263554` /
   `func_002B0FB4`. Não há correlação de banda com o `uncommitted_guard`
   (bandas disjuntas — ver "Hipótese" acima); a pista concreta é outra: a
   mesma `func_00263554` aparece também no top-10 do `uncommitted_guard`
   (offset `+0xD4`, diferente do `+0x1A8` deste evento, mas a mesma função
   base — `mascaras-e1-e2.md`), o que a torna candidata a uma auditoria
   sistemática de fallthrough cross-fragment (`CLAUDE.md`), sem que isso
   confirme ainda nenhuma relação causal entre os dois eventos.
   Potencialmente o mesmo alocador free-list ("megaprojecto") do
   `CLAUDE.md`, mas a identidade do chamador dominante (`func_000B951C`)
   com esse alocador continua **hipótese não confirmada** (Task 6, não
   re-verificada aqui).
2. **Fechar a lacuna do harness descoberta nesta task**: `scripts/
   baseline_run.sh`'s critério "boot_gow2 já está actualizado" (comparação
   de mtimes `RUNTIME_LIB -nt BOOT_BIN`) não detecta um `boot_gow2`
   linkado contra um `PS3_ENGINE_ROOT` diferente do worktree que está a
   medir — um risco real e já observado numa máquina partilhada com várias
   sessões a usar o mesmo `../gow2-recomp`. Uma verificação de proveniência
   mais forte (ex.: `nm` num símbolo-sentinela, ou gravar+comparar o
   `PS3_ENGINE_ROOT` usado no último build num ficheiro ao lado do
   binário) evitaria repetir o diagnóstico manual desta task.
3. Corrigir (ou pelo menos documentar como teste que falha) a
   incompatibilidade de `tools/trace_parse.py --baseline` com o schema
   agregado de `baseline_report.py` — hoje a única forma testada de usar
   `--baseline` é contra um `--json` anterior do próprio `trace_parse.py`,
   não contra `baseline.json`.
4. Seguir a lista de "O que falta para C1" na secção "Decisão" acima —
   nenhum destes é trabalho desta task.
