# Phase 23: O portão de adopção e o primeiro bloco que passa por ele - Context

**Gathered:** 2026-08-12
**Status:** Ready for planning
**Mode:** Auto-generated (fase de infraestrutura — sem áreas cinzentas de desenho)

<domain>
## Phase Boundary

Existir um portão que corre **entre blocos** de adopção do upstream e que rejeita um bloco
que faça a intro regredir — provado por **regressão injectada**, não por uma corrida verde
— e fazer o bloco mais pequeno e isolado do marco (a correcção do `sys_spu_image_import`)
atravessá-lo do princípio ao fim.

**Dentro:** UPST-02 (o portão) e LV2S-01 (o fix do `image_import` + o seu teste offline).

**Fora:** o interpretador SPU (Fase 24) e o pacing do fence GCM (Fase 25). Esta fase
constrói a ferramenta que essas duas vão atravessar, e prova-a com o bloco de risco ~zero.
</domain>

<decisions>
## Implementation Decisions

### O que já existe e NÃO se reimplementa (verificado no disco)
- `../gow2-recomp/boot_gow2` (binário de produção), `env_gow2.sh` (recipe canónica)
- `games/gow2/smoke_chain_gate.sh`, `games/gow2/accept_relift.sh` — o padrão de gate de
  três pernas com baseline congelado já existe e é o molde a seguir
- `docs/UPSTREAM_DELTA.tsv` (Fase 22) — a linha `LV2S` dá o sha do upstream (`dd936c4`) e a
  âncora nossa (`runtime/syscalls/lv2_register.c:825-826`)

### Não-negociáveis herdados (CLAUDE.md, protocolo G6)
- Matar **por PID** (TERM, depois `-9`). **Nunca `pkill -f boot_gow2`** — já custou load 75.
- `exit 124` do timeout é NORMAL no boot; não é falha.
- Probes gated por env var, OFF por default.
- Windows não pode regredir; nada de `Sleep(`/`<intrin.h>`/`__declspec` fora de `#ifdef _WIN32`.

### Claude's Discretion
Forma do script, formato do registo e decomposição em tarefas ficam ao critério do
executor, dentro dos critérios de sucesso.
</decisions>

<code_context>
## Existing Code Insights

- O bug do `LV2S-01` está em `runtime/syscalls/lv2_register.c:825-826`:
  `vm_write_be32(seg + 0x10, 0)` / `vm_write_be32(seg + 0x14, src_ea + p_off)`.
  `sys_spu_segment.src` é um **u32 em `+0x10`**, logo um guest que faça
  `read32(seg+0x10)` lê zero e faz DMA do endereço 0. Fix do upstream: `dd936c4`.
- O baseline da intro conhecido: `st620` chega a 11 na recipe de produção; o gate antigo
  exigia `>= 3`. A corrida desta sessão (`PS3_NO_RSX=1 PS3_MOVIE_EOS=0`) deu `st620=3`.
  O portão deve comparar contra um baseline **congelado e medido**, não contra um número
  citado de memória.
- Duas falhas pré-existentes em `scripts/test_python.sh` (`test_ppu_lift.py`,
  `test_ppu_lifter_emit.py`) por toolchain do host (`'string.h' file not found`). São
  dívida declarada em `.planning/phases/22-*/deferred-items.md` — **não** são regressão
  desta fase e o portão não as deve confundir com uma.
</code_context>

<specifics>
## Specific Ideas

- **Obrigação herdada da Fase 22, levantada pelo plan-checker:** esta fase altera
  `runtime/syscalls/lv2_register.c:825-826`, que é **âncora do inventário**. Depois de
  editar, correr `tools/upstream_delta.py validate --tsv docs/UPSTREAM_DELTA.tsv` e
  re-ancorar (há `--fix-anchors`), senão a CI que a Fase 22 montou fica vermelha. Isto tem
  de ser um passo do plano, não um comentário.
- O critério 4 obriga a escrever a distinção: o portão prova **não-regressão da intro**,
  e **nunca** que o caminho do `image_import` foi exercitado (`grep -r image_import
  games/gow2/` → zero). As duas afirmações ficam separadas no registo.
</specifics>

<deferred>
## Deferred Ideas

- Corrigir as duas falhas de toolchain do `scripts/test_python.sh`. Dívida declarada,
  fora da fronteira desta fase.
- Adoptar as melhorias que o upstream tem na `discover_jump_tables` e nós não (base com
  `ld` do TOC, shift do índice cru). Registado na Fase 22, fora deste marco.
</deferred>
