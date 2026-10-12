---
phase: 17-mid-asm-de-ponta-a-ponta
plan: 01
subsystem: tooling
tags: [ppu_lifter, recomp_config, toml, midasm, golden-test, tdd, python]

requires:
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "ledger PATCH_MIGRATION.tsv com o CE03C classificado P0/alvo=midasm e o baseline medido contra o qual a Fase 17 compara"
provides:
  - "tools/ppu_lifter.py emite `gow2_midasm_<Name>(ctx);` a partir de `[[midasm_hook]]`, atrás de `--config`"
  - "Contrato de colocação: antes por default, depois com `after_instruction`, `return;` a seguir com `return=true`"
  - "Declarações dos hooks no preâmbulo da TU gerada (a TU compila sem header host)"
  - "Propagação dos hooks para o lift paralelo (_worker_init/_WORKER_STATE/_worker_lift/_parallel_lift)"
  - "Recusas rc=2: jump_address, nome não-identificador, address negativo, EA órfão"
  - "Golden de não-regressão por diff: tools/testdata/midasm_golden_noconfig.c"
  - "tools/test_midasm_emit.py (50 asserções) + docs/RECOMP_CONFIG.md alinhado ao consumidor"
affects: [17-02 hooks host, 17-03 migração CE03C, 18 switch tables, 19 weak overrides, 21 métrica re-lift-safe]

tech-stack:
  added: []
  patterns:
    - "Golden capturado com a ferramenta ANTERIOR à mudança, comparado por difflib.unified_diff"
    - "Recusa rc=2 no lifter, alinhada ao contrato de recusa dos patch_*.py"
    - "Estado novo do lifter viaja pelo mesmo caminho que invalid_instructions até aos workers"

key-files:
  created:
    - tools/test_midasm_emit.py
    - tools/testdata/midasm_golden_noconfig.c
    - tools/testdata/README.md
  modified:
    - tools/ppu_lifter.py
    - docs/RECOMP_CONFIG.md

key-decisions:
  - "jump_address é RECUSADO (rc=2), não avisado: avisar-e-continuar deixaria no disco um lift que chama o hook e não salta — a surpresa 'declarado mas sem efeito' que o próprio recomp_config.py já rejeita no `default` do [[switch_table]]"
  - "Índice {EA: [hook, ...]} em vez de last-wins: dois hooks distintos no mesmo EA são legítimos e last-wins perderia um em silêncio"
  - "Duplicado exacto (name+address) é descartado COM aviso, não last-wins: emiti-lo correria o efeito secundário a dobrar"
  - "Cobertura dos hooks verificada no processo PAI (all_insns + func_bounds), antes do lift: dá o mesmo resultado com -j1 e -jN sem mudar a forma de retorno de _worker_lift"
  - "Linhas de hook excluídas de _last_line_is_terminator: sem isso, declarar um hook a seguir ao blr final passava a função a fallthrough e mudava o fluxo de controlo"
  - "O lifter declara os hooks no preâmbulo (extern \"C\" condicional): sem isso a TU gerada chamava uma função não declarada"

patterns-established:
  - "Golden por diff, não por inspecção: o baseline é capturado com a ferramenta pré-mudança e commitado; só a linha /* lifter-rev */ é normalizada"
  - "Prova de mutação do próprio teste: antes de dar o golden por bom, injectar o defeito que ele existe para apanhar e confirmar que morre"
  - "Limitação declarada em vez de dada por feita: o lifter verifica que EXISTE instrução no EA, não que é a esperada — o schema não tem campo para a declarar"

requirements-completed: [XEN-01]

