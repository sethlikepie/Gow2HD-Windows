---
phase: 23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele
plan: 04
subsystem: infra
tags: [gate, upstream-adoption, block-record, honesty, mutation-proof, ctest, st620]

requires:
  - phase: 23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele
    plan: 01
    provides: "scripts/upstream_block_gate.sh, o baseline congelado (st620_min=11) e o modo --depois-arvore"
  - phase: 23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele
    plan: 02
    provides: "o fix do sys_spu_image_import (+0x10 u32), o teste offline e a re-ancoragem do UPSTREAM_DELTA.tsv"
  - phase: 23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele
    plan: 03
    provides: "a prova de que o portao sabe REPROVAR e nomeia o elo certo"
provides:
  - "docs/upstream-blocks/LV2S-01.md -- o primeiro registo de bloco atravessado do principio ao fim, com as doze corridas medidas e as duas seccoes de honestidade separadas"
  - "tools/check_block_record.py -- as 5 regras de formato do registo de bloco impostas por comando (rc=0/1), com seleccao explicita de registos"
  - "tools/test_check_block_record.py -- 9 mutacoes, controlo positivo, casos de seleccao e a corrida sobre a pasta real"
  - "py.test_check_block_record no CTest + entrada no scripts/test_python.sh -- o criterio 4 passa a ser gate, nao promessa"
  - "A medicao de que --depois-arvore funciona in-boot sobre um bloco real"
affects: [24 interpretador SPU, 25 pacing do fence GCM]

tech-stack:
  added: []
  patterns:
    - "Registo de bloco como artefacto verificavel: o documento de honestidade tem um parser que o reprova quando as duas afirmacoes se fundem"
    - "Seleccao explicita de artefactos num directorio (nome + chave de cabecalho) para um gate documental nao reprovar a documentacao que o rodeia"
    - "Ref de um leg derivado do historico do proprio ficheiro ancorado (`git log -1 -- <ficheiro>`^), nunca escolhido a olho"

key-files:
  created:
    - docs/upstream-blocks/LV2S-01.md
    - tools/check_block_record.py
    - tools/test_check_block_record.py
  modified:
    - scripts/test_python.sh
    - CMakeLists.txt
    - docs/upstream-blocks/README.md
    - .planning/phases/23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele/deferred-items.md

key-decisions:
  - "--runs 6 em vez do --runs 3 do plano: e' o que a alinea (d) do proprio README manda para blocos importantes e a mitigacao registada no WINDOWS.md #24. Justificou-se -- ok_depois=4 == limiar=4"
  - "As doze corridas foram registadas como sairam, incluindo as quatro fracas; nenhuma repetida nem descartada"
  - "O docs/UPSTREAM_DELTA.tsv NAO foi tocado: o estado de adopcao vive em docs/upstream-blocks/, o inventario nao o simula (T-23-19)"
  - "Ordem dos commits trocada face ao plano (registo completo antes do verificador) para que NENHUM commit contenha uma suite vermelha"
  - "Regras extra no verificador (SECCAO_VAZIA, SEM_REGISTOS): sem elas um registo com a seccao 'NAO prova' vazia, ou uma pasta onde nada bate a seleccao, passariam o formato sem dizer nada"

requirements-completed:
  - UPST-02
  - LV2S-01
requirements-partial: []

