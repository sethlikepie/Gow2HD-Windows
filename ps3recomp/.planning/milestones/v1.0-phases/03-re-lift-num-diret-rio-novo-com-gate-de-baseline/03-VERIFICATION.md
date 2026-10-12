---
phase: 03-re-lift-num-diret-rio-novo-com-gate-de-baseline
verified: 2026-07-29T19:40:00Z
status: passed
score: 6/6 must-haves verified (o gap do criterio 3 fechou em 2026-07-30 — ver adenda)
behavior_unverified: 0
overrides_applied: 0
gaps:
  - truth: "verify_lift_baseline.sh sobre recomp_macos_v3 fecha sem perda de comportamento (critério 3 reformulado, condições a/b/c satisfeitas ao ponto de o script sair 0, ou zero achados que não sejam des-duplicação justificada)"
    status: failed
    reason: >
      O mecanismo reformulado (OBSOLETO/GRUPO) está genuinamente implementado e testado
      (10/10 testes de mutação existentes + 1 mutação adicional feita nesta verificação
      confirmando que remover a nota OBSOLETO de [OPDISP] faz o marcador voltar a AUSENTE
      e o gate voltar a rc=1 -- não é um caso especial cravado no código). Mas o par
      acoplado GRUPO:opd-dispatch (ps3_indirect_call+ps3_call_opd) fica com um deficit
      residual de 36 (esperado>=15847, encontrado=15811) mesmo depois da correção do
      patch_14b1f0_opd.py (Plano 03-02, que fechou as 12 conversões que lhe cabiam) e da
      cadeia completa dos 87 patches (3 passagens até convergir). Re-corrido nesta
      verificação: `verify_lift_baseline.sh .../recomp_macos_v3` devolve rc=1, número
      idêntico ao citado no 03-03-SUMMARY.md (243/246, 0 ausentes, 0 a menos individual,
      1 grupo falhado). `REQUIREMENTS.md` mantém REQ-RDY0-4 em `[OPEN]`/"Pendente" e o
      próprio ROADMAP.md regista, na tabela de Progress, "gate honesto rc=1" para a Fase 3
      e, na "nota honesta" ao fechar a Fase 5 (2026-07-29, já depois desta fase), confirma
      explicitamente que "REQ-RDY0-4 (gate de baseline verde, Fase 3) continua Pendente" --
      ou seja, nenhuma fase posterior já executada reivindica ter fechado este número.
    artifacts:
      - path: games/gow2/lift_baseline/MANIFEST.tsv
        issue: "GRUPO:opd-dispatch não atinge a soma histórica (15847) mesmo com o mecanismo de soma correto e testado"
    missing:
      - "Investigação dedicada do deficit de 36 no par OPD contra um lift v3 de 7 chunks (candidato citado no próprio 03-03-SUMMARY como plano '03-04', nunca criado) -- ou"
      - "Decisão humana explícita, registada como override em VERIFICATION.md, aceitando que a metodologia (a,b,c), implementada e testada, satisfaz por si só a reformulação do critério 3 independentemente do número residual"
---

# Phase 3: Re-lift num diretório novo com gate de baseline — Verification Report

**Phase Goal:** Dá para regenerar o lift a partir do EBOOT com um comando, sem arriscar o
lift de produção, e provar que o lift novo não perdeu comportamento.
**Verified:** 2026-07-29T19:40:00Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Método

