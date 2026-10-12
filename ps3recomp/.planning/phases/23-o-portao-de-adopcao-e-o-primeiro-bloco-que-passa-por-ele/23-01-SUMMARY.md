---
phase: 23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele
plan: 01
subsystem: infra
tags: [gate, upstream-adoption, boot-smoke, git-worktree, bash, g6, st620]

requires:
  - phase: 22-a-que-mede-o-que-o-upstream-avancou-e-nos-nao-temos
    provides: "docs/UPSTREAM_DELTA.tsv (inventario do delta + gate de ancoras) e a divida declarada das duas falhas de toolchain do scripts/test_python.sh"
  - phase: 07-gate-da-cadeia-de-boot
    provides: "games/gow2/lib_boot_chain_metrics.sh::extract_st620 e a aritmetica de limiar (RUNS*2+2)/3"
provides:
  - "scripts/upstream_block_gate.sh -- comando unico que mede a intro nas DUAS pontas de um bloco e escreve st620 e rc das duas no MESMO registo"
  - "games/gow2/lift_baseline/INTRO_GATE_BASELINE.tsv -- baseline MEDIDO da intro (st620_min=11 em 6/6) com proveniencia e divida conhecida"
  - "docs/upstream-blocks/README.md -- como se usa o portao, as tres pernas, o G6 com razoes, e o formato obrigatorio do registo por bloco"
  - "As funcoes de veredicto expostas por UPSTREAM_GATE_LIB_ONLY=1 (perna_a/b/c_veredicto, derive_rc_class) para o Plano 23-03 as exercitar sem gastar corridas de boot"
affects: [23-03 prova por mutacao, 23-04 travessia do bloco LV2S, 24 interpretador SPU, 25 pacing do fence GCM]

tech-stack:
  added: []
  patterns:
    - "Portao de adopcao de duas pontas: worktree destacado por leg, refs congelados para SHA imutavel a' entrada, um TSV por bloco com as duas pernas"
    - "Veredicto de N pernas que correm SEMPRE (nunca aborta cedo), com ultima linha de stdout canonica VEREDICTO=... ELO_CAIDO=..."
    - "Baseline congelado por medicao real, nunca por numero citado de memoria (a politica de check_boot_health.py estendida ao portao)"

key-files:
  created:
    - scripts/upstream_block_gate.sh
    - games/gow2/lift_baseline/INTRO_GATE_BASELINE.tsv
    - docs/upstream-blocks/README.md
  modified:
    - .planning/phases/23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele/deferred-items.md
    - .planning/WINDOWS.md

key-decisions:
  - "Os refs dos legs sao resolvidos para SHA imutavel a' entrada do portao: outro plano da mesma wave commita no mesmo ramo e HEAD e' alvo movel entre os dois builds"
  - "O portao exporta SDKROOT quando o operador nao o define -- sem isso NENHUM leg podia ser linkado neste host (o clang das Command Line Tools nao traz sysroot)"
  - "Um baseline com st620_min=0 e' recusado: um limiar que nada pode falhar nao e' um portao"
  - "A perna C nao afirma nada sobre sha256 quando o bloco e' vazio ou so' de documentacao -- o link de 122 MB NAO e' reproduzivel byte a byte (medido: dois legs do mesmo commit deram sha256 diferentes)"
  - "A verificacao 'git worktree list == 1 linha' foi substituida por 'zero worktrees do portao': a maquina tem 4 worktrees de agente pre-existentes que nao sao nossos para remover"

patterns-established:
  - "Estado de funcao por variavel global em vez de stdout quando a funcao regista limpeza: `wt=$(materializar_motor ...)` correria num subshell e o array de limpeza morreria com ele"
  - "Deteccao de cache invalida por classe de ficheiro: um cabecalho alterado sob include/ ou runtime/ppu/ forca FORCE_REBUILD_LIFT=1 nos dois legs, porque nenhum mtime denuncia essa staleness"

requirements-completed: []   # NAO completo de proposito -- ver requirements-partial
requirements-partial:
  - id: UPST-02
    entregue: "o portao existe, mede as duas pontas no mesmo registo, recusa correr sem baseline e recusa passar quando nao mediu nada; baseline congelado por corrida real"
    em-falta: "a prova por MUTACAO -- o criterio de aceite do UPST-02 exige que o gate REJEITE uma regressao injectada, nao so' que passe numa corrida verde. Isso e' o Plano 23-03"

