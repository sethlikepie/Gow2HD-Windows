# Checkpoint: FASE 0 — Baseline do jogo recompilado (gow2, NPUA80491)

## Hipótese

Com a instrumentação comum (`PS3_TRACE_*`, Tasks 1–5), o parser
(`tools/trace_parse.py`, Task 6) e o harness (`scripts/baseline_run.sh` +
`tools/baseline_report.py`, Task 7) já implementados e testados, três runs
headless consecutivos de `boot_gow2` sob o **mesmo** ambiente e o **mesmo**
binário param na **mesma barreira** — mesmo `ultimo_checkpoint` e mesma forma
normalizada de `primeiro_erro` — porque nenhum destes runs altera código,
seed ou entrada do jogo entre si. Esta tentativa não corrige nada: apenas
executa a Fase 0 do plano de origem e regista o estado real, para servir de
referência às Fases 1+.

## Baseline

Não existe `baseline.json` anterior — este é o **primeiro** run formal da
Fase 0 (Tasks 1–7 produziram e testaram o harness, mas o brief da Task 7
proibiu explicitamente correr os 3 runs formais nessa task). Estado de
partida: `libps3recomp_runtime.a` e `boot_gow2` já compilados e actualizados
(o harness confirmou "boot_gow2 ja esta actualizado -- a saltar
build_macos.sh", `build_ran=0`), pelas Tasks anteriores desta mesma branch.

## Alteração

**Nenhuma.** Esta task não altera runtime, lifter, allocator, scheduler nem
backend RSX (proibido pela nota final do plano de origem, secção "O que este
plano não faz"). Os únicos artefactos novos são os de reporte:
`docs/bringup/gow2/FASE0/baseline.json`, este ficheiro, e a actualização da
secção Fase 0 de `docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md`.

## Testes unitários

Nível: **offline-unit**. Comandos reais e saída:

```text
$ ./.venv/bin/python tools/test_baseline_report.py
ok: baseline_report (schema ps3recomp.baseline/1, barreira, 10 metricas por run, null nunca fabricado)

$ ./.venv/bin/python tools/test_trace_parse.py
ok: trace_parse (T6.1-T6.9 + diff_baseline volatil/review + parse_kv/is_adhoc_error auxiliares)

$ bash runtime/spu/tests/run_tests.sh   # ultima linha:
Results: 21 passed, 0 failed
```

Nenhum destes testes foi alterado por esta task; foram apenas re-corridos
para confirmar que continuam verdes antes de aceitar o baseline.

## Teste de integração

Nível: **in-boot**. Comando real:

```text
$ cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp-bringup
$ ./scripts/baseline_run.sh --runs 3 --timeout 60 --out <OUT>
```

com `PS3_ENGINE_ROOT` implícito no próprio harness (`ENGINE_ROOT` resolvido
como o directório-pai de `scripts/`, i.e. este worktree — o harness já invoca
`build_macos.sh` com `PS3_ENGINE_ROOT="$ENGINE_ROOT"` internamente, nunca o
motor por default). `<OUT>` é um directório de scratch fora dos dois repos
(G7); os `run_N.log` (101–145 MB cada) e os `run_N.meta` ficam lá,
**não commitados**.

Saída real do harness (stdout completo, resumida às linhas de estado):

```text
baseline_run.sh: engine=.../ps3recomp-bringup jogo=.../gow2-recomp out=<OUT> runs=3 timeout=60s
baseline_run.sh: boot_gow2 ja esta actualizado -- a saltar build_macos.sh.
baseline_run.sh: build_duration_s=0 build_ran=0
baseline_run.sh: env PS3_* activo gravado em <OUT>/env.txt (23 vars)
baseline_run.sh: run 1/3 (tentativa 1/3) -> <OUT>/run_1.log
baseline_run.sh: run 1: timeout de 60s -- kill -TERM 10296
baseline_run.sh: run 2/3 (tentativa 1/3) -> <OUT>/run_2.log
baseline_run.sh: run 2: timeout de 60s -- kill -TERM 10914
baseline_run.sh: run 3/3 (tentativa 1/3) -> <OUT>/run_3.log
baseline_run.sh: run 3: timeout de 60s -- kill -TERM 11162
baseline_run.sh: headless confirmado (nenhum banner de janela) em todos os 3 run(s)
baseline_run.sh: a gerar baseline.json...
baseline_report: escrito <OUT>/baseline.json (3 run(s), igual_nos_runs=True)
baseline_run.sh: OK -- <OUT>/baseline.json
```

**Flakiness conhecida ("vm commit (sys_memory window) failed"):** os três
`run_N.meta` mostram `attempts=1 flaked_final=0` e nenhum dos três `run_N.log`
contém a string `"vm commit (sys_memory window) failed"` (`grep -c` = 0 nos
três). **Zero repetições foram necessárias** — não escondido, apenas não
observado nesta leva.

**Headless confirmado (G6):** os três runs mostram
`[boot] RSX backend=none (PS3_NO_RSX)` e `[rsx-bridge] trace backend active
(no window)`; nenhum banner de janela (`window_backend_banner_seen=0` nos
três `.meta`). Morte sempre por PID (`kill -TERM`, depois `kill -9` se
necessário) — nunca `pkill`.

## Resultado esperado

Critério de aceite da Fase 0 (plano de origem, linha 150-152): *"Existe um
`baseline.json` e os três runs apresentam a mesma barreira ou uma variação
explicada."* Esperava-se `baseline.json` com 3 entradas em `runs`, dez
métricas cada (uma delas, `instrucoes_desconhecidas`, sempre `null` por ser
grandeza de lift-time — ver `tools/baseline_report.py`), e
`barreira.igual_nos_runs` decidido por identidade estável de
`ultimo_checkpoint` + `primeiro_erro` (não por texto/endereços voláteis).

## Resultado observado

`baseline.json` gerado com sucesso, **3 runs**, `barreira.igual_nos_runs =
true` (sem necessidade de `variacao_explicada`). Resumo real (in-boot):

| run | duração | último checkpoint | primeiro erro | oob | imports_nao_resolvidos | threads_criadas | jobs_spu | wads_abertos | frames_apresentados |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 61.0s | `FIRST_WAD_OPEN` | `MEMORY/OOB ea=0xFB788082 len=4 region=unmapped op=read` | 26 | 5 | 14 | 355 | 2 | 0 |
| 2 | 62.0s | `FIRST_WAD_OPEN` | `MEMORY/OOB ea=0xFB788082 len=4 region=unmapped op=read` | 26 | 5 | 14 | 355 | 2 | 0 |
| 3 | 61.0s | `FIRST_WAD_OPEN` | `MEMORY/OOB ea=0xFB788082 len=4 region=unmapped op=read` | 26 | 5 | 14 | 357 | 2 | 0 |

`jobs_spu` varia ligeiramente (355, 355, 357) — jitter esperado de quantos
"dispatch slices" SPURS correm antes do kill por timeout (ver a ressalva no
cabeçalho de `tools/baseline_report.py`: `TASK_START` conta dispatches, não
jobs logicamente distintos); não afecta `barreira.igual_nos_runs`, que só
compara `ultimo_checkpoint` e `primeiro_erro`.

`instrucoes_desconhecidas` é `null` nos três — motivo documentado no
cabeçalho de `tools/baseline_report.py`: é uma grandeza de *lift-time*
(opcodes PPC não reconhecidos por `ppu_lifter.py`), nunca observável por um
log de execução; um `0` aqui seria forjado (regra 4 do `CLAUDE.md`).

`frames_apresentados = 0` nos três é **medição real, não um bug do
harness**: o backend forçado é `PS3_RSX_BACKEND=trace` (logging, sem janela
— necessário para honrar G6/headless numa máquina onde o `cellGcmSys.c` tem
um segundo registo de backend independente de `PS3_NO_RSX`); esse backend
nunca chama `present()`. `FLIP_REQ`/`FLIP_DONE` continuam a disparar
normalmente nos três runs (`ultimo_flip_present.flip_done` não-nulo em
todos, confirmado via `trace_parse.py --json`), só `PRESENT` (que dependeria
de uma janela real) fica em zero.

Checkpoints atingidos, na mesma ordem cronológica nos três runs: `VM_READY →
ELF_LOADED → TLS_INIT → GUEST_ENTRY → FIRST_SPU_JOB → FIRST_THREAD →
FIRST_FS_OPEN → FIRST_FLIP → FIRST_DRAW → MOVIE_EOS → FIRST_WAD_OPEN`. Os
dois primeiros WADs abertos (`FIOS/OP_BEGIN wad=1`) são `r_lglsca.wad_ps3` e
`r_perma.wad_ps3` — este último com `len=20169344`, o mesmo valor de 20MB
documentado como a "Parede C vencida" no `CLAUDE.md` (stream do R_PermA). O
thread guest em todas as linhas relevantes é `0x4D41494E`, que em ASCII lê
"MAIN" — é a thread principal do jogo, não uma worker.

`SHUTDOWN` (checkpoint 13/13) **não disparou em nenhum dos três runs**
(`grep -c "CHECKPOINTS | SHUTDOWN"` = 0 nos três `run_N.log`) — **esperado**:
os três runs morreram por timeout (`kill -TERM` aos 60s), nunca por saída
ordeira.

`FIRST_PRESENT` também **não disparou em nenhum dos três runs**
(`grep -c "CHECKPOINTS | FIRST_PRESENT"` = 0 nos três `run_N.log`) — e é
**estruturalmente impossível neste harness**, não um gap de cobertura: o
disparo (`ps3_checkpoint_fire(PS3_CP_FIRST_PRESENT, ...)`,
`libs/video/cellGcmSys.c:1157`) vive dentro de `if (b->present)`
(`cellGcmSys.c:1120`), e o backend forçado pelo harness
(`PS3_RSX_BACKEND=trace`, `scripts/baseline_run.sh:245`) regista
`s_trace_backend` (`cellGcmSys.c:479-484`), que só define
`.set_shader`/`.clear`/`.draw_arrays`/`.draw_indexed` — `.present` fica
NULL, logo o `if` nunca é verdadeiro. É exactamente a mesma causa já
explicada acima para `frames_apresentados = 0`: o backend trace nunca chama
`present()`.

Isto é **11/13 checkpoints possíveis** neste run (13 no enum de
`include/ps3_checkpoint.h:44-56`, menos `SHUTDOWN` — que exigiria saída
ordeira em vez de timeout — e `FIRST_PRESENT` — que exigiria um backend com
`.present` não-NULL, incompatível com o requisito headless deste harness),
não um "gap" a esconder.

`threads_bloqueadas` (1 thread, bloqueada em `eventqueue`) e `fios_pendentes`
(4 operações — `gow2.psarc`, `smlogo_v2.m2v`, `r_lglsca.wad_ps3`,
`r_perma.wad_ps3`, todas em `state=open status=pendente`) aparecem
consistentemente nos três runs (via `trace_parse.py --json`, fora do schema
de `baseline.json` — que só usa `ultimo_checkpoint`+`primeiro_erro` para a
barreira). **Isto significa "aberto no instante do kill", não "leak"**: todo
log é truncado pelo timeout, e uma thread/ficheiro legitimamente à espera
nesse instante fica indistinguível de um leak neste harness — não deve ser
lido como defeito sem uma FASE 1+ dedicada a confirmar (ou refutar) leak
real via um run mais longo ou via encerramento ordeiro.

`dma_pendentes` variou entre runs (`0, 0, 1`) — jitter de timing no instante
exacto do kill, não uma divergência de barreira (excluído deliberadamente da
comparação de `igual_nos_runs`, que só usa `ultimo_checkpoint` e
`primeiro_erro` — ver o cabeçalho de `tools/baseline_report.py`).

`linhas_ignoradas = 0` nos três runs — o parser não rejeitou nenhuma linha
dos ~100–145 MB de log real (robustez confirmada **in-boot**, complementando
os testes sintéticos T6.7 **offline-unit**).

## Primeiro evento divergente

Não há divergência **entre** os três runs (mesma barreira). **Dentro** de
cada run, o primeiro evento divergente do caminho feliz é a primeira
`MEMORY/OOB`: um acesso de leitura de 4 bytes a `ea=0xFB788082`
(`region=unmapped`), na função guest `_Z13func_00263554P11ppu_context+0x1A8`,
pouco depois do checkpoint `FIRST_WAD_OPEN` disparar (mesma thread `MAIN`).
Logo a seguir seguem-se mais OOBs em `func_002B0FB4` (offsets crescentes:
`+0x1318`, `+0x1334`, `+0x136C`, `+0x13C0`, alternando `read`/`write` no
mesmo par de endereços `0xFB788082`/`0xFB788086`/`0xFB78808A`) — um padrão
que sugere um ponteiro/handle inválido sendo lido e reescrito repetidamente,
não um evento isolado. **Isto é observação, não diagnóstico**: identificar a
causa raiz (handle corrompido? offset errado num typemap? region realmente
não mapeada por decisão do allocator?) é trabalho de Fase 1+, fora do escopo
desta task (que só regista o baseline).

## Evidências

Nível de cada afirmação indicado entre parênteses.

- (**in-boot**) Linha real do `primeiro_erro`, run 1, `<OUT>/run_1.log:1104212`:
  ```
  [PS3T] 2380073144 | 0x4D41494E | _Z13func_00263554P11ppu_context+0x1A8 | MEMORY | OOB | ea=0xFB788082 len=4 region=unmapped op=read
  ```
- (**in-boot**) Linha real do `ultimo_checkpoint`, run 1, `<OUT>/run_1.log:1103886`:
  ```
  [PS3T] 2370097825 | 0x4D41494E | movie_io_open | CHECKPOINTS | FIRST_WAD_OPEN | path=/wad/r_lglsca.wad_ps3
  ```
- (**in-boot**) Abertura do segundo WAD (20MB), `<OUT>/run_1.log:1103959`:
  ```
  [PS3T] 2374990044 | 0x4D41494E | movie_io_open | FIOS | OP_BEGIN | op=1297481731 path=/wad/r_perma.wad_ps3 len=20169344 state=open wad=1
  ```
- (**in-boot**) `run_N.meta` dos 3 runs (`attempts=1 flaked_final=0` nos
  três; `duration_s` 61/62/61; `headless_verified=1` nos três).
- (**in-boot**) `<OUT>/baseline.json` (cópia sanitizada em
  `docs/bringup/gow2/FASE0/baseline.json`), `barreira.igual_nos_runs: true`.
- (**offline-unit**) `./.venv/bin/python tools/trace_parse.py --json
  <OUT>/run_1.log` (comando real, executado sobre um log real — por isso o
  próprio *parse* é in-boot quanto ao dado, offline-unit quanto ao teste do
  parser): `threads_bloqueadas` (1 entrada, `on=eventqueue`),
  `fios_pendentes` (4 entradas), `dma_pendentes` (0), `linhas_ignoradas: 0`,
  `ultimo_flip_present.flip_done` não-nulo (`present: null`).
- (**offline-unit**) Testes: `tools/test_baseline_report.py`,
  `tools/test_trace_parse.py`, `runtime/spu/tests/run_tests.sh` — saídas
  coladas na secção "Testes unitários" acima.
- (**não-exercitado**) `PS3_TRACE_SNAPSHOT` observado apenas nos pontos de
  checkpoint (por desenho); nenhuma linha de `SNAPSHOT` fora desses
  instantes foi verificada nesta task.

**Sanitização de `baseline.json` (obrigatório, ver secção "Honestidade" do
brief):** o ficheiro bruto gerado pelo harness continha caminhos absolutos
desta máquina em `binario.path`, `env.PS3_MOVIE_CACHE` e `env.PS3_VFS_ROOT`
(prefixo `/Users/andrebrumcortezferreira/Documents/PESSOAL/...`). A cópia
commitada em `docs/bringup/gow2/FASE0/baseline.json` teve esse prefixo
substituído por `../` (caminho relativo ao par de repos irmãos, a mesma
convenção já usada no `CLAUDE.md` para `../gow2-recomp`) — ex.:
`../gow2-recomp/boot_gow2`, `../gow2-recomp/movie_cache`,
`../gow2-recomp/extracted/USRDIR`. Nenhum outro campo continha caminho
absoluto, dado de jogo ou excerto de memória guest (os campos `texto` de
`primeiro_erro` são linhas de instrumentação — endereço/tamanho/região —
não bytes do jogo). Os `run_N.log` brutos (101–145 MB, podem conter volumes
de memória/estado do jogo via as sondas ad-hoc `[vm]`) **não foram
commitados** e permanecem no directório de scratch fora dos dois repos, para
permitir re-derivação independente da medição.

## Decisão

**Aceitar.** O critério de aceite da Fase 0 está cumprido: existe um
`baseline.json` real e medido, com 3 entradas em `runs`, e os três runs
apresentam **a mesma barreira** (`ultimo_checkpoint=FIRST_WAD_OPEN`,
`primeiro_erro` de forma normalizada idêntica) — não foi necessário
preencher `variacao_explicada` porque `igual_nos_runs=true`. A Fase 0 está
**concluída**: o harness é repetível, honesto (métricas medidas, `null`
declarado onde a fonte não existe) e não abriu janela nem exigiu retries
de flake nesta leva.

## Próxima tentativa

Fase 0 está fechada; a próxima tentativa pertence à **Fase 1 — Integridade
da memória guest e do heap** (`docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_
BRINGUP.md`, linha 160), que deve tratar o `primeiro evento divergente`
acima como o seu ponto de partida: investigar a origem do acesso
`ea=0xFB788082` (unmapped, 4 bytes, `func_00263554` e depois
`func_002B0FB4`) na thread MAIN, logo após `FIRST_WAD_OPEN` — antes de
qualquer correcção, confirmar com uma nova instrumentação dedicada (por
exemplo, dump do stack/caller e do valor do ponteiro fonte) se é um
handle/typemap corrompido, um offset de lift errado (auditoria pendente do
padrão de fallthrough cross-fragment já referida no `CLAUDE.md`), ou uma
região genuinamente não mapeada por decisão do allocator guest. Registar
também, como follow-up de menor prioridade: os 5 NIDs não resolvidos
(`0x63FF6FF9`, `0xDB869F20`, `0x4692AB35`, `0xEFEB2679`, `0x4A5EAB63`) e se
`threads_bloqueadas`/`fios_pendentes` persistem como "pendente para sempre"
ou resolvem naturalmente num run sem timeout (não exercitado nesta task).
