# Phase 21: Endurecimento, métricas e fecho - Context

**Gathered:** 2026-08-02
**Status:** Ready for planning
**Mode:** Smart discuss (autónomo). Fase de fecho — quase tudo é medir e escrever.

<domain>
## Phase Boundary

O que o marco construiu passa a estar **no caminho** de aceitação de um re-lift, e a
melhoria é **um número medido antes e depois** — não uma afirmação.

**Dentro:** sequência canónica de re-lift documentada, suite T1–T10 com `rc` real por linha,
tabela antes/depois, e o handoff que diz o que isto desbloqueia para a wall D.

**Fora:** qualquer implementação nova; resolver walls; opts ABI, SIMDe, shader recomp
(explicitamente fora, para o próximo marco não herdar scope creep por omissão).

</domain>

<decisions>
## Implementation Decisions

### O que já está feito e não se refaz

- **`accept_relift.sh` JÁ chama o `verify_lift.sh`** (perna 2, `:73`). Metade do `LEDG-02`
  está de pé desde antes deste marco. **Verificar, não reconstruir** — e dizer no results
  doc que já lá estava, em vez de o apresentar como entrega nova.
- **As 10 suites existem todas** (medido): `test_recomp_config`, `test_midasm_emit`,
  `test_invalid_instructions`, `test_ps3_analyse_jtables`, `test_weak_override_symbols`,
  `test_switch_tables`, `test_oracle_manifest`, `lift_parity`, mais as 8 de
  `games/gow2/lift_baseline/` (`test_check_contracts`, `test_gen_manifest`,
  `test_gen_catalog`, `test_gen_migration_ledger`, `test_baseline_delta`,
  `test_manifest_delta_gate`, `test_gen_contracts_candidates`, `test_oracle_step`).

### A suite T1–T10 — o `rc` é o entregável

- **Correr e registar o `rc` REAL de cada linha, incluindo os que falharem.** Um `rc` não
  medido fica marcado `não-exercitado`, **nunca presumido `0`** (G5). Uma tabela de dez
  zeros que ninguém correu é pior que uma tabela com dois vermelhos honestos.
- **T9 (smoke) e T10 (prova de re-lift) custam tempo real.** Se não forem exercitados nesta
  fase, ficam `não-exercitado` com a razão — não se inventa o número.

### A tabela antes/depois — números medidos, não estimados

| Métrica | Antes (Fase 16) | Depois | Fonte |
|---|---|---|---|
| `patch_*.py` CORRECTNESS activos | (do cabeçalho do ledger) | **72** medido agora | `PATCH_MIGRATION.tsv` |
| Fixes re-lift-safe | 0 | **4 linhas / 3 mecanismos** | ledger + `CONTRACTS.tsv` |
| `redundant` (não conta) | — | **2** | ledger |
| `verify_lift` no caminho de aceite | **já estava** | sim | `accept_relift.sh:73` |
| Passo de oráculo opcional | não | sim (`VERIFY_ORACLE=1`) | Fase 20 |
| Regressões de intro pós-midasm | — | 0 (`st620_max=11`, `OOB=0`) | smokes das Fases 17–19 |
| Tempo para achar callers de um EA | manual | `ghidra_lookup.py` | Fase 20 |

**A contagem do XEN-05 usa as duas leituras** (linhas **e** mecanismos), como as Fases 18–19
estabeleceram. A leitura conservadora (3 mecanismos) é a que decide, e fecha.

### Honestidade no fecho — a regra que governa esta fase

Este marco produziu vários resultados que **não** são vitórias e que **têm de aparecer no
results doc**, não só nos SUMMARYs:

- **2 patches `redundant`** — os jumptables nunca foram necessários; não contam para o XEN-05.
- **`RE-03` fecha só na metade soft**; o MCP ficou `wont` com 4 bloqueios medidos.
- **`sticky` é NÃO-OBSERVÁVEL** (Fase 16) — e o CLAUDE.md prescreve `sticky_pub>=1` como
  critério de não-regressão pós-merge, que **não é verificável neste build**. Isto é um
  achado que afecta a documentação do projecto e deve ser dito.
- **`bytes_read` não-exercitado** — o aceite `smoke:` do `patch_fallthrough_2550c8.py`
  continua por correr.
- **Os P0 `OPD` não são migráveis por weak** (Fase 19) — são cirurgia no interior, não
  substituição de função. O `alvo=weak` dessas linhas ficou "por rever".