coverage:
  - id: D1
    description: "Comando unico que mede a intro nas duas pontas de um bloco e escreve st620 e rc das duas no mesmo TSV"
    requirement: "UPST-02"
    verification:
      - kind: e2e
        ref: "scripts/upstream_block_gate.sh --bloco NULO --antes HEAD --depois HEAD --runs 3 --out-dir $TMPDIR/gate_nulo  (rc=0, TSV com linhas antes+depois)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Baseline congelado por corrida REAL, com proveniencia e a divida conhecida do test_python.sh declarada"
    requirement: "UPST-02"
    verification:
      - kind: e2e
        ref: "scripts/upstream_block_gate.sh --congelar-baseline --ref HEAD --runs 6  (st620=11 em 6/6; 6 chaves de proveniencia + 4 chaves de dados)"
        status: pass
    human_judgment: false
  - id: D3
    description: "O portao recusa correr sem baseline (rc=2) e recusa passar quando os binarios sao identicos num bloco nao-vazio"
    requirement: "UPST-02"
    verification:
      - kind: integration
        ref: "baseline movido -> rc=2; perna_c_veredicto 'runtime/syscalls/lv2_register.c' aa aa -> FALHA"
        status: pass
    human_judgment: false
  - id: D4
    description: "G6 verificavel no proprio script: PS3_NO_RSX=1 + PS3_RSX_BACKEND=trace depois do source, PS3_ENGINE_ROOT por leg, morte por PID, token do matador proibido ausente"
    requirement: "UPST-02"
    verification:
      - kind: automated_ui
        ref: "grep do token proibido = 0; kill -TERM >= 1 e kill -9 >= 1 fora de comentario; logs de boot mostram os dois legs a linkar de PS3_ENGINE_ROOT diferentes"
        status: pass
    human_judgment: false
  - id: D5
    description: "README do portao com as tres pernas, o G6 com razoes, o que o portao NAO prova, e o formato obrigatorio do registo por bloco"
    requirement: "UPST-02"
    verification:
      - kind: other
        ref: "grep das seccoes obrigatorias e dos 7 termos de conteudo (upstream_block_gate.sh, PS3_RSX_BACKEND=trace, PS3_ENGINE_ROOT, exit 124, offline-unit, in-boot, fix-anchors)"
        status: pass
    human_judgment: false

duration: 52min
completed: 2026-08-12
status: complete
---

# Phase 23 Plano 01: O portão de adopção Summary

**Um comando único constrói os dois motores de um bloco em worktrees destacados, linka um `boot_gow2` por leg com `PS3_ENGINE_ROOT` próprio, corre a intro sob G6 matando por PID, e escreve `st620` + `rc` das duas pontas no mesmo TSV — com o baseline congelado por medição real (`st620=11` em 6/6) e um veredicto de três pernas cuja última linha é sempre `VEREDICTO=... ELO_CAIDO=...`.**

## Performance

- **Duração:** ~52 min (dos quais ~8 min de medição real: 3m21s a congelar o baseline + 4m11s no bloco nulo)
- **Tasks:** 3/3
- **Ficheiros criados:** 3 · **modificados:** 2 (ledgers de planeamento)

## Accomplishments

