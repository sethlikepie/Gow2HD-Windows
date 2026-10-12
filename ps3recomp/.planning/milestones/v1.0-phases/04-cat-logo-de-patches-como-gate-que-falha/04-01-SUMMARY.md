---
phase: 04-cat-logo-de-patches-como-gate-que-falha
plan: 01
subsystem: tooling
tags: [python, ast, patch-classification, gow2, tdd]

requires: []
provides:
  - "games/gow2/lift_baseline/gen_catalog.py: classify(name, src) -> (classe, subclasse, no_gate) puro, 4 ramos deterministicos (inventar comportamento / Diagnostic only / _probe|_trace sem despacho / resto), deteccao ESCREVE/VERIFICA por AST (nunca grep de substring)"
  - "games/gow2/lift_baseline/PATCH_CATALOG.tsv: 87 linhas derivadas + cabecalho, 64 FUNCIONAL / 23 PROBE, zero indeterminados, PROBE listados nominalmente com razao"
  - "games/gow2/lift_baseline/test_gen_catalog.py: 11 testes (7 classify() contra patches reais + 4 cobertura/mutacao/cruzamento MANIFEST), rc=0"
affects: ["04-04", "04-05"]

tech-stack:
  added: []
  patterns:
    - "classificacao por AST (ast.walk + ast.get_docstring), nunca grep de substring -- falso-positivo medido dentro do proprio docstring de patch_ce03c_wait_idle_f2b_movie.py (contem a string 'write_text' em prosa)"
    - "'nome mente, comportamento manda': sufixo _probe/_trace nao decide sozinho -- so' se o corpo (sem docstring) tambem nao tiver token de despacho; caso contrario FUNCIONAL/dispatch-fix"
    - "cobertura medida em tempo de execucao (len(list(PATCH_DIR.glob(...)))), nunca um N hardcoded morto"
    - "PATCH_DIR aponta sempre para ../gow2-recomp/recomp_mid_v2 (corpus que apply_all_patches.sh de facto corre), nunca para o espelho subtree atrasado games/gow2/recomp_mid_v2"

key-files:
  created:
    - games/gow2/lift_baseline/gen_catalog.py
    - games/gow2/lift_baseline/test_gen_catalog.py
    - games/gow2/lift_baseline/PATCH_CATALOG.tsv
  modified: []

key-decisions:
  - "Dois commits (test antes de feat) seguindo o gate TDD do plano: Task 1 = test(04-01) com classify() + 7 testes; Task 2 = feat(04-01) com build_catalog()/render_tsv()/main() + 4 testes + PATCH_CATALOG.tsv real gerado."
  - "razao_para() e' funcao separada de classify() (nao faz parte da tripla pura); computa o texto da coluna razao a partir da mesma logica de deteccao, sem duplicar o parsing AST."
  - "Nome sugere probe/trace mas o corpo despacha de facto (patch_2b0fb4_trace.py, patch_a1_chain_probe.py) ganhou subclasse propria 'dispatch-fix' em vez de cair no dicionario generico de tokens do ramo 4 -- a subclasse marca explicitamente que passou pelo desempate do ramo 3."

requirements-completed: [REQ-RDY0-5]

coverage:
  - id: D1
    description: "classify() classifica os 87 patch_*.py reais em PROBE/FUNCIONAL por 4 ramos deterministicos, zero indeterminados"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: unit
        ref: "test_gen_catalog.py::check_ramo1_orfao_sem_fonte, check_ramo2_diagnostico_autodeclarado, check_ramo3_positivo_probe_sem_despacho, check_ramo3_negativo_nome_mente_comportamento_manda, check_protecao_docstring_menciona_mas_nao_escreve, check_ramo4_subclasse_por_token_de_nome"
        status: pass
      - kind: integration
        ref: "gen_catalog.py contra ../gow2-recomp/recomp_mid_v2 real: 87 linhas, 64 FUNCIONAL / 23 PROBE (bate exactamente a medicao do planeamento)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Deteccao ESCREVE/VERIFICA por AST evita o falso-positivo de grep de substring dentro de docstrings"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: unit
        ref: "test_gen_catalog.py::check_has_write_call_ast_vs_substring_false_positive"
        status: pass
    human_judgment: false
  - id: D3
    description: "PATCH_CATALOG.tsv derivado, comitado, 87/87 cobertura, PROBE listados nominalmente com razao nao-vazia, determinismo (2 geracoes byte-identicas)"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: unit
        ref: "test_gen_catalog.py::check_cobertura_87_87_contra_corpus_real, check_zero_indeterminado, check_mutacao_patch_sintetico_cai_em_funcional_misc, check_cruzamento_com_manifest"
        status: pass
      - kind: integration
        ref: "diff entre duas geracoes consecutivas de PATCH_CATALOG.tsv: identico"
        status: pass
    human_judgment: false

