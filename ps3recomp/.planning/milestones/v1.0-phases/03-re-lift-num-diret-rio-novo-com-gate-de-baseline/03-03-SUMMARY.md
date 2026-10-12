---
phase: 03-re-lift-num-diret-rio-novo-com-gate-de-baseline
plan: 03
subsystem: build-tooling
tags: [python, regex, gen_manifest, patch_mid_v2, gow2, opd, tdd]

requires:
  - phase: 03-re-lift-num-diret-rio-novo-com-gate-de-baseline
    provides: "recomp_macos_v3 gerado por RELIFT=1 (03-01) + patch_14b1f0_opd.py corrigido (03-02)"
provides:
  - "gen_manifest.py::verify() com mecanismos OBSOLETO e GRUPO (aditivos, testados por mutacao)"
  - "patch_wadld_alloc_probe.py: instalador dedicado para [WADLD-ALLOC] (D-3.3), mesmo padrao do B71/BA808"
  - "MANIFEST.tsv reformulado: [OPDISP] obsoleto com prova; par ps3_indirect_call/ps3_call_opd GRUPO:opd-dispatch; 23 marcadores com limiar revisto contra v3"
  - "primeiro resultado real e honesto de verify_lift_baseline.sh contra recomp_macos_v3 pos-cadeia-completa de 87 patches"
affects: ["fase-4"]

tech-stack:
  added: []
  patterns:
    - "5a coluna opcional (nota) em MANIFEST.tsv, aditiva e retro-compativel: OBSOLETO:<razao> | GRUPO:<id>"
    - "ancora regex com lookahead nao-consumido para tolerar duas formas do lift (prefixo ctx->lr opcional, cast uint32/uint64) sem tocar na linha seguinte"

key-files:
  created:
    - ../gow2-recomp/recomp_mid_v2/patch_wadld_alloc_probe.py
    - ../gow2-recomp/recomp_mid_v2/test_patch_wadld_alloc_probe.py
    - games/gow2/recomp_mid_v2/patch_wadld_alloc_probe.py
  modified:
    - games/gow2/lift_baseline/gen_manifest.py
    - games/gow2/lift_baseline/test_gen_manifest.py
    - games/gow2/lift_baseline/MANIFEST.tsv
    - .planning/REQUIREMENTS.md

key-decisions:
  - "REQ-RDY0-4 NAO foi marcado completo: a sua aceitacao literal escrita (\"verify_lift_baseline.sh sai 0\") nao foi atingida -- o par GRUPO:opd-dispatch fica com deficit residual de 36 (soma historica 15847, soma medida 15811 contra v3 fresco). A metodologia reformulada (a,b,c) foi implementada e aplicada correctamente; o numero final nao fechou verde. Documentado honestamente em vez de forcado ou maquiado (Regra 4 CLAUDE.md)."
  - "Nao revisado o limiar individual do par GRUPO (ps3_indirect_call/ps3_call_opd) -- por instrucao explicita do plano (excepcao no passo 5), o par e' tratado SO pelo mecanismo de soma, nunca por revisao de min_count individual, mesmo com o deficit residual medido."
  - "apply_all_patches.sh precisou de 3 passagens para convergir contra v3 (nao 2 como o plano assumia) -- medido, nao forcado a bater com a expectativa."

requirements-completed: []

