---
phase: 21-endurecimento-metricas-e-fecho
plan: 01
subsystem: docs
tags: [relift, ppu_lifter, verify_lift, accept_relift, midasm, weak-overrides, switch-tables, gow2, macos]

# Dependency graph
requires:
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "PATCH_MIGRATION.tsv (ledger patch->mecanismo), baseline verify_lift rc=0, baseline smoke st620_max=11, achado 'sticky NAO-OBSERVAVEL'"
  - phase: 17-mid-asm-de-ponta-a-ponta
    provides: "ppu_lifter --config + [[midasm_hook]]; a armadilha do switch_table_file medida no 17-03"
  - phase: 18-jump-tables-detectadas-e-declaradas
    provides: "switch_table_file activo no TOML de producao; 2 patch_jumptable_* classificados redundant"
  - phase: 19-weak-overrides-e-as-migracoes-que-fecham-o-marco
    provides: "emit_weak_wrappers = true no TOML de producao; escada A/B/C; 4 linhas migrated"
  - phase: 20-o-oraculo-e-o-metodo
    provides: "4o passo opt-in do verify_lift (VERIFY_ORACLE=1), por delta"
provides:
  - "docs/RELIFT_CANONICAL.md — as 8 etapas canonicas do re-lift, com comandos, criterios por etapa e nivel de evidencia declarado"
  - "Evidencia file:line de que accept_relift.sh JA invocava verify_lift.sh antes do marco v1.3 (perna 2)"
  - "Facto operacional novo (medido): o default do PS3_ENGINE_ROOT so resolve a partir do checkout de build"
  - "Facto operacional novo (medido): ../gow2-recomp/verify_lift.sh e a copia pre-Fase-20; o caminho de aceite usa sempre a copia do motor"
  - "Ponteiros de descoberta em games/gow2/README.md e docs/MACOS_PORT_PLAN.md"
affects: [21-02, 21-03, auditoria-do-marco-v1.3, fase-11-as-quatro-paredes]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Documento canonico versionado no motor (docs/), com ponteiros a partir dos pontos de entrada dos operadores"
    - "Tabela de nivel de evidencia por afirmacao (medido-nesta-fase / medido-noutra-fase / NAO-EXERCITADO) dentro do proprio documento"

key-files:
  created:
    - docs/RELIFT_CANONICAL.md
  modified:
    - games/gow2/README.md
    - docs/MACOS_PORT_PLAN.md

key-decisions:
  - "Documento dedicado (docs/RELIFT_CANONICAL.md) em vez de enterrar a sequencia no MACOS_PORT_PLAN.md ou no results doc do 21-02 — a discricao estava aberta no CONTEXT e o criterio foi 'os operadores precisam de um caminho estavel'"
  - "A formulacao 'passo 4: so PROBE/LEGACY' do plano de origem foi documentada como ESTADO-ALVO, nao como o de hoje: medido agora, 72 patches CORRECTNESS continuam activos e continuam a ser reaplicados"
  - "Nao se tocou no accept_relift.sh: a perna 2 ja la estava e nao havia defeito medido para justificar reescrita"
  - "O ponteiro no MACOS_PORT_PLAN.md marca os passos 2-3 da Fase 4 como DESACTUALIZADOS em vez de os reescrever — o documento e um diagnostico datado de 2026-07-19 e reescreve-lo apagaria o registo historico"

patterns-established:
  - "Atribuicao honesta: o que ja existia e citado com file:line e rotulado 'ja la estava', nunca apresentado como entrega da fase"
  - "Cada afirmacao operacional do documento tem nivel de evidencia declarado, incluindo a admissao de que a sequencia 1->8 nunca foi corrida de ponta a ponta"

requirements-completed: [LEDG-02]