coverage:
  - id: D1
    description: "Emissão do hook antes da instrução ancorada (default), na posição exacta"
    requirement: XEN-01
    verification:
      - kind: unit
        ref: "tools/test_midasm_emit.py#test_before_hook_lands_immediately_before_the_translated_line"
        status: pass
    human_judgment: false
  - id: D2
    description: "Emissão depois da instrução com after_instruction=true"
    requirement: XEN-01
    verification:
      - kind: unit
        ref: "tools/test_midasm_emit.py#test_after_instruction_true_lands_immediately_after"
        status: pass
    human_judgment: false
  - id: D3
    description: "return=true injecta `return;` imediatamente a seguir à chamada, nas duas colocações"
    requirement: XEN-01
    verification:
      - kind: unit
        ref: "tools/test_midasm_emit.py#test_return_true_emits_return_right_after_the_hook_call"
        status: pass
      - kind: unit
        ref: "tools/test_midasm_emit.py#test_returns_false_does_not_inject_a_return"
        status: pass
    human_judgment: false
  - id: D4
    description: "Golden: sem --config o .c gerado é byte-a-byte idêntico ao do lifter anterior à fase (diff vazio)"
    requirement: XEN-01
    verification:
      - kind: integration
        ref: "tools/test_midasm_emit.py#test_golden_no_config_output_is_byte_identical"
        status: pass
      - kind: integration
        ref: "tools/test_midasm_emit.py#test_golden_holds_with_config_that_has_no_midasm_section"
        status: pass
      - kind: other
        ref: "prova de mutação: emitir um hook sem config -> 4 asserções FAIL, incl. os dois diffs (ver ## Provas)"
        status: pass
    human_judgment: false
  - id: D5
    description: "Os hooks sobrevivem ao lift paralelo (-j) — worker directo e mp.Pool real"
    requirement: XEN-01
    verification:
      - kind: unit
        ref: "tools/test_midasm_emit.py#test_worker_init_and_worker_lift_thread_midasm_hooks"
        status: pass
      - kind: integration
        ref: "tools/test_midasm_emit.py#test_parallel_lift_real_pool_threads_midasm_hooks"
        status: pass
      - kind: other
        ref: "prova de mutação: remover os hooks do tuplo de initargs -> FAIL (ver ## Provas)"
        status: pass
    human_judgment: false
  - id: D6
    description: "jump_address não é implementado: o lifter recusa (rc=2) em vez de emitir a chamada sem o salto"
    requirement: XEN-01
    verification:
      - kind: integration
        ref: "tools/test_midasm_emit.py#test_jump_address_is_refused_not_half_implemented"
        status: pass
      - kind: unit
        ref: "tools/test_midasm_emit.py#test_jump_address_never_invents_a_goto_at_api_level"
        status: pass
    human_judgment: false
  - id: D7
    description: "EA órfão falha alto (rc=2) com o endereço e o nome do hook listados"
    requirement: XEN-01
    verification:
      - kind: integration
        ref: "tools/test_midasm_emit.py#test_orphan_address_fails_loudly_via_cli"
        status: pass
      - kind: unit
        ref: "tools/test_midasm_emit.py#test_orphan_detection_distinguishes_the_three_ways_a_hook_dies"
        status: pass
    human_judgment: false
  - id: D8
    description: "A TU gerada com hooks compila: declarações emitidas no preâmbulo, válidas em C e C++"
    verification:
      - kind: other
        ref: "clang -fsyntax-only -std=c11 && clang++ -fsyntax-only -std=c++17 sobre o bloco emitido (ver ## Provas)"
        status: pass
      - kind: integration
        ref: "tools/test_midasm_emit.py#test_cli_with_midasm_emits_the_call_and_reports_the_count"
        status: pass
    human_judgment: false
  - id: D9
    description: "docs/RECOMP_CONFIG.md descreve o [[midasm_hook]] realmente consumido (metade midasm do XEN-06)"
    verification: []
    human_judgment: true
    rationale: "Correspondência doc<->consumidor é julgamento editorial; nenhum teste verifica que a prosa está correcta. XEN-06 só fecha quando a fase inteira (17-02/17-03) aterrar."

duration: ~55min
completed: 2026-08-02
status: complete
---

# Fase 17 Plano 01: Emissão de mid-asm no `ppu_lifter.py` — Summary

**O `ppu_lifter.py` passou a EMITIR `gow2_midasm_<Name>(ctx);` a partir de `[[midasm_hook]]`
quando recebe `--config` — com um golden por `diff` que prova que, sem `--config`, o output
não mudou um byte, e com o lift paralelo a carregar os hooks em vez de os perder em silêncio.**

## Performance

- **Duração:** ~55 min
- **Tasks:** 2/2 (Task 1 em TDD: RED → GREEN; Task 2 absorvida no mesmo caminho de código)
- **Ficheiros:** 3 criados, 2 modificados
- **Nível de evidência:** `offline-unit` (nenhuma corrida de jogo; nada aqui foi provado in-boot)

## Accomplishments

- **O consumidor que faltava.** O parser (`recomp_config.MidasmHook`) existia desde a Task A1
  e a secção era carregada e deitada fora. Agora `build_midasm_index` transforma-a em
  `{EA: [hook, ...]}` e `PPULifter.lift_function` emite a chamada ao lado da instrução ancorada.