coverage:
  - id: D1
    description: "gen_manifest.py::verify() suporta OBSOLETO e GRUPO de forma aditiva, retro-compativel, testada por mutacao sintetica"
    requirement: "REQ-RDY0-4"
    verification:
      - kind: unit
        ref: "games/gow2/lift_baseline/test_gen_manifest.py (10/10 [PASS], rc=0)"
        status: pass
      - kind: integration
        ref: "gen_manifest.py contra recomp_macos_v2 real: 219/246, rc=1, identico ao medido antes desta task (nao regride)"
        status: pass
    human_judgment: false
  - id: D2
    description: "patch_wadld_alloc_probe.py instala [WADLD-ALLOC] de forma idempotente, ancora unica, tolerando as duas formas do lift (pre_v4 e actual)"
    requirement: "REQ-RDY0-4"
    verification:
      - kind: unit
        ref: "../gow2-recomp/recomp_mid_v2/test_patch_wadld_alloc_probe.py (7/7 [PASS], rc=0: GREEN 1/1b/2/3a/3b/4 + gap RED)"
        status: pass
      - kind: integration
        ref: "e2e contra copia scratch de recomp_macos_v2/ppu_recomp_002.cpp: grep -c WADLD-ALLOC 0->1; original intacto por hash antes/depois"
        status: pass
    human_judgment: false
  - id: D3
    description: "[OPDISP] marcado OBSOLETO no MANIFEST.tsv com prova reconfirmada contra recomp_macos_v3 fresco (nao so' v2)"
    requirement: "REQ-RDY0-4"
    verification:
      - kind: integration
        ref: "python3 patch_jumptable_2a209c.py recomp_macos_v3 -> 'switch cobre 54/54 alvos; nada a injectar' (mesma saida que contra v2)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Resultado honesto do primeiro verify_lift_baseline.sh real contra recomp_macos_v3 pos-cadeia-completa: rc=1, 243/246, 0 ausentes, 0 a menos individual, 1 grupo falhado (deficit 36) -- citado sem maquiagem"
    requirement: "REQ-RDY0-4"
    verification:
      - kind: manual_procedural
        ref: "PS3_ENGINE_ROOT=$(pwd) games/gow2/verify_lift_baseline.sh ../gow2-recomp/recomp_macos_v3"
        status: pass
    human_judgment: true
    rationale: "rc!=0 e' o resultado medido, nao um bug do proprio verify -- decisao humana necessaria sobre se a metodologia (a,b,c) satisfeita basta para fechar REQ-RDY0-4/criterio 3-4 do ROADMAP, ou se o deficit residual de 36 no par OPD precisa de investigacao dedicada (candidato 03-04/Fase 4) antes de marcar completo."

duration: ~20min
completed: 2026-07-26
status: complete
---

# Phase 3 Plan 03: Gate de baseline reformulado (OBSOLETO/GRUPO) + primeira cadeia real contra recomp_macos_v3 Summary

**`gen_manifest.py::verify()` ganhou os mecanismos OBSOLETO/GRUPO (D-3.1/D-3.2); `[WADLD-ALLOC]` ganhou instalador dedicado (D-3.3); e a primeira corrida real do catálogo completo de 87 patches contra um `recomp_macos_v3` genuinamente fresco produziu 243/246 (rc=1, honesto) — o par OPD acoplado fica com um deficit residual de 36 que a correção do Plano 03-02 não eliminou contra um lift fresco.**

## Performance

- **Duration:** ~20 min (20:39:49Z a 20:58:51Z)
- **Started:** 2026-07-26T20:39:49Z
- **Tasks:** 3/3 completas
- **Files modified:** 7 (4 no monorepo `ps3recomp`, 2 novos + mirror no sibling `gow2-recomp`)

## Estado do ambiente ANTES de qualquer edição (MEDIDO)

```
$ (hash de conteudo find+tar+md5 de ../gow2-recomp/recomp_macos_v2)
70e7ad1204f41fbedee3fd94aa9c391e   (identico ao hash de fim de sessao do Plano 03-01)
```

`../gow2-recomp/recomp_macos_v3` já existia (Plano 03-01), fresco e SEM nenhum patch
aplicado (`grep -c ps3_call_opd` = 0 nos 3 chunks relevantes; `grep -c WADLD-ALLOC` = 0).
`patch_14b1f0_opd.py` já corrigido pelo Plano 03-02.

## Task 1: gen_manifest.py — mecanismos OBSOLETO e GRUPO

**Edição:** `verify()` passou a aceitar uma 5ª coluna opcional (`nota`) na linha do
`MANIFEST.tsv`, tolerante e aditiva. Marcadores sem essa coluna seguem exactamente o
comportamento pré-existente (confirmado: 219/246 contra `recomp_macos_v2` real, idêntico
ao medido antes desta task). Dois mecanismos novos:
- `OBSOLETO: <razão>` — nunca falha o gate, impresso numa linha informativa separada
  (`OBSOLETO  {kind}  {marker} -- {razão}`), seletivo por marcador (testado — um marcador
  SEM nota, ausente, continua a fazer `verify()` devolver 1).
