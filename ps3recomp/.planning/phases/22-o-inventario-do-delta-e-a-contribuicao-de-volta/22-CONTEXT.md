# Phase 22: O inventário do delta e a contribuição de volta - Context

**Gathered:** 2026-08-12
**Status:** Ready for planning
**Mode:** Auto-generated (fase de infraestrutura — sem áreas cinzentas de desenho)

<domain>
## Phase Boundary

Saber, com o sha do upstream de um lado e a âncora `ficheiro:linha` nossa do outro,
exactamente o que se adopta, o que se rejeita, o que já temos e o que se devolve — porque
nenhuma das fases 23–26 sabe que commit fazer cherry-pick sem isso. E preparar, no sentido
inverso, a nossa heurística de jump tables como contribuição para o upstream.

**Dentro:** UPST-01 (o inventário) e UPST-03 (a contribuição de volta).

**Fora:** executar qualquer adopção. Esta fase produz o mapa; quem atravessa o portão é a
Fase 23 em diante. Também fora: aceitação pelo upstream do patch de UPST-03 — não a
controlamos, e um critério dependente de terceiros não se fecha com trabalho nosso.
</domain>

<decisions>
## Implementation Decisions

### Convenção da casa (não é escolha livre — é o padrão já usado no repo)
O formato do inventário é **TSV versionado**, como os ledgers que já existem:
`lift_baseline/PATCH_MIGRATION.tsv`, `CONTRACTS.tsv`, `PATCH_CATALOG.tsv`. Colunas
obrigatórias pelo critério 1: área, sha do upstream, âncora `ficheiro:linha` nossa,
decisão ∈ {`adoptar`, `rejeitar`, `já-temos`, `contribuir-de-volta`}, razão.

### Claude's Discretion
O resto das escolhas de implementação fica ao critério do executor — fase de
infraestrutura, sem superfície de desenho. Guiar-se pelo objectivo da fase, pelos
critérios de sucesso e pelas convenções do código.

### Não-negociáveis herdados (PROJECT.md / CLAUDE.md)
- A comparação corre contra o branch **vivo** do upstream, nunca contra `upstream/master`
  (parado em 2026-07-16, esconde ~230 commits).
- **Nenhuma linha do inventário sem âncora nossa.** Uma linha sem âncora é uma decisão
  sobre código que ninguém localizou.
- Nível de evidência declarado em cada aceite: `in-boot` vs `offline-unit` vs
  `nao-exercitado`. UPST-03 é `offline-unit` e não se finge outra coisa.
</decisions>

<code_context>
## Existing Code Insights

Medições já feitas a 2026-08-12 nesta sessão, a reconfirmar na execução (o critério 3
manda re-medir e escrever o número com a data, seja ele qual for):

- `git rev-list --count HEAD..upstream/fix/fold-merge-dropped-fixes` → **461**
- `upstream/master` parado em `25e5efe` (2026-07-16); branches vivos:
  `fix/fold-merge-dropped-fixes` (2026-08-09), `ydkj` (2026-08-05)
- Âncoras nossas já verificadas: `runtime/spu/spu_workload.h:6` (a frase "We do not
  interpret SPU ELFs at runtime" está lá), `libs/video/cellGcmSys.c:615`
  (`case 0x0050` → `vm_write32(GCM_CTRL_EA + 8, val)`),
  `runtime/syscalls/lv2_register.c:825-826` (`+0x10`=0 / `+0x14`=`src_ea + p_off`),
  `libs/spurs/spurs_kernel.c` (existe)
- A nossa heurística de jump tables vive em `tools/ppu_lifter.py:3840-3870`
  (`r_base_cands`, escolher o candidato que valida mais alvos). O comentário no próprio
  ficheiro já nomeia o defeito das duas linhagens do upstream ("first TOC-loaded operand
  wins", PR #82 de sp00nznet) e regista o ganho medido no GoW2: +20 dispatchers,
  +208 case targets.
- Suites de teste do lifter que servem de molde para o teste de UPST-03:
  `tools/test_ppu_lift.py`, `tools/lift_selftest.py`, `tools/test_lift_parity.py`.
</code_context>

<specifics>
## Specific Ideas

- O inventário deve registar explicitamente as duas linhas `já-temos` que já estão
  medidas, para não serem reinvestigadas: as três correcções que o upstream restaurou em
  `6bc76ae` (2026-08-09) — `bl` escreve `ctx->lr`, TOCFIX `ld r2,N(r1)`, regra rA=0 D-form
  — e o oráculo RPCS3 (`tools/rpcs3_probe/`, commit `1096c6d`).
- O `tools/compare/` do upstream (comparação Xbox 360 ↔ PS3) é candidato natural a
  `rejeitar`: o GoW2 HD é PS2→PS3 e não tem build de 360, logo a premissa da ferramenta
  não se aplica a este port.
</specifics>

<deferred>
## Deferred Ideas

- Submeter de facto o patch de UPST-03 ao upstream (PR). Fora do critério por desenho —
  não controlamos a aceitação.
- Reconciliar as linhas do v1.3 na tabela `## Progress` do ROADMAP (fases 17–21 aparecem
  `Not started` mas têm planos executados). É trabalho da auditoria do v1.3, não deste
  marco.
</deferred>
