---
phase: 16-invent-rio-de-patches-e-baseline-medido
plan: 01
subsystem: tooling
tags: [tsv, catalogo, ledger, patches, lift, python, gow2]

requires:
  - phase: 04 (catalogo de patches)
    provides: gen_catalog.py + PATCH_CATALOG.tsv + MANIFEST.tsv
provides:
  - PATCH_CATALOG.tsv regenerado contra o corpus REAL de build (139 patches, era 89)
  - gen_migration_ledger.py (derivador do ledger, com --merge idempotente)
  - PATCH_MIGRATION.tsv esqueleto (139 linhas, 7 colunas LEDG-01 + 4 auxiliares)
  - contagens de inventario congeladas e medidas no cabecalho do ledger
affects: [16-02 (classificacao humana P0), 16-03 (baseline verify/smoke), 17-21 (migracoes)]

tech-stack:
  added: []
  patterns:
    - "fronteira derivado/editado declarada no proprio ficheiro (gerador irmao, nao extensao)"
    - "coluna de medicao so' aceita valor CONFIRMADO; palpite vai para coluna de pista separada"

key-files:
  created:
    - games/gow2/lift_baseline/gen_migration_ledger.py
    - games/gow2/lift_baseline/test_gen_migration_ledger.py
    - games/gow2/lift_baseline/PATCH_MIGRATION.tsv
  modified:
    - games/gow2/lift_baseline/PATCH_CATALOG.tsv
    - games/gow2/lift_baseline/test_gen_catalog.py

key-decisions:
  - "O universo do ledger e' o corpus de build (139); o unico patch so'-no-monorepo nao ganha linha, fica declarado no cabecalho"
  - "EA so' entra na coluna `ea` quando confirmada pelo corpo do script; o palpite do nome vai para `ea_pista_nome` (coluna nova, nao prevista no plano)"
  - "gen_catalog.py NAO foi alterado: os 4 ramos de classificacao absorveram +50 patches sem indeterminados"

patterns-established:
  - "Ledger regeneravel: --merge preserva as 4 colunas humanas e recalcula as derivadas; regen sobre o proprio ficheiro e' byte-a-byte idempotente"
  - "Cabecalho TSV congela contagens MEDIDAS na corrida e marca explicitamente o que ainda esta' [nao-exercitado]"

requirements-completed: []  # LEDG-01 fica PARCIAL: falta 16-02 (decisao humana) e 16-03 (baseline medido)

duration: ~35min
completed: 2026-08-02
status: complete
---

# Phase 16 Plano 01: Inventário de patches — catálogo e esqueleto do ledger

**O catálogo passou a ver o corpus que o build realmente aplica (89 → 139 patches, 50 estavam invisíveis) e existe agora um `PATCH_MIGRATION.tsv` regenerável com a classe derivada por join, defaults seguros e as contagens medidas congeladas no cabeçalho.**

## Performance

- **Duração:** ~35 min
- **Tasks:** 2/2
- **Ficheiros modificados/criados:** 5 (3 criados, 2 modificados)
- **Commits:** 3 (1 tracer, 1 RED, 1 GREEN)

## Números REAIS medidos (2026-08-02, nível de evidência: **offline-unit**)

Todos medidos nesta sessão pelos comandos indicados — nenhum copiado do plano.

| Métrica | Valor | Comando |
|---|---|---|
| `patch_*.py` no corpus de build | **139** | `ls ../gow2-recomp/recomp_mid_v2/patch_*.py \| wc -l` |
| `patch_*.py` no espelho monorepo | **88** | `ls games/gow2/recomp_mid_v2/patch_*.py \| wc -l` |
| Divergência build − monorepo | **51** | derivada |
| Patches só no monorepo (sem linha no ledger) | **1** (`patch_freelist_guard_262610.py`) | `comm -23` das duas listas |
| `PATCH_CATALOG.tsv` — antes | **89** (64 FUNCIONAL / 25 PROBE) | cabeçalho anterior |
| `PATCH_CATALOG.tsv` — depois | **139** (87 FUNCIONAL / 52 PROBE), zero indeterminados | `grep -cE '^patch_'` |
| `PATCH_MIGRATION.tsv` — linhas de dados | **139** | `grep -cE '^patch_'` |
| Ledger por classe | **CORRECTNESS=77 / PROBE=50 / OPD=10 / OBSOLETE=2** (BUILD=0) | cabeçalho gerado |
| EA **confirmada pelo corpo** do script | **78/139** | teste 4 |
| EA não confirmada, com pista do nome | **14** (`ea_pista_nome`) | teste 4 |
| Sem EA e sem pista | **47** | `awk` sobre o TSV |
| `test_gen_catalog.py` | **rc=0**, 11 PASS | corrida directa |
| `test_gen_migration_ledger.py` | **rc=0**, 7 PASS | corrida directa |