- `GRUPO:<id>` — marcadores com o mesmo id são verificados pela SOMA de
  `found`/`min_count`, não isoladamente; falha imprime `A MENOS (grupo)  {id}: ...`.

**Comando e saída real:**
```
$ .venv/bin/python3 games/gow2/lift_baseline/test_gen_manifest.py; echo "rc=$?"
[PASS] verify() sai 0 quando g_trampoline_fn e ps3_indirect_call estao presentes
[PASS] verify() por mutacao detecta ausencia de g_trampoline_fn (...)
[PASS] verify() por mutacao detecta ausencia de ps3_indirect_call (...)
[PASS] geracao exclui ps3_timebase_now do kind=PREAMBLE (...)
[PASS] MANIFEST.tsv reconcilia as tres cifras do criterio 1 (...)
[PASS] gen_manifest.py contem o mecanismo OBSOLETO/GRUPO (gap fechado)
[PASS] marcador OBSOLETO ausente (found==0) nao aparece em AUSENTE, verify() continua rc=0
[PASS] OBSOLETO e' seletivo por marcador (...)
[PASS] GRUPO soma as contagens (...)
[PASS] GRUPO cuja soma nao cumpre o limiar combinado (...)
rc=0
```
10/10 `[PASS]` (5 pré-existentes da Fase 2 intactos + 5 novos).
```
$ grep -c 'OBSOLETO\|GRUPO' games/gow2/lift_baseline/gen_manifest.py
10
```

**Commit:** `9c2c58f` (feat, monorepo).

## Task 2: patch_wadld_alloc_probe.py — instalador do [WADLD-ALLOC] (D-3.3)

Instalador idempotente, modelado byte-a-byte no `patch_ba808_wadld_eof.py`. Âncora
regex com lookahead não-consumido tolera as duas formas do lift: `.pre_v4` (sem prefixo
`ctx->lr=...;`, cast `uint32_t`) e a actual (com prefixo, cast `uint64_t`) — confirmado
lendo os dois `func_002BACE8` reais antes de escrever a âncora.

**Comandos e saída real:**
```
$ .venv/bin/python3 ../gow2-recomp/recomp_mid_v2/test_patch_wadld_alloc_probe.py; echo "rc=$?"
[PASS] patch_wadld_alloc_probe.py existe e contem WADLD-ALLOC (gap fechado)
[PASS] GREEN 1: instala [WADLD-ALLOC] exactamente 1x em func_002BACE8 (...)
[PASS] GREEN 1b: ancora tolera a forma pre_v4 (...)
[PASS] GREEN 2: 2a corrida sobre ficheiro ja patcheado -> ALREADY, rc=0, hash identico
[PASS] GREEN 3a: ancora ausente (0x) -> FAILED, rc!=0
[PASS] GREEN 3b: ancora duplicada (2x) -> FAILED, rc!=0
[PASS] GREEN 4: gating no-op confirmado (...)
rc=0
```
```
$ mkdir -p /tmp/wadld_alloc_e2e && cp .../ppu_recomp_002.cpp /tmp/wadld_alloc_e2e/
$ .venv/bin/python3 .../patch_wadld_alloc_probe.py /tmp/wadld_alloc_e2e; echo "rc=$?"
ppu_recomp_002.cpp: APPLIED probe [WADLD-ALLOC] em func_002BACE8 (1 site, gated PS3_TRACE_TYMAP)
rc=0
$ grep -c WADLD-ALLOC /tmp/wadld_alloc_e2e/ppu_recomp_002.cpp
1
```
Original `../gow2-recomp/recomp_macos_v2/ppu_recomp_002.cpp` confirmado intacto por
sha256 antes/depois (idêntico). `diff` entre `../gow2-recomp/recomp_mid_v2/patch_wadld_alloc_probe.py`
e `games/gow2/recomp_mid_v2/patch_wadld_alloc_probe.py`: vazio (mirror idêntico).

**Commits:** `6ab8bb5` (feat, gow2-recomp — script + teste); `2838453` (feat, ps3recomp — mirror).