coverage:
  - id: D1
    description: "docs/RELIFT_CANONICAL.md lista as 8 etapas canonicas do re-lift, por ordem, com comandos copy-paste"
    requirement: "LEDG-02"
    verification:
      - kind: other
        ref: "rg -c '^### Etapa ' docs/RELIFT_CANONICAL.md  -> 8"
        status: pass
    human_judgment: false
  - id: D2
    description: "Evidencia registada de que accept_relift.sh ja invocava verify_lift.sh na perna 2 (pre-existente, nao construido nesta fase)"
    requirement: "LEDG-02"
    verification:
      - kind: other
        ref: "rg -n 'verify_lift\\.sh|PERNA 2|leg2_rc' games/gow2/accept_relift.sh -> :72,:73,:74-77,:129-130,:262"
        status: pass
      - kind: other
        ref: "bash -n games/gow2/accept_relift.sh -> rc=0"
        status: pass
    human_judgment: false
  - id: D3
    description: "O documento regista que o TOML de producao ja tem emit_weak_wrappers = true (Fase 19) e a armadilha do switch_table_file"
    verification:
      - kind: other
        ref: "rg -n 'emit_weak_wrappers|switch_table_file' games/gow2/config/gow2_recomp.toml -> :24, :23"
        status: pass
    human_judgment: false
  - id: D4
    description: "Ponteiros de descoberta a partir de games/gow2/README.md e docs/MACOS_PORT_PLAN.md"
    verification:
      - kind: other
        ref: "rg -l 'RELIFT_CANONICAL' games/gow2/README.md docs/MACOS_PORT_PLAN.md -> ambos"
        status: pass
    human_judgment: false
  - id: D5
    description: "A sequencia documentada e operacionalmente correcta e suficiente para um re-lift real"
    verification: []
    human_judgment: true
    rationale: "Nenhuma corrida 1->8 de ponta a ponta foi feita nesta fase (custa horas de lift + build + 6 smokes). Cada etapa tem evidencia isolada das Fases 16-20, mas a sequencia inteira como unidade continua NAO-EXERCITADA — declarado na §7 do proprio documento."

# Metrics
duration: 21min
completed: 2026-08-02
status: complete
---

# Phase 21 Plan 01: Sequência canónica de re-lift e evidência do LEDG-02 Summary

**`docs/RELIFT_CANONICAL.md` fixa as 8 etapas do re-lift com comandos e critérios por etapa, e regista com `file:line` que a perna 2 do `accept_relift.sh` (`verify_lift`) já lá estava antes do marco v1.3 — verificada, não construída.**

## Performance

- **Duração:** ~21 min
- **Tarefas:** 3 de 3
- **Ficheiros:** 1 criado, 2 modificados
- **Código de produto tocado:** nenhum (`git diff --name-only HEAD~2 HEAD` só devolve docs)

## Accomplishments

- **Sequência canónica versionada e encontrável.** `docs/RELIFT_CANONICAL.md` (8 etapas do §9/M5.1 do plano de origem) com comandos copy-paste, critério de cada etapa, e a tabela das 4 pernas do aceite. Cada etapa aponta para o script real e para a linha que implementa o critério.
- **Metade "wiring" do `LEDG-02` verificada e atribuída honestamente.** Cinco pontos de evidência no `accept_relift.sh` — `:72` (resolução do `PS3_ENGINE_ROOT`), `:73` (o path do `verify_lift.sh`), `:74-77` (ausente ⇒ `exit 2`), `:129-130` (invocação + `leg2_rc`), `:262` (`leg2_rc` no AND final). O documento diz explicitamente que isto é pré-existente.
- **Dois factos operacionais novos, medidos nesta fase**, que nenhum SUMMARY anterior tinha:
  - o default `PS3_ENGINE_ROOT="${PS3_ENGINE_ROOT:-$REPO/../ps3recomp}"` só resolve a partir do **checkout de build**; a partir de `ps3recomp/games/gow2/` resolve para `games/gow2/../ps3recomp/...`, que não existe, e o `accept_relift.sh` sai `rc=2`;
  - `../gow2-recomp/verify_lift.sh` (5803 B) é a cópia **anterior à Fase 20**, sem `VERIFY_ORACLE`. O caminho de aceite nunca a usa (resolve sempre pela cópia do motor via `PS3_ENGINE_ROOT`), mas um operador que corra `./verify_lift.sh` dentro do checkout de build apanha a versão antiga.
- **Realidades de produção registadas onde quem re-lifta as lê:** `emit_weak_wrappers = true` (`gow2_recomp.toml:24`) e a armadilha do `switch_table_file` (`:23`) que mata o `--config` **inteiro** — logo os `[[midasm_hook]]` também — quando o TOML de switch tables não viaja junto.
- **`sticky_pub >= 1` declarado não-verificável**, com a medição refeita: `ps3_fios_sticky_publish/_peek/_consume` em `runtime/ppu/ppu_loader.cpp:1370/:1383/:1393`, **zero `fprintf`** com `sticky` em `runtime/` e `libs/`.