- **O portão existe e mede.** `scripts/upstream_block_gate.sh` faz o ciclo completo por leg: `git worktree add --detach` → `cmake` do motor → `build_macos.sh` com `PS3_ENGINE_ROOT` explícito → N corridas da intro → TSV. Os dois legs correm sequencialmente (medido: os objectos de host vivem no `$LIFT` **partilhado**, e em paralelo um leg linkaria objectos do motor do outro).
- **O baseline é medido, não citado.** `INTRO_GATE_BASELINE.tsv` foi congelado por 6 corridas reais: `st620 = 11,11,11,11,11,11`, `rc = 143` em todas, classificadas `timeout-normal`. A proveniência (`medido_em`, `ref` + SHA, `binario_sha256`, `runs`, `timeout_s`, `recipe`) fica no ficheiro, com a nota explícita de que `st620=11` da recipe de **produção** com Metal e o limiar `>=3` do gate antigo são de **outras** recipes.
- **O veredicto tem três pernas e nunca aborta cedo.** PERNA A (intro contra `st620_min`, limiar `(RUNS*2+2)/3`, mais `ok(depois) >= ok(antes)` e sem saídas anormais novas); PERNA B (suite Python por leg, com `known_debt_python` subtraída); PERNA C (bloco não-vazio com binários de `sha256` idêntico reprova — o portão não mediu nada).
- **O bloco nulo passou com as duas pontas no mesmo registo.** `rc=0`, `VEREDICTO=PASSOU ELO_CAIDO=nenhum`, `bloco_vazio=sim`, 6 linhas de TSV (3 `antes` + 3 `depois`), zero worktrees do portão pendurados.
- **As funções de veredicto são testáveis sem gastar um boot.** `UPSTREAM_GATE_LIB_ONLY=1` define `perna_a_veredicto`, `perna_b_veredicto`, `perna_c_veredicto` e `derive_rc_class` e devolve o controlo sem construir nem arrancar nada — o Plano 23-03 exercita-as com entradas sintéticas.

## Task Commits

1. **Task 1 (tracer): uma perna do portão ponta-a-ponta + baseline congelado** — `61e2cbb` (feat)
2. **Task 2: as duas pontas no mesmo registo e o veredicto de três pernas** — `75da899` (feat)
3. **Task 3: o README do portão** — `e8ce402` (docs)

## Ficheiros criados/modificados

- `scripts/upstream_block_gate.sh` — o portão. Worktree por leg, build, link, corridas G6, TSV, resumo, veredicto de três pernas.
- `games/gow2/lift_baseline/INTRO_GATE_BASELINE.tsv` — baseline congelado (`st620_min=11`, `st620_mediana=11`, `ok_runs_min=6`, `runs=6`, `timeout_s=25`, `known_debt_python`) + proveniência.
- `docs/upstream-blocks/README.md` — os três modos, as três pernas, o G6 com razões, `## O que o portao NAO prova`, `## Registo por bloco`, e a obrigação de re-ancorar o `UPSTREAM_DELTA.tsv`.
- `.planning/phases/23-*/deferred-items.md`, `.planning/WINDOWS.md` — os achados abaixo.

## Nível de evidência (disciplina do projecto)

| Afirmação | Evidência |
|---|---|
| O portão constrói os dois motores, linka dois binários distintos e corre a intro nas duas pontas | **in-boot** — bloco nulo, 6 corridas, 2 binários com `sha256` diferente |
| A intro não regride sob a recipe G6 no commit medido | **in-boot** — `st620=11` em 6/6 no baseline, 5/6 no bloco nulo (uma corrida a 1, ver achado 2) |
| O portão recusa correr sem baseline (`rc=2`) | **offline-unit** — baseline movido, `rc=2` observado |
| A dívida conhecida do `test_python.sh` nunca é lida como regressão | **offline-unit** — `perna_b_veredicto "" "b.py" "a.py,b.py"` → PASSA. **NÃO foi exercitado in-boot**: com `SDKROOT` exportado pelo portão as duas falhas deixaram de ocorrer (ver achado 1) |
| `--depois-arvore` (pré-commit) e `--antes-bin` | **nao-exercitado** — só a validação de argumentos foi corrida |
| Que o caminho alterado por um bloco foi exercitado | **nao-exercitado, por construção** — o portão nunca prova isto, e o README di-lo explicitamente |

## Decisões tomadas