## Task 3: MANIFEST.tsv reformulado + cadeia real completa contra recomp_macos_v3

### Cadeia real (RELIFT já feito no 03-01; aqui: `apply_all_patches.sh` sobre `recomp_macos_v3`)

**Achado honesto não previsto pelo plano:** precisou de **3 passagens**, não 2, para
convergir:

| Passagem | APPLIED | ALREADY-APPLIED | FAILED |
|---|---|---|---|
| 1 | 66 | 18 | 3 |
| 2 | 2 | 82 | 3 |
| 3 | 0 | 84 | 3 |

`FALHARAM` (idênticos nas 3 passagens): `patch_cc9d0_live_yield.py`,
`patch_ce03c_pre_play_stop.py`, `patch_ce03c_wait_idle_f2b_movie.py`.

**Gap novo específico de v3 (nunca antes medido — o próprio objectivo do plano avisava
disto):** estes 3 são **diferentes** dos 4 historicamente documentados no cabeçalho do
`apply_all_patches.sh` (`patch_2b0fb4_trace.py`, `patch_factory_opd_gate.py`,
`patch_tymap_18e814.py`, `patch_tymap_probes2.py`). Esses 4 antigos agora aparecem
`ALREADY-APPLIED` contra `v3` — um resultado **positivo** não garantido de antemão. Os 3
novos falham pela mesma classe já diagnosticada em `03-CONTEXT.md`: comportamento de
continuação/probe sem escritor conhecido no repo (`patch_ce03c_wait_idle_f2b_movie.py`
imprime literalmente `sem escritor conhecido no repo`), nenhum altera o boot default.
Não investigados a fundo (fora do escopo explícito desta task); candidatos a Fase 4 ou a
um `03-04` dedicado.

### `gen_manifest.py --verify` ANTES da edição do MANIFEST (medição, passo 1)

```
AUSENTE  TAG    [OPDISP]  (esperado >=1, encontrado 0)
A MENOS  GLOBAL g_f2b_fill_file_pos  (esperado >=28, encontrado 14)
... (23 marcadores A MENOS, todos batendo EXACTAMENTE com o ballpark da sessão de
     planeamento — nenhuma anomalia nova nestes 23)
A MENOS  PREAMBLE ps3_indirect_call  (esperado >=15795, encontrado 15759)

baseline: 221/246 marcadores intactos, 1 ausentes, 24 a menos
```
`ps3_call_opd` medido em 52 (agregado dos 3 chunks) — já cumpre o `min_count` histórico
de 52 nesta contagem isolada (o problema aparece só na SOMA do grupo, ver abaixo).

### Edição do MANIFEST.tsv (passo 2)

- `[OPDISP]` → `OBSOLETO: ...` (prova reconfirmada contra `v3`: `patch_jumptable_2a209c.py`
  devolve a mesma saída "switch cobre 54/54 alvos; nada a injectar" que contra `v2`).
- `ps3_indirect_call` (PREAMBLE) e `ps3_call_opd` (SYM) → `GRUPO:opd-dispatch`, sem
  revisão de `min_count` individual (excepção explícita do plano — o par é tratado só
  pela soma).
- 23 marcadores `A MENOS` → `min_count` revisto para o valor medido (idêntico ao
  ballpark: `g_f2b_fill_file_pos` 28→14, `g_f2b_fill_mfd` 9→7, `g_f2b_fill_sz` 30→11,
  `g_f2b_natural_movie_fo` 6→5, `f2b_fo_mfd_get` 6→4, `f2b_fo_sz_get` 3→2,
  `f2b_stream_fill` 6→4, `ps3_fios_aread_hle` 3→2, `ps3_fios_sticky_consume` 5→2,
  `ps3_fios_sticky_publish` 9→6, `ps3_indirect_tail` 493→478, `ps3_mp_on_enter` 19→17,
  `ps3_plp_on_enter` 14→9, `ps3_pt_on_enter` 20→19, `ps3_sa_on_enter` 23→18,
  `ps3_type15_note_resolve` 4→3, `ps3_type15_repair_if_needed` 4→1, `[FACTORY]` 9→6,
  `[FIOSOPEN]` 70→35, `[FIOSSCHED]` 13→4, `[POSTINTRO]` 74→20, `[TYPE15]` 28→24,
  `[WADLD-VT28]` 2→1).