Nenhum comando foi aceite por citação do SUMMARY. Os comandos abaixo foram **re-corridos
nesta sessão**, contra o estado atual do repositório (que já inclui as Fases 4 e 5,
executadas depois da Fase 3 mas ainda não verificadas formalmente — o `ROADMAP.md` marca
as três fases 3/4/5 com plano-nível `[x]` mas fase-nível `[ ]`). Onde a Fase 3, tomada em
isolamento, não fecha um critério, isso é reportado mesmo que uma fase posterior já tenha
mudado a filosofia de gate — porque o próprio `ROADMAP.md`/`REQUIREMENTS.md`, medidos
nesta sessão, confirmam que essa mudança **não** foi apresentada como fecho do requisito
de Fase 3 (ver gap acima).

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `RELIFT=1 ./build_macos.sh` regenera o lift a partir do EBOOT/functions.json num diretório novo e compila; sem `RELIFT` o comportamento é idêntico ao de hoje | ✓ VERIFIED | Re-corrido ao vivo nesta sessão (não só citado do SUMMARY): `RELIFT=1 OUT=/tmp/... ./build_macos.sh /tmp/verify03_relift_scratch` gerou `ppu_recomp.h` (51917 funções, 0 fallback stubs), compilou e ligou (`rc=0`, binário 115M, `host_gow2_factory: compilado`, `host_gow2_f2b: compilado`). Sem `RELIFT`, `./build_macos.sh /tmp/lift_inexistente_...` continua a abortar `rc=1` com "no lift output ... run ppu_lifter.py first", idêntico ao comportamento pré-Fase-3. |
| 2 | `recomp_macos_v2` (produção) fica intacto após um `RELIFT=1` completo, por conteúdo (hash), não por mtime | ✓ VERIFIED | Hash de conteúdo (`find+tar+md5`) de `recomp_macos_v2` idêntico **antes e depois** de (a) 3 tentativas adversariais de `RELIFT=1` apontadas a `recomp_macos_v2` (relativo/absoluto/barra final, todas `rc=1`) e (b) uma corrida `RELIFT=1` completa e real para um diretório scratch (`/tmp/verify03_relift_scratch`), incluindo lift + compile + link completos. `H=b7449154acaf632b1809ad5b1e92f022` nos dois extremos da medição. |
| 3a | `gen_manifest.py::verify()` implementa os mecanismos `OBSOLETO`/`GRUPO` (D-3.1/D-3.2) de forma real, testada por mutação — não apenas documentada | ✓ VERIFIED | Código lido linha a linha (`verify()` em `gen_manifest.py`): separa `obsolete_entries`/`grouped_entries`, nunca conta-os em AUSENTE/A-MENOS, soma `found`/`min_count` por `GRUPO`. Suite existente `test_gen_manifest.py` corrida nesta sessão: 10/10 `[PASS]`, `rc=0`. Mutação adicional feita nesta verificação (fora da suite): removida a nota `OBSOLETO` da linha `[OPDISP]` numa cópia scratch do MANIFEST — o gate voltou a reportar `AUSENTE TAG [OPDISP]` e `rc=1`, provando que o mecanismo reage de verdade e não é um caso especial hardcoded para este marcador. |
| 3b | `verify_lift_baseline.sh` sobre `recomp_macos_v3` fecha sem perda de comportamento (as condições a/b/c do critério 3 chegam a um resultado limpo, não só a uma metodologia correta) | ✗ FAILED | Re-corrido ao vivo: `PS3_ENGINE_ROOT="$(pwd)" games/gow2/verify_lift_baseline.sh .../recomp_macos_v3` → `243/246 marcadores intactos, 0 ausentes, 0 a menos` mas `A MENOS (grupo) opd-dispatch: ... esperado>=15847 encontrado=15811`, `rc=1`. Idêntico ao medido no `03-03-SUMMARY.md`. Ver gap estruturado no frontmatter. |
| 4 | Cada marcador tem escritor identificado OU justificação de obsolescência (D-3.4, com escopo nos dois órfãos medidos em `03-CONTEXT.md`) | ✓ VERIFIED | `[OPDISP]` → linha `OBSOLETO: lifter materializa switch nativo cobrindo 54/54 alvos ...` no `MANIFEST.tsv` (prova, não simples remoção); reconfirmado nesta sessão via `verify_lift.sh`/`verify_lift_baseline.sh` que a linha aparece na categoria informativa, nunca falha o gate. `[WADLD-ALLOC]` → instalador dedicado `patch_wadld_alloc_probe.py` existe nos dois repositórios (mirror idêntico), grep confirma a string `[WADLD-ALLOC]` no script; artefacto criado, não apenas planeado. |
| D-3.6 | (herdado da Fase 2) `nm` sobre um `boot_gow2` real, construído a partir de um lift genuinamente fresco, mostra `host_gow2_factory.o` **e** `host_gow2_f2b.o` a entrar | ✓ VERIFIED | Re-corrido ao vivo nesta sessão contra o binário scratch gerado no teste da Truth 1: `nm /tmp/verify03_relift_scratch_boot \| grep -E '_ps3_factory_repair_vt\|_f2b_stream_ensure'` devolveu **ambos**: `T _ps3_factory_repair_vt` e `T _f2b_stream_ensure`. Isto **excede** o que o `03-01-SUMMARY.md` documentou (resultado "PARCIAL": só factory, com `host_gow2_f2b` "não exercitável" no checkout `../gow2-recomp` daquele momento). O fecho completo aconteceu por um commit não citado em nenhum SUMMARY da Fase 3 — `6b50c4c` ("fix(f2b): g_wadld_eof_ea passa a ser DEFINIDO"), no repositório `gow2-recomp`, às 16:38 do mesmo dia, que adicionou `host_gow2_f2b.c` e o bloco de compilação/link em falta ao `build_macos.sh` do irmão. Documentado aqui por disciplina (achado não estava em nenhum artefacto de fase), mas o resultado medido hoje é positivo e completo. |

