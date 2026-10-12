---
phase: 23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele
plan: 03
subsystem: infra
tags: [gate, upstream-adoption, mutation-proof, ctest, g6, st620, git-worktree, bash]

requires:
  - phase: 23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele
    plan: 01
    provides: "scripts/upstream_block_gate.sh, o baseline congelado (st620_min=11) e as funcoes puras expostas por UPSTREAM_GATE_LIB_ONLY=1"
  - phase: 22-a-que-mede-o-que-o-upstream-avancou-e-nos-nao-temos
    provides: "docs/UPSTREAM_DELTA.tsv (ancoras) e a divida declarada do scripts/test_python.sh"
provides:
  - "scripts/test_upstream_block_gate.sh -- 46 verificacoes em 0,1 s; as regras G6 passam a ser guarda permanente da CI, e as tres pernas tem prova nos dois sentidos por vectores"
  - "add_test(test_upstream_block_gate) no CTest, com SKIP_REGULAR_EXPRESSION -- corre em qualquer maquina; salta limpo sem o checkout do jogo"
  - "docs/upstream-blocks/GATE-MUTATION-PROOF.md -- o registo MEDIDO dos dois sentidos, com a producao verificada intacta por sha256"
  - "A medicao de que a PERNA A tem um modo de falso vermelho sob carga da maquina (com o experimento discriminador que o prova)"
affects: [23-04 travessia do bloco LV2S, 24 interpretador SPU, 25 pacing do fence GCM]

tech-stack:
  added: []
  patterns:
    - "Auto-teste de um script de medicao por vectores sinteticos: o script exporta as funcoes puras sob uma env var de modo-biblioteca e o teste sourceia-o, sem gastar corridas caras"
    - "Prova por mutacao em dois niveis: do auto-teste (trocar trace por metal no portao deixa o teste vermelho) e do proprio portao (regressao comportamental numa copia descartavel)"
    - "Token proibido construido em runtime por concatenacao para que o ficheiro de teste nao seja a sua propria contra-prova"

key-files:
  created:
    - scripts/test_upstream_block_gate.sh
    - docs/upstream-blocks/GATE-MUTATION-PROOF.md
  modified:
    - CMakeLists.txt
    - .planning/phases/23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele/deferred-items.md

key-decisions:
  - "A linha de salto do Grupo 6 so' e' impressa quando nao ha nenhum FAIL: o SKIP_REGULAR_EXPRESSION do CTest e' desancorado e mascararia um vermelho dos Grupos 1 a 5"
  - "O vector de FRONTEIRA do plano (antes=[3,3,3] depois=[3,3,0]) foi codificado como REPROVA, nao PASSA -- a regra escrita no portao exige ok(depois)>=ok(antes) alem do limiar"
  - "A injeccao final foi o candidato B (ppu_loader.cpp): o candidato A (sys_tty_write) foi REFUTADO por medicao -- a syscall 403 nao e' chamada nesta intro"
  - "O sentido mutado usou --antes-bin (binario do leg antes reutilizado): poupa ~4 min por corrida e exercita um caminho que o 23-01 deixou nao-exercitado; a consequencia (ORIGEM_A e raiz da suite Python) fica declarada no documento"
  - "Nenhuma alteracao ao scripts/upstream_block_gate.sh: nenhum defeito do portao foi encontrado, logo nao houve de repetir os dois sentidos por correccao (T-23-12)"

patterns-established:
  - "Quando dois legs do MESMO commit divergem, o experimento discriminador e' correr os binarios JA' LINKADOS alternadamente, sem rebuilds: separa 'binario diferente' de 'ordem/carga da maquina' em 2,5 minutos"
  - "Uma injeccao de regressao line-neutral (ou num ficheiro nao ancorado) evita que a perna errada reprove por deriva de ancoras"

requirements-completed:
  - UPST-02
requirements-partial: []