coverage:
  - id: D1
    description: "O bloco LV2S atravessa o portao com as duas pontas medidas no mesmo registo"
    requirement: "UPST-02"
    verification:
      - kind: e2e
        ref: "scripts/upstream_block_gate.sh --bloco LV2S-01 --antes 24ad9d6 --depois-arvore $PWD --runs 6 -> rc=0, VEREDICTO=PASSOU ELO_CAIDO=nenhum; antes=0,0,11,11,11,1 depois=1,11,11,11,1,11; 12 linhas no TSV"
        status: pass
    human_judgment: false
  - id: D2
    description: "PERNA C nao saltada: os dois binarios sao distintos"
    requirement: "UPST-02"
    verification:
      - kind: e2e
        ref: "antes 18e9d30e4ff1bcfb84e0215ca1e2ece018f1c910a943b124bfd59cc665ae1b35 vs depois cd366278b2fa3aa82b9734edfaa7a209f816c89d68e55a7a649a271cf38fec2a; perna_c=PASSA ficheiros=48 compilados=25"
        status: pass
    human_judgment: false
  - id: D3
    description: "As duas afirmacoes ficam em seccoes separadas e a separacao e' verificavel por comando"
    requirement: "LV2S-01"
    verification:
      - kind: unit
        ref: "tools/check_block_record.py docs/upstream-blocks/ -> rc=0 (registos=1 violacoes=0); a MESMA copia com as seccoes fundidas -> rc=1 com 2x SECCAO_EM_FALTA"
        status: pass
    human_judgment: false
  - id: D4
    description: "Cada regra do verificador reprovada por mutacao, com controlo positivo"
    requirement: "LV2S-01"
    verification:
      - kind: unit
        ref: "tools/test_check_block_record.py -> rc=0, 'todos os casos passaram'; 9 mutacoes + controlo positivo + 3 casos de seleccao + corrida sobre a pasta real + invariante de nao-escrita por sha256"
        status: pass
    human_judgment: false
  - id: D5
    description: "grep -r image_import games/gow2/ = zero, medido e escrito no registo"
    requirement: "LV2S-01"
    verification:
      - kind: unit
        ref: "contagem literal 0 (rc=1 do grep) + controlo negativo do padrao (encontra runtime/syscalls/lv2_syscall_table.h); escrito em docs/upstream-blocks/LV2S-01.md"
        status: pass
    human_judgment: false
  - id: D6
    description: "A CI da Fase 22 continua verde e a decisao do inventario preservada"
    requirement: "UPST-02"
    verification:
      - kind: integration
        ref: "upstream_delta.py validate rc=0 (10 linhas); test_upstream_delta.py rc=0; LV2S decisao=adoptar; test_python.sh 20 testes PASS 18/SKIP 2/FAIL 0; test_runtime.sh 100% tests passed out of 30 (py.test_check_block_record #3 Passed)"
        status: pass
    human_judgment: false

duration: ~75min
completed: 2026-08-12
status: complete
---

# Phase 23 Plano 04: A travessia do bloco LV2S Summary

**O bloco `LV2S` atravessou o portão do princípio ao fim em modo árvore-viva e saiu `VEREDICTO=PASSOU ELO_CAIDO=nenhum` — e o registo que ficou não usa esse verde para dizer mais do que ele diz: numa secção está a não-regressão da intro (`in-boot`), noutra está que `grep -r image_import games/gow2/` continua a dar zero e que por isso **não existe aceite `in-boot` para o `LV2S-01`** — separação que agora é imposta por `tools/check_block_record.py`, provado a reprovar a própria cópia deste registo com as duas secções fundidas.**

## Performance

- **Duração:** ~75 min (dos quais ~28 min de medição real: 1 corrida de portão com 2 legs e 12 boots de 25 s, mais as duas suites completas no fecho)
- **Tasks:** 3/3
- **Ficheiros criados:** 3 · **modificados:** 4

## Accomplishments

- **O bloco atravessou o portão, com `ref` derivado e não escolhido.** `ANTES = $(git log --format=%H -1 -- runtime/syscalls/lv2_register.c)^` = `24ad9d6`, confirmado por `git diff --name-only 24ad9d6..HEAD` conter o ficheiro antes de qualquer build. `DEPOIS` = árvore viva em `64aad9e`.
- **`--depois-arvore` deixou de ser `nao-exercitado`.** O uso pré-commit que o `UPST-02` pede correu sobre um bloco real: `arvore_suja=sim`, as 31 entradas listadas pelo portão, e a medição declarada como pré-commit e nunca como medição de um commit limpo.
- **As doze corridas estão no registo tal como saíram.** `antes = 0,0,11,11,11,1`, `depois = 1,11,11,11,1,11`, `rc = 143` nas doze (`timeout-normal`, zero `saida-anormal`). Nenhuma repetida, nenhuma descartada.
- **As três pernas passaram e estão no mesmo relatório.** `perna_a=PASSA limiar=4 ok_antes=3 ok_depois=4`, `perna_b=PASSA novos=nenhum`, `perna_c=PASSA ficheiros=48 compilados=25 binarios=distintos`.
- **O critério 4 deixou de ser prosa.** `tools/check_block_record.py` impõe 5 regras (as duas secções separadas e não-vazias; marca `[evidencia: X]` por afirmação; uma marca por afirmação; nada `in-boot` no que não se prova; cabeçalho completo), lista todas as violações com ficheiro e linha, e corre sobre ficheiro ou directório.
- **Provado a reprovar, não só a aprovar.** Nove mutações sintéticas, cada uma a nomear o seu check, mais o teste decisivo sobre o artefacto real: uma **cópia** do `LV2S-01.md` com as duas secções fundidas num título só sai `rc=1` com `SECCAO_EM_FALTA` duas vezes, enquanto o original sai `rc=0`.
- **Ligado à CI nas duas listas espelho.** `scripts/test_python.sh` (20 testes) e `foreach(_ps3_pytest)` do `CMakeLists.txt` → `py.test_check_block_record` corre no CTest (`3/30 ... Passed 0.03 sec`).
- **A honestidade tem controlo negativo.** `grep -r image_import games/gow2/` dá `0` (rc=1 do grep), e o mesmo padrão **encontra** `runtime/syscalls/lv2_syscall_table.h` e os dois ficheiros de teste — o zero é do alvo, não de um comando partido.