duration: ~35min
completed: 2026-07-26
status: complete
---

# Phase 4 Plan 01: Catálogo de patches derivado (classify() + PATCH_CATALOG.tsv) Summary

**`gen_catalog.py` classifica os 87 `patch_*.py` reais em 64 FUNCIONAL / 23 PROBE por 4 ramos determinísticos (AST, nunca grep), gerando `PATCH_CATALOG.tsv` — derivado, cobertura 87/87, zero indeterminados, PROBE listados nominalmente com razão.**

## Performance

- **Duration:** ~35min
- **Tasks:** 2/2 completas
- **Files modified:** 3 (todos novos)

## Estado

Fechado sem desvios factuais em relação ao plano. A classificação medida pela execução
bate **exatamente** o número do planeamento: **64 FUNCIONAL / 23 PROBE, zero
indeterminados**, contra os **87** `patch_*.py` reais de `../gow2-recomp/recomp_mid_v2`
(nunca o espelho subtree atrasado `games/gow2/recomp_mid_v2`, que tem só 75/87).

## Por tarefa: comando e saída real

### Task 1 — `classify()` fim-a-fim contra 6 patches reais

```
$ .venv/bin/python3 games/gow2/lift_baseline/test_gen_catalog.py; echo "rc=$?"
[info] PATCH_DIR resolvido: /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_mid_v2
[PASS] has_write_call() por AST: True em patch_wadld_alloc_probe.py, False em
       patch_ce03c_pre_play_stop.py e em patch_ce03c_wait_idle_f2b_movie.py
       (este ultimo contem 'write_text' na prosa -- prova do falso-positivo de substring)
[PASS] ramo 1 (orfao-sem-fonte): os 2 CE03C classificam PROBE/orfao-sem-fonte/True;
       'inventar comportamento' casa em exactamente 2 ficheiros no corpus real
[PASS] ramo 2 (diagnostico autodeclarado): patch_cc9d0_live_yield.py classifica
       PROBE/diagnostico/True; 'Diagnostic only' casa em exactamente 1 ficheiro
[PASS] ramo 3 positivo: patch_wadld_alloc_probe.py classifica PROBE/diagnostico/True
[PASS] ramo 3 negativo (nome mente, comportamento manda): patch_2b0fb4_trace.py e
       patch_a1_chain_probe.py classificam FUNCIONAL/dispatch-fix/False
[PASS] protecao contra falso-positivo de docstring: patch_fios_open_probe.py continua
       PROBE/diagnostico/True
[PASS] ramo 4: patch_32e200_opd.py classifica FUNCIONAL/opd/False; classify() e' pura
rc=0
```

```
$ grep -l "inventar comportamento" ../gow2-recomp/recomp_mid_v2/patch_*.py | wc -l
       2
```

7/7 [PASS], exatamente como o plano exigia. Confirmado também `grep -l "Diagnostic
only"` = 1 ficheiro (dentro do teste, não como comando avulso do plano).

### Task 2 — cobertura 87/87, cruzamento com MANIFEST.tsv, PATCH_CATALOG.tsv comitado

```
$ .venv/bin/python3 games/gow2/lift_baseline/test_gen_catalog.py; echo "rc=$?"
... (as 7 linhas acima) +
[PASS] cobertura: build_catalog() produz 87 linhas, igual a
       len(list(PATCH_DIR.glob('patch_*.py'))) == 87 (medido: 87)
[PASS] zero indeterminado: 87 linhas, classe sempre PROBE/FUNCIONAL, subclasse nunca
       vazia, razao nunca vazia quando PROBE (medido: 64 FUNCIONAL / 23 PROBE)
[PASS] mutacao: patch_zz_teste_cobertura.py (sintetico, tempfile.TemporaryDirectory())
       cai em FUNCIONAL/misc -- entra na contagem, nunca fica indeterminado
[PASS] cruzamento com MANIFEST: patch_wadld_alloc_probe.py cita [WADLD-ALLOC] na coluna
       marcador; 63/87 linhas do catalogo tem marcador nao-vazio
rc=0
```

```
$ .venv/bin/python3 games/gow2/lift_baseline/gen_catalog.py \
    ../gow2-recomp/recomp_mid_v2 games/gow2/lift_baseline/MANIFEST.tsv \
    > games/gow2/lift_baseline/PATCH_CATALOG.tsv
$ N=$(grep -vc '^#' games/gow2/lift_baseline/PATCH_CATALOG.tsv); echo "linhas_de_dados=$N (esperado 87)"
linhas_de_dados=87 (esperado 87)
$ cut -f3 games/gow2/lift_baseline/PATCH_CATALOG.tsv | grep -v '^#' | sort | uniq -c
   1 classe
  64 FUNCIONAL
  23 PROBE
$ # determinismo: segunda geracao -> diff sem saida (byte-identico)
byte-identical: OK
$ grep -P '^[^\t]+\tPROBE\t' games/gow2/lift_baseline/PATCH_CATALOG.tsv | cut -f6 | grep -c '^$'
0
```