## Task Commits

1. **Task 1: Verificar o wiring `accept_relift` → `verify_lift`** — sem commit (tarefa de **medição**, zero ficheiros alterados; a evidência foi para o documento da Task 2 e para este SUMMARY)
2. **Task 2: Escrever `docs/RELIFT_CANONICAL.md`** — `58481ec` (docs)
3. **Task 3: Ponteiros no README e no MACOS_PORT_PLAN** — `eed7376` (docs)

## Files Created/Modified

- `docs/RELIFT_CANONICAL.md` *(criado)* — as 8 etapas, os dois checkouts e a resolução do `PS3_ENGINE_ROOT`, a evidência da perna 2, as 4 realidades de produção medidas, o que **não** serve de critério (`sticky`), higiene G6, âmbito, e a §7 com o nível de evidência de cada afirmação.
- `games/gow2/README.md` *(modificado)* — nova secção **"Re-lifting production — read this first"**: o esboço de 4 passos do "How to rebuild" **não** é o caminho de aceite; ponteiro para o documento canónico. Em inglês, para não partir a língua do ficheiro.
- `docs/MACOS_PORT_PLAN.md` *(modificado)* — nota na Fase 4 marcando os passos 2–3 ("lift sem `--config`", "reaplicar `patch_*.py` à mão") como **desactualizados desde o marco v1.3**, com ponteiro. O resto do documento (diagnóstico de 2026-07-19) ficou intacto.

## Decisions Made

- **Documento dedicado, não um parágrafo enfiado noutro ficheiro.** A discrição estava aberta no `21-CONTEXT.md` (`docs/MACOS_PORT_PLAN.md` vs `games/gow2/README` vs documento próprio). Escolhido documento próprio: os operadores precisam de um caminho estável, e o results doc do 21-02 é um relatório de marco, não uma página de operação.
- **"Só PROBE/LEGACY" documentado como estado-alvo, não como presente.** Medido agora no ledger: 140 linhas de dados, **4 `migrated`**, **2 `redundant`**, **134 `todo`**, **72 de classe `CORRECTNESS` ainda activos**. O `SKIP_LIST` do `apply_all_patches.sh` está vazio — corre tudo. O que o v1.3 garante é que aqueles 6 já não dependem do `apply_all_patches.sh` para sobreviver (degrau C da escada A/B/C: `WEAK`/skip, `sha256` idêntico, `19-03-SUMMARY.md` §3). Escrever "só PROBE/LEGACY" sem esta ressalva seria uma afirmação falsa sobre o estado de hoje.
- **`accept_relift.sh` não foi tocado.** A pré-condição do plano só autorizava mexer se houvesse um defeito medido. Não há: o wiring está correcto e o `bash -n` sai `rc=0`.
- **O `MACOS_PORT_PLAN.md` foi anotado, não reescrito.** É um diagnóstico datado; reescrever os passos apagava o registo histórico do que se sabia em 2026-07-19.

## Deviations from Plan

Nenhuma no sentido das regras 1–4 — nenhum bug, nenhuma funcionalidade crítica em falta, nenhum bloqueio. Três **precisões factuais** face ao que o PLAN/CONTEXT assumia, resolvidas escrevendo o facto medido em vez do assumido:

1. **O `accept_relift.sh` tem QUATRO pernas, não três.** O `21-CONTEXT.md` diz "3 pernas + contadores" e cita o ficheiro com **221 linhas**; o ficheiro tem **268** e a perna 4 (`smoke_chain_gate.sh`, GATE-03, Fase 7) existe desde 2026-07-30. A âncora `:73` do CONTEXT está **correcta** — é a linha do `VERIFY_LIFT=`. O documento descreve as quatro.
2. **A cópia do `verify_lift.sh` no checkout de build está desactualizada** (sem o passo de oráculo da Fase 20). Não é regressão do caminho de aceite — que usa sempre a cópia do motor — mas é uma armadilha real para quem correr o script à mão. Registada no documento, §0. Não foi "corrigida" aqui: sincronizar o espelho é dívida do v1.0 e está explicitamente diferida no `21-CONTEXT.md`.
3. **O `accept_relift.sh` do espelho e do monorepo são byte-idênticos** (`diff -q` limpo, 13641 B), tal como os dois TOML de configuração. Só o `verify_lift.sh` diverge. Medido, não assumido.