## Task Commits

1. **Task 1: o bloco atravessa o portão, as duas pontas medidas** — `9d57713` (feat)
2. **Task 3 (conteúdo do registo): as duas afirmações em separado** — `0d4f889` (docs)
3. **Task 2: o verificador + o teste por mutação + a CI** — `68b9969` (test)
4. **Ledger + SUMMARY da fase** — este commit (docs)

## Ficheiros criados/modificados

- `docs/upstream-blocks/LV2S-01.md` — comando literal, tabela por leg (ref, `sha256`, `arvore_suja`), tabela por corrida (`st620`, `rc`, `rc_class`), veredicto de três pernas, a linha `VEREDICTO=...` verbatim, uma secção **"o que esta corrida tem de frágil"**, as duas secções de honestidade e a janela de medição.
- `tools/check_block_record.py` — 5 regras + selecção explícita de registos (`<BLOCO>-<NN>.md` ou `bloco:` no cabeçalho), com `README.md` e `GATE-MUTATION-PROOF.md` fora por construção.
- `tools/test_check_block_record.py` — controlo positivo, 9 mutações, 3 casos de selecção, a pasta real e o invariante de não-escrita por `sha256`.
- `scripts/test_python.sh` / `CMakeLists.txt` — uma linha em cada (listas espelho).
- `docs/upstream-blocks/README.md` — template alinhado com o formato agora imposto (ver Desvio 2).
- `.planning/phases/23-*/deferred-items.md` — três achados novos.

## Nível de evidência (disciplina do projecto)

| Afirmação | Evidência |
|---|---|
| A intro não regrediu entre as duas pontas do bloco `LV2S` | **in-boot** — 12 corridas, `ok_antes=3` / `ok_depois=4`, limiar 4, zero saídas anormais |
| O portão mediu mesmo este bloco (não comparou binários iguais) | **in-boot** — `sha256` distintos, `perna_c=PASSA ... binarios=distintos` |
| `--depois-arvore` funciona sobre um bloco real | **in-boot** — leg `depois` construído da árvore viva, `arvore_suja=sim` |
| `sys_spu_image_import` escreve o EA como u32 em `+0x10` | **offline-unit** — teste do Plano 23-02 e a sua prova por mutação (não re-medido aqui) |
| A suite Python não ganhou FAIL novo | **offline-unit** — `FAIL 0` nos dois legs; `perna_b=PASSA novos=nenhum` |
| O verificador reprova um registo que junte as duas afirmações | **offline-unit** — cópia do registo real fundida → `rc=1`, `SECCAO_EM_FALTA` ×2; original → `rc=0` |
| O caminho corrigido do `image_import` foi exercitado em boot | **nao-exercitado** — `grep` = 0, com controlo negativo do padrão; nenhum aceite `in-boot` reclamado |
| O portão sabe reprovar um bloco **real** | **nao-exercitado** — só foi visto a reprovar injecção sintética (23-03); ver ledger |

## Decisões tomadas

1. **`--runs 6`.** A alínea (d) do `README.md` manda 6 para blocos importantes e o `WINDOWS.md` #24 regista `--runs 6` como mitigação do viés de cache fria. Justificou-se: `ok_depois` deu exactamente 4, o limiar de `--runs 6`. Com `--runs 3` (limiar 2) o resultado teria dependido de que três corridas calhassem.
2. **Nenhuma corrida repetida.** As quatro corridas fracas (duas por leg) estão na tabela. A regra de ouro do marco proíbe subir `--runs` até a estatística ceder; aqui nem sequer foi preciso — a primeira corrida passou.
3. **O `UPSTREAM_DELTA.tsv` não foi tocado.** `decisao=adoptar` e `evidencia=medido` são anteriores a esta leva; o estado de adopção vive em `docs/upstream-blocks/`. `validate` sai `rc=0` sem re-ancoragem (o bloco não mexeu em nenhuma linha ancorada nesta leva).
4. **Duas regras além das cinco do plano** (`SECCAO_VAZIA`, `SEM_REGISTOS`): uma secção "NAO prova" vazia satisfaria o formato sem dizer nada, e um verificador que não vê nenhum registo numa pasta não é um gate. Ambas provadas por mutação.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] `depends_on: [23-03]` estava por cumprir quando o plano foi escrito**
- **Problema:** o `<context>` do plano referencia `23-03-SUMMARY.md`, que não existia à data do recon.
- **Estado real na execução:** o Plano 23-03 correu antes (commits `d1c392a`, `630b746`, `a142d9b`, `64aad9e`) e o `GATE-MUTATION-PROOF.md` já existia — o que **obrigou** a regra de selecção de registos do verificador a excluí-lo explicitamente, com caso de teste próprio.
- **Verificação:** `[bloco] resumo registos=1 ... (LV2S-01.md)`.