coverage:
  - id: D1
    description: "Auto-teste do portao com >=15 verificacoes, em segundos, sem builds nem boots, registado no CTest"
    requirement: "UPST-02"
    verification:
      - kind: unit
        ref: "bash scripts/test_upstream_block_gate.sh -> rc=0, 46 linhas PASS, 0 FAIL, 0,1 s; ctest -R '^test_upstream_block_gate$' -> Passed"
        status: pass
    human_judgment: false
  - id: D2
    description: "Prova por mutacao do proprio auto-teste: PS3_RSX_BACKEND=trace -> metal no portao deixa o teste vermelho"
    requirement: "UPST-02"
    verification:
      - kind: unit
        ref: "sed no portao -> rc=1 com 'FAIL G1.6 ... obtido=0'; portao restaurado (git diff --quiet)"
        status: pass
    human_judgment: false
  - id: D3
    description: "As tres pernas com prova nos dois sentidos: perna_a/b/c e derive_rc_class por vectores sinteticos"
    requirement: "UPST-02"
    verification:
      - kind: unit
        ref: "Grupos 2 a 5 do auto-teste: 7 vectores de derive_rc_class, 6 de perna_b, 8 de perna_c, 10 de perna_a"
        status: pass
    human_judgment: false
  - id: D4
    description: "Sentido LIMPO: a copia sem injeccao faz o portao sair rc=0"
    requirement: "UPST-02"
    verification:
      - kind: e2e
        ref: "COPIA-LIMPA-2: depois=11,11,11 -> VEREDICTO=PASSOU ELO_CAIDO=nenhum, rc=0, bloco_vazio=sim"
        status: pass
    human_judgment: false
  - id: D5
    description: "Sentido MUTADO: a mesma copia com regressao comportamental faz o portao sair rc!=0 nomeando o elo certo, com as outras duas pernas passadas"
    requirement: "UPST-02"
    verification:
      - kind: e2e
        ref: "COPIA-MUTADA-B: depois st620=0,0,0 rc=97 saida-anormal -> VEREDICTO=REJEITADO ELO_CAIDO=intro (st620), rc=1, PERNA B PASSA e PERNA C PASSA no mesmo resumo"
        status: pass
    human_judgment: false
  - id: D6
    description: "Producao intacta e copia destruida"
    requirement: "UPST-02"
    verification:
      - kind: integration
        ref: "sha256 677f60a7... igual antes e depois; HEAD igual; git status --porcelain identico (31 linhas); zero worktrees em TMPDIR; zero _exit(97) em runtime/ libs/ include/"
        status: pass
    human_judgment: false

duration: ~95min
completed: 2026-08-12
status: complete
---

# Phase 23 Plano 03: A prova por mutacao do portao Summary