- Bloco novo no cabeçalho documentando D-3.1 a D-3.4, o mecanismo OBSOLETO/GRUPO, a
  leitura honesta do deficit residual do par OPD, e a cadeia real com o resultado das
  3 passagens.

### `gen_manifest.py --verify` DEPOIS da edição (passo 3 — resultado final)

```
$ .venv/bin/python3 games/gow2/lift_baseline/gen_manifest.py ../gow2-recomp/recomp_macos_v3 --verify games/gow2/lift_baseline/MANIFEST.tsv
OBSOLETO  TAG  [OPDISP] -- lifter materializa switch nativo cobrindo 54/54 alvos ...
A MENOS (grupo)  opd-dispatch: ps3_indirect_call+ps3_call_opd esperado>=15847 encontrado=15811

baseline: 243/246 marcadores intactos, 0 ausentes, 0 a menos
obsoletos: 1 citados (nunca falham o gate) | grupos: 0 passaram, 1 falharam (de 1 grupos, 2 marcadores)
rc=1
```

### Comando literal do critério 3 do ROADMAP

```
$ PS3_ENGINE_ROOT="$(pwd)" games/gow2/verify_lift_baseline.sh ../gow2-recomp/recomp_macos_v3
-- baseline do lift: recomp_macos_v3 --
OBSOLETO  TAG  [OPDISP] -- ...
A MENOS (grupo)  opd-dispatch: ps3_indirect_call+ps3_call_opd esperado>=15847 encontrado=15811

baseline: 243/246 marcadores intactos, 0 ausentes, 0 a menos
obsoletos: 1 citados (nunca falham o gate) | grupos: 0 passaram, 1 falharam (de 1 grupos, 2 marcadores)
rc=1
```

**Não é `rc=0`. Não maquiado.** O que falta, classificado:

| Item | Classificação |
|---|---|
| Par `ps3_indirect_call`+`ps3_call_opd` (deficit combinado de 36) | **Candidato Fase-4 / des-duplicação inconclusiva** — consistente com o mesmo padrão de des-duplicação dos outros 23 marcadores (lift com 7 chunks vs 31 antigos), mas não medido directamente para este par específico. Não maquiado como resolvido. |
| `patch_cc9d0_live_yield.py`, `patch_ce03c_pre_play_stop.py`, `patch_ce03c_wait_idle_f2b_movie.py` (FAILED em `apply_all_patches.sh`) | **Gap novo específico de v3** — nunca antes medido (primeira vez que o catálogo inteiro corre contra um lift fresco). Mesma classe já diagnosticada (probes/comportamento sem escritor). Não investigados a fundo aqui — fora do escopo explícito. |

**Commit:** `91646a9` (docs, monorepo — `MANIFEST.tsv`).

## `ps3_call_opd` — contagens antes/depois

- **Antes desta task** (v3 fresco, sem nenhum patch aplicado): `ps3_call_opd` = 0,
  `ps3_indirect_call` = 0 (lift limpo, nenhum ainda extraído).
- **Depois da cadeia completa** (v3 pós-`apply_all_patches.sh` 3x, incluindo o
  `patch_14b1f0_opd.py` corrigido no Plano 03-02): `ps3_call_opd` = **52** (agregado
  `ppu_recomp_000/001/003.cpp`), `ps3_indirect_call` = **15759** (agregado dos 7 chunks).
  Soma = 15811 — abaixo da soma histórica de referência (15847) por 36, deficit
  documentado, não eliminado, no MANIFEST.

## Hash de `recomp_macos_v2` — antes e depois desta task

```
antes: 70e7ad1204f41fbedee3fd94aa9c391e
depois: 70e7ad1204f41fbedee3fd94aa9c391e   (identico)
```
`recomp_macos_v2` (produção) nunca foi tocado por esta task — todas as operações desta
sessão (instalação/teste do patch novo, cadeia de `apply_all_patches.sh`) correram
contra `recomp_macos_v3` ou cópias scratch em `/tmp`, nunca contra a produção real.