1. **Refs congelados para SHA imutável à entrada.** Medido em execução: o Plano 23-02 commita no **mesmo ramo** enquanto o portão corre — `HEAD` mudou de `16d7433` para `ebc7a49` entre a corrida do baseline e a do bloco nulo. Com `HEAD` resolvido só no `worktree add`, os dois legs podiam apanhar commits diferentes sem nada o denunciar. O rótulo original fica gravado ao lado do SHA.
2. **`OUT_DIR` sempre fora dos dois repositórios**, com verificação activa (`rc=2` se o operador apontar para dentro de um deles).
3. **Estado por variável global em `materializar_motor`/`linkar_binario`.** `wt=$(materializar_motor ...)` correria a função num subshell e o `WORKTREES+=(...)` morreria com ele — o `trap` nunca veria o worktree e ele ficaria pendurado. Bug apanhado antes da primeira corrida.
4. **A perna C não afirma nada sobre `sha256` em blocos vazios.** Confirmado por medição: dois legs do **mesmo** commit produziram `deb83446...` e `5e46e948...`. O link de 122 MB não é reproduzível byte a byte, e exigi-lo faria o portão reprovar blocos válidos.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] `SDKROOT` ausente: nenhum leg podia ser construído**
- **Encontrado durante:** Task 1 (primeira corrida real do `--congelar-baseline`)
- **Problema:** `build_macos.sh` morreu em `include/ps3emu/endian.h:12:10: fatal error: 'string.h' file not found` ao compilar `ppu_loader.cpp`. Reproduzido isoladamente: `clang -c` num ficheiro com `#include <string.h>` falha neste host; `xcode-select -p` = `/Library/Developer/CommandLineTools` e o `clang` não traz sysroot por default. O `cmake` não sofria porque passa `-isysroot` explícito; o `build_macos.sh` invoca `clang++` directamente.
- **Fix:** o portão exporta `SDKROOT="$(xcrun --show-sdk-path)"` **apenas quando o operador não o definiu** — uma escolha explícita de quem corre o portão nunca é sobreposta. O valor usado é gravado no baseline como `# ambiente do host: SDKROOT=...`.
- **Verificação:** o leg passou a linkar; `boot_gow2_baseline` produzido e corrido 6 vezes.
- **Commit:** `61e2cbb`

**2. [Rule 1 — Bug] `HEAD` é alvo móvel com outro plano a commitar no mesmo ramo**
- **Encontrado durante:** Task 1 (o `worktree add` do baseline aterrou em `16d7433`, um commit do Plano 23-02 feito durante a minha execução)
- **Problema:** com os refs resolvidos só no momento do `worktree add`, e minutos de build entre os dois legs, `--antes HEAD --depois HEAD` podia produzir dois legs em commits **diferentes** — e o "bloco nulo" deixaria de o ser, silenciosamente.
- **Fix:** `congelar_ref()` resolve cada ref com `git rev-parse --verify "<ref>^{commit}"` antes de qualquer trabalho longo; o resto do portão só usa SHAs. `rc=2` se um ref não resolver.
- **Verificação:** no bloco nulo os dois legs registaram o mesmo `ebc7a494...` no TSV.
- **Commit:** `61e2cbb`

**3. [Rule 2 — Correctness] Recusa de congelar um baseline com `st620_min=0`**
- **Encontrado durante:** Task 1 (revisão do próprio critério antes da primeira corrida)
- **Problema:** o plano manda gravar o **mínimo** observado. Se uma corrida não produzisse `st620` nenhum (falha estrutural do boot), o mínimo seria 0 — e um limiar de 0 é um limiar que **nenhuma** corrida pode falhar. Um gate que nasce verde para tudo não é um gate.
- **Fix:** `st620_min <= 0` aborta com `rc=1`, imprime os `st620`/`rc` por corrida e o caminho dos logs, e **não escreve** o baseline.
- **Verificação:** caminho não disparado na corrida real (`st620_min=11`); a condição é lida directamente no código.
- **Commit:** `61e2cbb`

### Verificação adaptada (não é auto-fix, é um facto do ambiente)

**`git worktree list | wc -l == 1`** (verify 4 da Task 1 e verify 5 da Task 2) **não é executável como escrita nesta máquina**: existem 4 worktrees de agente pré-existentes (`.claude/worktrees/agent-*`) criados por outras sessões, que não são meus para remover. A afirmação mensurável equivalente — **zero worktrees do portão pendurados** — foi verificada e passa nas duas tasks:

```
git worktree list | grep -c 'upstream_gate\|gate_nulo'   ->  0
```

O `trap ... EXIT` com `worktree remove --force` + `worktree prune` está no script e funcionou nas duas corridas reais (incluindo na corrida que **falhou** a meio, no primeiro `--congelar-baseline`).

---