**O portao de adopcao sabe reprovar, e nomeia o elo certo: a mesma copia descartavel saiu `rc=0` sem injeccao e `rc=1` com uma regressao comportamental (`_exit(97)` a' entrada de `ppu_load_elf`), nesse caso com `st620 = 0,0,0`, `rc = 97` classificado `saida-anormal` e as PERNAS B e C reportadas PASSA no MESMO relatorio — com a producao verificada intacta por `sha256` antes e depois.**

## Performance

- **Duração:** ~95 min (dos quais ~55 min de medição real: 4 corridas do portão + 12 boots de diagnóstico)
- **Tasks:** 2/2 (Task 1 + Task 1b de verificação; Task 2 nos seus 4 passos)
- **Ficheiros criados:** 2 · **modificados:** 2

## Accomplishments

- **O auto-teste existe e é guarda permanente da CI.** `scripts/test_upstream_block_gate.sh`: **46 verificações em 0,1 s**, sem builds, sem boots, sem worktrees. Registado no CTest (`test_upstream_block_gate`, teste #29) com `SKIP_REGULAR_EXPRESSION` — o `scripts/test_runtime.sh` apanha-o sem qualquer alteração.
- **As regras G6 deixaram de ser promessas de cabeçalho.** O Grupo 1 mede, sobre as **linhas de código** do portão (comentários filtrados): zero ocorrências do token do matador por nome de processo, `kill -TERM` e `kill -9` presentes, `PS3_NO_RSX=1` + `PS3_RSX_BACKEND=trace`, `PS3_ENGINE_ROOT`, zero `eval`.
- **As três pernas têm prova nos dois sentidos.** 7 vectores de `derive_rc_class` (143/137/124 e o `matamos=1` → `timeout-normal`; 97 → `saida-anormal`), 6 de `perna_b` (a dívida declarada nunca conta; um FAIL novo é nomeado), 8 de `perna_c` (binários idênticos reprovam; bloco vazio e bloco só-de-documentação passam declarados) e 10 de `perna_a` (limiar, comparação com o leg `antes`, saídas anormais novas, e um vector contra o `st620_min=11` real do baseline).
- **Prova por mutação do próprio auto-teste.** Trocar `PS3_RSX_BACKEND=trace` por `metal` no portão deixa o auto-teste **vermelho** (`FAIL G1.6 ... obtido=0`, `rc=1`); a alteração foi revertida e `git diff --quiet` confirma.
- **Prova por mutação do portão, nos dois sentidos, numa cópia.** `git worktree add --detach ${TMPDIR}/copia HEAD`; sentido limpo `VEREDICTO=PASSOU ELO_CAIDO=nenhum`; sentido mutado `VEREDICTO=REJEITADO ELO_CAIDO=intro (st620)`.
- **O portão discrimina.** No relatório vermelho, a `PERNA B` (suite Python) e a `PERNA C` (mediu mesmo) aparecem **PASSA** — o portão acusa o elo que caiu, não reprova em bloco.
- **A produção ficou intacta.** `producao_sha256 = 677f60a746ba8bc253bd9b194849d535ea317abaf10148a317e1caf405ca6b89` antes e depois, `HEAD` igual, `git status --porcelain` idêntico linha a linha (31 linhas, todas de outra sessão), zero worktrees em `${TMPDIR}`, zero `_exit(97)` residual.

## Task Commits

1. **Task 1 (+1b): auto-teste do portão + registo no CTest + prova por mutação** — `d1c392a` (test)
2. **Task 2: a prova por mutação do portão nos dois sentidos** — `630b746` (docs)
3. **Ledger da fase** — `a142d9b` (docs, deferred-items)

## Ficheiros criados/modificados

- `scripts/test_upstream_block_gate.sh` — 6 grupos, 46 verificações; Grupos 1-5 correm em qualquer máquina, Grupo 6 salta limpo sem o checkout do jogo.
- `docs/upstream-blocks/GATE-MUTATION-PROOF.md` — comandos literais, tabelas corrida a corrida dos dois sentidos, a injecção nomeada e justificada, os três valores de produção antes/depois, as **duas refutações** do caminho, e `## O que esta prova NAO cobre`.
- `CMakeLists.txt` — `add_test(test_upstream_block_gate)` + `set_tests_properties(... SKIP_REGULAR_EXPRESSION)`, antes do `endif()` do bloco de testes.
- `.planning/phases/23-*/deferred-items.md` — os quatro achados abaixo.

## Nível de evidência (disciplina do projecto)

| Afirmação | Evidência |
|---|---|
| O auto-teste corre em segundos, 46 verificações, 0 FAIL, e no CTest | **offline-unit** — `rc=0`; `ctest -R '^test_upstream_block_gate$'` → `Passed 0.10 sec` |
| Quebrar o G6 do portão deixa o auto-teste vermelho | **offline-unit** — `trace`→`metal`, `rc=1`, `FAIL G1.6`, revertido |
| A cópia **sem** injecção faz o portão sair `rc=0` | **in-boot** — `COPIA-LIMPA-2`, 6 corridas, `depois = 11,11,11` |
| A cópia **com** injecção faz o portão sair `rc!=0` nomeando `intro (st620)` | **in-boot** — `COPIA-MUTADA-B`, 6 corridas, `depois = 0,0,0` com `rc=97` |
| O portão nomeia o elo **certo** (intro caída, suite Python sã) | **in-boot** — `PERNA B ... PASSA` e `PERNA C ... PASSA` no mesmo resumo |
| A produção fica intacta | **in-boot/integration** — `diff` das duas fotografias completas: só o título e a data diferem |
| `derive_rc_class` distingue o nosso timeout de uma saída anormal | **in-boot _e_ offline-unit** — vectores no auto-teste **e** `143`→`timeout-normal` vs `97`→`saida-anormal` medidos nas corridas reais |
| A `PERNA A` é estável sob carga | **REFUTADO por medição** — ver Issue 1 |
| `sys_tty_write` (403) é chamada no arranque do GoW2 | **REFUTADO por medição** — ver Issue 2 |

## Decisões tomadas

1. **A linha de salto só é impressa quando não há nenhum FAIL.** O `SKIP_REGULAR_EXPRESSION` do CMake é **desancorado** (decisão já documentada no `CMakeLists.txt` a partir da F0-5/F0-6): uma linha de salto impressa sempre mascararia um vermelho dos Grupos 1-5 como `Skipped`.
2. **O token proibido é construído por concatenação em tempo de execução** (`"p"+"kill"`), e o próprio teste verifica que não existe literalmente nem no portão nem em si mesmo (T-23-13).
3. **O vector de FRONTEIRA foi codificado pela regra escrita, não pela sugestão do plano** (ver Desvio 1).
4. **A injecção final é o candidato B**, num ficheiro **não ancorado** no `UPSTREAM_DELTA.tsv`, e *line-neutral* na intenção (uma linha acrescentada, sem âncoras abaixo) — para que o sentido mutado medisse a **intro** e não a deriva de âncoras (ver Issue 3).
5. **`--antes-bin` nas duas corridas finais.** Poupa ~4 min por corrida, torna os dois sentidos comparáveis entre si (mesmo binário `antes`, `f584d03e…`) e exercita um caminho que o 23-01 deixou `nao-exercitado`. Consequência declarada no documento.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 — Correctness] O vector de FRONTEIRA do plano contradiz a regra escrita no portão**
- **Encontrado durante:** Task 1 (escrita do Grupo 5)
- **Problema:** o plano sugeria `antes=[3,3,3] depois=[3,3,0] -> PASSA`. Medido: `perna_a_veredicto 3 3 "3,3,3" "3,3,0"` devolve `rc=1` com `motivo=ok(depois)=2 < ok(antes)=3` — a regra do portão exige `ok(depois) >= ok(antes)` **além** do limiar.
- **Fix:** o esperado codificado é **FALHA** (o próprio plano manda "decidir pela regra escrita no portão"). Comentado no ficheiro de teste.
- **Verificação:** `PASS G5.5` / `PASS G5.6`.
- **Commit:** `d1c392a`

**2. [Rule 3 — Blocking] A prova da string `intro (st620)` não é possível ao nível de biblioteca**
- **Problema:** o plano pedia que o vector VERMELHO do Grupo 5 contivesse `intro (st620)`. Essa string só existe no mapeamento `ELO_CAIDO` do portão (linha 774), não na saída de `perna_a_veredicto`.
- **Fix:** o Grupo 5 assevera `rc=1` + o `motivo` exacto; a prova da string ficou na corrida real (Task 2), onde aparece na última linha canónica.
- **Verificação:** `grep -qxF 'VEREDICTO=REJEITADO ELO_CAIDO=intro (st620)'` na corrida mutada.
- **Commit:** `d1c392a` / `630b746`

**3. [Rule 1 — Bug no plano] O candidato A da injecção não é atingido**
- **Encontrado durante:** Task 2 (primeira corrida do sentido mutado)
- **Problema:** com `_exit(97)` em `sys_tty_write`, as três corridas do leg `depois` deram `rc=143` e `st620 = 11,1,0` — o padrão normal. A syscall 403 **não é chamada** nesta intro, apesar do comentário do próprio `lv2_register.c`.
- **Fix:** candidato B, `_exit(97)` à entrada de `ppu_load_elf` (`runtime/ppu/ppu_loader.cpp`), compilado incondicionalmente a cada leg (`build_macos.sh:176-177`). Resultado: `st620 = 0,0,0`, `rc = 97`, `saida-anormal` ×3.
- **Verificação:** TSV do `COPIA-MUTADA-B`; a refutação do candidato A ficou escrita no documento, não apagada.
- **Commit:** `630b746`

**4. [Rule 3 — Blocking] `set_tests_properties(... SKIP_REGULAR_EXPRESSION)` não estava no plano**
- **Problema:** sem isso o salto do Grupo 6 apareceria como `Passed` no CTest, exactamente o mascaramento que a convenção F0-3 existe para evitar.
- **Fix:** acrescentado, como nos `py.*`. Reforçado com a regra de só imprimir a linha de salto quando não há FAIL.
- **Commit:** `d1c392a`

### Verificações adaptadas (factos do ambiente, não auto-fixes)

- **`git worktree list | wc -l == 1`** não é executável nesta máquina: existem 4 worktrees de agente pré-existentes sob `.claude/worktrees/`. Afirmação mensurável equivalente, verificada: **`git worktree list | grep -c "${TMPDIR}"` → 0**, e a contagem total voltou às 5 linhas da fotografia inicial.
- **`git status --porcelain runtime/ libs/ include/` vazio** também não: 23 das 31 linhas sujas são de outra sessão (`runtime/spu/tests/out_*`). O que a verificação prova de facto é **`grep -rq '_exit(97)' runtime/ libs/ include/` sem resultados** — verificado.
- **Verifies do plano com `$?` a seguir a um `echo` ou a um `tee`** (Task 1 verify 1, Task 2 verify 6) mediriam o `rc` do `echo`/`tee`. Corridos com o `rc` guardado em variável antes.

---

**Total de desvios:** 4 auto-corrigidos (1× Rule 1, 1× Rule 2, 2× Rule 3) + 3 verificações adaptadas.
**Impacto no plano:** nenhum alargamento de âmbito; nenhuma alteração ao `scripts/upstream_block_gate.sh` (nenhum defeito do portão foi encontrado, logo T-23-12 não foi accionado).

## Issues Encountered

**1. A `PERNA A` tem um modo de FALSO VERMELHO sob carga da máquina — medido, não corrigido.**
O sentido limpo **reprovou à primeira** (`COPIA-LIMPA`: `antes = 0,11,11`, `depois = 1,1,1`) com os dois legs no **mesmo commit**. O experimento discriminador (os dois binários já linkados, corridos alternadamente A,B,A,B,A,B, sem rebuilds, 2,5 min) mostrou que **ambos** produzem `1` e `11` — logo a diferença não está no código. Repetido com `timeout` de 45 s: `A = 11,11,11`, `B = 0,1,11`. A corrida seguinte, com um link novo da mesma cópia, deu `depois = 11,11,11` e `rc=0`. Com `load` entre 6 e 21 (outras sessões a construir), a intro atinge `st620=11` em cerca de metade das corridas; com `--runs 3` (limiar 2) isso basta para um falso vermelho. Direcção do erro: **rejeita o que devia passar** — o sentido seguro para um portão, mas caro. Deferido: corrida de aquecimento por leg (candidato já aberto no 23-01) ou `--runs 6` por default.

**2. `sys_tty_write` (syscall 403) não é chamada na intro do GoW2.** Contraria o comentário do próprio `runtime/syscalls/lv2_register.c:36`. Medição, não suposição: `.o` recompilado, três corridas com `rc=143`.

**3. Um bloco que mude o número de linhas de um ficheiro ancorado no `UPSTREAM_DELTA.tsv` faz a `PERNA B` reprovar por deriva de âncora.** Medido com o candidato A (`FAIL ANCORA_DESLOCADA linha=29 ... linha_actual=841`). Não é defeito do portão — a suite falha de facto — mas é uma armadilha operacional: **re-ancorar (`--fix-anchors`) faz parte de submeter um bloco ao portão**. Registado no ledger; escrevê-lo no `README.md` do portão ficou deferido (fora dos ficheiros deste plano).

**4. Ruído de job control (`Terminated: 15`) no stderr das corridas.** Igual ao 23-01: é a notificação do `kill -TERM` que **nós** mandámos. Não suprimido, de propósito.

## User Setup Required

Nenhum. O auto-teste corre sem o checkout do jogo (salta o Grupo 6); a prova por mutação exige o checkout físico (`../gow2-recomp`) e é um exercício pontual, já registado.

## Known Stubs

Nenhum stub. Superfícies **implementadas e ainda não exercitadas** depois deste plano: `FORCE_REBUILD_LIFT=1` no sentido positivo (nenhuma corrida deste plano tocou um `.h` de `include/` ou `runtime/ppu/`), e a subtracção do `known_debt_python` na `PERNA B` in-boot (com `SDKROOT` exportado as duas falhas não ocorrem). `--depois-arvore` e `--antes-bin`, que o 23-01 deixou `nao-exercitado`, passaram a estar **exercitados in-boot** por este plano (4 corridas e 2 corridas, respectivamente).

## Threat Flags

- `T-23-11` (regressão a chegar à produção) — **mitigado e verificado**: injecção só dentro do worktree destacado em `${TMPDIR}`, `worktree remove --force` + `prune`, `git worktree list | grep -c "${TMPDIR}"` → 0, `grep -rq '_exit(97)' runtime/ libs/ include/` sem resultados, `sha256`/HEAD/`git status` da produção idênticos.
- `T-23-12` (registo escrito antes de uma correcção do portão) — **não accionado**: nenhum defeito do portão foi encontrado; o `scripts/upstream_block_gate.sh` não foi alterado neste plano (a mutação `trace`→`metal` da Task 1b foi revertida e verificada com `git diff --quiet`).
- `T-23-13` (auto-teste que contém o token proibido) — **mitigado**: token concatenado em runtime; `PASS G1.2` verifica zero ocorrências no próprio ficheiro de teste, e a verificação externa do plano confirma.
- `T-23-14` (boots órfãos) — **mitigado**: toda a morte por PID (TERM → 1 s → `-9`), no portão e no script de diagnóstico descartável; `PASS G1.3`/`G1.4` fazem disso uma guarda de CI.
- `T-23-15` (logs com conteúdo do jogo) — **mitigado**: todos os TSV, resumos e logs de boot em `${TMPDIR}`, fora dos dois repositórios; o documento cita números e hashes, nunca conteúdo do jogo.
- `T-23-16` (instalações de gestor de pacotes) — **accept**: nenhum pacote instalado.

## Self-Check: PASSED

Ficheiros afirmados: `scripts/test_upstream_block_gate.sh` e `docs/upstream-blocks/GATE-MUTATION-PROOF.md` existem em disco; `CMakeLists.txt` e `deferred-items.md` modificados. Commits afirmados: `d1c392a`, `630b746`, `a142d9b` existem no histórico. Números **re-lidos das fontes**, não da memória: `resumo: PASS=46 FAIL=0` no log do auto-teste; `Test #29: test_upstream_block_gate ... Passed 0.10 sec` no CTest; TSV `COPIA-LIMPA-2` `depois = 11,11,11` e `COPIA-MUTADA-B` `depois = 0,0,0` com `rc = 97,97,97`; `st620_min=11` no baseline congelado; `producao_sha256 = 677f60a746ba8bc253bd9b194849d535ea317abaf10148a317e1caf405ca6b89` idêntico nas duas fotografias.

---

## Adenda (2026-08-12, pos-review): o guarda do Grupo 1 nao sabia reprovar

Tudo o que esta escrito acima fica como esta': era verdade a' data e as medicoes
sao reais. Esta adenda **acrescenta** o que o review desta fase (`PASSA-COM-RESSALVAS`)
apanhou e que ficou por corrigir ate' agora.

### O que estava errado

O Grupo 1 entregue pela Task 1 media **ocorrencias de um token no ficheiro inteiro**
do portao (`ao_menos ... 1` sobre `codigo | grep -c`). Isso nao mede disciplina
nenhuma: cada token de G6 tem no portao **duas** ocorrencias — uma **portadora**
(a linha que faz o trabalho) e uma **decorativa** (uma mensagem de `echo`, ou a
string de proveniencia `RECIPE_LITERAL`, que e' registo do que se mediu e nao a
recipe que se corre). Com o limiar em `>=1`, apagar a portadora deixa a decorativa
e o guarda fica **verde**.

Isto foi **medido**, nao deduzido. Numa copia do portao no scratchpad, com as
cinco disciplinas de G1.3 a G1.7 removidas de uma so' vez (motor por leg,
recipe headless, morte por PID no fim do nosso timeout), o auto-teste **da versao
`d1c392a`** deu:

```
PASS G1.3 morte por PID: 'kill -TERM' no codigo do portao [1 >= 1]
PASS G1.4 morte por PID: 'kill -9' no codigo do portao [1 >= 1]
PASS G1.5 recipe headless: 'PS3_NO_RSX=1' no codigo do portao [1 >= 1]
PASS G1.6 recipe headless: 'PS3_RSX_BACKEND=trace' no codigo do portao [1 >= 1]
PASS G1.7 motor por leg: 'PS3_ENGINE_ROOT' no codigo do portao [1 >= 1]
resumo: PASS=42 FAIL=0        ->  rc=0
```

O caso grave e' o G1.7: sem `PS3_ENGINE_ROOT="$wt"` o `build_macos.sh` resolve
`$HERE/../ps3recomp` e os **dois legs linkam o mesmo motor** — o portao passaria
sempre, por nao ter medido nada. E o guarda que existe para proteger essa
disciplina nao dava por isso.

Correccao ao briefing do review, por medicao directa: as contagens de token
citadas (3/3/4/3/4) sao do ficheiro inteiro; o que os guardas contavam era
`codigo | grep -c`, com comentarios ja' filtrados, e da' **2** para os cinco
tokens. A conclusao mantem-se inteira; so' o numero era outro.

### O que substituiu