- **Golden de regressão que não é decorativo.** `tools/testdata/midasm_golden_noconfig.c` foi
  capturado com o lifter **anterior** a esta mudança (git `4bf3bf8`) e é comparado por
  `difflib.unified_diff`. Provado discriminante por mutação (ver `## Provas`).
- **O `-j` não perde hooks.** `midasm_hooks_by_ea` viaja por `_worker_init` → `_WORKER_STATE` →
  `_worker_lift` e pelo tuplo de `initargs` de `_parallel_lift`, o mesmo caminho do
  `invalid_instructions`. Provado por mutação também.
- **Falha alto onde antes falharia em silêncio.** Quatro recusas com `rc=2` (o código de recusa
  dos `patch_*.py` deste repo): `jump_address`, nome não-identificador, address negativo e
  **EA órfão** (declarado mas que nunca produziria emissão nenhuma).
- **A TU gerada compila.** O preâmbulo passa a declarar os hooks; sem isso o lift saía a chamar
  uma função não declarada.

## Task Commits

1. **Task 1 (RED): suite + golden antes do código** — `0d670d7` (test)
2. **Task 1 (GREEN) + Task 2: emissão, wiring paralelo, recusas, contagem no CLI e `--help`** — `a800471` (feat)
3. **Task 1 (docs) — `docs/RECOMP_CONFIG.md`** — `a955c78` (docs)

## Ficheiros Criados/Modificados

- `tools/test_midasm_emit.py` **(novo, 50 asserções)** — a suite que fixa o contrato da emissão.
- `tools/testdata/midasm_golden_noconfig.c` **(novo)** — output do lifter pré-mudança sobre a
  fixture sintética de 4 palavras. Nenhum dado de jogo envolvido.
- `tools/testdata/README.md` **(novo)** — proveniência do golden (o ficheiro é output verbatim e
  não pode levar cabeçalho, por isso a explicação vive ao lado).
- `tools/ppu_lifter.py` — ver `## Onde foi emitido` abaixo.
- `docs/RECOMP_CONFIG.md` — secção `[[midasm_hook]]` reescrita; corrigida também a frase da
  secção `invalid_instructions` que ainda afirmava que todas as outras secções eram ignoradas.

## Onde foi emitido (mapa da mudança no lifter)

| Sítio | O quê |
|---|---|
| `MIDASM_SYMBOL_PREFIX`, `MidasmConfigError`, `build_midasm_index`, `check_midasm_coverage` (antes de `class PPULifter`) | Índice por EA, recusas, e a guarda de cobertura |
| `PPULifter.__init__` | `self.midasm_hooks_by_ea: dict[int, list]` (vazio = comportamento legado) |
| `PPULifter._midasm_lines` | Linhas C de um EA, filtradas por colocação; injecta `return;` com `returns` |
| `PPULifter.lift_function` (loop por instrução) | `_midasm` hoisted para fora do loop e guardado por `if _midasm`; hooks `before` **depois** do `loc_XXXX:` e antes da linha traduzida, hooks `after` depois |
| `PPULifter.lift_function` (detecção de terminador) | Linhas de hook filtradas antes de `_last_line_is_terminator` |
| `PPULifter._preamble_lines` | Declarações `void gow2_midasm_<Name>(ppu_context* ctx);` dentro de `extern "C"` condicionado a `__cplusplus` |
| `_worker_init` / `_worker_lift` / `_parallel_lift` | Propagação para o lift paralelo |
| `main()` | Carregamento + contagem + `--help`; a guarda de cobertura corre onde `func_bounds` fica final, **antes** do lift |

## Provas (comandos e resultados reais)

### Os três testes do plano

```
$ .venv/bin/python3 tools/test_midasm_emit.py          -> rc=0   (50 asserções ok)
$ .venv/bin/python3 tools/test_recomp_config.py        -> rc=0
$ .venv/bin/python3 tools/test_invalid_instructions.py -> rc=0   (74 asserções ok)
```

Não-regressão adicional corrida na mesma leva, todas `rc=0`: `test_switch_tables`,
`test_ppu_lift`, `test_ppu_jumptables`, `test_ppu_bounds_repair`, `test_lift_parity`.

### O golden (must_have #2) — por `diff`, não por inspecção

