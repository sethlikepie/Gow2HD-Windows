# Phase 5: Smoke de equivalência e promoção - Context

**Gathered:** 2026-07-29
**Status:** Ready for planning
**Mode:** Autónomo — tudo abaixo é MEDIDO

<domain>
## Phase Boundary

O lift regenerado é aceite ou rejeitado por medição repetida, e só depois de aceite
substitui o de produção — fechando o ciclo re-lift → prova → promoção.
</domain>

<measured_state>
## Estado MEDIDO — não re-derivar

### A lição que redefine esta fase

**O ciclo já foi percorrido uma vez, e o gate deixou passar perda funcional.**

Em 2026-07-26 o lift regenerado passou o smoke M0 com `st620=11` em **6/6** — melhor do
que o critério 1 exige (≥3 em ≥4 de 6) — e foi promovido a produção com base nisso.
Depois descobriu-se que lhe faltavam **36 conversões OPD** (`ps3_call_opd` de 47 para 11),
**35 guardas** `0x4F000000u` e **9 famílias de marcador**. O caminho que alimenta o
registry de shaders tinha perdido as conversões e o smoke não piscou.

Porquê: o gate M0 mede a intro até `st620=11`. A perda manifesta-se **depois** — no WADLD
e no caminho de shaders. **`st620=11 em 6/6` prova equivalência NA INTRO, não equivalência
funcional.** Foi essa a inferência errada que sustentou a promoção.

**Consequência para esta fase:** o critério 1, sozinho, é insuficiente como aceite de
promoção. O `smoke_relift_equiv.sh` tem de ser condição **necessária e não suficiente** —
a promoção exige também o `verify_lift.sh` (Fase 4) e o `apply_all_patches.sh --status`
(Fase 4), que hoje existem e não existiam quando a promoção foi feita.

### Critério 2: os contadores estão desactualizados

| Contador | ROADMAP | Medido no v3 |
|---|---|---|
| funções no lift | (56072) | **51 917** |
| `lifted functions registered` | 56223 | **52 068** |

Os 56072/56223 são do lift **antigo**, escrito à mão. Um gate que os exija falha sempre
contra qualquer lift regenerado — é o mesmo erro de limiar absoluto que já apanhámos duas
vezes (o `PREAMBLE ps3_indirect_call` a falhar quando o lift melhorou, e o grupo
`opd-dispatch` a somar lixo com código recuperado).

A diferença está explicada e é **benigna**: das ~4200 funções a menos, **1919 caem fora de
qualquer secção executável** — o lift antigo desassemblou dados. Zero dos 20750 starts do
`functions.json` faltam; zero dos 13006 EAs da `.opd` faltam; zero `ICALL-BAD` em runtime.

### Critério 4: a promoção JÁ foi executada uma vez

`recomp_macos_v2` é hoje o lift regenerado; o antigo está preservado em
`recomp_macos_v2.pre_v4` e o binário anterior em `boot_gow2.pre_v4`. O procedimento
existe de facto mas **não está escrito** em lado nenhum — foi feito à mão nesta sessão.

### O que existe hoje e não existia quando a promoção foi feita

| Ferramenta | Estado |
|---|---|
| `games/gow2/verify_lift.sh` | gate por delta; **detecta regressão e não pune melhoria** (provado por injecção nos dois sentidos) |
| `lift_baseline/CONTRACTS.tsv` | 56 contratos; cobre 22 dos 64 patches FUNCIONAL |
| `lift_baseline/check_contracts.py` | rc=0 pass / rc=1 fail / rc=2 sem contrato |
| `apply_all_patches.sh --status <tsv>` | seis estados; `UNVERIFIED` e `NO-MATCH` não são verdes |
| `lift_baseline/PATCH_CATALOG.tsv` | 87/87, 64 FUNCIONAL / 23 PROBE |
| `tools/lift_parity.py` | sinais lift↔ELF; **exige `--functions`** (sem ela é circular) |

`smoke_relift_equiv.sh` **não existe** — é a entrega do critério 1.

### Baseline de referência (medido)

- Smoke M0 do `boot_gow2` de produção: `st620=11`, tipicamente 5–6 de 6. Um run preso em 1
  é flakiness de timing (log ~3180 linhas contra ~3230, **e faz o allocate de `0x7D00000`**)
  — verificar isso antes de chamar regressão.
- `apply_all_patches.sh recomp_macos_v3 --status`: `ALREADY-APPLIED 22 / UNVERIFIED 62 /
  FAILED 3`, rc=1. Os 3 FAILED são todos PROBE.
- `verify_lift.sh recomp_macos_v3`: `lift_parity PASS / MANIFEST FAIL / audit_boundaries
  PASS`, rc=1. O `MANIFEST FAIL` é dívida declarada da Fase 3 (grupo `opd-dispatch`).
</measured_state>

<decisions>
## Decisões