Parar de contar **tokens** e passar a exigir **constructos dentro da funcao** que
a disciplina obriga. Tres primitivas novas em `scripts/test_upstream_block_gate.sh`:

- `corpo FUNCAO` — extrai o corpo da funcao homonima do portao (abre em `nome() {`
  na coluna 0, fecha na primeira `}` na coluna 0 — o estilo do portao inteiro),
  ja' sem comentarios.
- `constructo NOME FUNCAO ERE` — veredicto em dois niveis: o constructo tem de
  existir no **codigo** (as ERE exigem forma de atribuicao/comando com operando
  `$var`, logo um `echo` ou uma string entre plicas nunca as satisfazem) **e**
  dentro da funcao ancora. As tres mensagens de falha sao deliberadamente
  diferentes — "DISCIPLINA G6 AUSENTE", "existe no portao mas FORA de `fn()`",
  "a funcao nao existe — se foi RENOMEADA, actualiza a ancora; nao a apagues" —
  porque o remedio de cada caso e' outro.
- `ordem NOME FUNCAO ERE_1 ERE_2` — G6(2) e' uma regra de **ordem**, e nenhuma
  contagem a pode medir.

O `ao_menos` **continua a existir e a ser usado** pelos Grupos 2 a 6 (e por G5.9),
onde contar e' de facto o que se quer. Os guardas de **ausencia** (G1.1, G1.2, G1.8,
`igual ... 0`) nao mudaram: esses ja' sabiam reprovar.