Nota sobre a linha `1 classe` no `uniq -c`: é a própria linha de cabeçalho de colunas
do TSV (`# patch\tescreve\tclasse\t...`), que também usa tabs — `cut -f3` extrai o
literal `classe` dessa linha porque o campo isolado já não começa por `#` (só a linha
inteira começa). O mesmo padrão já existe no `MANIFEST.tsv` pré-existente (linha 129,
`# marcador\ttipo\t...`). Não é um indeterminado real — confirmado pelo teste Python
`check_zero_indeterminado`, que percorre as linhas de dados estruturadas (não faz
`cut`/`grep` de texto) e afirma `classe in ("PROBE", "FUNCIONAL")` para as 87 linhas.

## Classificação final: 64 FUNCIONAL / 23 PROBE / 0 indeterminados

**Bate exatamente 64/23/0**, a medição do planeamento (`04-01-PLAN.md` linha 30).

Lista PROBE nominal (23), com subclasse:

```
patch_17acc_enter_probe.py             PROBE  diagnostico
patch_2a4fe4_trace.py                  PROBE  diagnostico
patch_2b3d1c_probe.py                  PROBE  diagnostico
patch_cc9d0_live_yield.py              PROBE  diagnostico
patch_cd498_enter_probe.py             PROBE  diagnostico
patch_ce03c_pre_play_stop.py           PROBE  orfao-sem-fonte
patch_ce03c_wait_idle_f2b_movie.py     PROBE  orfao-sem-fonte
patch_crc_lookup_probe.py              PROBE  diagnostico
patch_fios_open_probe.py               PROBE  diagnostico
patch_fios_sched_probe.py              PROBE  diagnostico
patch_flip_path_enter_probe.py         PROBE  diagnostico
patch_introseq_probe.py                PROBE  diagnostico
patch_mc_schedule_probe.py             PROBE  diagnostico
patch_menu_present_schedule_probe.py   PROBE  diagnostico
patch_oob_ra_probe.py                  PROBE  diagnostico
patch_postthr_pc_probe.py              PROBE  diagnostico
patch_present_loop_probe.py            PROBE  diagnostico
patch_sched_arm_probe.py               PROBE  diagnostico
patch_smpd_probe.py                    PROBE  diagnostico
patch_snd_open_probe.py                PROBE  diagnostico
patch_st3_probe.py                     PROBE  diagnostico
patch_wadld_alloc_probe.py             PROBE  diagnostico
patch_wavdrv_probe.py                  PROBE  diagnostico
```

Os 2 `orfao-sem-fonte` são os verificadores puros CE03C (regra 4 do CLAUDE.md, sem
bloco a repor). Os outros 21 são `diagnostico` (1 auto-declarado "Diagnostic only" +
20 por sufixo `_probe`/`_trace` sem token de despacho no corpo). Os 2
casos-armadilha do enunciado (`patch_2b0fb4_trace.py`, `patch_a1_chain_probe.py`) NÃO
aparecem nesta lista — corretamente classificados FUNCIONAL/dispatch-fix, apesar do
sufixo `_trace`/`_probe`, porque o corpo (fora do docstring) escreve
`ps3_call_opd(`/`g_trampoline_fn =` de facto.

## Task Commits

Cada task foi committada atomicamente, seguindo o gate TDD do plano (`type: tdd`):

1. **Task 1 (test):** `2a51970` — `test(04-01): classify() testado fim-a-fim contra 6 patches reais, 4 ramos`
   — `gen_catalog.py` (classify() + funções de suporte) + `test_gen_catalog.py` (7 testes).
2. **Task 2 (feat):** `4d62e7c` — `feat(04-01): PATCH_CATALOG.tsv derivado, 87/87, 64 FUNCIONAL / 23 PROBE`
   — extensão de `gen_catalog.py` (build_catalog/render_tsv/main/razao_para/
   load_manifest_tags/markers_in) + `test_gen_catalog.py` (+4 testes) + `PATCH_CATALOG.tsv`
   gerado e comitado.

`git log --oneline`: `test(04-01)` aparece antes de `feat(04-01)` — gate TDD do plano
(`type: tdd`) satisfeito.

Nenhum push. Nenhum commit em `../gow2-recomp` (D-4.3 explícito no plano: script de
tooling do monorepo, nunca corre a partir do gow2-recomp).

## Files Created/Modified