## Task Commits

1. **Task 1: gen_manifest.py OBSOLETO/GRUPO** — `9c2c58f` (feat, ps3recomp/monorepo)
2. **Task 2: patch_wadld_alloc_probe.py** — `6ab8bb5` (feat, gow2-recomp) + `2838453`
   (feat, ps3recomp — mirror)
3. **Task 3: MANIFEST.tsv reformulado** — `91646a9` (docs, ps3recomp/monorepo)

Nenhum `git push` em nenhum dos dois repositórios.

## Files Created/Modified
- `games/gow2/lift_baseline/gen_manifest.py` — mecanismos OBSOLETO/GRUPO em `verify()`
- `games/gow2/lift_baseline/test_gen_manifest.py` — 5 testes novos por mutação
- `games/gow2/lift_baseline/MANIFEST.tsv` — reformulado (OBSOLETO/GRUPO + 23 limiares revistos + bloco de cabeçalho novo)
- `../gow2-recomp/recomp_mid_v2/patch_wadld_alloc_probe.py` (novo) — instalador [WADLD-ALLOC]
- `../gow2-recomp/recomp_mid_v2/test_patch_wadld_alloc_probe.py` (novo) — 7 testes
- `games/gow2/recomp_mid_v2/patch_wadld_alloc_probe.py` (novo) — mirror idêntico
- `.planning/REQUIREMENTS.md` — REQ-RDY0-4 actualizado com o resultado medido (permanece `[OPEN]`)

## Decisions Made
- REQ-RDY0-4 permanece `[OPEN]` — ver `key-decisions` no frontmatter. A aceitação
  literal ("sai 0") não foi atingida; a metodologia reformulada (a,b,c) foi.
- Par GRUPO não teve o seu `min_count` individual revisto (excepção explícita do
  plano) — mesmo com o deficit residual medido, mantém-se o limiar histórico como
  âncora de comparação futura, conforme D-3.1c.
- `apply_all_patches.sh` corrido 3x contra `v3` (não 2) para confirmar convergência
  real — medido, não assumido a partir do plano.

## Deviations from Plan

Nenhuma deviação de Regra 1-4 (nenhum bug extra corrigido além do previsto no plano,
nenhuma funcionalidade crítica em falta introduzida, nenhuma mudança arquitectural).

**Duas notas de processo, não correções:**

1. **3 passagens em vez de 2** para `apply_all_patches.sh` convergir contra `v3` — o
   plano assumia 2 (padrão histórico contra `v2`); medido honestamente como 3. Não
   afecta o resultado final (convergência confirmada, arvore estável na 3ª), só o
   número de passagens documentado.
2. **Gap novo específico de v3** (3 `FAILED` diferentes dos 4 históricos) — antecipado
   pelo próprio `<objective>` do plano como possível ("pode ou não convergir da mesma
   maneira"); medido e documentado, não investigado a fundo (fora do escopo explícito
   da Task 3), citado como candidato a plano `03-04` ou Fase 4.

**Total deviations:** 0 (Regras 1-4); 2 notas de processo documentadas acima.
**Impact on plan:** Nenhum sobre a correcção — o resultado final é o resultado medido,
citado sem maquiagem, exactamente como o plano exigia.

## Issues Encountered

Nenhum bloqueio. A única superfície de atenção foi confirmar, antes de escrever a
âncora do `patch_wadld_alloc_probe.py`, que as duas formas do lift (`.pre_v4` e actual)
tinham as diferenças exactas descritas no `<read_first>` do plano (prefixo `ctx->lr`
opcional, cast `uint32_t`/`uint64_t`) — confirmado por leitura directa dos dois
ficheiros reais antes de escrever a regex, evitando uma âncora frágil.

## Known Stubs

Nenhum stub introduzido por este plano. A probe `[WADLD-ALLOC]` é diagnóstico gated
(`PS3_TRACE_TYMAP`, OFF por default), não um stub de comportamento.

## Threat Flags