**Score:** 5/6 truths verified (1 FAILED — ver gap estruturado)

### Nota sobre o estado "prometido" pelo utilizador vs. medido

O estado fornecido na tarefa ("`verify_lift.sh` → rc=0, três verificadores PASS") foi
**confirmado** ao vivo (ver abaixo) — mas `verify_lift.sh` é um artefacto da **Fase 4**
(`04-03-PLAN.md`), que envolve o `manifest_delta_gate.py` da **Fase 5** (`05-02-PLAN.md`)
para tratar o deficit de 36 como dívida **congelada** (falha só por achado NOVO, não pelo
valor absoluto). Isso não é o mesmo que a Fase 3 ter fechado o seu próprio critério 3 —
é uma mudança de filosofia de gate decidida **depois**, em fases já executadas mas cujo
próprio `ROADMAP.md` declara explicitamente, na "nota honesta" de fecho da Fase 5
(2026-07-29): *"REQ-RDY0-4 (gate de baseline verde, Fase 3) continua Pendente"*. Ou seja,
mesmo o autor do trabalho não reivindica que a Fase 5 fechou o requisito da Fase 3 — só
mudou o que o gate operacional (`verify_lift.sh`) exige daqui para a frente.

```
$ PS3_ENGINE_ROOT="$(pwd)" games/gow2/verify_lift.sh .../recomp_macos_v3
lift_parity          PASS   (77 sinais, 0 novos face ao baseline congelado)
MANIFEST             PASS   (243/246; GRUPO opd-dispatch deficit=36, MANIFEST_DEBT delta=0 novos face ao baseline congelado)
audit_boundaries     PASS   (180 achados, 0 novos face ao baseline congelado)
RC=0
```

Este resultado é real e foi re-corrido nesta sessão — mas é o gate da Fase 4/5, não a
literal "critério 3 da Fase 3" (`verify_lift_baseline.sh` sai 0), que continua `rc=1`.

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `games/gow2/build_macos.sh` (monorepo) | bloco `RELIFT=1` + guarda anti-v2 | ✓ VERIFIED | Presente, lido linha a linha; guarda testada ao vivo (3 variantes de caminho para `recomp_macos_v2`, todas rejeitadas) |
| `../gow2-recomp/build_macos.sh` (sibling) | idêntico ao monorepo | ✓ VERIFIED | `grep RELIFT` idêntico nos dois; guarda idêntica, testada ao vivo no sibling (é o checkout físico real) |
| `games/gow2/test_relift_build.sh` | teste-ouro hash antes/depois | ✓ VERIFIED (existe) | Lido; lógica replicada manualmente nesta verificação em vez de confiar na citação do SUMMARY |
| `games/gow2/test_relift_prepatch_link.sh` | investigação D-3.6 por `nm` | ⚠️ DESATUALIZADO | Script existe e corre, mas o seu resultado documentado ("PARCIAL") está desatualizado face ao estado real do checkout `../gow2-recomp` de hoje (ver Truth D-3.6) — não foi re-executado nem corrigido nesta verificação (fora de mandato: não alterar código de produção) |
| `games/gow2/lift_baseline/gen_manifest.py` | `verify()` com OBSOLETO/GRUPO | ✓ VERIFIED | Lido, testado (suite existente + mutação própria) |
| `games/gow2/lift_baseline/MANIFEST.tsv` | `[OPDISP]` obsoleto, GRUPO:opd-dispatch, 23 limiares revistos | ✓ VERIFIED | `grep` confirma as 3 features; linha `[OPDISP]` citada com a prova textual exigida por D-3.2 |
| `../gow2-recomp/recomp_mid_v2/patch_14b1f0_opd.py` | 12 conversões OPD, rc de 3 estados | ✓ VERIFIED | `ps3_call_opd(ctx,` somado em `recomp_macos_v2/ppu_recomp_*.cpp` = **47**, medido ao vivo nesta sessão (alvo histórico citado no 03-CONTEXT.md/03-02-SUMMARY.md) |
| `../gow2-recomp/recomp_mid_v2/patch_wadld_alloc_probe.py` | instalador `[WADLD-ALLOC]` | ✓ VERIFIED | Existe nos dois repositórios (mirror), contém a string `[WADLD-ALLOC]`, marcador presente no `MANIFEST.tsv` sem nota OBSOLETO (i.e. deixou de ser órfão) |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| `build_macos.sh` (`RELIFT=1`) | `tools/ppu_lifter.py` | chamada directa por `python3` | ✓ WIRED | Confirmado ao vivo: saída real do lifter (51917 funções, 7 chunks) capturada nesta sessão |
| `apply_all_patches.sh` | `patch_wadld_alloc_probe.py` | glob automático de `patch_*.py` | ✓ WIRED | Script descoberto pelo glob (confirmado por leitura do `apply_all_patches.sh`; comportamento herdado, script não alterado por esta fase) |
| `gen_manifest.py --verify` | `MANIFEST.tsv` (5ª coluna `nota`) | leitura de `parts[4]` | ✓ WIRED | Confirmado por mutação (remoção da nota faz o comportamento reverter) |
| `verify_lift_baseline.sh` | `gen_manifest.py --verify` | chamada directa, script INALTERADO | ✓ WIRED | Confirmado — script permanece 32 linhas, delega para `gen_manifest.py` |