**2. [Rule 3 — Referência partida] O template do `README.md` contradizia o formato que o plano manda impor**
- **Problema:** `docs/upstream-blocks/README.md` documentava `evidencia:` em linha própria e um cabeçalho sem `bloco`/`fase`/`data`. Quem seguisse o README escreveria um registo que o novo verificador reprova.
- **Fix:** template alinhado com o formato imposto (marca `[evidencia: X]` inline, cabeçalho completo) e as regras reescritas a apontar para o comando. O parser aceita o cabeçalho **com e sem** o `- ` inicial (caso de teste dedicado), para não invalidar o estilo de lista do README.
- **Ficheiro fora do `files_modified`** do plano — desvio declarado.
- **Commit:** `68b9969`

**3. [Rule 2 — Ordem] Commits trocados face à ordem das tasks**
- **Problema:** o teste do verificador exige que `docs/upstream-blocks/` real valide. Committar a Task 2 antes da Task 3 deixaria um commit com a suite Python **vermelha** no histórico.
- **Fix:** ordem `Task 1 → conteúdo da Task 3 → Task 2`. Cada commit é atómico no conteúdo e **nenhum** contém uma suite vermelha. O plano previa esta sequência ("correr as duas tasks em sequência e só commitar esta depois de a Task 3 fechar").

### Verificações adaptadas (factos do ambiente, não auto-fixes)

- **`git worktree list | wc -l == 1`** (Task 1, verify 4) não é executável nesta máquina: existem 4 worktrees de agente pré-existentes sob `.claude/worktrees/`, de outras sessões. Equivalente medido e verificado: **`git worktree list | grep -c 'gate_lv2s\|upstream_gate'` → 0**.
- **Task 2, verify 2** tinha `set -e` seguido de `... > out; RC=$?` — com `set -e` o shell aborta no comando que **tem** de falhar, antes de atribuir `RC`. Corrido com `|| RC=$?`.
- **Task 3, verify 1** fazia `grep -q "$N"` com `N=0`, o que casa com qualquer `0` do documento (a data, `+0x10`, um sha) e é praticamente vácuo. Corrido com `grep -qE 'image_import[^\n]*(zero|= *0|-> *0)'`, que exige a frase.
- **Task 3, verify 3** correria o verificador sobre a pasta inteira: sem regra de selecção, o `README.md` e o `GATE-MUTATION-PROOF.md` seriam lidos como registos e a task nunca fecharia. Regra de selecção implementada e provada por teste.

---

**Total de desvios:** 3 auto-corrigidos (2× Rule 3, 1× Rule 2) + 4 verificações adaptadas.
**Impacto no plano:** nenhum alargamento de âmbito; nenhuma alteração ao `scripts/upstream_block_gate.sh`; nenhum ficheiro de outra sessão tocado; nenhum push.

## Issues Encountered

1. **O leg `antes` deu `st620=0` nas duas primeiras corridas** — investigado antes de aceitar qualquer veredicto: os logs mostram o boot vivo e a FSM a arrancar só no fim da janela de 25 s (penalização de cache fria depois do `worktree add` + build), e `grep -c 'image_import\|spu_image'` nos três logs do leg dá **0** — a syscall não é chamada, logo a diferença não podia vir do bloco. A corrida 3 do mesmo leg deu `11`.
2. **`ok_depois == limiar`** — passou por um. Registado no `deferred-items.md` como segundo ponto de medição a favor de decidir o item 1 do 23-03 (aquecimento por leg ou `--runs 6` por default).
3. **A dívida `known_debt_python` não se reproduziu no fecho** (`FAIL 0` no `test_python.sh` com `SDKROOT` vazio; `100% tests passed` no `test_runtime.sh`). Não é prova de correcção — nada neste plano lhe tocou. Registado no ledger como dívida **condicional ao ambiente**.

## Next Steps

- **Fase 23 fechada** nos seus quatro critérios: o portão existe (23-01), o bloco existe e está provado offline (23-02), o portão sabe reprovar (23-03) e um bloco real atravessou-o com a honestidade separada e verificável (23-04).
- Para as Fases 24 e 25: correr o portão em `--depois-arvore` **antes** de commitar o bloco, e re-ancorar o `UPSTREAM_DELTA.tsv` antes de o correr (item 2 do ledger do 23-03).
- Decidir o item 1 do ledger (aquecimento por leg vs `--runs 6` por default) **antes** do primeiro bloco de risco da Fase 25 — um falso vermelho num bloco grande custa uma corrida inteira.