A comparação é `difflib.unified_diff` entre `tools/testdata/midasm_golden_noconfig.c` e o `.c`
gerado agora sem `--config`, ambos com a linha `/* lifter-rev: <sha> */` normalizada (é um
carimbo de proveniência não-determinista por construção — corre `git rev-parse` e marca
`-dirty`). **Resultado: diff vazio.** Idem com `--config` presente mas sem secção `midasm`.

**Prova de que o golden não é vazio** (o teste discriminador): injectada a emissão de um hook
no caminho sem config e o teste morre com 4 asserções, incluindo os dois diffs —

```
[midasm] FAIL zero simbolos gow2_midasm_ no corpo
[midasm] FAIL diff vazio contra tools/testdata/midasm_golden_noconfig.c
[midasm] FAIL zero simbolos gow2_midasm_ no output sem --config
[midasm] FAIL diff vazio contra o golden (config sem seccao midasm)
```

Mutação revertida; `git status` limpo e `rc=0` reconfirmado depois.

### Propagação para o lift paralelo (must_have #3)

Duas guardas: chamada directa a `_worker_init`/`_worker_lift` (apanha o wiring) e um `mp.Pool`
**real** via `_parallel_lift` (apanha a dessincronização do tuplo posicional de `initargs`, que
a chamada directa não consegue apanhar por contornar a fonte do `_parallel_lift`).

**Prova de mutação:** removidos os hooks do tuplo de `initargs` →
`FAIL caminho paralelo emite o hook na posicao certa  i_hook=-1`. Revertido, `rc=0`.

### A TU gerada compila

Bloco de declarações emitido, extraído de um lift real com 2 hooks:

```
$ clang   -fsyntax-only -std=c11   ...  -> C ok
$ clang++ -fsyntax-only -std=c++17 ...  -> C++ ok
```

Corpo gerado (fixture sintética, hook `before` em `0x1004` e hook `after`+`return` em `0x1008`):

```c
void func_00001000(ppu_context* ctx) {
        ctx->gpr[3] = (int64_t)(int32_t)(1);
        gow2_midasm_IntroSeqWaitIdle(ctx);
        ctx->gpr[4] = (int64_t)(int32_t)(2);
        /* nop */;
        gow2_midasm_AfterAndReturn(ctx);
        return;
        return;   /* o blr traduzido, agora código morto — aceite e documentado na v1 */
}
```

## Decisões Tomadas

1. **`jump_address` é recusado (rc=2), não avisado.** O plano permitia "WARN **ou** hard-fail";
   escolhi recusar. Avisar-e-continuar deixaria no disco um lift que compila, corre, chama o
   hook e **não salta** — a classe de surpresa "declarado mas sem efeito" que o próprio
   `recomp_config.py` já rejeita com erro no `default` do `[[switch_table]]`. Seguir esse
   precedente do repo é mais seguro do que um warning que ninguém lê no meio de um lift longo.
2. **Índice `{EA: [hook, ...]}`, não last-wins.** Last-wins é a regra dos duplicados de
   `invalid_instructions`/`switch_table` porque lá a segunda entrada substitui mesmo um valor.
   Aqui não: dois hooks distintos no mesmo EA são legítimos.
3. **Duplicado exacto (`name`+`address`) é descartado com aviso.** Emiti-lo correria o efeito
   secundário a dobrar; "last-wins" e "manter os dois" não são equivalentes quando há efeitos.
4. **A guarda de cobertura corre no processo pai, contra `all_insns` + `func_bounds`.** Alternativa
   seria os workers reportarem o que emitiram, o que obrigava a mudar a forma de retorno de
   `_worker_lift` — e essa mudança partia `test_invalid_instructions.py`, que a desempacota.
   Como está, `-j1` e `-jN` dão o mesmo veredicto e falha em ~2 s em vez de ao fim do lift.
5. **O símbolo do hook não leva `--symbol-prefix`.** Os hooks são símbolos host versionados, não
   funções liftadas; prefixá-los partiria o link com o código de `games/gow2/hooks/` (17-02).

## Deviations from Plan

### Rule 2 — funcionalidade crítica em falta, adicionada

**1. Declarações dos hooks no preâmbulo da TU gerada**
- **Onde:** Task 1 (GREEN), `PPULifter._preamble_lines`.
- **Problema:** o plano especificava só a emissão da chamada. Uma chamada a função não declarada
  é erro em C++ e implicit-declaration em C99+ — o lift sairia a não compilar por construção,
  e o defeito só apareceria no build do 17-02.