### D-5.1 — O aceite de promoção passa a ter TRÊS pernas, não uma
O `smoke_relift_equiv.sh` é necessário e insuficiente. A promoção exige:
1. **smoke** — `st620 ≥ 3` em ≥ 4 de 6 (o critério original);
2. **`verify_lift.sh`** — sem achados NOVOS face aos baselines congelados (delta, não absoluto);
3. **`apply_all_patches.sh --status`** — zero `NO-MATCH`, e zero `FAILED` fora de PROBE.
A perna 2 é a que teria apanhado a perda das 36 conversões. Sem ela repetimos o erro.

### D-5.2 — Os contadores do critério 2 são REFORMULADOS, com justificação
`56072`/`56223` são do lift antigo. Passam a ser medidos **por delta contra um baseline
congelado do lift aceite**, com a diferença justificada por escrito. Um contador absoluto
calibrado noutro artefacto não mede equivalência — mede semelhança com o passado.
Mantêm-se absolutos os que **não** dependem do lift: `151 imports / 13 modules`, zero órfãos.

### D-5.3 — O procedimento de promoção fica ESCRITO e reversível
Já foi executado à mão. Passa a script, com: backup do lift e do binário anteriores,
verificação das três pernas ANTES de trocar, e um caminho de reversão testado.
Nada de promoção como efeito colateral de um build.

### D-5.4 — O relatório de aceite declara os limites, sem maquiagem
`R_PermA` fora do aceite; o caminho de present não é coberto pela receita headless;
**Gate A RED é o valor esperado**, não uma falha. E acrescenta-se o limite descoberto
nesta sessão: **o smoke não cobre WADLD nem o caminho de shaders** — foi exactamente aí
que a perda anterior se escondeu.

### D-5.5 — Logs fora dos dois repositórios, kill por PID
Protocolo G6: `PS3_NO_RSX=1`, `PS3_ENGINE_ROOT` explícito, `kill -TERM` seguido de `-9`,
**nunca `pkill -f boot_gow2`** (já derrubou a máquina com load 75). Logs em `/tmp`.
</decisions>

<code_context>
| Caminho | Papel |
|---|---|
| `gow2-recomp/smoke_m0_baseline.sh` | smoke M0 existente, 6 runs; base para o novo script |
| `gow2-recomp/lift_baseline/counters_pre.tsv` | histórico de todas as medições desta sessão |
| `games/gow2/verify_lift.sh` | perna 2 do aceite |
| `games/gow2/apply_all_patches.sh` | perna 3 (`--status`) |
| `gow2-recomp/recomp_macos_v2` | produção (é o regenerado, desde 2026-07-26) |
| `gow2-recomp/recomp_macos_v2.pre_v4` | o lift antigo, preservado |
| `gow2-recomp/boot_gow2.pre_v4` | o binário anterior, preservado |

## Armadilhas verificadas

- **Matar por PID** (TERM → 9). Nunca `pkill -f boot_gow2`.
- **`exit 124` é normal** no boot (corre até timeout).
- O grep do smoke: o formato é `[MOVIEFSM] st620 <de> -> <para>`, **não** `st620=N`;
  `4294967295` é o sem-estado inicial, não um valor.
- Testes: NUNCA `-DCMAKE_BUILD_TYPE=Release`. `ctest --test-dir build-tests`. Baseline 22/22.
- **`grep -E` não suporta lookahead negativo** — para analisar código, usar Python.
- Um run preso em `st620=1` costuma ser timing: confirmar pelo tamanho do log e pela
  presença do `allocate(0x7D00000)` antes de chamar regressão.
</code_context>

<specifics>
## Critérios, com o estado real

| # | Critério do ROADMAP | Estado |
|---|---|---|
| 1 | `smoke_relift_equiv.sh` existe e sai 0, `st620≥3` em ≥4/6 | **não existe** |
| 2 | Contadores batem: 56072 / 56223 / 151 imports / 13 modules / zero órfãos | **56072 e 56223 desactualizados** → D-5.2 |
| 3 | Protocolo G6 (kill por PID, logs fora dos repos) | por implementar |
| 4 | `git status` limpo nos dois repos; promoção escrita e executada uma vez | executada, **não escrita** → D-5.3 |
| 5 | Relatório declara os limites sem maquiagem | por escrever → D-5.4 |
</specifics>

<deferred>
## Fica para depois

- Os 42 patches FUNCIONAL sem contrato (`UNVERIFIED`) — cobertura do `CONTRACTS.tsv`.
- O `MANIFEST FAIL` do grupo `opd-dispatch` (dívida da Fase 3).
- Os 27 RED do `lift_parity` (9 TRUNCATED, 18 CALL_MISMATCH).
- Desempenho: `apply_all_patches.sh` leva ~2m14s porque o `check_contracts.py` relê o lift
  a cada chamada (~65×).
- Itens 3, 5–11 do plano de adopção do `auto-re-agent`.
</deferred>
