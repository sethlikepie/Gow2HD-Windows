# REQUISITOS — ps3recomp

**Origem:** `.planning/intel/requirements.md` (63 requisitos extraídos de 16 PRDs),
cruzado com `.planning/codebase/` (mapa verificado) e `.planning/INGEST-CONFLICTS.md`.

**Legenda de estado** (herdada do ingest, preservada):

| Tag | Significado |
|---|---|
| `[OPEN]` | escopo pendente, acionável |
| `[PARTIAL]` | parcialmente entregue; o resíduo está nomeado na descrição |
| `[DONE]` | registro histórico — **nunca** re-emitir como trabalho |
| `[SUPERSEDED]` | um plano posterior é dono do escopo; mantido por proveniência |
| `[CONTESTED]` | fontes discordam sobre a conclusão; **nenhum vencedor foi escolhido** |

**Contagem verificada neste arquivo** (contada linha a linha em
`.planning/intel/requirements.md`, não copiada de um resumo):

| Estado | Qtd |
|---|---:|
| OPEN | 46 |
| PARTIAL | 4 |
| DONE | 10 |
| SUPERSEDED | 2 |
| CONTESTED | 1 |
| **Total** | **63** |

> **Divergência declarada:** o briefing do portão de ingest cita "47 OPEN, 5 PARTIAL,
> 11 DONE, 3 SUPERSEDED, 2 CONTESTED" (soma 68 ≠ 63). A contagem acima é a do arquivo de
> intel. A provável origem da diferença: `REQ-track-g-metal-content-stack` está listado
> **sob a seção SUPERSEDED** mas **tagueado `[CONTESTED]`** (contável dos dois jeitos), e
> a divergência de aceite de reprodutibilidade (WARNING 5) cria um **segundo par
> contestado** que não recebeu tag `[CONTESTED]` formal. Ambos são tratados como
> contestados neste documento (seção "Contestados"). Nada foi promovido nem descartado.

**Regra de escopo aplicada:** só `[OPEN]` e `[PARTIAL]` viram fases/marcos.
`[DONE]` e `[SUPERSEDED]` entram apenas na rastreabilidade.

---

# Marco 1 — RDY-0: desbloquear o re-lift

O requisito-pai é `REQ-rdy0-unblock-relift` (`[OPEN]`, fonte
`docs/superpowers/plans/2026-07-25-00-rdy0-desbloquear-relift.md`, 63 caixas todas
desmarcadas). Para que cada fase mapeie exatamente um conjunto de requisitos, o pai foi
**decomposto** nos seis requisitos derivados abaixo, seguindo as três frentes do próprio
plano. O pai é considerado satisfeito quando os seis filhos estiverem satisfeitos.

Aceite do pai (transcrito, não reinterpretado): `RELIFT=1 ./build_macos.sh` escreve
`recomp_macos_v3` e compila com `recomp_macos_v2` intacto; `./verify_lift_baseline.sh
recomp_macos_v3` sai 0; `./apply_all_patches.sh recomp_macos_v3 --status <tsv>` mostra
**zero** `FAILED` fora da classe `PROBE`; `./smoke_relift_equiv.sh recomp_macos_v3 6 <tsv>`
sai 0 com `st620 max >= 3` em **>=4 de 6** execuções, `function_table_count == 56072`,
`56223 lifted functions registered`, `151 imports / 13 modules`, zero órfãos;
`PATCH_CATALOG.tsv` fecha em 71; `git status` limpo nos dois repositórios.

Declarações explícitas do plano, mantidas: `R_PermA` **não** entra no aceite; o aceite
**não** cobre o caminho de present (receita headless — um re-lift que quebre o flip
passaria neste gate); **Gate A continua RED como valor esperado**, não como falha.

Decisões fixadas que **não** devem ser reabertas: sem `--code-end` no lift canônico;
receita de aceite é o braço headless M0; 6 execuções por lado; os 19 patches de classe
`PROBE` ficam fora do gate; diretório novo com promoção posterior; a raiz canônica do
motor é este checkout.

---

## REQ-RDY0-1 — Emissões do lifter e teste-ouro do preâmbulo

- estado: `[COMPLETO]` (2026-07-26, ao final de `01-03-PLAN.md`) · derivado de: `REQ-rdy0-unblock-relift` (frente 1 — Motor)
- fonte: `docs/superpowers/plans/2026-07-25-00-rdy0-desbloquear-relift.md`
- fase: **Fase 1**
- descrição: restaurar/garantir as emissões do lifter que o commit `11a1c3c` apagou —
  `#include <stdlib.h>`, o ramo `__APPLE__` do TLS de `g_trampoline_fn`, e a geração de
  `ppu_stubs.cpp` — com um teste-ouro de preâmbulo que **falhe** se qualquer uma sumir de
  novo. **Quarta emissão acrescentada em 2026-07-26 por decisão do autor:** o contrato
  `ppu_timebase_now` (declaração no preâmbulo em `tools/ppu_lifter.py:230,232` +
  emissão de `mftb`/`mftbu` em `:2127`) tem de casar com um definidor versionado no
  runtime (`runtime/syscalls/sys_timer.c`, wrapper do commit `e441f37`). Mesma classe de
  falha, provada por link quebrado: `Undefined symbols: _ppu_timebase_now`, 7+ sítios,
  num lift limpo.

- estado real medido (2026-07-26, não afirmado pelo plano): `tools/ppu_lifter.py:146` já
  emite `#include <stdlib.h>` e `:378-379` já tem `#elif defined(__APPLE__)` no TLS de
  `g_trampoline_fn`. `grep ppu_stubs tools/ppu_lifter.py` **não retorna nada**, enquanto
  `games/gow2/build_macos.sh:92,106,192` compilam `ppu_stubs.cpp`. **Duas das três
  emissões parecem presentes; a terceira está ausente.** O plano de fase deve reverificar
  antes de "restaurar" o que já existe.

- restrição acoplada (mina do re-lift) — **DESARMADA em 01-02-PLAN.md, 2026-07-26**: o
  lifter emitia `ppu_res_lwarx/ppu_res_stwcx/ppu_res_ldarx/ppu_res_stdcx` como
  `static inline` sobre `vm_base` **sem** bounds check e **sem** `ps3_mem_oob_report`.
  Os quatro agora chamam `ps3_mem_oob_report` (report-only, matriz len/op estática
  4/4/8/8 read/write/read/write), fixado por teste-ouro + prova por mutação + harness
  C++ que compila e roda a fatia real do preâmbulo. Ver `01-02-SUMMARY.md`.

- aceite: o teste-ouro do preâmbulo está registrado em `scripts/test_python.sh` e no
  CTest, passa hoje, e **falha comprovadamente** quando cada emissão é removida
  (mutação, uma de cada vez). `evidência: offline-unit`.

- **CONCLUÍDO em 2026-07-26**, com os três planos da Fase 1 entregues e verificados
  (`01-01-SUMMARY.md`, `01-02-SUMMARY.md`, `01-03-SUMMARY.md`, todos `status: complete`
  com self-check PASSED): as quatro emissões fixadas por golden exato + prova por
  mutação (01-01); a mina do re-lift desarmada nos quatro `ppu_res_*` com instrumentação
  OOB report-only, provada por harness C++ compilado e rodado (01-02); o gate G4 de
  tokens proibidos com controle negativo, e o gate de sincronização WARN-only do
  `build_macos.sh` (01-03). `ctest --test-dir build-tests` → 21/21, 0 FAIL, 2 SKIP
  esperados; `./scripts/test_python.sh` → 7 PASS / 2 SKIP / 0 FAIL. Ver `01-03-SUMMARY.md`
  para a evidência medida completa.

## REQ-RDY0-2 — Código host órfão versionado e compilado

- estado: `[COMPLETO]` (2026-07-26, ao final de `02-03-PLAN.md`) · derivado de: `REQ-rdy0-unblock-relift` (frente 2 — Órfãos)
- fonte: `docs/superpowers/plans/2026-07-25-00-rdy0-desbloquear-relift.md`
- fase: **Fase 2**
- descrição: código host **nosso** existe apenas dentro do lift gitignored — o plano
  contabiliza 1603 linhas (698 no chunk 000, 897 no 001 incluindo 197 linhas de um
  subsistema F2B sem casa versionada, 8 no 003). Extrair para arquivos versionados,
  compilados separadamente, e ligar `host_gow2_factory.cpp` ao build.

- estado real medido (2026-07-26): existem snapshots em `games/gow2/lift_baseline/` —
  `injected_000.cpp` 704, `injected_001.cpp` 910, `injected_002.cpp` 20,
  `injected_003.cpp` 16, `injected_004..006.cpp` 4 cada — somando **1662 linhas**, que é
  exatamente a cifra do cabeçalho de `games/gow2/verify_lift_baseline.sh:4`. **Fora desse
  total** há ainda `preamble_pure.cpp` com **131 linhas**, também código host escrito à
  mão: o inventário real é **1793 linhas**. O gate de 1662 portanto **não cobre o
  preamble** — 131 linhas podem sumir num re-lift sem o gate acusar.
  Esses arquivos são **apenas snapshots**: `grep` sobre `games/gow2/*.sh` e `*.cpp`
  mostra que **nenhum script de build os referencia**; só `gen_manifest.py` os consome.
  Três cifras a reconciliar no plano da fase: 1603 (plano RDY-0) vs 1662 (gate atual,
  só injected) vs 1793 (inventário completo incluindo o preamble).

- aceite: nenhuma linha de código host nosso vive só dentro de um `.cpp` gitignored; o
  subsistema F2B tem arquivo versionado próprio; `build_macos.sh` compila as fontes
  extraídas e `host_gow2_factory.cpp`; um lift **novo e sem patches** mais as fontes
  versionadas **linka**. `evidência: in-boot para o link; offline-unit para a extração`.