- **Fix:** emitir `void gow2_midasm_<Name>(ppu_context* ctx);` dentro de `extern "C"`
  condicionado a `__cplusplus` (o mesmo preâmbulo serve o `.c` single-file e os chunks `.cpp`),
  **só quando há hooks** — para o golden sem `--config` não mexer.
- **Verificação:** `clang -std=c11` e `clang++ -std=c++17` sobre o bloco emitido, ambos ok.
- **Commit:** `a800471`.

**2. Linhas de hook excluídas de `_last_line_is_terminator`**
- **Onde:** Task 1 (GREEN), `PPULifter.lift_function`.
- **Problema:** `gow2_midasm_X(ctx);` não é um terminador. Um hook `after_instruction` no `blr`
  final fazia o lifter concluir que a função cai para a seguinte e emitir um trampolim de
  fallthrough que não existia. Declarar um hook mudaria o **fluxo de controlo** da função —
  exactamente o oposto de "o hook corre ao lado da instrução".
- **Fix:** filtrar as linhas de hook (e só essas — o `return;` de `return=true` continua a contar,
  porque é um terminador a sério) antes da detecção, e só quando há hooks.
- **Verificação:** `test_after_hook_does_not_flip_the_terminator_detection`.
- **Commit:** `a800471`.

**3. Validação do `name` como identificador C e recusa de `address` negativo**
- **Onde:** Task 1 (GREEN), `build_midasm_index`.
- **Problema:** o parser não valida nenhum dos dois. Um `name` com hífen ou espaço gerava uma TU
  que não compila, com o erro a apontar para código gerado em vez de para o TOML.
- **Fix:** recusa com mensagem que nomeia o TOML.
- **Commit:** `a800471`.

### Desvio de forma (não de conteúdo)

**4. Task 2 não teve commit próprio**
- Os dois entregáveis da Task 2 (linha de contagem `Config: N midasm_hook(s) loaded from ...` e
  actualização do `--help`) vivem no mesmo bloco de código que a Task 1 GREEN. Separá-los exigia
  um commit intermédio em que o CLI carrega hooks sem os reportar — um estado deliberadamente
  pior. Ambos os critérios de aceitação da Task 2 estão verificados:
  `test_cli_with_midasm_emits_the_count` (asserção sobre `1 midasm_hook(s)` no stdout) e
  `test_invalid_instructions.py` `rc=0` (sem regressão no caminho `--config` partilhado).

---

**Total de desvios:** 3 auto-fixes (Rule 2 ×3) + 1 desvio de forma.
**Impacto:** nenhum alargamento de âmbito. Os três auto-fixes são condições de correcção do que
o plano pedia (o lift tem de compilar; declarar um hook não pode mudar o fluxo de controlo).

## Sobre os must_haves — estado honesto

| # | Truth | Estado |
|---|---|---|
| 1 | `test_midasm_emit.py` PASS cobrindo before / after / return | **Cumprido** — `rc=0`, 50 asserções, com assertions de **posição** (índice adjacente), não de substring |
| 2 | Sem `--config`, output byte-a-byte idêntico, por `diff` | **Cumprido** — diff vazio contra golden capturado com o lifter pré-mudança; **e** provado discriminante por mutação. Ressalva declarada: a linha `/* lifter-rev */` é normalizada, porque é não-determinista por construção |
| 3 | Hooks sobrevivem ao `-j` pelo mesmo caminho do `invalid_instructions` | **Cumprido** — 2 guardas, uma com `mp.Pool` real; provado por mutação do tuplo de `initargs` |
| 4 | `jump_address` NÃO implementado; WARN ou hard-fail | **Cumprido, na forma mais forte** — hard-fail `rc=2`, sem lift escrito |

**O que NÃO foi feito e não é dado por feito:**

- **O contrato de recusa está implementado na forma fraca possível hoje.** O 17-CONTEXT pede que
  "se a instrução-âncora no EA declarado não for a esperada, o lifter falha ruidosamente". O que
  está implementado é: falha se **não existir** instrução no EA (ou se o EA nunca for liftado, ou
  cair num stub HLE). **Não** verifica que a instrução é a *esperada* — o schema de
  `[[midasm_hook]]` não tem campo para declarar a instrução-âncora, portanto não há nada com que
  comparar. Estender o schema estava fora do âmbito da v1. Registado como limitação em
  `docs/RECOMP_CONFIG.md`, não dado por cumprido.