Nenhum novo. Os três boundaries do `threat_model` do `03-03-PLAN.md` foram mitigados
como previsto:
- T-03-08 (OBSOLETO mal-formado mascarando regressão): mitigado — mecanismo testado
  como seletivo por marcador (Teste 2 da Task 1), prova citada para `[OPDISP]`
  reconfirmada contra `v3`.
- T-03-09 (limiar de grupo revisto baixo demais escondendo perda real): mitigado —
  limiar do grupo NÃO foi revisto (mantido no valor histórico), por isso o deficit real
  aparece no gate em vez de ser escondido.
- T-03-10 (`apply_all_patches.sh` corrompendo `v3` de forma nunca antes vista): mitigado
  — resultado medido e documentado honestamente, incluindo os 3 `FAILED` novos;
  `recomp_macos_v2` (produção) confirmado intacto por hash antes/depois.

## User Setup Required

None - nenhuma configuração de serviço externo necessária.

## Next Phase Readiness

- **Fase 3 fica com um resultado honesto, não um verde forçado.** Os mecanismos
  metodológicos dos critérios 3/4 reformulados do ROADMAP (D-3.1 a D-3.4) estão
  implementados, testados e aplicados; o número final (`rc=1`, 243/246) reflecte um
  deficit real e medido no par OPD acoplado (36) mais 3 `FAILED` novos específicos de
  `v3` nunca antes vistos.
- **REQ-RDY0-3**: já estava `[COMPLETO]` desde o Plano 03-01 — não alterado por este
  plano (fora do seu escopo de requirements; a frontmatter deste plano só lista
  `REQ-RDY0-4`).
- **REQ-RDY0-4**: permanece `[OPEN]`, com o estado medido honesto registado em
  `REQUIREMENTS.md`. Fechamento requer uma de duas coisas, nenhuma feita nesta sessão:
  (a) investigação dedicada do deficit de 36 no par OPD (candidato a `03-04` ou Fase 4),
  ou (b) decisão humana de que a metodologia (a,b,c) satisfeita já cumpre a
  reformulação do ROADMAP, independentemente do número residual.
- **Proposta explícita ao orquestrador:** um plano `03-04` (gap closure) focado
  especificamente em (1) investigar se o deficit de 36 no par OPD é des-duplicação
  genuína ou perda real, e (2) diagnosticar os 3 `FAILED` novos de `v3`
  (`patch_cc9d0_live_yield.py`, `patch_ce03c_pre_play_stop.py`,
  `patch_ce03c_wait_idle_f2b_movie.py`) — ou, alternativamente, decisão humana de
  encerrar a Fase 3 com o resultado medido tal como está, movendo ambos os itens para
  a Fase 4 (que já é "literalmente sobre isto").
- Divergência "21 marcadores" (ROADMAP original) vs 246 (MANIFEST real): continua
  citada como gap conhecido, não silenciada, não resolvida por este plano (fora de
  escopo explícito).

---
*Phase: 03-re-lift-num-diret-rio-novo-com-gate-de-baseline*
*Completed: 2026-07-26*

## Self-Check: PASSED

Ficheiros confirmados no disco: `games/gow2/lift_baseline/gen_manifest.py`,
`games/gow2/lift_baseline/test_gen_manifest.py`, `games/gow2/lift_baseline/MANIFEST.tsv`,
`../gow2-recomp/recomp_mid_v2/patch_wadld_alloc_probe.py`,
`../gow2-recomp/recomp_mid_v2/test_patch_wadld_alloc_probe.py`,
`games/gow2/recomp_mid_v2/patch_wadld_alloc_probe.py`, este SUMMARY. Commits confirmados
por `git log --oneline --all`: `9c2c58f`/`91646a9` (ps3recomp monorepo, `MANIFEST.tsv`+
`gen_manifest.py`), `2838453` (ps3recomp, mirror do patch), `6ab8bb5` (gow2-recomp,
script+teste). Testes finais: `test_gen_manifest.py` rc=0 (10/10 PASS);
`test_patch_wadld_alloc_probe.py` rc=0 (7/7 PASS); `verify_lift_baseline.sh` contra
`recomp_macos_v3` rc=1 (resultado honesto, citado, não maquiado).