**Impacto no plano:** nenhum. Nada de scope creep; nenhum ficheiro fora dos três declarados em `files_modified`.

## Issues Encountered

Nenhum bloqueio. Nota de coordenação: o plano 21-02 corre em paralelo e escreve `games/gow2/notes/milestone-2026-08-xenon-re-results.md`. Esse ficheiro **não foi tocado** (confirmado em `git diff --name-only HEAD~2 HEAD`); o `RELIFT_CANONICAL.md` referencia-o como destino do handoff — referência para a frente, sem dependência de build.

## Known Stubs

Nenhum stub de código. Uma lacuna de **exercício**, declarada dentro do próprio documento (§7) e não escondida:

| Item | Estado |
|---|---|
| A sequência de 8 etapas corrida **1→8 de ponta a ponta, numa só passagem** | **NÃO-EXERCITADA** |

Cada etapa tem evidência isolada das Fases 16–20 (`verify_lift` `rc=0`, escada A/B/C, `st620_max=11`), mas ninguém correu a cadeia inteira para produzir esta página — custa um lift completo + build + 6 execuções de smoke. Declarado no documento e aqui, em vez de presumido verde (G5 / regra 4 do `CLAUDE.md`).

## O que foi de facto exercitado vs só documentado

| Afirmação | Nível de evidência |
|---|---|
| `accept_relift.sh` invoca `verify_lift.sh` na perna 2 (`:73`/`:129`/`:262`) | **medido nesta fase** (`rg`, leitura do ficheiro) |
| `bash -n` limpo em `accept_relift.sh` e `verify_lift.sh` | **medido nesta fase** (`rc=0` nos dois) |
| Resolução do `PS3_ENGINE_ROOT` nos dois checkouts | **medido nesta fase** (`test -x` nos dois paths derivados) |
| `verify_lift.sh` do espelho está desactualizado; `accept_relift.sh` e os TOML são idênticos | **medido nesta fase** (`diff`, `rg -c VERIFY_ORACLE`) |
| `emit_weak_wrappers = true` e `switch_table_file` no TOML de produção | **medido nesta fase** (`rg`, `:24` e `:23`) |
| Ledger: 140 / 4 `migrated` / 2 `redundant` / 134 `todo` / 72 `CORRECTNESS` activos | **medido nesta fase** (`awk` sobre `PATCH_MIGRATION.tsv`) |
| `sticky` sem emissor (0 `fprintf`) | **medido nesta fase** (reconfirma a Fase 16) |
| `verify_lift.sh` `rc=0` contra o lift de produção | **medido nas Fases 16 e 20** — não re-corrido aqui |
| Escada A/B/C, degrau C ⇒ `sha256` idêntico | **medido na Fase 19** — não re-corrido aqui |
| `st620_max=11` no smoke da intro | **medido in-boot na Fase 16** — não re-corrido aqui |
| A sequência 1→8 numa só corrida | **NÃO-EXERCITADA** |

## Next Phase Readiness

- **21-02** (suite T1–T10 com `rc` real) pode citar a §2 deste documento para a linha "`verify_lift` no caminho de aceite = **já estava**" da tabela antes/depois, sem repetir a medição.
- **21-03** (results doc / handoff) tem aqui a metade documental do `LEDG-02` fechada e as ressalvas já escritas em forma citável.
- **Pendente para o próximo marco** (não bloqueia o fecho): sincronizar `../gow2-recomp/verify_lift.sh` com a cópia do motor (dívida do v1.0, diferida no CONTEXT); e uma corrida 1→8 de ponta a ponta que promova a §7 de "NÃO-EXERCITADA" para "medido".

## Self-Check: PASSED

- Ficheiros afirmados: `docs/RELIFT_CANONICAL.md`, `games/gow2/README.md`, `docs/MACOS_PORT_PLAN.md` — todos presentes.
- Commits afirmados: `58481ec`, `eed7376` — ambos em `git log`.
- Números citados re-medidos: `accept_relift.sh` = 268 linhas; espelho vs monorepo = 13641 B nos dois; `../gow2-recomp/verify_lift.sh` = 5803 B.

---
*Phase: 21-endurecimento-metricas-e-fecho / Plan 01*
*Completed: 2026-08-02*