**Ainda NÃO medido (nível de evidência: não-exercitado):** `rc` do `verify_lift.sh`,
`st620` máximo, `sticky`, OOB pós-TLS. São o plano **16-03**; o cabeçalho do ledger
regista-os literalmente como `POR MEDIR ... [nivel de evidencia: nao-exercitado]` em vez
de deixar campos que pareçam preenchidos.

## Accomplishments

- **`PATCH_CATALOG.tsv` regenerado contra `../gow2-recomp/recomp_mid_v2`** (o corpus que
  `build_macos.sh`/`apply_all_patches.sh` aplicam), não o espelho subtree. Os 50 patches
  das Fases 9–11 que eram invisíveis ao catálogo passaram a estar classificados.
  `gen_catalog.py` **não** precisou de alteração: os 4 ramos (D-4.3/D-4.4) absorveram o
  corpus novo com zero indeterminados.
- **`gen_migration_ledger.py`**: irmão de `gen_catalog.py`, não extensão. A coluna
  `classe` é derivada por join com o catálogo (mapa de 16-RESEARCH); uma classe de
  catálogo desconhecida levanta `ValueError` e um patch do corpus ausente do catálogo
  aborta com instrução de regeneração — nunca há default silencioso.
- **`PATCH_MIGRATION.tsv`** com as 7 colunas de LEDG-01 (`patch`, `classe`, `alvo`, `ea`,
  `prioridade`, `aceite`, `estado`) mais 4 auxiliares (`ea_pista_nome`,
  `subclasse_catalogo`, `so_no_build`, `marcador`), defaults seguros e cabeçalho com as
  contagens medidas, o domínio de cada coluna e a regra do `aceite` executável.
- **Regeneração é um comando e é idempotente:** `--merge` sobre o próprio ficheiro
  produziu saída byte-a-byte igual (`diff -q` limpo), e o teste prova que um
  `prioridade=P0` editado à mão sobrevive ao regen enquanto `classe`/`ea` continuam a ser
  recalculadas.

## Task Commits

1. **Task 1 (tracer): regenerar PATCH_CATALOG contra o corpus de build** — `9452cad` (feat)
2. **Task 2 RED: teste antes do gerador** — `0ed8ba5` (test)
3. **Task 2 GREEN: gerador + ledger** — `45c6e55` (feat)

Gate TDD cumprido: `test(...)` (RED observado: `ModuleNotFoundError`) → `feat(...)` (GREEN,
7 PASS). Sem fase REFACTOR (não houve limpeza a fazer sem alterar comportamento).

## Files Created/Modified

- `games/gow2/lift_baseline/PATCH_CATALOG.tsv` — regenerado: 89 → 139 linhas de dados.
- `games/gow2/lift_baseline/test_gen_catalog.py` — removido o literal `(medido: 87)` da
  mensagem de cobertura (ver desvio 1).
- `games/gow2/lift_baseline/gen_migration_ledger.py` — derivador do ledger (mapa de
  classes, heurística de EA em 6 ramos, `--merge`, cabeçalho de baseline).
- `games/gow2/lift_baseline/test_gen_migration_ledger.py` — 7 verificações.
- `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` — esqueleto de 139 linhas.

Nada em `runtime/`, `libs/`, `tools/`, nem qualquer dado de jogo. Todos os `git add` com
paths explícitos. Os ficheiros da outra sessão (`.planning/PROJECT.md`,
`.planning/MILESTONES.md`, `.planning/milestones/v1.2-*.md`, `CLAUDE.md`) ficaram
intocados e fora dos commits — confirmado com `git status --short` no fim.

## Decisions Made

1. **O único patch só-do-monorepo não ganha linha.** `patch_freelist_guard_262610.py`
   existe em `games/gow2/recomp_mid_v2` mas não no corpus de build. Como o ledger serve
   para decidir o que migrar do que o build aplica, dar-lhe linha inflacionaria o
   denominador da métrica da Fase 21. Fica declarado no cabeçalho pelo nome.
2. **`so_no_build=1` significa "existe no build e não no espelho"** (52 dos 139) — é a
   forma de a divergência do subtree ficar consultável por linha, não só no cabeçalho.
3. **`BUILD` fica reservado e sem ocorrências.** Nenhum patch do corpus mexe só em
   build/scripts; forçar a classe seria inventar população.
4. **Os `alvo`/`prioridade`/`aceite` das linhas `CORRECTNESS`/`OPD` nascem vazios.**
   Preenchê-los por heurística seria dar por feita a decisão humana da 16-02.

## Deviations from Plan

### Auto-fixed Issues

**1. [Regra 2 — honestidade de aceite (CLAUDE.md G5/regra 4)] Mensagem de teste com `87` congelado**
- **Encontrado em:** Task 1
- **Problema:** `test_gen_catalog.py` imprimia `... == 139 (medido: 87)` — a asserção é
  dinâmica, mas a linha impressa contradizia-se e podia ser lida como "o corpus tem 87".
- **Fix:** o literal passou a estar rotulado como linha de base histórica de 2026-07-26,
  com a frase "o numero acima e' o medido AGORA".
- **Verificação:** `test_gen_catalog.py` rc=0, 11 PASS.
- **Committed em:** `9452cad`.