- **`analyze_eboot_ghidra.sh` não correu de ponta a ponta** (Fase 20) — re-análise headless
  custa horas.
- **`patch_fallthrough_2550c8.py` foi reclassificado, não migrado** (`alvo=lifter`) — a
  auditoria do fallthrough cross-fragment continua P1.

**Um marco que fecha com estas ressalvas escritas vale mais que um que as omite.** O
CLAUDE.md tem uma regra 4 explícita sobre isto.

### O handoff

- **O que desbloqueia a wall D:** a sessão de RE da Fase 20 já produziu material real — a
  tabela de tipos `PTR_DAT_0053ef1c` é escrita por `0x0041FA90` indexada por
  `(w0>>16)&0xFFF`, mas 12 dos 18 consumidores (incluindo a parede 1 `func_002545B0`)
  indexam por `w0 & 0xFFFF`; divergem em `0x40030001` (1 vs 3). Isso acrescenta uma
  **terceira hipótese** ao critério 1 da Fase 11, que só listava duas exclusivas.
  O próximo passo já está desenhado: sonda `PS3_TYPEWALK_TRACE=1`, OFF por default.
- **A lista P1 seguinte:** mais midasm, auditoria do fallthrough cross-fragment, os 6 P1
  `weak` restantes, instrumentar `ps3_fios_sticky_*`, rever o `alvo` dos P0 OPD.
- **Explicitamente FORA:** opts ABI (`*_as_local`, `skip_lr`), SIMDe, shader recomp — cada
  um com plano próprio.

### Claude's Discretion

- O nome e local do results doc (o plano de origem sugere
  `games/gow2/notes/milestone-2026-08-xenon-re-results.md`).
- Se a sequência canónica de re-lift vai para `docs/MACOS_PORT_PLAN.md`, para o
  `games/gow2/README`, ou para documento próprio.
- Quais das 10 linhas da suite correr agora vs declarar não-exercitado.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets

- `games/gow2/accept_relift.sh` (221) — 3 pernas + contadores por delta; **já invoca**
  `verify_lift.sh` em `:73`.
- `games/gow2/verify_lift.sh` (155+) — agora com passo de oráculo opt-in (Fase 20).
- `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` — 140 linhas: **4 `migrated`,
  2 `redundant`, 134 `todo`**; 72 são `CORRECTNESS` ainda activos.
- `games/gow2/promote_lift.sh` — a promoção tem gate próprio (não é desta fase promover).
- Os SUMMARYs e VERIFICATIONs das Fases 16–20 — a matéria-prima do results doc.

### Established Patterns

- **`rc` medido e registado, `não-exercitado` quando não foi** (G5).
- **Delta contra baseline congelado** — nunca limiar cru.
- **Duas leituras da métrica** (linhas e mecanismos), a conservadora decide.
- **Diagnóstico honesto é entrega válida** — Fases 8, 9, 10, 18, 20.

### Integration Points

- O results doc é o que a auditoria do marco vai ler.
- A Fase 11 do v1.1 (as quatro paredes) retoma depois, com o playbook e o mid-asm/weak já
  disponíveis — as correcções dela passam a sobreviver ao re-lift, que era a razão de o
  v1.3 vir primeiro.

</code_context>

<specifics>
## Specific Ideas

- A sequência canónica de re-lift, do plano de origem §9:
  1. `find_functions` / `functions.json` (se bounds mudaram)
  2. `ppu_lifter --config gow2_recomp.toml --functions ... -o LIFT_NEW`
  3. **(não)** aplicar patches de correctness migrados
  4. `apply_all_patches.sh` — só PROBE / LEGACY
  5. `verify_lift.sh LIFT_NEW`
  6. `build_macos.sh`
  7. smoke intro 25–40 s
  8. `accept_relift` / promoção só se 5–7 verdes
- Nota medida na Fase 19: **o TOML de produção já tem `emit_weak_wrappers = true`**, logo um
  `RELIFT=1` sobre produção passa a emitir o par `__imp_`/wrapper. A sequência documentada
  deve dizer isto.

</specifics>

<deferred>
## Deferred Ideas

- Promover qualquer lift a produção — gate próprio, decisão humana.
- Resolver a wall D ou as quatro paredes — Fase 11 do v1.1.
- Reconciliar o espelho subtree — dívida do v1.0.

</deferred>