**Total de desvios:** 3 auto-corrigidos (1× Rule 1, 1× Rule 2, 1× Rule 3) + 1 verificação adaptada por facto do ambiente.
**Impacto no plano:** nenhum alargamento de âmbito. Os três desvios são pré-condições de correcção do próprio portão — sem o (1) o portão não construía, sem o (2) não media o que dizia medir, sem o (3) podia nascer verde para tudo.

## Issues Encountered

**1. A dívida de toolchain do `test_python.sh` desapareceu — e isso é um achado, não uma vitória a reclamar.**
Com `SDKROOT` exportado pelo portão, os **dois** legs deram `PASS 16 / SKIP 3 / FAIL 0`. As duas falhas que a Fase 22 registou (`tools/test_ppu_lift.py`, `runtime/ppu/tests/test_ppu_lifter_emit.py`) já não ocorrem — corroborando o diagnóstico que a Fase 22 marcou como `nao-exercitado` ("ambiente de toolchain, não código do repo"). Consequência honesta: **a subtracção da dívida na PERNA B não foi exercitada in-boot**, só por unidade. A linha `known_debt_python` fica no baseline como rede para quem corra o portão sem `SDKROOT`. Fixar o `SDKROOT` na própria suite fica deferido (fora da fronteira desta fase).

**2. O leg medido em primeiro lugar absorve a penalização de cache fria — e o enviesamento é no sentido de PASSAR.**
Medido no bloco nulo, com os dois legs no **mesmo** commit: `antes = 1,11,11` e `depois = 11,11,11`. Como o leg `antes` corre sempre primeiro, é sempre ele a apanhar a penalização, o que empurra `ok(antes)` para baixo e torna `ok(depois) >= ok(antes)` **mais fácil** de satisfazer. Não corrigido nesta fase: intercalar as corridas obriga a re-linkar entre elas (objectos de host no `$LIFT` partilhado) e multiplica o custo. Mitigações hoje: `--runs 6` em blocos importantes, e o TSV corrida-a-corrida que o portão já grava. Registado em `deferred-items.md` e em `WINDOWS.md` (#24).

**3. Ruído de job control no stderr.** Cada corrida imprime `Terminated: 15` com a linha de comando do subshell. É a notificação do shell sobre o `kill -TERM` que **nós** mandámos, não um erro. Não suprimido de propósito: esconder o sinal esconderia a prova de que a morte por PID aconteceu.

## User Setup Required

Nenhum — não há configuração de serviço externo. O portão exige apenas que o checkout físico do jogo (`../gow2-recomp`) exista com `EBOOT.ELF`, `env_gow2.sh`, `build_macos.sh`, `lib_boot_chain_metrics.sh` e o lift `recomp_macos_v2`; caso contrário sai `rc=2` a nomear o caminho em falta.

## Known Stubs

Nenhum stub. Três superfícies **implementadas e não exercitadas**, declaradas em `deferred-items.md` e em `WINDOWS.md` (#23): `--depois-arvore` (modo pré-commit, fica para o Plano 23-04), `--antes-bin` (reutilização de binário), e o sentido positivo da detecção de `FORCE_REBUILD_LIFT=1` (espera um bloco que altere um `.h` sob `include/` ou `runtime/ppu/`).

## Threat Flags

Nenhuma superfície de segurança nova para além do registo do plano. `T-23-01` (injecção por argumentos) mitigado: rótulo validado contra `^[A-Za-z0-9_.-]+$` antes de compor nomes de ficheiro, todas as expansões citadas, zero `eval` no script. `T-23-02` (worktrees/processos vivos) mitigado e **provado numa falha real**. `T-23-03` (logs) mitigado com verificação activa de que `OUT_DIR` não cai dentro de nenhum dos dois repositórios. `T-23-04` (proveniência) e `T-23-05` (leg medir o motor errado) mitigados por `PS3_ENGINE_ROOT` explícito + PERNA C + `sha256` no registo.

## Self-Check: PASSED

Ficheiros afirmados: os 5 existem em disco. Commits afirmados: `61e2cbb`, `75da899`, `e8ce402` existem no histórico. Números re-lidos das fontes (não da memória): `st620_min=11` no baseline; TSV do bloco nulo `antes = 1 11 11`, `depois = 11 11 11`; `sha256` dos dois binários distintos (`deb83446…` / `5e46e948…`).