- `games/gow2/lift_baseline/gen_catalog.py` (novo) — `classify(name, src) ->
  (classe, subclasse, no_gate)` puro por AST; `build_catalog()`/`render_tsv()`/`main()`
  geram o TSV real cruzando com `MANIFEST.tsv`.
- `games/gow2/lift_baseline/test_gen_catalog.py` (novo) — 11 testes, rc=0.
- `games/gow2/lift_baseline/PATCH_CATALOG.tsv` (novo, derivado) — 87 linhas de dados +
  cabeçalho, comando de regeneração documentado no próprio ficheiro.

## Decisions Made

- Commit `test` antes de `feat` (RED conceitual → GREEN), embora as `<action>` do plano
  já pedissem construir implementação+testes juntos dentro de cada task — reconstruí a
  fronteira das duas tasks por edição incremental dos ficheiros para produzir dois
  commits reais e distintos, confirmando 7/7 [PASS] antes do primeiro commit e 11/11
  [PASS] antes do segundo (nunca commitei um estado não-verificado).
- `razao_para()` ficou fora de `classify()` (que continua uma tripla pura, exatamente
  como o artefato do plano exige) — computa o texto da razão a partir da mesma lógica
  de deteção, sem duplicar o parsing AST nem contaminar a assinatura pura.
- Subclasse `dispatch-fix` dedicada para o caso "nome sugere probe/trace mas o corpo
  despacha" (em vez de cair no dicionário genérico de tokens do ramo 4) — torna visível
  no próprio TSV que esses dois patches passaram pelo desempate do ramo 3, não são um
  FUNCIONAL comum.

## Deviations from Plan

Nenhuma das Regras 1-4. O plano estava factualmente correto em toda a medição
verificada nesta sessão (contagem de 87 ficheiros, 2 ficheiros com "inventar
comportamento", 1 com "Diagnostic only", os 2 casos-armadilha de ramo 3, o marcador
`[WADLD-ALLOC]` em `MANIFEST.tsv`). Nada exigiu parar e reportar.

**Total deviations:** 0.
**Impact on plan:** Nenhum.

## Issues Encountered

- Ao correr `git status` a meio da execução apareceram fixtures SPU regeneradas
  (`runtime/spu/tests/out_*/spu_recomp.{c,h}`) como modificadas — não geradas por mim
  nesta sessão (provavelmente resíduo de um `ctest` anterior de outra sessão). Restaurei
  com `git checkout --` antes de qualquer commit, conforme a regra do CLAUDE.md
  ("Fixtures SPU regeneradas pelo ctest: `git checkout --`, nunca commitar").
- Execução em paralelo com os planos 04-02 (`patch_14a01c_opd.py`,
  `patch_icg_ctor_opd.py`) e 04-03 (`verify_lift.sh`, `baseline_delta.py`): usei
  `git commit <pathspec>` (nunca `git add -A`/`.`) para commitar apenas os meus três
  ficheiros em cada commit, deixando as alterações não commitadas do 04-02 intocadas no
  working tree; confirmado por `git status --porcelain` antes/depois de cada commit.

## Known Stubs

Nenhum. `chunk_alvo` fica vazio nas 87 linhas por desenho explícito do plano ("fora de
escopo: exigiria correr o patch contra um lift" — Plano 04-04/04-05 preenche via
`CONTRACTS.tsv`), não é um stub não-declarado.

## Threat Flags

Nenhuma superfície nova fora do `threat_model` do plano. T-04-01 (docstring engana
classify()) e T-04-02 (directorio errado) já estavam cobertos e mitigados pelos testes
6 (Task 1) e cobertura (Task 2) do próprio plano.

## User Setup Required

None - nenhuma configuração de serviço externo necessária.

## Next Phase Readiness

- `PATCH_CATALOG.tsv` disponível para o Plano 04-05 (o `apply_all_patches.sh` passa a
  consumir a coluna `classe`/`no_gate` para excluir os 23 PROBE do gate).
- `chunk_alvo` vazio: o Plano 04-04 (`CONTRACTS.tsv`) é quem vai preencher essa
  informação correndo os patches contra um lift.
- Nenhum bloqueador para os planos paralelos 04-02/04-03 (ficheiros disjuntos,
  confirmado por `git status` ao longo da execução).

---
*Phase: 04-cat-logo-de-patches-como-gate-que-falha*
*Completed: 2026-07-26*

## Self-Check: PASSED

Ficheiros confirmados no disco: `games/gow2/lift_baseline/gen_catalog.py`,
`games/gow2/lift_baseline/test_gen_catalog.py`,
`games/gow2/lift_baseline/PATCH_CATALOG.tsv`, este SUMMARY. Commits confirmados por
`git log --oneline`: `2a51970` (test), `4d62e7c` (feat). Suite de testes final:
`.venv/bin/python3 games/gow2/lift_baseline/test_gen_catalog.py` → 11/11 [PASS], rc=0.