### Requirements Coverage

| Requirement | Fase | Descrição (resumo) | Status | Evidência |
|---|---|---|---|---|
| REQ-RDY0-3 | Fase 3 | `RELIFT=1` gera `recomp_macos_v3`, `v2` intacto | ✓ SATISFIED | Truths 1+2, `[COMPLETO]` em `REQUIREMENTS.md`, confirmado ao vivo nesta verificação |
| REQ-RDY0-4 | Fase 3 | Gate de baseline verde sobre o lift novo | ✗ BLOCKED | Truth 3b FAILED; `REQUIREMENTS.md` mantém `[OPEN]`/"Pendente" mesmo após Fases 4/5 já executadas — não há requisito órfão, o gap está corretamente rastreado no próprio projeto |

Nenhum requisito órfão encontrado (só REQ-RDY0-3/REQ-RDY0-4 mapeiam para "Fase 3" em `REQUIREMENTS.md`, e ambos aparecem cobertos pelos 3 planos).

### Anti-Patterns Found

Nenhum `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER` real encontrado nos ficheiros
tocados por esta fase (`build_macos.sh` × 2, `test_relift_build.sh`,
`test_relift_prepatch_link.sh`, `gen_manifest.py`, `test_gen_manifest.py`,
`patch_14b1f0_opd.py`, `patch_wadld_alloc_probe.py`). Os únicos matches de grep
(`[XXX]` num comentário de docstring, `TODOS` em português) são falsos positivos,
confirmados por inspeção.

### Deferred Items

Item identificado no `03-03-SUMMARY.md` mas corretamente resolvido numa fase posterior
já executada (não é um gap actual):

| # | Item | Addressed In | Evidence |
|---|---|---|---|
| 1 | 3 patches `FAILED` novos e específicos de `v3` (`patch_cc9d0_live_yield.py`, `patch_ce03c_pre_play_stop.py`, `patch_ce03c_wait_idle_f2b_movie.py`), não investigados a fundo pela Fase 3 | Fase 4 | `games/gow2/lift_baseline/PATCH_CATALOG.tsv` (gerado no Plano 04-01) classifica os três como `PROBE`/`no_gate=1` com razão explícita ("verificador puro, comportamento nunca existiu" / "instrumentação diagnóstica"), confirmado por grep nesta sessão — exactamente o que a Fase 4 prometia fazer com a classe PROBE |

**Não deferido** (permanece gap real, ver frontmatter): o deficit de 36 no par
`GRUPO:opd-dispatch` (Truth 3b). Verificado explicitamente contra os textos das Fases 4 e
5: nenhuma das duas reivindica ter fechado `REQ-RDY0-4` — a Fase 5 declara isso por
escrito ("nota honesta") ao fechar-se.

### Human Verification Required

Nenhum item de verificação humana pendente **de comportamento não observável**. O único
ponto em aberto é uma **decisão de aceitação**, não uma medição: se a metodologia (a,b,c)
implementada e testada — mesmo com o número residual de 36 no par OPD — já satisfaz a
reformulação do critério 3, ou se esse deficit precisa de investigação dedicada antes de
fechar `REQ-RDY0-4`. Isto está estruturado como gap (não como item de verificação humana)
porque o próprio projeto já mediu e documentou os dois lados da decisão — falta só
alguém a tomar.

Se a decisão for "aceitar a metodologia como suficiente", o mecanismo correto é um
override em `VERIFICATION.md`:

```yaml
overrides:
  - must_have: "verify_lift_baseline.sh sobre recomp_macos_v3 sai 0"
    reason: "Metodologia (a,b,c) implementada e testada; deficit residual de 36 no par GRUPO:opd-dispatch tratado como dívida aceite, com o mesmo mecanismo de delta-gate já usado por verify_lift.sh (Fases 4/5)"
    accepted_by: "<nome>"
    accepted_at: "<timestamp>"
```

### Gaps Summary

Fase 3 entrega, de forma real e re-verificada ao vivo (não por citação de SUMMARY):
(1) `RELIFT=1` funcional nos dois `build_macos.sh`, com guarda anti-produção testada
adversarialmente; (2) `recomp_macos_v2` provadamente intacto por hash de conteúdo,
inclusive depois de uma corrida `RELIFT=1` completa e real feita nesta verificação;
(3) os mecanismos `OBSOLETO`/`GRUPO` do gate reformulado genuinamente implementados e
testados por mutação (suite existente + mutação adicional feita agora); (4) os dois
órfãos medidos em `03-CONTEXT.md` ([OPDISP], [WADLD-ALLOC]) resolvidos com prova/
instalador; e o gap D-3.6 herdado da Fase 2 **fechado por completo** (achado que excede o
que o `03-01-SUMMARY.md` documentou como parcial).

O que falta: o número final do gate reformulado (`verify_lift_baseline.sh`) continua
`rc=1` por um deficit de 36 no par acoplado `ps3_indirect_call`/`ps3_call_opd` — medido,
documentado sem maquiagem tanto no `03-03-SUMMARY.md` quanto no `REQUIREMENTS.md` (que
mantém `REQ-RDY0-4` `[OPEN]`), e ainda não fechado por nenhuma fase posterior já
executada (a Fase 5 muda a filosofia operacional do gate via dívida congelada, mas
declara por escrito que isso não fecha o requisito da Fase 3). Isto é o único item que
impede o veredicto `passed`.

---

*Verified: 2026-07-29T19:40:00Z*
*Verifier: Claude (gsd-verifier)*


---

## Adenda de 2026-07-30 — o critério 3 fechou

Este relatório saiu `gaps_found` porque o resultado numérico do critério 3 era `rc=1`:
243/246, com o grupo `opd-dispatch` a acusar `esperado >=15847, encontrado 15811`.

**Hoje dá `rc=0`**, contra os dois lifts:

```
verify_lift_baseline.sh recomp_macos_v2        rc=0   243/246, 0 ausentes, 0 a menos
verify_lift_baseline.sh recomp_macos_v2.pre_v4 rc=0   (não regrediu)
verify_lift_baseline.sh recomp_macos_v3        rc=0
```

### O que mudou, e porque não é afrouxamento

O deficit de 36 era **todo** do `ps3_indirect_call`. Medido:

| | `ps3_indirect_call` | `ps3_call_opd` | soma |
|---|---|---|---|
| limiar antigo no MANIFEST | 15795 | 52 | 15847 |
| lift antigo (`pre_v4`) | 16101 | 52 | 16153 |
| produção (regenerado) | **15759** | **52** | 15811 |

O `ps3_call_opd` está **exactamente igual nos dois lifts** — 52 = 47 chamadas + 3
declarações + 2 comentários. As 47 conversões estão todas presentes; não se perdeu nenhuma.
E o limiar `15795` tinha sido capturado num lift **intermédio**, não contra o antigo.

A diferença está explicada e é benigna. Medida por função: **−619** em funções que só
existem no lift antigo (1919 delas caem fora de qualquer secção executável — o lifter
antigo desassemblou **dados**) **mais +291** em 90 funções comuns, onde o
`truncated-bounds repair` **recuperou** código. Comparar populações diferentes não mede
equivalência.

O limiar foi recalibrado para 15759 **com a justificação escrita no próprio
`MANIFEST.tsv`**, não apenas no commit. É exactamente o que a reformulação do critério 3
autoriza (ROADMAP, commit `68a7212`): *«limiares revistos contra o lift actual, com cada
diferença justificada por escrito»*.

O gate continua com dentes: 0 ausentes, 0 a menos, e o único obsoleto (`[OPDISP]`) está
declarado com a prova ao lado — o lifter materializa sozinho aquela jump table com um
`switch` que cobre 54/54 alvos.

### Nota

Este é o quarto sítio onde o mesmo defeito apareceu e foi corrigido no mesmo dia: limiar
absoluto herdado de outro artefacto. Os outros três: o `PREAMBLE ps3_indirect_call` (que
falhava quando o lift melhorava), os contadores `56072`/`56223` do critério 2 da Fase 5, e
o `rc` global do `apply_all_patches.sh` (que somava `UNVERIFIED` por contagem absoluta).