- **Nada foi exercitado in-boot.** Todos os aceites são `offline-unit`. Nenhum hook host existe
  ainda (17-02), nenhum re-lift do CE03C foi feito (17-03), nenhum smoke do GoW2 foi corrido.
- **XEN-06 fica PARCIAL.** Só a metade midasm da documentação foi fechada. `functions_override` e
  `[optimizations]` continuam parseados e ignorados — documentados como tal, que é a forma honesta.
- **`games/gow2/config/gow2_recomp.toml` não foi tocado.** O comentário que lá diz que
  `midasm_hook` entra "na Task A3" fica para o 17-03, que é quem declara o hook do CE03C.

## Issues Encountered

- **O output do lifter não é determinista de raiz:** o preâmbulo carrega `/* lifter-rev: <sha> */`,
  gerado por `git rev-parse` (e com sufixo `-dirty`). Um golden de ficheiro inteiro mudaria a cada
  commit. Resolvido com **uma** normalização, estreita e documentada, dessa linha; todo o resto do
  ficheiro entra no diff literalmente.
- **A guarda de cobertura queria informação que vive nos workers.** Reportá-la de volta obrigava a
  mudar o retorno de `_worker_lift`, que `test_invalid_instructions.py` desempacota posicionalmente
  — partir uma suite verde para ganhar precisão que a verificação no processo pai já dá seria mau
  negócio. Ficou no pai.

## Restrições do projecto respeitadas

- Commits em português, sem `Co-Authored-By`, `git add` com paths explícitos (nunca `-A`/`.`).
- Zero dados de jogo: a fixture são 4 palavras PPC sintéticas; o lift de verificação foi para `/tmp`.
- Trabalho na árvore principal, sem worktree.
- Nenhum ficheiro da outra sessão activa tocado (`PROJECT.md`, `MILESTONES.md`, `CLAUDE.md`,
  `games/gow2/CLAUDE.md`, `.planning/milestones/v1.2-*`, `.planning/config.json`,
  `GSD-MODEL-ROUTING.md`, `scripts/gsd_plan*.sh`). **`ROADMAP.md` também NÃO foi tocado**: está
  modificado no working tree pela outra sessão e editá-lo arriscava commitar trabalho alheio
  (regra 7 do CLAUDE.md). A actualização do progresso da Fase 17 no ROADMAP fica por fazer,
  declarado aqui em vez de silenciado.
- Windows não regride (G4): mudança é Python + C gerado com `#ifdef __cplusplus`; zero tokens Win32.

## Next Phase Readiness

**Pronto para o 17-02** (hooks host em `games/gow2/hooks/`): o contrato do símbolo está fixo e
verificado — `void gow2_midasm_<Name>(ppu_context* ctx)`, `extern "C"`, sem prefixo de símbolo. O
lifter já declara o protótipo na TU gerada, por isso o header host só precisa de bater com esta
assinatura. Todos os hooks declarados no TOML precisam de corpo (stub no-op incluído), senão o
link falha — isso é intencional e é o que o 17-02 tem de cobrir.

**Para o 17-03** (migração do CE03C): declarar `[[midasm_hook]]` com o EA `0x000CE03C` e re-liftar
com `--config`. Se o EA estiver errado, o lifter aborta com `rc=2` e diz porquê, em vez de produzir
um lift em silêncio sem o fix.

---
*Fase: 17-mid-asm-de-ponta-a-ponta*
*Concluído: 2026-08-02*

## Self-Check: PASSED

Verificado a 2026-08-02 (ficheiros no disco, commits em `git log`, contagem real de asserções):

- 6/6 ficheiros declarados existem (`tools/test_midasm_emit.py`,
  `tools/testdata/midasm_golden_noconfig.c`, `tools/testdata/README.md`, `tools/ppu_lifter.py`,
  `docs/RECOMP_CONFIG.md`, este SUMMARY).
- 3/3 commits existem: `0d670d7`, `a800471`, `a955c78`.
- `tools/test_midasm_emit.py` -> `rc=0`, **50** asserções `ok` (o número citado no SUMMARY foi
  contado, não estimado).
- `git status --porcelain -- tools/ docs/` vazio: as mutações de prova foram todas revertidas.