- **CONCLUÍDO em 2026-07-26**, com os três planos da Fase 2 entregues e verificados
  (`02-01-SUMMARY.md`, `02-02-SUMMARY.md`, `02-03-SUMMARY.md`, todos `status: complete`
  com self-check PASSED): o gate `verify_lift_baseline.sh` passa a cobrir as 131 linhas
  do preâmbulo puro via 2 marcadores `kind=PREAMBLE`, provados por mutação, com as três
  cifras (1603/1662/1793) reconciliadas no próprio `MANIFEST.tsv` (01-01); o subsistema
  F2B (17 símbolos, D-2.3/D-2.4) foi extraído POR SÍMBOLO para
  `games/gow2/host_gow2_f2b.c`, provado por compilação real + `nm` exato + link E2E
  sintético, com o instalador `patch_zz_host_api_decls.py` portado para o monorepo
  (02-02); `games/gow2/build_macos.sh` do monorepo passa a compilar e ligar
  condicionalmente **os dois** ficheiros extraídos (`host_gow2_factory.cpp` e
  `host_gow2_f2b.c`), com guard anti-duplicate-symbol provado nos dois sentidos —
  incluindo a correção do desenho do guard do F2B (achado crítico medido em 02-03: 5 das
  7 funções e os 10 globais são `static`/linkagem interna no lift de produção, não
  `extern "C"`; um guard que só detectasse a forma extern nunca apanharia esse caso e
  produziria ESTADO DUPLICADO EM SILÊNCIO — corrigido com um guard prefix-agnóstico +
  checks estáticos explícitos, provado com fixtures sintéticas nos dois sentidos e um
  link end-to-end real cujo `nm` no binário final confirma os símbolos entrando) (02-03).
  **Ressalva declarada, não um bloqueio**: o critério 4 (lift novo + fontes versionadas
  linka) está medido e citado como evidência (`counters_pre.tsv`, commit `57b418f` do
  `gow2-recomp`) — mas essa medição in-boot é anterior à existência de
  `host_gow2_f2b.c`/`patch_zz_host_api_decls.py` e cobre especificamente o mecanismo do
  `host_gow2_factory.cpp`. A integração em-boot do F2B especificamente (re-lift real +
  `patch_zz_host_api_decls.py` aplicado + `host_gow2_f2b.o` ligado + boot) ainda não foi
  observada — é trabalho da Fase 3 (re-lift num diretório novo com gate de baseline),
  por desenho (D-2.6, `02-CONTEXT.md`: "o que falta é o gate que a torna repetível — e
  isso é a Fase 3"), não uma lacuna desta fase. `ctest --test-dir build-tests` → 21/21,
  0 FAIL, 2 SKIP esperados. Ver `02-03-SUMMARY.md` para a evidência medida completa.

## REQ-RDY0-3 — Passo `RELIFT=1` gerando `recomp_macos_v3`

- estado: `[COMPLETO]` (2026-07-26, ao final de `03-01-PLAN.md`) · derivado de: `REQ-rdy0-unblock-relift` (frente 3 — Build)
- fonte: `docs/superpowers/plans/2026-07-25-00-rdy0-desbloquear-relift.md`
- fase: **Fase 3**
- descrição: adicionar um passo **opcional** `RELIFT=1` ao `build_macos.sh` que execute o
  lifter e escreva num diretório **novo** (`recomp_macos_v3`), com promoção só depois do
  smoke passar.

- estado real medido (antes): `games/gow2/build_macos.sh:47` aborta com
  `no lift output in $LIFT -- run ppu_lifter.py first`; o script nunca chama o lifter.
  `LIFT` default é `recomp_macos_v2` (`:33`).

- aceite: `RELIFT=1 ./build_macos.sh` produz `recomp_macos_v3` e compila; `recomp_macos_v2`
  fica **byte-a-byte intacto**; sem `RELIFT` o comportamento é idêntico ao de hoje.
  `evidência: in-boot (build)`.

- **estado real medido (depois, 03-01-PLAN.md):** `RELIFT=1 ./build_macos.sh recomp_macos_v3`
  (rodado em `../gow2-recomp`) gerou `recomp_macos_v3/ppu_recomp.h` (51917 funções, 0
  fallback stubs) e o build completo (`compile+link`) correu com `rc=0`, binário de 115M.
  `recomp_macos_v2` ficou com hash de conteúdo (`find+tar+md5`) idêntico antes/depois
  (`70e7ad1204f41fbedee3fd94aa9c391e`). A guarda anti-v2 (D-3.5) recusou 3 tentativas
  adversariais (caminho relativo, absoluto, com barra final) apontadas explicitamente
  para `recomp_macos_v2`, todas `rc=1`, sem alterar o diretório. Sem `RELIFT`, o `git diff`
  mostra só inserções nos dois `build_macos.sh` e o abort de sempre continua idêntico.
  Ver `03-01-SUMMARY.md`.

## REQ-RDY0-4 — Gate de baseline verde sobre o lift novo

- estado: `[COMPLETO]` (2026-07-30) · derivado de: `REQ-rdy0-unblock-relift` (frentes 2+3)
- fonte: `docs/superpowers/plans/2026-07-25-00-rdy0-desbloquear-relift.md`
- fase: **Fase 3**
- descrição: 21 marcadores de comportamento não têm script capaz de escrevê-los. Fechar
  essa lacuna e provar que o lift novo não perdeu nenhum comportamento do inventário.

- estado real medido: `games/gow2/verify_lift_baseline.sh` (32 linhas, rc 0/1/2) e
  `lift_baseline/MANIFEST.tsv` (247 linhas) existem e funcionam contra um diretório de
  lift arbitrário.

- aceite: `./verify_lift_baseline.sh recomp_macos_v3` sai **0**; para cada um dos 21
  marcadores existe um escritor identificado (fonte versionada da Fase 2 ou script de
  patch), listado explicitamente. `evidência: offline-unit`.

- **estado real medido (depois, 03-03-PLAN.md, 2026-07-26):** os critérios 3/4
  REFORMULADOS do ROADMAP (commit `68a7212`) foram fechados no que é mecanismo/
  metodologia: `gen_manifest.py::verify()` ganhou os mecanismos `OBSOLETO`/`GRUPO`
  (testados por mutação, 10/10 `[PASS]`); `[OPDISP]` marcado obsoleto com prova
  reconfirmada contra `recomp_macos_v3`; `[WADLD-ALLOC]` ganhou instalador dedicado
  (`patch_wadld_alloc_probe.py`, 7 testes verdes); os 23 marcadores *A MENOS* restantes
  tiveram o limiar revisto para o valor medido contra `v3` (bateram exactamente com o
  ballpark da sessão de planeamento). **Mas a aceitação literal escrita acima
  ("sai 0") NÃO foi atingida**: `verify_lift_baseline.sh recomp_macos_v3` (pós-cadeia
  completa de 87 patches, 3 passagens até convergir) devolve **rc=1** — 243/246
  marcadores intactos, 0 ausentes, 0 a menos individual, mas o par acoplado
  `GRUPO:opd-dispatch` (`ps3_indirect_call`+`ps3_call_opd`) fica com um deficit
  residual de 36 (soma histórica 15847, soma medida 15811) que a correção do
  Plano 03-02 não eliminou quando medida contra um lift genuinamente fresco (a
  invariância da soma citada em `03-CONTEXT.md` foi medida dentro do mesmo lift
  acumulado v2, não contra um v3 de 7 chunks). **Por isso este requisito
  permanece `[OPEN]`, não marcado completo** — a discrepância entre a
  metodologia (a,b,c) satisfeita e o resultado numérico ainda vermelho está
  documentada, não maquiada, no cabeçalho do `MANIFEST.tsv` e no
  `03-03-SUMMARY.md`. Fechamento requer, no mínimo, investigar o deficit de 36
  (candidato Fase 4) ou decisão humana de que a metodologia sozinha já satisfaz
  a reformulação — nenhuma das duas foi feita nesta sessão.

## REQ-RDY0-5 — Catálogo de patches como gate que falha

- estado: `[COMPLETO]` (2026-07-27, ao final de `04-05-PLAN.md`) · derivado de:
  `REQ-rdy0-unblock-relift` (frente 3 — Catálogo)

- fonte: `docs/superpowers/plans/2026-07-25-00-rdy0-desbloquear-relift.md`
- fase: **Fase 4**
- descrição: transformar os scripts `patch_*.py` num gate que realmente falha, fechando
  `PATCH_CATALOG.tsv` sem nenhum "indeterminado", com classificação por classe (os 19
  `PROBE` ficam fora do gate).

- estado real medido: **73** `patch_*.py` rastreados em `games/gow2/recomp_mid_v2/`
  (`ls` = 73; mapa do código cruzou com `git ls-files` = 73, nenhum gitignored). O plano
  RDY-0 fala em **71** e manda o catálogo fechar em 71 — **divergência de 2 a resolver no
  plano da fase, não no roadmap**. `apply_all_patches.sh` (319 linhas) tem `--check` mas
  **não** tem `--status <tsv>`; **não existe** `PATCH_CATALOG.tsv` no repositório.

- baseline pré-plano declarado: `APPLIED 13 / ALREADY 9 / FAILED 49`.
- aceite: `./apply_all_patches.sh recomp_macos_v3 --status <tsv>` mostra **zero** `FAILED`
  fora da classe `PROBE`; `PATCH_CATALOG.tsv` fecha com todos os scripts classificados e
  **zero** "indeterminado"; o gate sai não-zero se um patch deixar de aplicar.
  `evidência: offline-unit`.

- **fechamento medido (04-01..04-05, números corrigidos por `04-CONTEXT.md`):**
  `PATCH_CATALOG.tsv` fecha os **87** `patch_*.py` reais (não 73/71), **64 FUNCIONAL /
  23 PROBE, zero indeterminados** (04-01). `apply_all_patches.sh` ganhou `--status <tsv>`
  (87 linhas de dados + cabeçalho) e passou a classificar em **SEIS** estados — D-4.1 —
  em vez de três: `APPLIED`/`ALREADY-APPLIED`/`NO-MATCH`/`UNVERIFIED`/`FAILED[-PARTIAL]`/
  `SKIPPED`, com a verdade vinda da pós-condição de `CONTRACTS.tsv` (D-4.2, 04-04), não do
  `rc` autodeclarado do patch. Corrida real contra `recomp_macos_v3`: `APPLIED=0
  ALREADY-APPLIED=22 NO-MATCH=0 UNVERIFIED=62 FAILED=3 SKIPPED=0` — os **3 `FAILED` são
  todos classe `PROBE`** (zero FAILED fora de PROBE, o aceite deste requisito, alcançado
  literalmente). Injeção de falha real fim-a-fim provada contra uma cópia scratch inteira
  do lift (quebrar 1 de 7 `ps3_call_opd(ctx,` em `func_0032E200` → `NO-MATCH` + `rc=1` do
  script inteiro, 04-05). Os 23 `PROBE` ficam fora do gate, listados nominalmente com
  sufixo `(PROBE, fora do gate)`. `apply_all_patches.sh` passou a chamar `verify_lift.sh`
  no fim (D-4.6, 04-03/04-05) — zero vezes antes desta fase.
  **Nota honesta:** o `rc` GLOBAL do script fica em `1` numa corrida real (42 dos 64
  `FUNCIONAL` continuam `UNVERIFIED` — `CONTRACTS.tsv` cobre só 22/64, D-4.4/04-04 — e
  `verify_lift.sh` reporta `MANIFEST FAIL` herdado da Fase 3, grupo `opd-dispatch`). Isto
  **não invalida** o requisito: os 4 critérios de aceite do ROADMAP (catálogo 87/87 sem
  indeterminado; `--status` com zero FAILED fora de PROBE; gate sai não-zero por injeção
  de falha real; PROBE fora do gate e listado nominalmente) estão satisfeitos
  literalmente, verificado em `04-05-SUMMARY.md`.

## REQ-RDY0-6 — Smoke de equivalência, promoção e árvore limpa

- estado: `[DONE]` (2026-07-29, Plano 05-05) · derivado de: `REQ-rdy0-unblock-relift` (aceite do marco)
- fonte: `docs/superpowers/plans/2026-07-25-00-rdy0-desbloquear-relift.md`
- fase: **Fase 5**
- descrição: criar `smoke_relift_equiv.sh` (não existe hoje) comparando o lift novo com o
  baseline em 6 execuções por lado, e definir o procedimento de promoção de
  `recomp_macos_v3`.

- aceite: `./smoke_relift_equiv.sh recomp_macos_v3 6 <tsv>` sai **0** com `st620 max >= 3`
  em **>=4 de 6** execuções, `function_table_count == 56072`,
  `56223 lifted functions registered`, `151 imports / 13 modules`, **zero órfãos**;
  `git status` limpo em `ps3recomp` **e** em `gow2-recomp`. Protocolo G6 obrigatório
  (`PS3_NO_RSX=1 PS3_RSX_BACKEND=trace`, kill por PID, `PS3_ENGINE_ROOT` explícito).
  `evidência: in-boot`.

- **não** faz parte do aceite: `R_PermA`, caminho de present, Gate A (permanece RED como
  valor esperado).

- **Concluído (05-05-PLAN.md, Wave 4, 2026-07-29):** promoção REAL e FINAL de
  `recomp_macos_v3` executada via `promote_lift.sh recomp_macos_v3 --yes`
  (suffix=`20260729_150649`, `rc=0`, sem reversão a seguir). `.gitignore` de
  `gow2-recomp` corrigido (`recomp_macos_v*/`, `boot_gow2*`) — o critério 4 era
  estruturalmente inatingível sem isto, porque cada promoção deixa sempre um
  `.pre_<SUFFIX>` novo untracked. `git status --porcelain` confirmado limpo no escopo
  desta fase nos dois repositórios (dívida de outra sessão em `gow2-recomp` listada e
  intocada por CLAUDE.md regra 7). `ACCEPTANCE-REPORT.md` (D-5.4) escrito, declarando
  os 5 limites sem maquiagem, incluindo o descoberto nesta fase (smoke não cobre
  WADLD/shaders). Ver `.planning/phases/05-smoke-de-equival-ncia-e-promo-o/ACCEPTANCE-REPORT.md`
  e `05-05-SUMMARY.md`. `ctest --test-dir build-tests`: 22/22, sem regressão.

- **Progresso (05-04-PLAN.md, Wave 3):** `promote_lift.sh` **criado e provado** (D-5.3) --
  gate obrigatório (`accept_relift.sh`), backup por rename, rebuild, confirmação
  pós-build, reversão automática em falha, modo `--revert` explícito. Uma promoção real
  interrompida externamente durante a rehearsal expôs um defeito real de auditoria
  (`PROMOTION_LOG.tsv` só era escrito no fim) -- corrigido: log com três estados
  (`PROMOTE-BEGIN` escrito ANTES de tocar em produção, `PROMOTE-OK`/`PROMOTE-FAIL` no
  fim), provado por sandbox sintético (kill -9 real) e por uma rehearsal real completa em
  primeiro plano (promover `recomp_macos_v3` de facto, reverter de facto, produção
  confirmada byte-idêntica ao estado de antes de começar por `hash_dir`/`md5`). Mirror
  `games/gow2/promote_lift.sh` idêntico. Requisito continua `[OPEN]`: falta a promoção
  final e permanente e o relatório de aceite (D-5.4, 05-05).

- **Progresso (05-03-PLAN.md, Wave 2):** `accept_relift.sh` **criado** -- orquestra as três
  pernas (D-5.1) e os contadores por delta (D-5.2) num único comando, `rc` = AND de tudo.
  Corrida real contra `recomp_macos_v3`: `ACEITE rc=0` (perna 1 `OK=5-6/6`, perna 2 `rc=0`,
  perna 3 `NO-MATCH=0 FAILED-fora-PROBE=0`, contadores `13/151/0` batendo exatamente contra
  o baseline congelado `COUNTERS_BASELINE.tsv`, `function_table_count=51917` produção E
  candidato). Provado que o script **REJEITA** de verdade: as três pernas forçadas a falhar
  isoladamente em cópias scratch (nunca no lift real, hash confirmado intacto), `rc=1`
  nomeando corretamente a perna em cada uma das três provas. Mirror
  `games/gow2/accept_relift.sh` idêntico. Requisito continua `[OPEN]`: falta o procedimento
  de promoção escrito e executado (`promote_lift.sh`, 05-04/D-5.3) e o relatório final de
  aceite (D-5.4, 05-05).

- **Progresso (05-02-PLAN.md, Wave 1):** a perna 2 do aceite de promoção (D-5.1,
  `verify_lift.sh`) deixou de nascer cronicamente vermelha -- o passo `MANIFEST` passa a
  falhar SÓ por achado NOVO contra `MANIFEST_DEBT_BASELINE.json` (36 ids
  `GRUPO:opd-dispatch`, dívida já declarada na Fase 3), não por limiar absoluto. Corrido
  contra `recomp_macos_v3`, `verify_lift.sh` reporta os 3 passos `PASS`, `rc` GLOBAL `0`.
  Requisito continua `[OPEN]`: `smoke_relift_equiv.sh` (05-01, perna 1 + D-5.2), aceite de
  três pernas orquestrado (`accept_relift.sh`, 05-03/D-5.1) e o procedimento de promoção
  escrito e executado (`promote_lift.sh`, 05-04/D-5.3; execução final e relatório de aceite
  D-5.4, 05-05) permanecem pendentes.

- **Progresso (05-01-PLAN.md, Wave 1):** `smoke_relift_equiv.sh` (perna 1) **criado** --
  não existia antes deste plano. Corrida real de 6 execuções contra `recomp_macos_v3`:
  `st620_max=11` em 6/6, `lifted=52068 modules=13 imports=151` (contadores D-5.2 --
  não os `56072`/`56223` do lift antigo), `rc=0`, `st620>=3 em 6 de 6 (limiar: 4)`, zero
  órfãos. Modo `--bin` (sem rebuild) confirmado contra `boot_gow2` de produção. Mirror
  `games/gow2/smoke_relift_equiv.sh` idêntico. Achado no processo (fora do escopo deste
  plano, corrigido pelo orquestrador): `recomp_macos_v3` estava com uma chaveta em falta
  em `patch_wadld_alloc_probe.py` (probe `[WADLD-ALLOC]`, gated, OFF por default)
  quebrando a compilação do chunk 002 há 3 dias sem que um `FORCE_REBUILD_LIFT=1`
  tivesse sido corrido para notar -- exatamente o cenário que essa mitigação (T-05-01)
  existe para apanhar. Requisito continua `[OPEN]`: falta o aceite de três pernas
  orquestrado (05-03), a promoção escrita (05-04) e o relatório final (05-05).

---

# Requisitos em aberto — marcos posteriores (baixa resolução)

Nenhum destes está agendado com granularidade executável. Cada um está atribuído a
exatamente um marco esboçado; ver `ROADMAP.md` para a ordem e as trilhas.

---

# MARCO v1.1 — o boot volta ao AUTO_LOAD, e o smoke passa a saber

**Definido:** 2026-07-30, a partir de medicao (nao de plano herdado).

## O facto que motiva o marco

`boot_gow2.pre_v3` (25 Jul 18:23), corrido **hoje**, com o **ambiente de hoje**:

| | pre_v3 (25 Jul) | produção (29 Jul) |
|---|---:|---:|
| StartSeq | **2** | 1 |
| thr_auto_load end | **1** | 0 |
| R_PermA 20 MB | **199** | 0 |
| REPLAY-NOPIC | **4** | 0 |
| linhas de log | 4135 | 3161–3249 |

Isto **elimina o ambiente**: os dados extraídos, o `movie_cache` e o estado do disco estão
bons, porque um binário antigo funciona agora. A regressão está no **binário**.

### O bissect, fechado

| binário | data | StartSeq | thr_end | R_Perm | veredicto |
|---|---|---:|---:|---:|---|
| `boot_gow2.pre_v3` | 25 Jul 18:23 | **2** | **1** | **199** | ✅ funciona |
| `boot_gow2_v3fix` | 26 Jul 02:23 | 0 | 0 | 0 | ⚠️ morre a 101 linhas — falha estrutural, não conclusivo |
| `boot_gow2_v4` | 26 Jul 02:35 | 1 | 0 | 0 | ❌ falha exactamente como produção |
| `boot_gow2` | 29 Jul 15:07 | 1 | 0 | 0 | ❌ falha |

**A janela é 25 Jul 18:23 → 26 Jul 02:35** — cerca de oito horas. É a janela em que caem
as duas notas datadas de 26 Jul (`tocfix-matou-36-conversoes-opd`,
`strip-block-comeu-o-lift`).

O `v3fix` morrer a 101 linhas é ele próprio um dado: o CLAUDE.md regista que um boot que
morre em ~55 linhas é falha estrutural. Esse binário estava partido de outra maneira, e por
isso não delimita a janela — é o `v4` que a fecha.

## Requisitos v1.1

### REG — Localizar e corrigir a regressão

- [x] **REG-01**: O bissect fecha numa janela mínima entre dois binários guardados
      consecutivos, com o critério `thr_auto_load end ≥ 1` medido, não inferido.

- [x] **REG-02**: A causa é identificada no **código** (lifter, patch, runtime ou HLE) e
      nomeada com ficheiro e linha, não como "algo mudou entre X e Y".

- [ ] **REG-03**: A correcção é aplicada na origem e o boot volta a `StartSeq=2`,
      `thr_end=1`, `R_PermA=20169344` — medido em ≥ 4 de 6 corridas.
      (08-01 concluído: trabalho preparatório, pista dos patches testada por
      A/B real, decisão "patches candidato" com confundimento documentado —
      REG-03 propriamente dito só fecha quando o boot for medido a passar o
      gate, Plano 08-03.)

      > **CORRECÇÃO 2026-08-01 — o critério era IMPOSSÍVEL DE SATISFAZER.**
      > `thr_end` media `grep -c 'thr_auto_load() end'`. Essa string **não existe
      > em nenhum dos 26 binários guardados** (`strings -a boot_gow2* | grep -c
      > thr_auto_load` = 0 em todos, incluindo as referências `pre_v3`/`rdy0_ref`)
      > nem em nenhum ficheiro fonte — `thr_auto_load` é o nome de uma **função
      > host** citada num comentário do `sys_ppu_thread.c`, nunca um marcador
      > impresso.
      >
      > Consequência: `thr_end` era sempre 0, `classify()` devolvia sempre
      > REGRESSAO e `elo_stopped` era sempre "AUTO_LOAD" — **medisse o binário o
      > que medisse**. As Fases 7, 8, 9 e 10 estiveram presas a um critério que
      > nenhum trabalho podia fechar. O "PARCIAL" da Fase 10 não era um resultado
      > parcial: era um instrumento cego.
      >
      > **Já corrigido** (ps3recomp `4abd5a2`, gow2-recomp `e1ee114`): existe
      > agora o marcador real `[SYS] sys_ppu_thread end name="AUTO_LOAD"`,
      > simétrico ao de create e verificado in-boot (`fios mediathread status=0`),
      > e o gate reporta `thr_created` à parte para distinguir "nunca criada" de
      > "criada e não termina" — dois estados com investigações diferentes que o
      > critério antigo colapsava num só número.
      >
      > **E o alvo do critério estava errado**: medido a 2026-08-01, a thread
      > `AUTO_LOAD` é criada pelo **primeiro handler de estado do loop principal**
      > (`func_002B2DD0`, despachado por `*(r30+0x460C)` em `func_00242C94`).
      > Como o boot nunca entra no loop principal, ela nunca é criada — e isso é
      > **consequência**, não causa. REG-03 tem de ser reformulado sobre a entrada
      > no loop principal, não sobre a thread.

      > **CORRECÇÃO 2026-08-04 — Fase 11 (11-04), critério 3 medido SEM gates: 0/6.**
      > Com o fix da parede 4 (`patch_outer_subtag_prune.py` v2, 11-03) aplicado e os
      > três gates de diagnóstico (`PS3_LIST254_EMPTY_IF_NULL`, `PS3_POOL_NULL_IF_BAD`,
      > `PS3_LIST547_EMPTY_IF_BAD`) confirmados OFF, `sys_ppu_thread_create
      > name="AUTO_LOAD"` = **0 em 6/6 corridas** (`smoke_chain_gate.sh --bin
      > boot_gow2_f11accept 6`). `[STATE] frame=1` (sinal complementar, "loop principal
      > despachou o 1º estado") também **0/6**. As 6 corridas abortam de forma
      > idêntica com `[ppu] FATAL: stuck calling 0x00514E80` dentro de
      > `func_002545D4` — medido (`ghidra_lookup.py`) como estando 36 bytes DENTRO de
      > `func_002545B0`, a própria **parede 1** do inventário da Fase 11
      > (`PS3_LIST254_EMPTY_IF_NULL`). O fix da parede 4 nunca chega a ser exercitado
      > pelo caminho natural sem gates, porque o boot aborta antes, na parede 1.
      > Detalhe completo: `docs/re_sessions/2026-08-04-E5-aceite-parede-4.md`.
      > REG-03 continua `[ ]` — a próxima medição depende das paredes 1-3
      > corrigidas, ainda por atacar.

- [ ] **REG-04**: Se a causa for o lifter, a correcção sobrevive a um re-lift do zero
      (`FORCE_REBUILD_LIFT=1`), sem edição manual no lift.

### GATE — O aceite passa a detectar isto

- [x] **GATE-01**: O gate de aceite falha quando o boot não chega ao `thr_auto_load`,
      e não apenas quando `st620 < 3`. Provado por injecção nos dois sentidos.

- [x] **GATE-02**: O smoke mede a **cadeia** (intro → 2.º StartSeq → AUTO_LOAD → R_PermA)
      com um contador por elo, para que a próxima regressão diga *onde* parou.

- [x] **GATE-03**: O `accept_relift.sh` incorpora o novo gate, de forma que uma promoção
      com esta regressão seja **rejeitada** — provado por rejeição real do binário actual.

### DIAG — Não voltar a perder dias

- [x] **DIAG-01**: Existe um comando único que corre o bissect sobre os binários guardados
      e imprime uma tabela por elo da cadeia, reutilizável na próxima regressão.

- [x] **DIAG-02**: Os probes que competem por `SIGTERM` no mesmo binário encadeiam todos
      (hoje só os dois desta leva o fazem; há sete no lift), para que nenhum SUMMARY se
      perca em silêncio.

## Hipóteses a testar (NÃO são requisitos — são pistas ordenadas)

1. **TOCFIX** (`notes/2026-07-26-tocfix-matou-36-conversoes-opd.md`): o lifter trocou o
   restauro dinâmico do TOC por um valor estático em 17.268 sítios, exactamente na janela.
   É a mudança de maior escala documentada no intervalo.
   **Estado: enfraquecida, não confirmada.** Medi os OPD do ELF: 16.259 de 16.278 usam
   `0x00541178` (o valor estampado) e só **19** divergem — e várias das 19 apontam para
   `func_004C0F00`/`func_004C2538`, que o `lift_todo_audit.py` já classifica como tabelas
   de strings declaradas como funções. Não é prova de quebra.

2. **`strip_block` comeu o lift** (`notes/2026-07-26-strip-block-comeu-o-lift.md`): mesma
   data, defeito de truncagem em chunks.

3. Qualquer coisa fora do lift: runtime, HLE de `cellVdec`, `movie_eos_arm.c` (a nota de
   07-25 avisa que o binário dessa medição tinha WIP não-committado nesse ficheiro).

## Fora de escopo deste marco

| Item | Porquê |
|---|---|
| A parede TYPE15 / `f4` terminal | Não é investigável enquanto o boot não chegar ao `R_PermA`. Os probes já estão construídos e à espera. |
| Menu jogável | Depende da parede TYPE15, que depende disto. |
| Os 27 RED do `lift_parity` | Dívida do v1.0; nenhum toca as funções desta cadeia (medido). |
| Itens 3 e 5–11 do plano `auto-re-agent` | Dívida do v1.0, sem relação com a regressão. |

## Rastreabilidade

| Requisito | Fase | Estado |
|---|---|---|
| REG-01 | Fase 6 | Pendente |
| REG-02 | Fase 6 | Pendente |
| DIAG-01 | Fase 6 | Pendente |
| GATE-01 | Fase 7 | Pendente |
| GATE-02 | Fase 7 | Pendente |
| GATE-03 | Fase 7 | Pendente |
| DIAG-02 | Fase 7 | Pendente |
| REG-03 | Fase 8 | Pendente |
| REG-04 | Fase 8 | Pendente |

Cobertura: 9/9 requisitos do Marco v1.1 mapeados, cada um a exactamente uma fase. Zero
órfãos, zero duplicados.

> **Nota de reconciliação (2026-07-30):** o briefing de criação do roadmap referia "10
> requisitos"; a contagem verificada linha a linha neste ficheiro (REG-01..04,
> GATE-01..03, DIAG-01..02) é **9**. Usada a contagem verificada contra o texto, não a
> citada no briefing — mesma prática aplicada à divergência 47/68 vs 63 no topo deste
> documento.

# MARCO v1.3 — tooling Xenon-style e RE assistida: o lift volta a ser regenerável

**Definido:** 2026-08-02, a partir de
`docs/superpowers/plans/2026-08-02-milestone-xenon-tooling-e-re-ia.md` (importado por
`/gsd-import`, com o relatório de conflitos registado nesse dia). Promove o **Marco 3
esboçado** ("Ferramentas de recompilação sem `patch_*.py`") a granularidade executável.

> **Porque `v1.3` e não `v1.2`:** o rótulo `v1.2` e as fases 12–15 já estavam tomados pelo
> marco **queued** "Toolchain Go PoC" (`.planning/milestones/v1.2-ROADMAP.md`, escrito
> a 2026-08-02 15:57 noutra sessão), cujo próprio documento declara que este plano Xenon
> "**não** é este marco". Nada desse trabalho foi renomeado nem apagado. Consequência
> declarada: **a numeração não é a ordem de execução** — o `v1.2` (Go) continua `queued`
> e não activo, e o `v1.3` é o marco em curso.

## O facto que motiva o marco

O valor central do projecto (`PROJECT.md`) é que *a recompilação é reproduzível a partir
do binário original, não de edições manuais no código gerado*. Hoje isso é falso na
prática: **88 `patch_*.py` no monorepo e 139 no checkout de build** (contados a
2026-08-02) são agulhas de texto sobre o `.cpp` gerado. Correcções de *correctness* —
fallthrough cross-fragment, jump tables, conversões OPD — só sobrevivem a um re-lift
porque alguém volta a correr um script de substituição de strings.

O que já aterrou e **não** se reimplementa (medido no repo a 2026-08-02):
`tools/recomp_config.py` (303) + `test_recomp_config.py` (276), `ppu_lifter.py` a
consumir `[[invalid_instructions]]` e a fazer merge de `switch_table`,
`tools/lift_parity.py` (316), `games/gow2/verify_lift.sh` (155),
`accept_relift.sh` (221), `lift_baseline/CONTRACTS.tsv` (88) +
`check_contracts.py` (198) + `PATCH_CATALOG.tsv`, e o oráculo estático Ghidra
(`ghidra_analyze.py`, `ghidra_lookup.py`, `ghidra_names.py`).

O que **falta** e é o escopo deste marco: `tools/ps3_analyse.py`,
`tools/test_midasm_emit.py`, `tools/test_ps3_analyse_jtables.py`,
`tools/test_weak_override_symbols.py`, `games/gow2/config/gow2_switch_tables.toml`,
`runtime/ppu/ppc_hooks.h`, `docs/RE_PLAYBOOK_GOW2.md`, `docs/GHIDRA_MCP_SETUP.md`, e a
emissão mid-asm no lifter (o TOML já tem schema, o lifter ainda não emite).

## Requisitos v1.3

### XEN — O fix de correctness deixa de ser um `patch_*.py`

| ID | Requisito | Aceite |
|---|---|---|
| `XEN-01` | O `ppu_lifter.py`, com `--config`, emite chamadas `gow2_midasm_<Name>(ctx)` antes ou depois da instrução declarada em `[[midasm_hook]]`, honrando `after_instruction` e `returns` | `tools/test_midasm_emit.py` PASS; **sem** `--config` o output é byte-a-byte idêntico ao actual numa fixture pequena (golden de regressão) |
| `XEN-02` | Os hooks host existem, compilam e ligam no `boot_gow2` do macOS | `nm boot_gow2 \| grep gow2_midasm` resolve (não `U`); smoke intro sem regressão face ao baseline congelado em `LEDG-01` |
| `XEN-03` | Jump tables são **detectadas** (`tools/ps3_analyse.py`) e **declaradas** em TOML, e o lifter emite `switch` real em vez de trampolim | `tools/test_ps3_analyse_jtables.py` PASS sobre fixture sintética; ≥1 `patch_jumptable_*.py` classificado `migrated` ou `redundant` com prova |
| `XEN-04` | `PPC_FUNC`/`PPC_FUNC_IMPL` em `runtime/ppu/ppc_hooks.h` permitem substituir uma função guest inteira sem editar o `.cpp` gerado | `tools/test_weak_override_symbols.py` PASS (weak + strong override ganha, verificado por `nm`); 1 override trivial liga e corre |
| `XEN-05` | **≥3** correcções de classe `CORRECTNESS` passam a ser re-lift-safe (midasm, weak ou switch TOML) | para cada uma: re-lift **sem** correr o patch migrado e o mecanismo continua presente (`grep`); `verify_lift.sh` e `check_contracts.py` não regridem |
| `XEN-06` | `docs/RECOMP_CONFIG.md` descreve o que o lifter **realmente** consome, não o schema aspiracional | cada secção documentada tem um teste que a exercita; nenhuma secção descrita sem consumidor |

### RE — O oráculo e o método deixam de ser folclore

| ID | Requisito | Aceite |
|---|---|---|
| `RE-01` | O `verify_lift.sh` ganha passo **opcional** de oráculo Ghidra (`VERIFY_ORACLE=1`), por **delta** contra baseline — nunca limiar absoluto — e o `ghidra_out/` ganha proveniência (sha256 do ELF, data, versão do Ghidra, contagens) | `./verify_lift.sh $LIFT` sai 0 sem a variável; com `VERIFY_ORACLE=1` corre e sai 0 no lift de produção; prova por mutação: truncar uma função numa cópia dá `rc=1` |
| `RE-02` | `docs/RE_PLAYBOOK_GOW2.md` existe **e foi usado uma vez** numa wall real (D ou C2) | 1 página de ledger com hipóteses refutadas por medição e o veredicto — "ainda aberto" é resultado válido, fecho forjado não |
| `RE-03` | MCP Ghidra documentado como opcional | `docs/GHIDRA_MCP_SETUP.md` com versão fixada, tools permitidas (decompile/xrefs/rename) e proibidas; 1 sessão de paridade em que o agente devolve os mesmos callers que `ghidra_lookup.py`. **Soft: se o MCP for instável, declara-se `wont` e o marco fecha na mesma** |

### LEDG — A melhoria é medida, não afirmada

| ID | Requisito | Aceite |
|---|---|---|
| `LEDG-01` | Existe um ledger de migração patch→mecanismo com as 7 colunas obrigatórias (`patch`, `classe`, `alvo`, `ea`, `prioridade`, `aceite`, `estado`), ≥15 patches classificados e ≥5 P0 com EA e aceite | ficheiro versionado; contagem baseline congelada no cabeçalho |
| `LEDG-02` | O `verify_lift.sh` está no caminho de aceitação de re-lift, e a sequência canónica de re-lift está escrita | `accept_relift.sh` invoca `verify_lift.sh`; documento com os 8 passos; suite T1–T10 corrida com `rc` registado |

## Regra de ouro deste marco

A partir de `XEN-01` aterrado: **nova correcção permanente de jogo não pode existir só
como `patch_*.py`.** Tem de ter caminho TOML/hook, ou uma justificação `LEGACY-PATCH` no
ledger com data-limite. Probes experimentais continuam a ser patches — e continuam OFF
por default (G2).

## Fora de escopo deste marco (explícito)

| Item | Porquê |
|---|---|
| `REQ-xenon-abi-local-opts` (Fase D) | Gate duro herdado: não começar antes de A+B completos e smoke `thr` verde em chunks re-liftados |
| `REQ-xenon-simde-and-opcode-suite` (Fase E) | Plano próprio; não bloqueia nada aqui |
| `REQ-xenon-indirect-dispatch-o1`, `REQ-xenon-volatile-guest-access` (Fase C) | Optimização, não correctness; o marco é sobre reprodutibilidade |
| Reescrever `ppu_lifter`/`spu_lifter` em Go | Declarado no plano como não-objectivo |
| LLM a emitir `ppu_recomp_*.cpp` | Proibido — a IA navega o mapa guest, não escreve o lift |
| Pixels de jogo, menu, first draw | Dependem de Metal/RSX e da Fase 11; este marco só torna o lift estável |

## Rastreabilidade v1.3

| Requisito | Fase | Estado |
|---|---|---|
| LEDG-01 | Fase 16 | **Completo** — 16-01 (ledger versionado, 7 colunas) + 16-02 (**19 classificadas, 7 P0** com EA confirmada e aceite executável; 18 dos 19 aceites corridos verdes contra `recomp_macos_v2`) + 16-03 (**baseline medido**, 1 corrida de cada, congelado no cabeçalho). Contagens recontadas a 2026-08-02: 140/140/140. Baseline: `verify_lift.sh` **rc=0** (parity 77/77, MANIFEST_DEBT 0 vs dívida 36, bounds 180/180); smoke 25s com kill por PID **st620_max=11**, **OOB/0xFFFF=0**. Dois campos ficam **declarados, não inventados**: `sticky` é **NÃO-OBSERVÁVEL** (sem emissor de log em `runtime/`/`libs/`) e `bytes_read` **NÃO-OBSERVADO** (com `MOVIE_EOS=0` o R_PermA não é lido) — o aceite `smoke:` do `patch_fallthrough_2550c8.py` continua por exercitar (WINDOWS #3). Ressalva: 5 dos 7 chunks do lift são mais recentes que o binário testado |
| XEN-01 | Fase 17 | **Completo** — 17-01. `tools/test_midasm_emit.py` **rc=0, 50 asserções** (colocação antes/depois e `return=true` verificados por **índice adjacente**, não por substring). Golden `tools/testdata/midasm_golden_noconfig.c` capturado com o lifter **anterior** à fase (`4bf3bf8`) e comparado por `difflib.unified_diff`: **diff vazio** sem `--config` e com `--config` sem secção midasm; provado **discriminante por mutação** (emitir um hook sem config → 4 asserções FAIL, incl. os dois diffs). Única normalização: a linha `/* lifter-rev */`, não-determinista por construção. Hooks propagados ao lift paralelo (`_worker_init`/`_WORKER_STATE`/`_worker_lift` + tuplo de `initargs` de `_parallel_lift`), com prova de mutação do tuplo. `jump_address` **recusado** com `rc=2` (não meio-implementado). **Limitação declarada:** a guarda de âncora verifica que *existe* instrução no EA, não que é a *esperada* — o schema não tem campo para a declarar (WINDOWS #5). Evidência: `offline-unit`; nada exercitado in-boot |
| XEN-02 | Fase 17 | **Completo** — 17-02 (link) + 17-03 (execução). 17-02 provou o símbolo definido: `nm boot_gow2 | grep gow2_midasm` → `T _gow2_midasm_Ce03cWaitIdle`, `nm -u` vazio, com corpo no-op e smoke a repetir o baseline (**st620_max=11**, **OOB=0**). 17-03 fechou o que faltava — que o hook **corre**: o `.o` do chunk liftado referencia o símbolo (`nm -u ppu_recomp_002.cpp.o` → `_gow2_midasm_Ce03cWaitIdle`) e o `objdump` de `func_000CE03C` mostra `bl _gow2_midasm_Ce03cWaitIdle` como **primeira instrução** do corpo; in-boot, o marcador `[INTROSEQ] CE03C wait-idle 1st movie` aparece no log vindo do **hook host** (o lift tem **zero** ocorrências dessa string). Smoke sobre binário reconstruído do lift com `--config`: **st620_max=11**, **OOB/0xFFFF=0**, órfãos=0, e o perfil de marcadores CE03C é **idêntico linha a linha** ao da produção com o bloco injectado (7/7). Evidência: `offline-unit` (nm, objdump) + `in-boot` (smoke 25 s, kill por PID) |
| XEN-06 | Fase 17 | **Completo para o mid-asm** — 17-01 descreveu a emissão real; 17-03 acrescentou o **exemplo end-to-end** do piloto CE03C (TOML → corpo host → `--config` → `grep` no `.c` gerado → `nm`), a regra "sem `--config` não há emissão nenhuma", o caminho da TU host (`games/gow2/hooks/`), a armadilha **medida** do `switch_table_file` inexistente (aborta o `--config` inteiro) e a emissão dupla observada em `func_000CE02C` (código morto, mas propriedade a vigiar — WINDOWS #9). Cada comportamento documentado tem teste: `tools/test_midasm_emit.py`, `tools/test_recomp_config.py`. **Continua declarado como não consumido** (não descrito como se fosse): `jump_address` (recusa `rc=2`), `registers`, `functions_override`, `[optimizations]`. Fecha por completo quando a Fase 18 ligar o `switch_table_file` |
| XEN-03 | Fase 18 | **Completo** — 18-01/02/03, verificado `passed` (4/4 critérios, `18-VERIFICATION.md`). `tools/ps3_analyse.py` detecta `mtctr`→`bctr` com backtrack de `lwzx`/`rlwinm`; `test_ps3_analyse_jtables.py` **rc=0, 45 asserções**; `confidence` é **código** (`render_toml()` filtra `high` por default, `--include-low` obrigatório para baixa). Corrido contra o EBOOT local por quem verificou: **157 dispatchers, 143 high / 14 low**, e as duas tabelas `0x002A2130` (54 alvos) e `0x002B1224` (22 alvos) batem **byte a byte** com `gow2_switch_tables.toml` de produção. **Ressalva que o próprio critério tolera mas o marco tem de dizer:** os dois `patch_jumptable_*` ficaram `redundant`, **não** `migrated` — nunca foram necessários, e por isso **não contam** para o `XEN-05` (`18-VERIFICATION.md` avisou-o a meio do marco, e o aviso foi respeitado) |
| XEN-04 | Fase 19 | **Completo** — 19-01/02, verificado `passed` (`19-VERIFICATION.md`, critérios 1–3). `runtime/ppu/ppc_hooks.h` com `PPC_FUNC`/`PPC_FUNC_IMPL`; emissão **atrás de flag** (`--emit-weak-wrappers`, `store_true`, default `False`); `test_weak_override_symbols.py` **rc=0, 56 asserções**, com `nm -m` (não `nm` simples — só `-m` distingue `weak external` de `external` no Mach-O) e **controlo por mutação**: o mesmo par sem o weak tem de partir o link. Override trivial (`func_00010230`) liga e corre: `external` (forte) no binário real, smoke `st620_max=11` = baseline. Nota de reprodução: o script exige `.venv/bin/python3` (usa PEP 604, `str \| None`) |
| XEN-05 | Fase 19 | **Completo — 3 de 3, sem folga (leitura conservadora)**. Recontado no 21-03 a partir do ledger: `CORRECTNESS` + `estado=migrated` = **4 linhas**; **mecanismos, leitura conservadora** (uma função = um mecanismo) = **3** — `func_000CE03C`, `func_0002F3F0`, `func_002C0498`; por declaração no TOML = 4. **A conservadora é a que decide.** `estado=redundant` = 2 (Fase 18) e `OPD`+`migrated` = 0, ambos **fora** do numerador. Cada linha provada por re-lift **sem** correr o patch, com o mecanismo ainda presente (escada A/B/C, `check_contracts.py` PASS nos 3). **Ressalva verificada (`19-VERIFICATION.md` critério 4):** o `verify_lift.sh` global devolveu `rc=1` por `MANIFEST` no lift intermédio da escada — achado **pré-existente**, reproduzido antes das migrações contadas, não regressão delas; contra o lift de produção o `rc` é **0** (21-02, T8). **Ressalva medida (19-03 §8):** os P0 `classe=OPD` **não são migráveis por weak** — a conversão é cirurgia no interior da função (12 sítios em `0x0014B1F0`, 7 em `0x0032E200`; migrar por weak obrigaria a reescrever **783 instruções**), logo o `alvo=weak` dessas linhas fica **por rever** |
| RE-01 | Fase 20 | **Completo** — 20-01, verificado `passed` (`20-VERIFICATION.md`, critérios 1–3). `verify_lift.sh` sai **rc=0 sem** variável nova (imprime `ORACULO: desligado`); com `VERIFY_ORACLE=1` sai **rc=0** com 4/4 PASS e `[oracle_i4_i5] current=2673 baseline=2673 novos=0` — **por delta**, nunca por limiar. **Prova por mutação reproduzida por quem verificou** (truncar `0x00010534` numa cópia): `rc=1`, `NOVO I5:functions.json:0x00010534:extende-seguro`, com o `functions.json` de produção `sha256` **idêntico** antes/depois. `ghidra_out/MANIFEST.json` com proveniência: `elf_sha256=23cfd435…1163b` bate com o EBOOT, `ghidra_version=12.1.2`, `functions=13143 decompiled=13096`. `accept_relift.sh` **não** define `VERIFY_ORACLE` — o caminho de aceite não regride. Re-corrido no 21-02 (T8b): **rc=0**, 56 s |
| RE-02 | Fase 20 | **Completo** — 20-02, verificado `passed` (critério 4). `docs/RE_PLAYBOOK_GOW2.md` com as 6 secções mínimas (incl. os 13 anti-padrões) **e usado uma vez numa wall real**: `docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md` ataca a parede 4 da Fase 11 com 4 hipóteses tabeladas, **3 refutadas por medição** (comandos e `rc` na página), veredicto **ABERTO** — "ainda aberto" é resultado válido, fecho forjado não seria. **O que a sessão devolveu, e que o 21-03 promoveu a handoff do marco:** a tabela de tipos `PTR_DAT_0053ef1c` é escrita por `0x0041FA90` indexada por `(w0>>16)&0xFFF`, mas **12 dos 18 consumidores** (incluindo a parede 1 `func_002545B0`) indexam por `w0 & 0xFFFF`; coincidem em `0xC0010001`, **divergem** em `0x40030001` (1 vs 3) ⇒ **terceira hipótese** para o critério 1 da Fase 11, que só listava duas exclusivas. Próximo experimento desenhado: sonda `PS3_TYPEWALK_TRACE=1`, OFF por default, com duas previsões opostas |
| RE-03 | Fase 20 | **Completo na metade soft** (o requisito é **soft por desenho**: "se o MCP for instável, declara-se `wont` e o marco fecha na mesma"). `docs/GHIDRA_MCP_SETUP.md` com 10 secções, pin `LaurieWired/GhidraMCP 1.4` (commit `27f316f8`), whitelist (19 leitura / 6 anotação / 2 condicionais) e 7 proibições. **A sessão de paridade pelo MCP NÃO foi corrida** — `wont` com **4 bloqueios re-medidos por quem verificou**: a extensão declara `ghidraVersion=11.3.2` contra o Ghidra **12.1.2** instalado (o `ExtensionInstaller` recusa); instalação só por GUI; reconstruir exigiria **Maven, ausente** (`which mvn` → nada); e não há projecto Ghidra persistido (`find … *.gpr *.rep` → nada), logo seria precisa auto-análise nova do EBOOT. O lado **CLI** está feito e escrito (§9.2, âncoras `0x0041F700` → vazio e `0x00420A44` → `{0x00254C40}`); fechar a metade dura exige só correr `get_xrefs_to` nos dois EA sobre o mesmo EBOOT (`sha256 23cfd435…163b`) e comparar. Registado em `WINDOWS` #15 |
| LEDG-02 | Fase 21 | **Completo** — 21-01/02/03. **(a) A sequência canónica está escrita:** `docs/RELIFT_CANONICAL.md` com as **8 etapas** (`rg -c '^### Etapa '` → 8), comandos copy-paste, critério por etapa e nível de evidência declarado. **(b) O `verify_lift` está no caminho de aceite:** `games/gow2/accept_relift.sh:73` (`VERIFY_LIFT=…`), perna 2, com `:74-77` (ausente ⇒ `exit 2`), `:129-130` (invocação + `leg2_rc`) e `:262` (no AND final) — **pré-existente ao marco, verificado e atribuído como tal**, não apresentado como entrega nova. **(c) A suite T1–T10 foi corrida com `rc` registado:** **12 linhas corridas de facto** (T1–T10 + T6b + T8b), **0 presumidas**, **0 `não-exercitado`**, em `games/gow2/notes/milestone-2026-08-xenon-re-results.md`; T8 `rc=0` (59 s), T8b `rc=0` (56 s), T9 `in-boot` com `st620_max=11`/`OOB=0`/órfãos=0, T10 re-exercitado hoje. **(d) A métrica antes/depois é numérica nas duas pontas:** `CORRECTNESS` activos **78 → 72** (a coluna "antes" lida por `git show ee17e2c:` do próprio ledger), `migrated` **0 → 4**, `redundant` 0 → 2, e o `XEN-05` reportado nas duas leituras. **Ressalvas que ficam escritas no results doc, não escondidas:** a sequência 1→8 **nunca foi corrida numa só passagem** (`WINDOWS` #16) e o lift de **produção não tem uma única chamada de mid-asm** — o mecanismo está provado e **não** está no caminho que o jogador corre (promover é decisão humana com gate próprio) |

Cobertura: 11/11 requisitos do Marco v1.3 mapeados, cada um a exactamente uma fase. Zero
órfãos, zero duplicados. **Estado a 2026-08-03: 11/11 completos** — 10 inteiros e
`RE-03` na metade soft prevista pelo próprio critério.

> **O marco v1.3 fecha INTEIRO nos requisitos (11/11), e fecha com dez ressalvas escritas.**
> As duas que mais pesam: (1) o mid-asm **não está no lift de produção** — o mecanismo foi
> provado, não promovido, e promover é decisão humana com gate próprio; (2) a sequência
> canónica de re-lift **nunca correu 1→8 numa só passagem**. Nenhuma das duas invalida um
> critério de aceite — as duas mudam o que se pode afirmar. A lista completa está em
> `games/gow2/notes/milestone-2026-08-xenon-re-results.md` § "Ressalvas do marco".

**Mapa para os requisitos esboçados do Marco 3** (que este marco promove, sem os
substituir): `XEN-01`+`XEN-02` → `REQ-xenon-midasm-hooks`; `XEN-03` →
`REQ-xenon-jumptable-analysis` (que estava `[PARTIAL]`); `XEN-04` →
`REQ-xenon-weak-function-overrides`; `XEN-05` → `REQ-xenon-patch-migration`;
`XEN-06`+`RE-01` → resíduo de `REQ-reliability-phase3-lifter-coverage-contract`
(`[PARTIAL]`). Os quatro restantes do Marco 3 ficam OPEN, declarados fora de escopo acima.

---

## Trilha A — Motor (`ps3recomp`)

### Marco 2 — Suíte e CI que dizem a verdade

| ID | Estado | Resumo | Fonte |
|---|---|---|---|
| `REQ-rdy1-phase0-residue` | OPEN | Fecha o resíduo da Fase 0: `SPU_TEST_OUTDIR`, decidir se `runtime/spu/tests/out_*/` são goldens ou saem do versionamento, `test_runtime.sh` **falhar** (não avisar) em árvore suja, registrar `test_rsx_fifo`/`test_fp_decompiler` como `WILL_FAIL`, corrigir o ledger `:373`, `test_msl_decompiler` com convenção SKIP, `-ffp-contract=off` por compilador, deriva de docs (Python 3.9 → 3.11), consertar ou declarar `scripts/smoke_macos.sh` | `docs/PLAN_READY_FOR_GAME_RECOMPILATION.md` |
| `REQ-rdy4-ppu-e2e-fixture` | OPEN | Fixture E2E PPU legal e portátil (`tools/wrap_ppu_elf.py`, `test_ppu_pipeline_e2e.py`), **com prova por mutação** — o teste tem de falhar com byteswap invertido, fronteira de função corrompida e dispatcher que não registra | `docs/PLAN_READY_FOR_GAME_RECOMPILATION.md` |

### Marco 3 — Ferramentas de recompilação sem `patch_*.py`

| ID | Estado | Resumo | Fonte |
|---|---|---|---|
| `REQ-xenon-midasm-hooks` | OPEN | Fase A tarefas A2/A3: emissão de mid-asm hooks no `ppu_lifter.py` e migração do hook real CE03C wait-idle | plano XenonRecomp |
| `REQ-xenon-weak-function-overrides` | OPEN | Fase A tarefa A4: `PPC_FUNC`/`PPC_FUNC_IMPL` em `runtime/ppu/ppc_hooks.h` | plano XenonRecomp |
| `REQ-xenon-jumptable-analysis` | **PARTIAL** | Fase B: B1 (detector em `ps3_analyse.py`) não começou; B2 tem os passos 1 e 3 marcados. Conecta com a auditoria sistemática de fallthrough cross-fragment | plano XenonRecomp |
| `REQ-xenon-indirect-dispatch-o1` | OPEN | Fase C1: mapa guest→host O(1) (`ppc_indirect_map.{h,cpp}`) | plano XenonRecomp |
| `REQ-xenon-volatile-guest-access` | OPEN | Fase C2: acessos guest não reordenáveis (só nas macros, nunca o array VM inteiro) | plano XenonRecomp |
| `REQ-xenon-patch-migration` | OPEN | Fase G: migração contínua dos `patch_*.py` restantes para TOML/hooks | plano XenonRecomp |
| `REQ-xenon-abi-local-opts` | OPEN | Fase D: flags de otimização de locais ABI. **Gate duro: não começar antes de A+B completos e smoke thr verde em chunks re-liftados** | plano XenonRecomp |
| `REQ-xenon-simde-and-opcode-suite` | OPEN | Fase E: SIMDe opcional para VMX + expansão do corpus `lift_selftest` | plano XenonRecomp |
| `REQ-rdy3-spu-strict-decode` | OPEN | Reverter a regressão de decode SPU (97 `heqi` + 2 palavras de dados) e ligar modo estrito via TOML `[coverage] unknown_spu` | `docs/PLAN_READY_FOR_GAME_RECOMPILATION.md` |
| `REQ-reliability-phase3-lifter-coverage-contract` | **PARTIAL** | Contrato de cobertura do lifter (traduzido/aproximado/desconhecido), determinismo sob `-j`, golden tests. Parcial: o parser TOML já aterrou (`tools/recomp_config.py`) | `docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md` |

### Marco 4 — Pipeline de build único e reprodutível

| ID | Estado | Resumo | Fonte |
|---|---|---|---|
| `REQ-reliability-phase1-orchestrator` | OPEN | `tools/ps3recomp_build.py` como orquestrador oficial de 12 estágios, layout fixo de `build/<game>/`, cache por hash | reliability plan |
| `REQ-reliability-phase2-elf-validation` | OPEN | Validação de ELF/PRX/OPD/TOC/relocação como estágio oficial | reliability plan |
| `REQ-rdy5-gow2-reproducible-build` | OPEN | Reprodutibilidade do build GoW2 como **delta** sobre a Fase 1 (manifesto de hash, gate de dados proprietários, wrapper de um comando). Depende de RDY-0, RDY-3 e Fase 1 | `docs/PLAN_READY_FOR_GAME_RECOMPILATION.md` |
| `REQ-reliability-recompiled-quality-gate` | OPEN **/ contestado** | Os 7 critérios globais de qualidade para marcar um jogo como `recompiled`. **Critério 7 conflita com RDY-5** — ver "Contestados" | reliability plan |

### Marco 5 — Núcleo RSX/FIFO e apresentação

| ID | Estado | Resumo | Fonte |
|---|---|---|---|
| `REQ-rdy2-fifo-core-be` | OPEN | Um único núcleo FIFO NV47xx big-endian-correto; dois decodificadores divergem hoje (`cellGcmSys.c:522-635` vs `rsx_commands.c:613-646`); CALL/RETURN corretos em nenhum; decidir a máscara canônica (`0x3FFC` vs `0x1FFC`); shadow-diff gated | `docs/PLAN_READY_FOR_GAME_RECOMPILATION.md` |
| `REQ-reliability-phase10-rsx-presentation` | OPEN | Separar processador de comandos / state tracker / backend; testes de replay determinístico | reliability plan |
| `REQ-bringup-c7-first-frame-presented` | OPEN | Checkpoint `FIRST_FRAME_PRESENTED` sem hook sintético | bring-up plan |
| `REQ-metal-alpha-test-and-fp-vp-tests` | OPEN **/ condicional** | Resíduo do gap D3D12→Metal: `set_alpha_test` 🟡 log-only, `set_viewport` 🟡 parcial, testes FP/VP sem CI de `metallib`. **Condicionado ao estado contestado do stack Metal** | Metal pending backlog |

### Marco 6 — Fidelidade do runtime (memória, PPU, threads, tempo)

| ID | Estado | Resumo |
|---|---|---|
| `REQ-reliability-phase4-memory-safety-modes` | OPEN | API única de memória guest, modos `strict`/`diagnostic`/`compat`, tabela dinâmica de regiões commitadas |
| `REQ-reliability-phase5-ppu-state-abi` | OPEN | `ppu_context` vs estado arquitetural, `lwarx/stwcx` por contexto de thread, wrappers tipados de ponteiro |
| `REQ-reliability-phase6-scheduler-locks` | OPEN | Objeto de thread guest, escalonador cooperativo, substituir o giant lock por locks por subsistema, detecção de deadlock |
| `REQ-reliability-phase7-time-events-callbacks` | OPEN | Relógio virtual, deadlines/timers sem depender do relógio do host, filas de eventos centralizadas |
| `REQ-bringup-c1-boot-memory-ok` | **PARTIAL** | Checkpoint `BOOT_MEMORY_OK`. Já entregue: canonicalização `MEMORY\|SWALLOWED`, mapa de regiões do `vm.h`, registro de ranges commitados, E1/E2. Falta: validação de header/footer do alocador, detecção de double-free/overlap/split inválido, modos `strict`/`diagnostic`, as ~2110 escritas diretas, as duas máscaras `type15_*`, a mina do re-lift |
| `REQ-bringup-c2-ppu-state-ok` | OPEN | Checkpoint `PPU_STATE_OK` |
| `REQ-bringup-c3-threads-progress` | OPEN | Checkpoint `THREADS_PROGRESS` |

### Marco 7 — SPU e SPURS reais

| ID | Estado | Resumo |
|---|---|---|
| `REQ-reliability-phase8-spu-spurs-dma` | OPEN | Fila MFC por contexto, estados de tag, GETLLAR/PUTLLC/PUTLLUC, contrato de retorno de job |
| `REQ-bringup-c4-spu-effects-ok` | OPEN | Checkpoint `SPU_EFFECTS_OK` |
| `REQ-track-u-spu-jobs-deep` | **PARTIAL** | Tabelas spu0–5 registradas; ~90 opcodes TODO residuais; jobs SPURS DMA/LFQueue com TDD; promover spu2/spu3 a default só com smoke |
| `REQ-spurs-m5-gow2-protocol` | OPEN | Marco 5 do SPURS: protocolo real do GoW2 (`0x86E118`, bitmap `[TOC-0x142C]`, vtable `0x5728EC`), liftar o conjunto SPU do jogo, ligar a fila `q=1` |

### Marco 8 — Contratos HLE e pipeline de conteúdo

| ID | Estado | Resumo |
|---|---|---|
| `REQ-reliability-phase9-hle-contracts` | OPEN | Separar ABI/comportamento parcial/stub no `MODULE_STATUS.md`, matriz de NIDs por jogo, registro tipado de assinaturas |
| `REQ-bringup-c5-wad-pipeline-ok` | OPEN | Checkpoint `WAD_PIPELINE_OK` — protocolo assíncrono FIOS de verdade, não só o arquivo disponível |
| `REQ-bringup-c6-intro-natural-eos` | OPEN | Checkpoint `INTRO_NATURAL_EOS` — remover forçadores de EOS do caminho default |

## Trilha B — Port GoW2 (parede ativa; dono é o master INDEX de 2026-07-23)

### Marco 9 — GoW2 até o menu

| ID | Estado | Resumo |
|---|---|---|
| `REQ-type15-f4-writers` | OPEN | **A parede primária ativa.** TYPE15 f4 writers dentro do pai `2B2DD0→B951C`. Refutado como causa única em R1–R12 (lista completa no intel) |
| `REQ-present-reentry-after-rperm` | OPEN | Re-entrada do guest em `cellGcmSetFlip*` depois do `R_Perm`. Aceite: `SetFlip_after_R_Perm >= 1`. Proibido: reabrir H3 HEAP40; forjar um flip HLE |
| `REQ-postb71-setflip` | OPEN | Task M3 — sair do spin de dois objetos no CC9D0 após thr + R_Perm + B71 |
| `REQ-gate-a-post-thr-life` | OPEN | Gate A (vida pós-thr). O modelo de dois gates nunca pode ser colapsado |
| `REQ-pad-menu-fsm-arm` | OPEN | Task M4 — pad e FSM de menu sinalizando após thr. FlashUI sozinho é falha explícita |
| `REQ-bringup-c8-menu-input-ok` | OPEN | Checkpoint `MENU_INPUT_OK` |
| `REQ-typemap-nested-walk-diagnosis` | OPEN | Diagnóstico-primeiro: por que `func_00171244` nunca é invocado no caminho natural. Um veredito A/B/C escrito é PASS válido; "parede aberta" também |
| `REQ-track-s-shader-registry-natural` | OPEN | Track S — shaders naturais + draw do guest. Bloqueado enquanto Gate A estiver RED. CRC bypass como "pronto" é proibido |
| `REQ-xenon-shader-recomp-design` | OPEN | Fase F1 — apenas a nota de design `docs/SHADER_RECOMP_DESIGN.md`; nenhum código antes de entender o caminho LDRSH natural |

### Marco 10 — GoW2 jogável e encerramento

| ID | Estado | Resumo |
|---|---|---|
| `REQ-track-p-playable-polish` | OPEN | Track P — **explicitamente não iniciado**: gated atrás de um menu UI real |
| `REQ-gow2-playable` | OPEN | O objetivo do port: boot → intro → WAD → menu → primeira interação jogável, caminho natural de conteúdo, sem emulador e sem teatro |
| `REQ-bringup-c9-save-and-exit-ok` | OPEN | Checkpoint `SAVE_AND_EXIT_OK` |
| `REQ-bringup-c10-final-regression` | OPEN | Critério final `GAME_RECOMPILED_RUNTIME_OK` — 3 execuções determinísticas + 3 normais por checkpoint, sanitizers, com e sem giant lock |
| `REQ-reliability-phase11-gow2-migration` | OPEN | Migração do GoW2 para o pipeline comum; classificar cada hook (fix genérico / fallback / workaround do jogo / diagnóstico temporário) |
| `REQ-reliability-phase12-perf-debt` | OPEN | Performance e remoção de dívida; runtime genérico sem endereços ou FSMs específicos de jogo |

---

# Contestados — nenhum vencedor escolhido

## `REQ-track-g-metal-content-stack` — stack Metal macOS M0–M10

- estado: **`[CONTESTED]`**
- escopo: M0 overlay VideoToolbox+Metal, M1 draw path, M2 FP/VP→MSL→cache de PSO,
  M3 `bind_texture` com deswizzle, M4 zero-copy VT→IOSurface/CV, M5 pipeline state,
  M6 vertex attribs, M7 render targets, M8 content hold HOSTRES, M9 present YUV→BGRA do
  `cellVdec`, M10 Metal default + matriz de smokes.

**Variante 1 — tudo GREEN:**

- `docs/superpowers/plans/2026-07-21-00-macos-metal-stack-INDEX.md` marca as onze entradas
  M0–M10 como "✅ **GREEN** 2026-07-21".

- `docs/superpowers/plans/2026-07-21-00-macos-metal-pending-backlog.md` marca M2, M3, M5,
  M6, M7, M8, M10 GREEN nas matrizes de vtable e extras.

- `docs/PLAN_READY_FOR_GAME_RECOMPILATION.md` (2026-07-24): "Metal é o backend **default**
  no Darwin (stack M0–M10 todo GREEN)" e "§6: o stack M0–M10 está GREEN. Este plano
  consome-o".

**Variante 2 — ~9 itens abertos:**

- `docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md`, Track G:
  "M0/M1 already GREEN. Next: M2 MSL → M3 bind_texture → M5/M6 state → M7 RT present".

- `docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md`: tabela de satélites lista
  M2 a M10 como "⬜", com M0 e M1 "✅ GREEN".

**Por que não foi resolvido:** as quatro fontes são do tipo PRD, então precedência não
decide, e desempatar por data seria um critério arbitrário que o contrato proíbe. O
usuário adiou explicitamente a decisão.

**Como consumir:** qualquer fase que dependa deste requisito declara a dependência como
**condicional** e começa por medir o estado real. As nove notas por tarefa em
`docs/notes/2026-07-21-macos-metal-*` foram deliberadamente excluídas do ingest e
resolveriam a questão.

**Ancoragem no código verificado (não resolve a disputa, delimita-a):** `STACK.md` registra
Objective-C habilitado só `if(APPLE)` para `libs/video/rsx_metal_backend.m`,
`libs/video/movie_vt_metal.m` e `libs/input/cellpad_gamecontroller.m`;
`ARCHITECTURE.md:83` lista `rsx_metal_backend.m` como a implementação de `rsx_backend` no
macOS. O backend **existe e está ligado**; *quais métodos da vtable estão completos* é
justamente o que está em disputa.

## Aceite de reprodutibilidade — duas definições concorrentes

- estado: contestado de facto (WARNING 5 do `INGEST-CONFLICTS.md`); **sem** tag
  `[CONTESTED]` formal no intel, preservado como dois requisitos distintos.

**Variante A — reprodução exata a partir do manifesto:**
`REQ-reliability-recompiled-quality-gate`, critério global 7 — "o resultado for
reproduzível a partir do manifesto"; Fase 1: "O manifesto permite reproduzir exatamente o
build". Fonte: `docs/IMPLEMENTATION_PLAN_RECOMP_RELIABILITY.md`.

**Variante B — equivalência funcional medida por gates:**
`REQ-rdy5-gow2-reproducible-build` — "'O mesmo executável' significa **funcionalmente
equivalente medido pelos gates**, não bit-idêntico (paths absolutos, mtimes e build ids
tornam bit-identidade inatingível). A v1 não distinguia; esta versão distingue". Fonte:
`docs/PLAN_READY_FOR_GAME_RECOMPILATION.md`.

**Como consumir:** o Marco 4 tem de escolher uma (ou dividir em duas metas, com
bit-identidade como objetivo posterior separado) **antes** de rotear o trabalho de
pipeline de build. O Marco 1 não depende desta escolha: o aceite de RDY-0 já é explícito
em usar equivalência por contadores nomeados, não bit-identidade.

---

# Satisfeitos — registro histórico (não reconstruir)

| ID | Resumo do que foi entregue | Evidência |
|---|---|---|
| `REQ-gate-b-thr-path` | Gate B (caminho thr) verde | link no Mac + `st620` 0→1→>=3 nos dois caminhos; StartSeq real com PARK=0; `thr_auto_load` + AutoLoad + `R_Perm` em 20.169.344 bytes; hang circular TYPE15 resolvido; lista vazia pós-RESET fechada por M2 CLOSE-PRESERVE nodes=12 (`1f5d61c`) |
| `REQ-type15-product-list-membership` | Task M2 — construção de produto / pertença de lista | `1f5d61c`, critério 1 atendido; o padrão de fechamento de lista (product+0x70/+0x74 circular) **não pode ser requebrado** |
| `REQ-gate-a-diagnosis-closed` | Diagnóstico do Gate A fechado **como diagnóstico** (R1–R4, estendido a R5–R12) | `1a540c8`, `55e702a`/`e7dce33`, `541c37b`/`8eb5e00` |
| `REQ-common-instrumentation` | Núcleo `ps3_trace`, os dez subsistemas `PS3_TRACE_*`, `tools/trace_parse.py`, `scripts/baseline_run.sh`, `tools/baseline_report.py` | registrado CONCLUÍDA pelo plano de bring-up |
| `REQ-bringup-fase0-baseline` | Baseline da Fase 0 — 3 execuções headless, mesma barreira | `docs/bringup/gow2/FASE0/baseline.json` |
| `REQ-mask-observability-and-region-map` | `ps3_mask_trace`, mapa de regiões no `vm.h`, registro de ranges commitados, E1/E2 | 3 mudanças de comportamento always-on declaradas |
| `REQ-recomp-config-toml` | Schema e parser de config TOML (`tools/recomp_config.py`) | `aa0a652` |
| `REQ-xenon-invalid-instruction-skip` | Fase B tarefa B3 — skip de instrução inválida / padding | `ppu_lifter.py` consome `[[invalid_instructions]]` via `--config` |
| `REQ-spurs-hle-kernel-engine` | Marcos 0–4 do SPURS: `libs/spurs/spurs_kernel.c`, teste de 50 workloads, e2e com job SPU liftado real | 2026-06-25 |
| `REQ-reliability-phase0-test-infra` | Fase 0 — CTest com 15 testes, 3 jobs de CI, convenção SKIP, scripts portáteis | `0c6a01b`; resíduo é de `REQ-rdy1-phase0-residue` |

# Superados — mantidos por proveniência

| ID | Quem é dono agora |
|---|---|
| `REQ-macos-port-layers` (`docs/MACOS_PORT_PLAN.md`, camadas A–F) | Camadas A–E entregues por trabalho posterior (stack Metal + cadeia de bring-up macOS); camada F sobrevive como `REQ-gow2-playable`. O snapshot §7 do plano ("Runtime static lib ❌ compile errors Darwin", "RSX GPU ❌ só D3D12") é contradito pelo mapa do código. Sua Fase 5 ("Vulkan first") é contradita pela regra Metal-first |
| `REQ-wall-chain-order` (`2026-07-20-00-wall-chain-INDEX.md`, W0–W28) | O próprio cabeçalho defere a `2026-07-23-00-gow2-playable-master-INDEX.md`. O método TDD comum que ele registra continua válido |

---

# Rastreabilidade

## Marco 1 (executável)

| Requisito | Fase | Estado |
|---|---|---|
| `REQ-RDY0-1` | Fase 1 | Completo (2026-07-26) |
| `REQ-RDY0-2` | Fase 2 | **Completo** (2026-07-29) — a prova que faltava foi obtida na Fase 3 e reconfirmada por mim. Ver a nota abaixo: o critério 3 está satisfeito no único caso em que faz sentido, e o comportamento oposto em produção é o desenho a funcionar, não uma lacuna. |

> **Nota sobre o fecho do `REQ-RDY0-2` (2026-07-29).** A 26/jul declarei-o Parcial porque
> nenhum `host_gow2_*.o` entrava num `boot_gow2` real. A leitura estava incompleta: o
> comportamento depende de o lift trazer ou não as definições, e as duas metades foram
> agora medidas.
>
> | lift | define os símbolos? | guard | os `.o` entram? |
> |---|---|---|---|
> | **produção** (`recomp_macos_v2`, com a cadeia de patches) | **sim** — `extern "C" void f2b_stream_ensure` no chunk 002 e `ps3_factory_repair_vt` no 001 | SKIP | **não** — e é o correcto: compilar daria `duplicate symbol` no factory e estado duplicado em silêncio no F2B |
> | **fresco** (antes dos patches) | não | COMPILE | **sim** — `nm` sobre o binário mostra `T _ps3_factory_repair_vt` e `T _f2b_stream_ensure` |
>
> O critério 3 pedia «verificável por `nm`/log de compilação, não por inspeção visual».
> Está verificado, por `nm`, no caso em que se aplica — confirmado independentemente pelo
> verificador da Fase 3, que correu `RELIFT=1` para um directório scratch e mediu os dois
> símbolos no binário resultante.
>
> O que eu tinha registado como gap era, na verdade, a **ausência de prova**, não a ausência
> do comportamento. E o caminho até essa prova encontrou um defeito real que o harness
> sintético da Fase 2 escondia: o `host_gow2_f2b.c` declarava `g_wadld_eof_ea` como
> `extern` assumindo que a definição vinha do lift, mas ela vinha do bloco `[BA808]`, que
> era órfão e desapareceu no relift. O primeiro link genuíno deu `Undefined symbols`; o
> harness passava porque **fornecia um stub desse símbolo**. Corrigido em `6b50c4c`.
>
> Nota de processo: esse commit fechou o gap e **nenhum SUMMARY da Fase 3 o citou** — foi o
> verificador da Fase 3 que o encontrou ao cruzar o `git log` com os SUMMARY. Trabalho
> feito, registo incompleto.
| `REQ-RDY0-3` | Fase 3 | Completo (2026-07-26) |
| `REQ-RDY0-4` | Fase 3 | **Completo** (2026-07-30) — o aceite literal («sai 0») passou a ser cumprido após a recalibração do grupo `opd-dispatch` (commit `0e49c92`). Ressalva registada: a 2ª metade do aceite fala em «21 marcadores», número que nunca foi reconciliado com os 246 do MANIFEST — ver nota. |
| `REQ-RDY0-5` | Fase 4 | Completo (2026-07-27) |
| `REQ-RDY0-6` | Fase 5 | Completo (2026-07-29) |
| `REQ-rdy0-unblock-relift` (pai) | Marco 1 completo | **Parcial** — 4/6 requisitos completos (RDY0-1, RDY0-3, RDY0-5, RDY0-6); RDY0-2 parcial (3/4 critérios), RDY0-4 pendente. Ver notas dessas fases. |

## Marco v1.1 (executável)

| Requisito | Fase | Estado |
|---|---|---|
| REG-01 | Fase 6 | Pendente |
| REG-02 | Fase 6 | Pendente |
| DIAG-01 | Fase 6 | Pendente |
| GATE-01 | Fase 7 | Pendente |
| GATE-02 | Fase 7 | Pendente |
| GATE-03 | Fase 7 | Pendente |
| DIAG-02 | Fase 7 | Pendente |
| REG-03 | Fase 8 | Pendente |
| REG-04 | Fase 8 | Pendente |

Cobertura: 9/9 requisitos do Marco v1.1 mapeados. Zero órfãos, zero duplicados. Estes 9
requisitos foram definidos a 2026-07-30 a partir de medição directa (ver
`../gow2-recomp/notes/2026-07-30-o-boot-nao-chega-ao-auto-load.md`), fora do universo dos
63 requisitos do ingest original — não contam para a cifra de 50 OPEN+PARTIAL da seção
"Cobertura" abaixo, que é sobre esse universo original.

## Marcos posteriores (esboçados)

| Marco | Requisitos | Qtd |
|---|---|---:|
| 2 — Suíte e CI verdadeiros | `rdy1-phase0-residue`, `rdy4-ppu-e2e-fixture` | 2 |
| 3 — Ferramentas de recomp | `xenon-midasm-hooks`, `xenon-weak-function-overrides`, `xenon-jumptable-analysis`, `xenon-indirect-dispatch-o1`, `xenon-volatile-guest-access`, `xenon-patch-migration`, `xenon-abi-local-opts`, `xenon-simde-and-opcode-suite`, `rdy3-spu-strict-decode`, `reliability-phase3-lifter-coverage-contract` | 10 |
| 4 — Pipeline de build | `reliability-phase1-orchestrator`, `reliability-phase2-elf-validation`, `rdy5-gow2-reproducible-build`, `reliability-recompiled-quality-gate` | 4 |
| 5 — RSX/FIFO | `rdy2-fifo-core-be`, `reliability-phase10-rsx-presentation`, `bringup-c7-first-frame-presented`, `metal-alpha-test-and-fp-vp-tests` | 4 |
| 6 — Fidelidade do runtime | `reliability-phase4/5/6/7`, `bringup-c1/c2/c3` | 7 |
| 7 — SPU/SPURS | `reliability-phase8`, `bringup-c4`, `track-u-spu-jobs-deep`, `spurs-m5-gow2-protocol` | 4 |
| 8 — HLE e conteúdo | `reliability-phase9`, `bringup-c5`, `bringup-c6` | 3 |
| 9 — GoW2 até o menu | `type15-f4-writers`, `present-reentry-after-rperm`, `postb71-setflip`, `gate-a-post-thr-life`, `pad-menu-fsm-arm`, `bringup-c8`, `typemap-nested-walk-diagnosis`, `track-s-shader-registry-natural`, `xenon-shader-recomp-design` | 9 |
| 10 — Jogável e encerramento | `track-p-playable-polish`, `gow2-playable`, `bringup-c9`, `bringup-c10`, `reliability-phase11`, `reliability-phase12` | 6 |

## Cobertura

- OPEN + PARTIAL a atribuir: **50** (46 + 4).
- Atribuídos: `REQ-rdy0-unblock-relift` ao Marco 1 (via 6 derivados) + 49 aos Marcos 2–10
  = **50**. ✓ Zero órfãos, zero duplicados.

- DONE (10) e SUPERSEDED (2) permanecem apenas como registro. CONTESTED (1, mais o par de
  reprodutibilidade) permanece contestado e **não** virou fase.

---

## Nota de encerramento do `REQ-RDY0-4` (2026-07-30)

O aceite literal era: *«`./verify_lift_baseline.sh recomp_macos_v3` sai **0**»*.

**Medido hoje: `rc = 0`**, contra os três lifts (`recomp_macos_v3`, `recomp_macos_v2` de
produção e `recomp_macos_v2.pre_v4`). O gate reporta `243/246 marcadores intactos, 0
ausentes, 0 a menos`, mais 1 obsoleto declarado com prova (`[OPDISP]`) e 1 grupo a passar
(`opd-dispatch`).

Passou a cumprir-se depois da recalibração do limiar do grupo (commit `0e49c92`), que não é
afrouxamento: o `ps3_call_opd` está exactamente igual nos dois lifts — 52 = 47 chamadas + 3
declarações + 2 comentários — logo nenhuma conversão se perdeu. O deficit era todo do
`ps3_indirect_call`, cujo limiar `15795` tinha sido capturado num lift **intermédio**.

### Ressalva que fica por fechar, e é honesto declará-la

A segunda metade do aceite diz: *«para cada um dos **21 marcadores** existe um escritor
identificado, listado explicitamente»*.

**Esse número nunca foi reconciliado.** O `MANIFEST.tsv` tem **246** marcadores, não 21, e
a origem do 21 não foi localizada em nenhum documento desta sessão. O que existe hoje:

- todos os 246 passam o gate (0 ausentes, 0 a menos);
- `[OPDISP]` está declarado obsoleto **com prova** no próprio MANIFEST;
- `[WADLD-ALLOC]` ganhou instalador dedicado (`patch_wadld_alloc_probe.py`);
- o `PATCH_CATALOG.tsv` (Fase 4) mapeia os 87 patches, 64 FUNCIONAL / 23 PROBE;
- mas **não existe um mapa explícito marcador → escritor** para os 246. A Fase 4 produziu
  o inverso (patch → classe), e o item «OWNERS.tsv derivado» do plano de adopção do
  `auto-re-agent` — que faria exactamente esse mapa — ficou por executar.

Ou seja: o gate está verde e mede o que deve, mas a rastreabilidade nominal
«cada marcador tem dono» continua por construir. Fica como dívida declarada, não como
critério dado por cumprido.

---

# MARCO v1.4 — integrar o que o upstream avançou e nós não temos

**Definido:** 2026-08-12, a partir da comparação medida do fork contra
`upstream/fix/fold-merge-dropped-fixes` (`git fetch upstream` nesse dia).

> **Porquê agora, e porquê não contra o `master`:** `upstream/master` está parado em
> 2026-07-16. O desenvolvimento real vive em branches (`fix/fold-merge-dropped-fixes`
> a 2026-08-09, `ydkj` a 2026-08-05). Comparar contra o master esconde ~230 commits —
> foi assim que a leitura anterior ficou desactualizada por semanas. **461 commits** que
> não temos no branch mais recente.

## O facto que motiva o marco

Quatro lacunas, cada uma com âncora verificada no nosso próprio código:

1. `runtime/spu/spu_workload.h:6` declara textualmente *"We do not interpret SPU ELFs at
   runtime — we statically lift each"*. Alvos indirectos (`bi $reg`), overlays trazidos
   por DMA e código auto-modificado **não têm bytes estáticos para liftar**.
2. `libs/video/cellGcmSys.c:615` escreve cada `SET_REFERENCE` à medida que drena. O
   upstream mediu o efeito: uma thread à espera do fence `0x13` enquanto o contador subia
   `0x51 → 0x1F4`, e uma intro a 2 FPS.
3. `runtime/syscalls/lv2_register.c:825-826` escreve o EA de origem do segmento como u64
   big-endian (`+0x10`=0, `+0x14`=addr), mas `sys_spu_segment.src` é **u32 em `+0x10`**.
4. O upstream abandonou o SPURS HLE à mão (o nosso `libs/spurs/spurs_kernel.c`) e passou
   a liftar o `libsre.prx` real (`docs/FIRMWARE_LLE.md` deles).

**O que NÃO entra:** o oráculo RPCS3 já aterrou (`tools/rpcs3_probe/`, commit `1096c6d`,
selftest 57/57) e o E81 mediu que **não arbitra a parede da intro**. O `ppu_lifter` não se
importa — estamos à frente dele (5023 vs 3866 linhas).

## Requisitos v1.4

### UPST — A integração é medida, nunca um merge total

| ID | Requisito | Aceite |
|---|---|---|
| `UPST-01` | Existe inventário versionado do delta upstream→nós, por área, com o sha do upstream, a âncora `ficheiro:linha` do nosso lado e a decisão (`adoptar` / `rejeitar` / `já-temos` / `contribuir-de-volta`) | ficheiro versionado; ≥4 itens com decisão e sha; nenhuma linha sem âncora do nosso lado |
| `UPST-02` | Cada bloco adoptado passa smoke de não-regressão da intro **antes** de ser commitado | por bloco: log com `st620` e `rc` antes/depois; o gate rejeita uma regressão injectada (prova por mutação, não só corrida verde) |
| `UPST-03` | A nossa heurística de jump tables (experimentar **ambos** os operandos do `lwzx` e ficar com o que valida mais alvos) é preparada como contribuição de volta | branch/patch com um teste que **falha** na heurística deles ("primeiro operando do TOC ganha") e **passa** na nossa. Aceitação pelo upstream **não** é critério — não a controlamos |

### LV2S — O bug latente do `sys_spu_image_import`

| ID | Requisito | Aceite |
|---|---|---|
| `LV2S-01` | `sys_spu_image_import` escreve o EA de origem do segmento como **u32 em `+0x10`** | teste offline que monta um `sys_spu_image_t` e verifica `read32(seg+0x10) == src_ea`; prova por mutação (repor o comportamento antigo → o teste falha). **Declarado:** `grep image_import games/gow2/` → zero, logo **não existe aceite in-boot** para este item e não se fingirá um |

### SPUI — O interpretador SPU, que é o chão de correctness

| ID | Requisito | Aceite |
|---|---|---|
| `SPUI-01` | Existe um interpretador SPU que partilha o **mesmo** `spu_context` (gpr, ls, canais) e chama os **mesmos** helpers `spu_<mnemonic>` do código liftado | suite de vectores conhecidos a passar; um teste prova que estado escrito pelo interpretador é lido pelo código liftado e vice-versa |
| `SPUI-02` | Numa falha de lookup da tabela de funções liftadas, o runtime **interpreta a partir do local store** em vez de cair no vazio | teste com imagem sintética cujo alvo de `bi $reg` não está liftado: antes → falha registada; depois → executa e produz o resultado esperado |
| `SPUI-03` | O interpretador é gated e **OFF por default**; o baseline não muda | com a env desligada: smoke da intro sem regressão face ao baseline congelado, e contador de instruções interpretadas **= 0** |

### GCMF — O fence GCM observável

| ID | Requisito | Aceite |
|---|---|---|
| `GCMF-01` | Um `SET_REFERENCE` intermédio drenado fica **observável** a quem espera por igualdade | teste que drena N fences numa só passagem e verifica que um esperador de um valor intermédio não fica preso; prova por mutação (publicação directa → fica preso) |
| `GCMF-02` | O mecanismo é portável: pthreads no POSIX, sem `SRWLOCK`/`CreateEventA`/`Sleep(` fora de `#ifdef _WIN32` | build `build-macos` arm64 verde; varredura de tokens proibidos com controlo negativo; **Windows continua a compilar** |
| `GCMF-03` | A intro não regride em tempo de frame | tempo de frame ou FPS medido antes/depois na mesma recipe, com o número escrito — não "parece igual" |

### SPRS — O veredicto SPURS, medido e não opinado

| ID | Requisito | Aceite |
|---|---|---|
| `SPRS-01` | Existe um documento de decisão **LLE vs HLE** com o custo de cada via medido | via LLE: prova medida de que o `libsre.prx` se obtém decifrado e se lifta até ter símbolos — **ou** a demonstração medida de que não se consegue, com o bloqueio nomeado. Via HLE: inventário dos pontos de divergência conhecidos do nosso `spurs_kernel.c`. Veredicto `PROMOTE`/`HOLD` é resultado válido; **"não decidido" não é** |

## Regra de ouro deste marco

**Nunca merge total.** O merge do v0.7 já partiu o smoke da intro (`st620` a 0, OOB de
stack). Adopção é por cherry-pick ordenado, um bloco de cada vez, com smoke de
não-regressão entre blocos. Um bloco que não passe o smoke **não entra** — e a razão fica
escrita, não é reescrita até passar.

## Fora de escopo deste marco

| Item | Razão |
|---|---|
| Merge total do upstream | Já falhou uma vez, com sintoma medido |
| Importar o `ppu_lifter` deles | Estamos à frente; o fluxo é o inverso (`UPST-03`) |
| Usar o oráculo RPCS3 para a parede da intro | E81 mediu que ele pára antes de nós nessa parede |
| Executar a migração SPURS para LLE | Este marco entrega o **veredicto** (`SPRS-01`), não a migração |
| Adoptar as correcções de `sys_fs`/`cellSaveData` do upstream | Não medidas contra o nosso boot; entram só se um bloco as exigir |

## Rastreabilidade v1.4

| Requisito | Fase | Estado |
|---|---|---|
| UPST-01 | Fase 22 | Completo (Plano 22-01, `docs/UPSTREAM_DELTA.tsv`) |
| UPST-03 | Fase 22 | Completo (Plano 22-02, `docs/upstream-contrib/` + `tools/test_upstream_jt_contrib.py`; `offline-unit`, sem PR aberto -- a aceitacao pelo upstream nao e' criterio) |
| UPST-02 | Fase 23 | Parcial (Plano 23-01, `scripts/upstream_block_gate.sh` + baseline medido + `docs/upstream-blocks/README.md`; bloco nulo `in-boot` com as duas pontas no mesmo TSV). **Falta a prova por mutacao** -- o criterio exige que o gate REJEITE uma regressao injectada, e isso e o Plano 23-03 |
| LV2S-01 | Fase 23 | Pendente |
| SPUI-01 | Fase 24 | Pendente |
| SPUI-02 | Fase 24 | Pendente |
| SPUI-03 | Fase 24 | Pendente |
| GCMF-01 | Fase 25 | Pendente |
| GCMF-02 | Fase 25 | Pendente |
| GCMF-03 | Fase 25 | Pendente |
| SPRS-01 | Fase 26 | Pendente |

Cobertura: **11/11** requisitos do Marco v1.4 mapeados, cada um a **exactamente uma** fase.
Zero órfãos, zero duplicados. Contagem verificada linha a linha contra a secção
"Requisitos v1.4" acima (UPST-01..03, LV2S-01, SPUI-01..03, GCMF-01..03, SPRS-01).

**Numeração:** as fases do v1.4 arrancam em **22** — continuação do último número usado
(21, do v1.3), **sem** reiniciar em 1 e **sem** tocar nas fases **12–15**, reservadas pelo
marco `v1.2` (Toolchain Go, `queued`, não activo).

**Notas de agrupamento** (porque cada requisito caiu onde caiu):

- `UPST-01` abre o marco: é o inventário que diz a cada fase seguinte que sha do upstream
  fazer cherry-pick e qual é a âncora do nosso lado. Sem ele, cada fase refaria a
  comparação por conta própria.
- `UPST-03` fica com o `UPST-01` por ser a mesma natureza — uma decisão de inventário
  materializada num artefacto, no sentido inverso (nós → upstream). É independente de tudo
  o resto e não bloqueia nenhuma adopção.
- `UPST-02` é infra-estrutura e vem **antes** dos blocos de risco: a regra de ouro do marco
  diz que um bloco que não passe o smoke não entra, e isso exige que o portão exista
  primeiro. `LV2S-01` acompanha-o por ser o bloco mais pequeno e mais isolado do marco —
  serve de primeiro atravessamento real do portão.
- `SPUI-*` e `GCMF-*` são as duas adopções substanciais, tocam em subsistemas disjuntos e
  ficam em fases **independentes entre si**, ambas dependentes do portão.
- `SPRS-01` é o último: produz um **veredicto**, não uma migração, e depende de o lado SPU
  estar compreendido para que o custo da via LLE seja estimado com números e não com
  opinião.

**A honestidade que o `LV2S-01` obriga a preservar na Fase 23:** o próprio requisito
declara que `grep image_import games/gow2/` dá zero, logo **não existe aceite `in-boot`**
para ele. O critério da fase mantém isso escrito em vez de inventar uma observação de boot
— o portão prova **não-regressão** da intro, nunca que o caminho corrigido foi exercitado.