Grupo 1 passou de 8 para **11 verificacoes** (+G1.4a escalada TERM→-9 pela ordem
certa, +G1.4b o trap `limpar` tambem mata por PID, +G1.6a a recipe **depois** do
`. env_gow2.sh`). Totais novos: **45** sem o checkout do jogo, **49** com ele
(as 46 e 42 citadas acima sao os numeros de `d1c392a` e ficam como registo).

### Prova nos dois sentidos (numa copia; a producao nao foi tocada)

| Corrida | Portao | Auto-teste | Resultado medido |
|---|---|---|---|
| Controlo | mutado (M1+M2+M3) | **`d1c392a`** (antigo) | `PASS=42 FAIL=0`, **rc=0** — verde com a disciplina fora |
| Sentido vermelho | mutado (M1+M2+M3) | novo | `PASS=38 FAIL=7`, **rc=1**, nomeando G1.3, G1.4, G1.4a, G1.5, G1.6, G1.6a, G1.7 |
| Sentido verde | intacto | novo | `PASS=45 FAIL=0`, **rc=0** |
| Producao | intacto (`git diff --quiet` OK) | novo | `PASS=49 FAIL=0`, **rc=0** |

Adversarial, ambos **vermelhos** (`rc=1`): comentar a atribuicao em vez de a apagar
→ `FAIL G1.7 ... DISCIPLINA G6 AUSENTE`; apaga-la e por uma igual em codigo morto
fora da funcao → `FAIL G1.7 ... existe no portao (1x) mas FORA de 'linkar_binario()'`.
Refactor legitimo (partir o `export` em tres linhas, `kill -s TERM`/`kill -s KILL`,
`PS3_ENGINE_ROOT` em `export` proprio, `$bpid`→`$boot_pid`) → `PASS=45 FAIL=0`, `rc=0`.