**2. [Regra 1 — bug] Heurística de EA falhava por diferença de maiúsculas**
- **Encontrado em:** Task 2 (primeira corrida GREEN, teste vermelho)
- **Problema:** o ramo que correlaciona o token hex do nome com o corpo comparava
  `f"func_{PAD}"` (prefixo minúsculo, sufixo maiúsculo) contra `src.upper()`, pelo que
  nunca casava. `patch_fallthrough_2550c8.py` caía no ramo seguinte e devolvia
  `0x00255178` — o **alvo** do fallthrough em vez da função corrigida.
- **Fix:** comparar `f"FUNC_{PAD}"` contra `src.upper()`.
- **Verificação:** as 5 âncoras (2550C8, 32E200, 1E7B50, CE03C, 14B1F0) passaram a bater.
- **Committed em:** `45c6e55`.

### Adição fora do plano

**3. [Regra 2 — evitar número forjado] Coluna `ea_pista_nome`**
- **Contexto:** a heurística do plano deixava 61 linhas com `ea='-'`. Muitas dessas têm um
  token hexadecimal no nome (`patch_254610_empty_list_gate.py`) que o corpo do script
  **nunca menciona** — o script ancora numa fatia de código ou num vizinho. Escrever
  `0x00254610` na coluna `ea` seria apresentar um palpite como medição (regra 4 do
  CLAUDE.md).
- **Decisão:** `ea` só aceita EA confirmada pelo corpo (78/139); o palpite do nome vai
  para a coluna auxiliar `ea_pista_nome` (14 linhas), rotulada no cabeçalho como "NAO
  confirmado — verificar antes de usar". A pista nunca duplica uma EA já confirmada.
- **Porque é legítimo:** o plano deixa a ordem/conjunto de colunas auxiliares à
  discrição ("ordem livre desde que as 7 existam"), e isto dá à 16-02 informação
  accionável sem falsificar a coluna medida.
- **Committed em:** `45c6e55`.

**Total de desvios:** 3 (1 Regra 1, 2 Regra 2). **Impacto:** nenhum alargamento de
escopo; os três aumentam a honestidade ou a correcção do artefacto.

### Desvio de processo

**Gate de feedback do tracer não pausou para verificação humana.** O modo do projecto é
`interactive` (`auto_advance: false`), o que normalmente implicaria parar após a Task 1 e
devolver um `checkpoint:human-verify`. Continuei porque (a) o utilizador pediu
explicitamente a execução completa do plano e a escrita do SUMMARY, e (b) o `<verify>` do
tracer é inteiramente automatizado e passou (`139 == 139`, `test_gen_catalog.py` rc=0) —
não havia julgamento visual/humano por fazer. Fica registado para não passar por
implícito.

## Issues Encountered

Nenhum bloqueio. O único vermelho foi o do TDD (esperado) mais o bug de maiúsculas da
heurística de EA, resolvido dentro da mesma task.

## Must-haves do plano — estado

| Must-have | Estado | Evidência |
|---|---|---|
| Catálogo classifica o corpus REAL de build; nº de linhas == nº de `patch_*.py` | ✅ | 139 == 139 (offline-unit) |
| `PATCH_MIGRATION.tsv` versionado com as 7 colunas obrigatórias | ✅ | teste 6; `grep` do cabeçalho de colunas |
| `classe` derivada por join com o catálogo, nunca copiada | ✅ | `map_classe()` + teste 1 (4 ramos, domínio fechado) |
| Cabeçalho congela N monorepo / N build / N catálogo e declara a divergência | ✅ | teste 6 e cabeçalho do TSV (88 / 139 / 139 / 51) |

**LEDG-01 fica PARCIAL, por design do faseamento:** este plano entrega o inventário; a
classificação humana (≥15 linhas, ≥5 P0) é a 16-02 e o baseline medido de
`verify_lift`/smoke é a 16-03. O requisito não é dado por concluído aqui.

## User Setup Required

Nenhum.

## Known Stubs

Nenhum stub de código. Há campos **deliberadamente vazios** no ledger, todos declarados no
cabeçalho como decisão humana pendente da 16-02:

- 87 linhas (`CORRECTNESS`/`OPD`) com `alvo`, `prioridade` e `aceite` vazios.
- A linha `# baseline verify/smoke: POR MEDIR (plano 16-03)` com `rc=?`, `st620_max=?`,
  `sticky=?`, `oob_pos_tls=?` e o rótulo `[nivel de evidencia: nao-exercitado]`.

## Self-Check: PASSED

- `games/gow2/lift_baseline/PATCH_CATALOG.tsv` — FOUND (139 linhas de dados)
- `games/gow2/lift_baseline/gen_migration_ledger.py` — FOUND
- `games/gow2/lift_baseline/test_gen_migration_ledger.py` — FOUND (rc=0)
- `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` — FOUND (139 linhas de dados)
- Commits `9452cad`, `0ed8ba5`, `45c6e55` — FOUND em `git log`