### O que esta adenda NAO prova

- **Nivel de evidencia: `offline-unit`.** Zero boots, zero builds, zero worktrees.
  Isto e' analise **estatica** do texto do portao: prova que a disciplina G6 esta'
  **escrita nos sitios certos**, nunca que foi **exercitada** numa corrida. Nao
  substitui um portao corrido de verdade.
- **Falso positivo conhecido e declarado:** renomear uma funcao ancora
  (`correr_intro`, `linkar_binario`, `limpar`) reprova, mesmo com a disciplina
  intacta. E' uma escolha, nao um descuido — sem a ancora, uma atribuicao em codigo
  morto voltaria a calar o guarda (caso adversarial acima, medido). A mensagem diz
  qual palavra do teste corrigir. Corolario operacional: **quem mexer no portao tem
  de mexer tambem no auto-teste.**
- **Limite da extraccao:** `corpo` assume o estilo do portao (cabecalho e `}` de
  fecho na coluna 0; verificado hoje para as tres funcoes ancora). Uma `}` na coluna 0
  a meio de uma funcao truncaria o corpo e daria falso vermelho — com a mensagem
  "DISCIPLINA G6 AUSENTE", que nesse caso seria enganadora.
- As ERE toleram `kill -s TERM|KILL|15|9` alem de `-TERM`/`-9`, mas **nao** formas
  indirectas (`local sinal=TERM; kill "-$sinal" "$pid"`). Deliberado: aceitar
  indireccao exigiria interpretar o script.
- O Grupo 6 salta na copia (sem checkout do jogo), logo o desenho novo **nao foi
  exercitado** contra os casos G6.1-G6.4 na copia — na producao correu e passou.
- **Revisto depois desta adenda:** `docs/upstream-blocks/GATE-MUTATION-PROOF.md:292`
  e `.planning/STATE.md:82` diziam "46 verificacoes" / `PASS=46` e passaram a **49**.
  A adenda tinha-os deixado por corrigir como "registo do que foi medido em
  `d1c392a`"; a distincao que faltava e' entre **medicao de uma corrida** e
  **descricao da ferramenta**. As medicoes de corrida (TSV, `st620`, `sha256`) sao
  historico e nao se tocam. Aquelas duas linhas nao sao medicoes: uma descreve o
  tamanho do auto-teste entre parenteses e a outra e' a tabela de veredicto de um
  documento de estado corrente -- ambas ficavam **falsas** para quem corresse a
  ferramenta hoje (`resumo: PASS=49 FAIL=0`, medido). O registo historico dos 46
  fica onde pertence: nas cinco ocorrencias deste SUMMARY, que **nao** foram
  alteradas.

### A outra ressalva do review, que continua aberta

`RESSALVA 3` (parcial): a separacao de honestidade imposta por
`tools/check_block_record.py` e' **estrutural** (reprova seccoes fundidas), nao
**semantica** — nao apanha uma frase, dentro da seccao certa, que funda as duas
afirmacoes. Registada aqui; nao corrigida por este commit.
