# Roadmap: ps3recomp

> Convenção de IDs de fase: `sequential` (o `.planning/config.json` existe desde 2026-08-12
> mas **não** tem chave `phase_id_convention`, logo vale o default `sequential`;
> `project_code` é `null`). Cabeçalhos `### Phase N:` são tokens estruturais lidos por
> ferramentas — o **nome** da fase está em português.
> Granularidade aplicada ao Marco v1.1: 3 fases (abaixo da faixa `standard` 4–6, por
> desenho — os 9 requisitos do marco formam três frentes coesas — diagnóstico, gate,
> correção — e nenhuma fase adicional teria critério de sucesso observável sozinha).
> Granularidade aplicada ao Marco v1.4: **5 fases** (22–26), dentro da faixa `standard`
> 4–6 do `config.json`. Os 11 requisitos agrupam-se em inventário, portão+bloco
> pequeno, e os dois blocos de risco independentes (SPU, GCM), com o veredicto
> SPURS no fim porque depende de o lado SPU estar compreendido.

## Milestones

- ✅ **v1.0 — RDY-0: desbloquear o re-lift** — Fases 1–5 (fechado 2026-07-30)
- 🔄 **v1.1 — o boot volta ao AUTO_LOAD, e o smoke passa a saber** — Fases 6–11 (**Fase 11 aberta, transita**; deixou de ser o STATE actual a 2026-08-12)
- 📋 **v1.2 — Toolchain Go PoC (parity)** — Fases 12–15 **queued** — requisitos/roadmap em [`milestones/v1.2-REQUIREMENTS.md`](milestones/v1.2-REQUIREMENTS.md) / [`milestones/v1.2-ROADMAP.md`](milestones/v1.2-ROADMAP.md) — **não activo**; não misturar com o boot
- ✅ **v1.3 — tooling Xenon-style e RE assistida: o lift volta a ser regenerável** —
  Fases 16–21 (requisitos 11/11 a 2026-08-03; a Fase 21 continua **sem `21-VERIFICATION.md`
  no disco** — o marco fecha nos requisitos, não na verificação de fase)

- 🔄 **v1.4 — integrar o que o upstream avançou e nós não temos** — Fases 22–26
  (**activo**, definido 2026-08-12; STATE actual)

> **A numeração não é a ordem de execução.** O `v1.2` (Toolchain Go) ficou `queued` a
> 2026-08-02 e reserva as fases 12–15; o `v1.3` arranca em 16 para não lhe pisar a
> numeração, e o `v1.4` **continua em 22** a partir do último número usado (21) — o
> v1.2 mantém 12–15 reservadas e **não** são reutilizadas. E a **Fase 11 do v1.1 fica
> aberta e transita** — não foi cancelada.
>
> **Porquê o v1.3 antes de fechar a Fase 11:** a Fase 11 ataca quatro paredes dentro do
> guest, e cada correcção dessas nasce hoje como `patch_*.py` — uma agulha de texto que o
> próximo re-lift apaga. O v1.3 constrói o mecanismo (midasm / weak / switch TOML) que faz
> essas correcções sobreviverem a um re-lift, e o playbook de RE que as encontra mais
> depressa. O plano de origem declara-se paralelo às walls e não bloqueante para o
> stack Metal (M0–M10).

## Phases

<details>
<summary>✅ v1.0 RDY-0 (Fases 1–5) — FECHADO 2026-07-30</summary>

- [x] Fase 1: Emissões do lifter e teste-ouro do preâmbulo (3/3 planos) — verificada `passed` 4/4
- [x] Fase 2: Código host órfão versionado e compilado (3/3) — `passed` 4/4
- [x] Fase 3: Re-lift num diretório novo com gate de baseline (3/3) — `passed` 6/6
- [x] Fase 4: Catálogo de patches como gate que falha (5/5) — `passed` 5/5
- [x] Fase 5: Smoke de equivalência e promoção (5/5) — `passed` 5/5

Detalhe completo: [`milestones/v1.0-ROADMAP.md`](milestones/v1.0-ROADMAP.md)
Auditoria: [`milestones/v1.0-MILESTONE-AUDIT.md`](milestones/v1.0-MILESTONE-AUDIT.md) — `tech_debt`, 6/6 requisitos

</details>

- [ ] **Phase 6: Diagnóstico da regressão até ao código** - bissect reutilizável fecha a janela e aponta a causa em ficheiro:linha
- [ ] **Phase 7: Gate de aceite que detecta a cadeia** - o smoke mede cada elo e o `accept_relift.sh` rejeita a regressão actual, sem perder `SUMMARY` de diagnóstico
- [ ] **Phase 8: Correção na origem e reposição do boot** - `StartSeq=2`/`thr_end=1`/`R_PermA` repostos, provado em ≥4/6 corridas, sobrevivendo a um re-lift se a causa for o lifter
- [ ] **Phase 9: Bissect commit-a-commit da janela** - `git bisect` automatizado sobre os 197 commits de 25-26 Jul, com o `smoke_chain_gate.sh` como critério, até nomear o commit que parte o 2.º `StartSeq`
- [ ] **Phase 10: A parede do DecodeAu — o guest abre o movie e não o alimenta** - `cellVdecDecodeAu` é chamado 10× no binário bom e 0× no actual, com tudo o resto idêntico

### Marco v1.3 — tooling Xenon-style e RE assistida (Fases 16–21, activo)

- [x] **Phase 16: Inventário de patches e baseline medido** - 3/3 planos, verificada `passed` 4/4; LEDG-01 fecha
- [x] **Phase 17: Mid-asm de ponta a ponta** - TOML → chamada no C gerado → símbolo ligado no `boot_gow2` → smoke sem regressão
- [x] **Phase 18: Jump tables detectadas e declaradas** - `ps3_analyse.py` acha, o TOML declara, o lifter emite `switch` real
- [x] **Phase 19: Weak overrides e as migrações que fecham o marco** - substituir uma função guest inteira sem editar o `.cpp`, e chegar a ≥3 fixes re-lift-safe
- [x] **Phase 20: O oráculo e o método** - Ghidra por delta no `verify_lift`, playbook de RE usado numa wall real
- [ ] **Phase 21: Endurecimento, métricas e fecho** - suite T1–T10 com `rc` registado e a tabela antes/depois preenchida

### Marco v1.4 — integrar o que o upstream avançou e nós não temos (Fases 22–26, activo)

- [x] **Phase 22: O inventário do delta e a contribuição de volta** - o que se adopta, o que se rejeita, o que já temos, o que se devolve — com sha do upstream e âncora nossa em cada linha
- [ ] **Phase 23: O portão de adopção e o primeiro bloco que passa por ele** - smoke entre blocos que rejeita regressão injectada, e o `sys_spu_image_import` a atravessá-lo
- [ ] **Phase 24: O interpretador SPU — o chão de correctness que não temos** - falha de lookup passa a interpretar do local store em vez de cair no vazio; gated, OFF por default
- [ ] **Phase 25: O fence GCM que se deixa observar** - um `SET_REFERENCE` intermédio deixa de ser invisível a quem espera por igualdade, em pthreads e sem custar frame
- [ ] **Phase 26: O veredicto SPURS — LLE contra HLE, medido** - documento de decisão com o custo de cada via medido; `PROMOTE`/`HOLD` é resultado, "não decidido" não é

> **Regra de ouro do v1.4, que todas as cinco fases herdam:** **nunca merge total**. O
> merge do v0.7 já partiu o smoke da intro uma vez (`st620` a 0, OOB de stack). A adopção
> é por **cherry-pick ordenado, um bloco de cada vez**, com o portão da Fase 23 entre
> blocos. **Um bloco que não passe o smoke não entra** — e a razão fica escrita como está,
> não é reescrita até passar.
>
> **Restrições que valem em todas as fases:** macOS/arm64 é onde tudo se mede e **Windows
> não regride** (G4) — proibidos fora de `#ifdef _WIN32`: `<intrin.h>`, `Sleep(`,
> `__declspec`, `GetModuleHandleA`, e no bloco GCM também `SRWLOCK`/`CreateEventA`, que
> exigem porte para pthreads. Sondas e experimentos são **gated e OFF por default**, no-op
> no baseline (G2). Todo o aceite declara o nível de evidência — `in-boot`,
> `offline-unit` ou `não-exercitado` (G5).

> **Ordenação:** 22 é a primeira porque é ela que diz às outras que sha fazer cherry-pick.
> 23 vem antes de 24 e 25 porque a regra de ouro exige o portão **antes** das adopções de
> risco. 24 e 25 são **independentes entre si** e podem correr em paralelo. 26 é a última
> porque o veredicto SPURS depende de o lado SPU estar compreendido.

## Phase Details

### Phase 6: Diagnóstico da regressão até ao código

**Goal**: A causa exacta da regressão que impede o 2.º `StartSeq` está identificada no código — ficheiro e linha, não "algo mudou entre X e Y" — através de um bissect reutilizável sobre os binários guardados.
**Depends on**: Nada (primeira fase do marco)
**Requirements**: REG-01, REG-02, DIAG-01
**Success Criteria** (o que tem de ser VERDADE):

  1. Um único comando corre o bissect sobre os binários guardados (`boot_gow2.pre_v3`, `.pre_v4`, `_v3fix`, `_v4`, e os posteriores) e imprime uma tabela com `StartSeq`/`thr_auto_load end`/`R_PermA` por binário — reutilizável na próxima regressão, não um procedimento manual repetido a cada vez (DIAG-01).
  2. Esse comando fecha a janela em dois binários guardados **consecutivos**, com `thr_auto_load end >= 1` medido no lado que funciona e `= 0` no lado que falha — medido por esta ferramenta, não inferido das notas de 22–25 Jul (REG-01).
  3. A causa está nomeada por ficheiro e linha (lifter, patch, runtime ou HLE), corroborada por diff real dentro da janela fechada — nunca "algo mudou entre X e Y" (REG-02).
  4. As duas hipóteses já registadas (`TOCFIX`, `strip_block`) recebem veredicto explícito — confirmada, refutada, ou substituída por uma terceira achada por medição — nenhuma fica em aberto sem conclusão escrita.

**Plans**: 2/2 plans executed

- [x] 06-01-PLAN.md — `bisect_regression.sh` reutilizável: mede StartSeq/thr_auto_load end/R_PermA nos 9 binários guardados e fecha a janela pre_v3→v4 (DIAG-01, REG-01)
- [x] 06-02-PLAN.md — diff de funções perdidas (4253, zero compilação) e alcançabilidade a partir de thr_auto_load primeiro; reversão cirúrgica de TOCFIX/`ctx->lr` em func_00147038 como fallback condicional; veredicto escrito das três hipóteses (REG-02)

### Phase 7: Gate de aceite que detecta a cadeia

**Goal**: O gate de aceite (smoke + `accept_relift.sh`) falha sozinho quando o boot não chega ao `AUTO_LOAD`, medindo cada elo da cadeia — e nenhum sinal de diagnóstico se perde por concorrência de `SIGTERM` entre probes.
**Depends on**: Nada — não depende da causa localizada na Fase 6, pode ser construído em paralelo com ela. A Fase 8 é que depende deste gate.
**Requirements**: GATE-01, GATE-02, GATE-03, DIAG-02
**Success Criteria** (o que tem de ser VERDADE):

  1. O smoke ganha um contador por elo da cadeia (intro concluída → 2.º `StartSeq` → `thr_auto_load` → `R_PermA`) — não um único `st620` — e, corrido contra o `boot_gow2` de produção actual, imprime exactamente em que elo parou (GATE-02).
  2. O gate sai não-zero corrido contra o binário de produção actual (`StartSeq=1, thr_end=0`) **e** sai zero corrido contra um binário que chega ao `thr_auto_load` (ex.: `boot_gow2.pre_v3`) — prova nos dois sentidos, não só um (GATE-01).
  3. `accept_relift.sh`, corrido hoje contra o binário de produção actual, **rejeita** de facto (`rc != 0`, nomeando o elo que falhou) — prova real contra o binário existente, não um cenário hipotético (GATE-03).
  4. Os sete probes do lift que competem por `SIGTERM` no mesmo binário encadeiam todos os handlers anteriores — corrido com os sete activos ao mesmo tempo (gated, ligados só para este teste), nenhum `SUMMARY` falta no log (DIAG-02).

**Plans**: 3/3 plans executed

- [x] 07-01-PLAN.md — `lib_boot_chain_metrics.sh` (núcleo extraído de `bisect_regression.sh`) + `smoke_chain_gate.sh` (contador por elo, contrato `--bin`/`LIFT_REL`), provado nos dois sentidos contra `boot_gow2`/`boot_gow2.pre_v3` (GATE-01, GATE-02)
- [x] 07-02-PLAN.md — `accept_relift.sh` ganha a PERNA 4 (`smoke_chain_gate.sh`), provado por rejeição real do lift de produção actual, sem regressão das 3 pernas + contadores da Fase 5 (GATE-03)
- [x] 07-03-PLAN.md — os sete probes de `SIGTERM` (`CD498`, `17ACC`, `FLIPPATH`, `PRESENTLOOP`, `MENUPRESENT`, `SCHEDARM`, `POSTTHR`) passam a encadear o handler anterior, mesmo padrão de `29AF0`/`CB56C` (commit `09c3850`) (DIAG-02)

### Phase 8: Correção na origem e reposição do boot

**Goal**: O boot volta a `StartSeq=2`, `thr_auto_load end=1`, `R_PermA=20169344`, com a correcção aplicada na origem apontada na Fase 6 — nunca um paliativo — e o gate da Fase 7 confirma a reposição sozinho.
**Depends on**: Phase 6 (precisa da causa localizada), Phase 7 (precisa do gate de cadeia para medir a reposição pelo critério novo, e para confirmar a rejeição a virar aceitação)
**Requirements**: REG-03, REG-04
**Success Criteria** (o que tem de ser VERDADE):

  1. Uma medição real (protocolo G6: `PS3_NO_RSX=1 PS3_RSX_BACKEND=trace`, kill por PID, `PS3_ENGINE_ROOT` explícito) mostra `StartSeq=2`, `thr_auto_load end>=1`, `R_PermA=20169344` em pelo menos 4 de 6 corridas (REG-03).
  2. A correcção vive no ficheiro e linha identificados na Fase 6 — nenhuma das técnicas proibidas pelo CLAUDE.md (CRC bypass, stub, mascarar guard, null-check cosmético) foi usada (REG-03).
  3. O gate da Fase 7, que rejeitava o binário antes da correcção (critério 3 da Fase 7), agora **aceita** o binário corrigido — a mesma ferramenta, dois vereditos diferentes, prova de que a correcção fecha exactamente o que o gate media.
  4. Se a causa identificada for o lifter: um re-lift do zero com `FORCE_REBUILD_LIFT=1`, sem edição manual no `.cpp` gerado, reproduz os mesmos três números do critério 1 (REG-04). Se a causa não for o lifter, este critério é declarado explicitamente não-aplicável — nunca omitido em silêncio.

**Plans**: 3/3 plans executed

- [x] 08-01-PLAN.md — corrige os 3 defeitos do teste de patches (PROBE incluído no diff, uma passagem, PATCH_DIR fixo), corre o A/B real sem build e decide se os patches são candidato
- [x] 08-02-PLAN.md — mede o binário (smoke_chain_gate.sh) para confirmar/refutar patches, ou testa o candidato de runtime concreto (runtime/ppu/ppu_loader.cpp:500-619, commit f6708cb) por reversão cirúrgica — nomeia a causa ou entrega diagnóstico honesto
- [x] 08-03-PLAN.md — aplica a correcção na origem (se houver causa), prova REG-03 com o gate completo 6x, e declara REG-04 (provado, ou explicitamente não-aplicável)

### Phase 9: Bissect commit-a-commit da janela

**Goal**: Nomear o commit exacto que parte o 2.º `StartSeq`, por bissecção automatizada sobre a janela onde o boot deixou de chegar ao `thr_auto_load` — depois de 9 candidatos terem sido eliminados por medição dirigida.
**Depends on**: Fase 7 (o `smoke_chain_gate.sh` é o critério automático que o `git bisect` precisa) e Fase 8 (o funil de eliminação e o método de relink barato).
**Requirements**: REG-02, REG-03
**Success Criteria** (o que tem de ser VERDADE):

  1. Um `git bisect run` automatizado percorre a janela `8648805..4e815cf` (197 commits) usando um script de veredicto que devolve 0/1 a partir do `smoke_chain_gate.sh` — não inspecção manual (REG-02).
  2. O bisect nomeia **um commit** e o veredicto é confirmado por medição directa: reverter esse commit repõe `thr_auto_load end >= 1`, mantê-lo reproduz a falha (REG-02).
  3. A correcção é aplicada **na origem** e o boot volta a `StartSeq=2`, `thr_end=1`, `R_PermA=20169344` em **≥4 de 6 corridas**, medido pelo `smoke_chain_gate.sh` (REG-03).
  4. Se o bisect **não** convergir (commits que não compilam, janela mal delimitada, ou falha não-determinística), a fase entrega o mapa do que foi testado e porquê não convergiu — diagnóstico honesto conta como entrega, correcção forjada não.

**Plans**: 1/2 plans executed

- [x] 09-01-PLAN.md — bisect_verdict.sh (oráculo de 3 níveis com herança memorizada), provado nos dois extremos da janela (8648805/4e815cf); resolve por medição directa a suspeita de não-determinismo (boot_gow2.pre_v3/.pre_v4 são byte-idênticos); audita o custo real dos 197 commits (REG-02, trabalho preparatório)
- [ ] 09-02-PLAN.md — corre `git bisect run --first-parent` até nomear um commit (ou entregar o mapa honesto de não-convergência), confirma por reversão dirigida, corrige na origem e prova REG-03 com o gate de 6 corridas (REG-02, REG-03)

### Phase 10: A parede do DecodeAu — o guest abre o movie e não o alimenta

**Goal**: Descobrir porque o guest chama `cellVdecDecodeAu` zero vezes, quando abre o `/_movies/smlogo_v2.m2v` correctamente e o binário de referência chama 10 vezes — e repor o `thr_auto_load`.
**Depends on**: Fase 9 (a cadeia FIOS reposta é o que trouxe o boot até aqui) e Fase 7 (o gate é o critério).
**Requirements**: REG-03
**Success Criteria** (o que tem de ser VERDADE):

  1. Está medido **onde** o guest deixa de progredir entre o `[movieio] open '/_movies/smlogo_v2.m2v'` (que acontece) e a primeira chamada a `cellVdecDecodeAu` (que não acontece) — com ficheiro e função, não "algures no vdec".
  2. A causa recebe veredicto escrito: mais um marcador apagado pelo re-lift, um defeito do lift, ou lógica do jogo que depende de estado que não temos.
  3. Se for reposição de código apagado, entra como `patch_*.py` idempotente commitado, como os sete da Fase 9.
  4. `thr_auto_load end >= 1` em **≥4 de 6** corridas do `smoke_chain_gate.sh`. Se não for atingido, a fase entrega o mapa do que foi eliminado — nunca um fecho forjado.

**Plans**: 2/2 plans executed

- [x] 10-01-PLAN.md — inventário completo (marcadores apagados, patches SKIP, patches sem escritor) + repõe AREAD-HLE em func_002B3D1C (REG-03)
- [x] 10-02-PLAN.md — build de teste + mede DecodeAu/gate de 6 corridas + acção honesta consoante o resultado + veredicto escrito de REG-03 (REG-03)

### Phase 11: A parede real — o boot não entra no loop principal

**Goal**: Fazer o boot entrar no loop principal do jogo (`func_00242C94`) e despachar
o seu primeiro estado **sem gates de diagnóstico** — que é o que "chegar ao AUTO_LOAD"
sempre quis dizer, agora que se sabe onde a thread é criada.
**Depends on**: as correcções de instrumento de 2026-08-01 (o marcador real de fim de
thread e o `thr_created` no gate) — sem elas nenhum critério desta fase é mensurável.
**Requirements**: REG-03 (reformulado), GATE-01

**O que mudou e obriga a esta fase** (tudo medido a 2026-08-01, ver
`gow2-recomp/notes/2026-08-01-*.md`):

- O critério das Fases 7–10 (`thr_end`) media uma string inexistente. Nada do que
  essas fases fizeram podia fechá-lo.

- A cadeia real, medida degrau a degrau com sonda de CONTROLO em cada bissecção:
  `main()` → `func_002B2E74` (7ª de 9) → `func_000B71B8` (B71, 9ª de 11) → **não
  retorna**. O loop principal é a 8ª chamada do `main()` e **nunca corre**.

- Três paredes em fila dentro do B71, todas a mesma doença — um método de contentor
  a correr sobre um objecto que não é um contentor:
  `func_002545B0` (+0x7C), `func_002182A4` (tabela de pools), `func_002547AC` (+0x7C).
  Com três gates de diagnóstico ligados o boot **entra** no loop principal e despacha
  o primeiro estado (`[STATE] frame=1 → 0x0052F3F0 code=0x002B2DD0`).

- O registry de tipos **não** é a causa: despejado no caminho natural, tem 26 entradas
  (tags 0..32), todas com fábrica válida e vtable viva.

- A Fase 10 concluiu que a referência tomava `idx=0x54/ent=0x47D00000 (válido,
  reused=1)`. **`0x47D00000` é a zona pinada fabricada pelo paliativo TYPE15**, removido
  a 2026-08-01 com prova de não-regressão. Essa "referência" era o comportamento do
  paliativo, não do jogo.

**Success Criteria** (o que tem de ser VERDADE):

  1. Está nomeado, com prova, **qual dos dois lados está errado** no conflito de layout:
     o objecto diz `tag=1` e o consumidor lê `+0x7C` como sentinela de lista quando esse
     offset é `matriz[0][3]` (provado desmontando `func_0024C1F8`). Ou o tag do objecto
     está errado, ou `tab[1]` despacha para a fábrica errada. Duas hipóteses exclusivas.

  2. A correcção é fiel ao CELL — não um gate. Os três gates de diagnóstico
     (`PS3_LIST254_EMPTY_IF_NULL`, `PS3_POOL_NULL_IF_BAD`, `PS3_LIST547_EMPTY_IF_BAD`)
     ficam OFF e servem só para comparação.

  3. `sys_ppu_thread_create name="AUTO_LOAD"` acontece ≥1 vez, **sem gates**, em ≥4 de 6
     corridas — e o gate reporta `elo_stopped=nenhum` ou nomeia o elo seguinte.

  4. Se não for atingido, a fase entrega o mapa do que foi eliminado — nunca um fecho
     forjado. (Já eliminados por medição: registry populado, objecto bem construído,
     lift fiel em 3 suspeitas, cadeia de indirecções sã.)

**INVENTÁRIO DAS PAREDES** (ordem de execução do boot, tudo medido 2026-08-01/02).
Cada uma é um plano; atacam-se **uma a uma**, e um gate NUNCA fecha uma parede.

| # | onde | sintoma medido | gate de diagnóstico | estado |
|---|---|---|---|---|
| — | intro / `StartSeq` / `REPLAY-NOPIC` | `st620=11`, `startseq=2`, `nopic=4` | — | ✅ passa |
| — | `R_PermA` | 20 298 800 bytes lidos | — | ✅ passa |
| **1** | `func_002545B0` | lista circular em `arg+0x7C` com `head=0`; o `arg` é um registo WAD (`w0=0x40030001`) cujo `+0x7C` é `matriz[0][3]`, provado desmontando `func_0024C1F8` | `PS3_LIST254_EMPTY_IF_NULL` | 🔴 por fixar |
| **2** | `func_002182A4` | `base[0]` é a **contagem** de um cabeçalho de bloco (5), lida como ponteiro de pool; o objecto nunca passou por `func_0022851C` (o produtor correcto, que só conhece um objecto: 107 chamadas, todas com `r3=0x406387E0`) | `PS3_POOL_NULL_IF_BAD` | 🔴 por fixar |
| **3** | `func_002547AC` | mesmo padrão da #1: `arg+0x7C` como sentinela, `head` implausível | `PS3_LIST547_EMPTY_IF_BAD` | 🔴 por fixar |
| **4** | `func_0041F700` | recursão **exponencial**: 27 539 083 iterações (99,96% do rasto). A poda `child_tag == parent_tag` só impede auto-recursão do mesmo tipo; não há conjunto de visitados e o grafo de tipos é completo (pai tag 1, filhos tags 3,4,9,15,16,17,18,20,21,23,25,32) → 12⁷ ≈ 35 M | **nenhum** | 🔴 activa, sem gate |
| — | `func_00242C94` (loop principal) | entra e despacha o 1.º estado **com** os gates 1–3 | — | 🟡 só com gates |
| — | `Pad` | `cellPadGetData` chamado 1× (fix `edd3e7a` removeu o SIGSEGV) | — | 🟡 1 frame |

**O que já está ELIMINADO por medição** (não voltar a perseguir):

- o registry de tipos **está populado** — 26 entradas, tags 0..32, todas com
  fábrica válida e vtable viva (`tab=0x00868D48`)

- o objecto entregue **está bem construído** — cabeçalho + matriz identidade
- o lift **é fiel** — 4 suspeitas de bug do lifter, 4 refutadas contra o binário
- a cadeia de indirecções **está sã** — os 3 ponteiros são válidos
- `AUTO_LOAD` **é consequência, não causa** — é criada pelo 1.º handler de estado
- as listas de irmãos **estão bem formadas** — `head == sentinela`, 12 nós, terminam
- o walk exterior `func_0041FF70` **termina** — 2 entradas, acaba em NULL

**Duas famílias de objectos**, distinguidas pelo `w0` — e tratá-las como uma foi
a origem de várias conclusões erradas desta sessão:

| família | `w0` | `+0x7C` é |
|---|---|---|
| listas | `0xC0010001` | lista intrusiva correcta (auto-ligada quando vazia) |
| registos WAD | `0x40030001` | `matriz[0][3]` |

**Plans**: 0/4 — um por parede, pela ordem acima

---

### Phase 16: Inventário de patches e baseline medido

**Goal**: Saber **exactamente** o que migrar e contra que número medir a melhoria — hoje
"80+ patches" é uma estimativa de cabeça, e sem uma classificação com EA e aceite por
patch nenhuma das fases seguintes sabe o que atacar primeiro.
**Depends on**: Nada — primeira fase do marco.
**Requirements**: LEDG-01

**Success Criteria** (o que tem de ser VERDADE):

  1. Existe um ledger versionado com as **7 colunas obrigatórias** (`patch`, `classe`,
     `alvo`, `ea`, `prioridade`, `aceite`, `estado`) e **≥15 patches classificados**,
     entre eles os de maior impacto conhecido (fallthrough, jumptable, CE03C, factory
     OPD, host inflate). `classe` ∈ {CORRECTNESS, PROBE, OPD, BUILD, OBSOLETE};
     `alvo` ∈ {midasm, weak, switch_table, invalid_insn, runtime, keep-probe, delete}.

  2. **≥5 itens P0** têm EA concreto (`0x…`, não `-`) e uma string de aceite executável
     (um `grep` de log, um contrato do `CONTRACTS.tsv`, ou um comando de smoke) — não
     "verificar manualmente".

  3. A contagem baseline está **congelada no cabeçalho do ledger**, medida e não copiada:
     o número de `patch_*.py` nos **dois** checkouts (monorepo e `../gow2-recomp`), que a
     2026-08-02 são **88 e 139** — a divergência entre eles é declarada, não escondida.
     *(Medido depois, no mesmo dia: o commit `7f7581d` de outra sessão sincronizou o
     subtree e a contagem passou a **140 e 140**, divergência **0**. O cabeçalho do ledger
     re-deriva estes números a cada regen, por isso mostra os actuais; o "88 e 139" fica
     aqui como o que era verdade quando o critério foi escrito. A divergência 0 é uma
     medição de **contagem de ficheiros**, não uma afirmação sobre o conteúdo.)*

  4. Um `verify_lift.sh` e um smoke de intro foram corridos **uma vez** contra o lift de
     produção e os seus números (`rc`, `st620` máximo, `sticky`, OOB pós-TLS) estão
     registados no ledger como a linha de base contra a qual as fases 17–21 comparam.
     Se algum deles já nascer vermelho, isso fica escrito como estado inicial — nunca
     corrigido em silêncio para o baseline parecer melhor.

**Plans**: 3/3 — planeados 2026-08-02; 16-01, 16-02 e 16-03 executados 2026-08-02

- [x] 16-01-PLAN.md — regenerar `PATCH_CATALOG.tsv` (corpus build **medido: 139**) + `gen_migration_ledger.py` + esqueleto `PATCH_MIGRATION.tsv` (contagens congeladas: mono 88 / build 139 / catálogo 139)
- [x] 16-02-PLAN.md — **19 classificadas / 7 P0** (11 P1, 1 P2), aceites executáveis; 18 dos 19 corridos verdes contra `recomp_macos_v2` (11 `check_contracts.py` rc=0, 7 `grep` n≥1), 1 `smoke:` fica **não-exercitado** para a 16-03. Pilotos derivados no cabeçalho: Fase 17 → `patch_ce03c_introseq_block.py` / `patch_fallthrough_2550c8.py`; Fase 18 → `patch_jumptable_2a209c.py` / `patch_jumptable_2b11b8.py`. **Corpus mexeu-se durante a sessão: 139 → 140** (commit `7f7581d` de outra sessão sincronizou o subtree; catálogo regenerado, diff = +1 linha)
- [x] 16-03-PLAN.md — baseline medido, 1 corrida de cada: `verify_lift.sh` **rc=0** (3 passos PASS por delta: parity 77/77, MANIFEST_DEBT 0 vs dívida 36, bounds 180/180) e smoke de intro 25s com kill por PID — **st620_max=11**, **OOB/0xFFFF=0**. Dois campos ficam declarados em vez de inventados: `sticky` é **NÃO-OBSERVÁVEL** (os `ps3_fios_sticky_*` não têm um único `fprintf` em `runtime/`/`libs/`, logo o grep media o nada) e `bytes_read` **NÃO-OBSERVADO** (com `MOVIE_EOS=0` o R_PermA não chega a ser lido → o aceite `smoke:` do `patch_fallthrough_2550c8.py` continua por exercitar). **Ressalva medida:** 5 dos 7 chunks do lift são mais recentes que o binário (31/jul vs 29/jul), pelo que o smoke não exercita o lift que o verify verificou

### Phase 17: Mid-asm de ponta a ponta

**Goal**: Um `[[midasm_hook]]` declarado no TOML atravessa toda a cadeia — lifter emite a
chamada, o hook host compila, o `boot_gow2` liga o símbolo — e o primeiro fix real passa a
sobreviver a um re-lift sem que ninguém corra um `patch_*.py`.
**Depends on**: Fase 16 (o ledger diz qual é o piloto P0 e qual o baseline de smoke).
**Requirements**: XEN-01, XEN-02, XEN-06

**Success Criteria** (o que tem de ser VERDADE):

  1. `tools/test_midasm_emit.py` passa sobre uma fixture sintética que exercita os três
     comportamentos: hook **antes** da instrução, hook **depois** (`after_instruction`),
     e `returns=true` a emitir `return;` a seguir ao hook.

  2. **Sem** `--config`, o output do lifter é idêntico ao actual numa fixture pequena —
     golden de regressão, provado por diff, não por inspecção. Um mid-asm nunca aparece
     onde ninguém o declarou.

  3. `nm boot_gow2 | grep gow2_midasm` resolve os símbolos no binário do macOS (não ficam
     `U`), e o smoke de intro não regride face ao baseline congelado na Fase 16.

  4. **A prova que define a fase:** re-liftar a função piloto **sem** correr o
     `patch_*.py` correspondente e o hook continua no `.cpp` gerado. O patch legado passa
     a `migrated` no ledger e o marcador ganha contrato em `CONTRACTS.tsv`.

  5. `docs/RECOMP_CONFIG.md` deixa de ser stub nas secções mid-asm: o que está
     documentado é o que o lifter consome, e cada secção documentada tem um teste que a
     exercita.

**Plans**: 0/? — a planear

### Phase 18: Jump tables detectadas e declaradas

**Goal**: Uma jump table deixa de ser descoberta à mão e remendada por patch: o
`ps3_analyse.py` acha-a no ELF, o TOML declara-a, e o lifter emite um `switch` real em vez
de cair num trampolim.
**Depends on**: Fase 16 (a lista de `patch_jumptable_*.py` e os seus EAs). Independente da
Fase 17 — pode correr em paralelo.
**Requirements**: XEN-03

**Success Criteria** (o que tem de ser VERDADE):

  1. `tools/ps3_analyse.py` existe, tem CLI documentada (`-o <toml>`, `--include-low`) e
     `tools/test_ps3_analyse_jtables.py` passa sobre fixture sintética que cobre o padrão
     `mtctr` → `bctr`/`bcctr` com backtrack de `rlwinm`/`lwzx`.

  2. Cada tabela emitida carrega um campo `confidence`, e **por default só as `high` são
     escritas** — as de baixa confiança exigem `--include-low` e nunca entram no TOML de
     produção sem revisão humana. Os limites da heurística estão escritos, não implícitos.

  3. Corrido contra o EBOOT **local** (dump bruto **não** commitado — regra de dados de
     jogo), o detector reencontra pelo menos uma tabela GoW2 já conhecida por um
     `patch_jumptable_*.py` — o patch existente é o oráculo, e a concordância é a prova.

  4. **≥1** `patch_jumptable_*.py` fica classificado `migrated` ou `redundant` no ledger,
     com o lift a produzir os mesmos alvos por `switch` declarado; smoke sem FATAL nem OOB
     novo.

**Plans**: 0/? — a planear

### Phase 19: Weak overrides e as migrações que fecham o marco

**Goal**: Poder substituir uma função guest **inteira** sem editar o `.cpp` gerado — e
usar esse mecanismo, mais os das fases 17–18, para chegar aos **≥3 fixes de correctness
re-lift-safe** que são o critério de fecho do marco.
**Depends on**: Fase 17 (mid-asm ligado e o padrão de hook host estabelecido) e Fase 18
(o `switch` declarado é um dos três mecanismos elegíveis).
**Requirements**: XEN-04, XEN-05

**Success Criteria** (o que tem de ser VERDADE):

  1. `runtime/ppu/ppc_hooks.h` define `PPC_FUNC`/`PPC_FUNC_IMPL` com o corpo liftado a
     virar `__imp_func_XXXXXXXX` e um wrapper fraco `func_XXXXXXXX` a chamá-lo — e a
     emissão está **atrás de flag** (`--emit-weak-wrappers` ou chave no TOML), nunca por
     default.

  2. `tools/test_weak_override_symbols.py` prova por `nm` num objecto real que o símbolo
     fraco existe e que **um override forte ganha o link** no toolchain do macOS/arm64 —
     o risco declarado no plano (weak symbols no `ld` da Apple) é medido, não assumido.

  3. Um override trivial (probe no-op) liga e corre, e o smoke de intro não regride.
  4. **O critério que fecha o marco:** o ledger mostra **≥3** correcções de classe
     `CORRECTNESS` com `estado=migrated`, cada uma provada da mesma forma — re-lift sem o
     patch, mecanismo ainda presente. `verify_lift.sh` e `check_contracts.py` verdes.
     Se só forem 2, isso é o resultado escrito — o marco fecha PARCIAL, não forjado.

**Plans**: 0/? — a planear

### Phase 20: O oráculo e o método

**Goal**: A IA/agente passa a ser **navegador do mapa guest** com oráculos determinísticos
por trás — não uma fonte de hipóteses sem verificação. O `verify_lift` ganha um passo de
oráculo Ghidra por delta, e o método de atacar uma wall fica escrito e **usado**.
**Depends on**: Fase 16 (baseline de `verify_lift`). Independente das fases 17–19 — pode
correr em paralelo.
**Requirements**: RE-01, RE-02, RE-03

**Success Criteria** (o que tem de ser VERDADE):

  1. `./verify_lift.sh $LIFT` continua a sair `0` **sem** variável nova (o default não
     muda); com `VERIFY_ORACLE=1` corre o passo Ghidra e compara **por delta** contra um
     baseline congelado — nunca um limiar absoluto cru, que nasceria vermelho.

  2. Prova por mutação, revalidada: truncar uma função numa **cópia** do lift faz o gate
     sair `rc=1`. Sem essa prova o passo é decorativo.

  3. `ghidra_out/MANIFEST.json` regista proveniência — sha256 do ELF analisado, data,
     versão do Ghidra, contagens — para que "o oráculo diz X" tenha data e binário.

  4. `docs/RE_PLAYBOOK_GOW2.md` existe com as 6 secções mínimas (pergunta de wall,
     âncoras EA/símbolo/tag de log, ferramentas, hipótese → experimento discriminador,
     aceite medido, anti-padrões) **e foi usado uma vez** numa wall real (D ou C2),
     deixando uma página de ledger com as hipóteses refutadas por medição. "Ainda aberto"
     é um resultado válido; um fecho forjado não é.

  5. `docs/GHIDRA_MCP_SETUP.md` documenta o MCP com versão fixada e a política —
     leitura e anotação sim, reescrita de binário ou geração de código de produto não.
     **Soft:** se o MCP for instável, fica `wont` declarado e o playbook CLI fecha a
     trilha na mesma.

**Plans**: 0/? — a planear

### Phase 21: Endurecimento, métricas e fecho

**Goal**: O que o marco construiu passa a estar **no caminho** de aceitação de um re-lift,
e a melhoria é um número medido antes e depois — não uma afirmação.
**Depends on**: Fases 17, 18, 19 e 20 (é o fecho).
**Requirements**: LEDG-02

**Success Criteria** (o que tem de ser VERDADE):

  1. A sequência canónica de re-lift está escrita e os elos que faltavam estão ligados:
     `accept_relift.sh` invoca `verify_lift.sh`, e a ordem (lift → patches PROBE/legado →
     verify → build → smoke → promoção) está documentada onde quem faz o re-lift a lê.

  2. A suite T1–T10 do plano foi corrida com o `rc` **real registado** por linha num
     documento de resultados — incluindo os que falharem. Um `rc` não medido fica marcado
     `não-exercitado`, nunca presumido `0` (G5).

  3. A tabela antes/depois está preenchida com números medidos nas duas pontas:
     `patch_*.py` de classe CORRECTNESS activos (Fase 16 → agora), fixes re-lift-safe
     (0 → ≥3), `verify_lift` no caminho de aceite (não → sim), regressões de intro no
     smoke pós-midasm (alvo 0).

  4. Está escrito o que isto desbloqueia para a wall D (typemap natural) e qual é a lista
     P1 seguinte — com `opts ABI`, `SIMDe` e `shader recomp` **explicitamente fora**, para
     que o próximo marco não herde scope creep por omissão.

**Plans**: 0/? — a planear

---

### Phase 22: O inventário do delta e a contribuição de volta

**Goal**: Saber, com o sha do upstream de um lado e a âncora `ficheiro:linha` nossa do
outro, exactamente o que se adopta, o que se rejeita, o que já temos e o que se devolve —
porque nenhuma das fases seguintes sabe que commit fazer cherry-pick sem isso. E preparar,
no sentido inverso, a nossa heurística de jump tables como contribuição para o upstream.
**Depends on**: Nada — primeira fase do marco.
**Requirements**: UPST-01, UPST-03

**Success Criteria** (o que tem de ser VERDADE):

  1. Existe um ficheiro **versionado** de inventário do delta com **≥4 itens**, cada um
     com as cinco colunas que o tornam accionável: área, **sha do upstream**, **âncora
     `ficheiro:linha` do NOSSO lado**, decisão ∈ {`adoptar`, `rejeitar`, `já-temos`,
     `contribuir-de-volta`} e a razão. **Nenhuma linha sem âncora nossa** — uma linha sem
     âncora é uma decisão sobre código que ninguém localizou (UPST-01).

  2. As quatro lacunas que motivaram o marco entram no inventário com a âncora já
     verificada — `runtime/spu/spu_workload.h:6`, `libs/video/cellGcmSys.c:615`,
     `runtime/syscalls/lv2_register.c:825-826`, `libs/spurs/spurs_kernel.c` — e cada uma
     nomeia o(s) sha(s) do upstream que a fecham. É esta coluna que as Fases 23–26
     consomem para o cherry-pick; sem ela, cada fase teria de refazer a comparação.

  3. O inventário é derivado de uma comparação **corrida nesta fase** contra o branch
     **vivo** do upstream (`fix/fold-merge-dropped-fixes` ou o que estiver mais recente à
     data), **nunca** contra `upstream/master` — parado em 2026-07-16 e a esconder ~230
     commits. O comando, a data e a contagem de commits do delta ficam escritos: **461** é
     o valor medido a 2026-08-12 e é o ponto de partida; se a re-medição der outro número,
     é o novo que fica no ficheiro, com a data ao lado.

  4. **UPST-03:** existe branch/patch com um teste da heurística de jump tables que
     **falha** contra o comportamento das duas linhagens deles ("o primeiro operando
     carregado do TOC ganha") e **passa** contra o nosso (experimentar **ambos** os
     operandos do `lwzx` e ficar com o que valida mais alvos). Prova por mutação: reverter
     a nossa heurística faz o teste ficar vermelho. Nível de evidência: **`offline-unit`**.
     **A aceitação pelo upstream NÃO é critério** — não a controlamos, e um critério que
     depende de terceiros seria um critério que não se pode fechar por trabalho nosso.

**Plans**: 2/2 plans executed

- [x] 22-01-PLAN.md
- [x] 22-02-PLAN.md
- [ ] `22-01-PLAN.md` — o inventário do delta: `docs/UPSTREAM_DELTA.tsv` medido contra o ramo vivo, validador com prova por mutação e registo na CI (UPST-01)
- [ ] `22-02-PLAN.md` — a contribuição de volta: diferencial contra recorte verbatim do upstream, patch que aplica e corrige, prova por mutação da nossa heurística (UPST-03)

### Phase 23: O portão de adopção e o primeiro bloco que passa por ele

**Goal**: Existir um portão que corre **entre blocos** e que rejeita um bloco que faça a
intro regredir — provado por regressão injectada, não por uma corrida verde — e fazer o
bloco mais pequeno e mais isolado do marco (a correcção do `sys_spu_image_import`)
atravessá-lo do princípio ao fim, incluindo a parte em que se declara o que ele **não**
prova.
**Depends on**: Fase 22 (a linha do inventário dá o sha do bloco `LV2S` e a âncora nossa).
O portão em si pode ser construído em paralelo com a Fase 22 — o que depende do inventário
é o bloco que o atravessa, não a ferramenta.
**Requirements**: UPST-02, LV2S-01

**Success Criteria** (o que tem de ser VERDADE):

  1. Um **comando único** corre o portão por bloco: smoke da intro **antes** e **depois**
     do bloco, com `st620` e `rc` das duas pontas escritos no mesmo registo — não duas
     corridas soltas que alguém compara de cabeça. Protocolo G6 obrigatório e verificável
     no próprio script: `PS3_NO_RSX=1`, `PS3_RSX_BACKEND=trace`, `PS3_ENGINE_ROOT`
     explícito, kill **por PID** (TERM e depois `-9`), nunca `pkill -f boot_gow2` (UPST-02).

  2. **Prova por mutação, nos dois sentidos:** uma regressão injectada numa **cópia** (a
     produção fica intacta, confirmado por hash antes/depois) faz o portão sair `rc != 0`
     nomeando o elo que caiu; a mesma cópia **sem** a injecção sai `rc = 0`. Um portão que
     só foi visto verde não prova que sabe reprovar (UPST-02).

  3. `sys_spu_image_import` escreve o EA de origem do segmento como **u32 em `+0x10`**:
     teste offline que monta um `sys_spu_image_t`, corre o caminho real de
     `runtime/syscalls/lv2_register.c` e verifica `read32(seg+0x10) == src_ea`. Prova por
     mutação: repor o par antigo (`+0x10`=0, `+0x14`=addr) faz o teste falhar. Nível de
     evidência: **`offline-unit`** (LV2S-01).

  4. **A honestidade que este bloco obriga a escrever:** `grep -r image_import games/gow2/`
     continua a dar **zero**, logo **não existe aceite `in-boot` para o `LV2S-01`** e
     nenhum é inventado. O que o portão prova sobre este bloco é **não-regressão** da intro
     (`st620` igual ao baseline), e **nunca** que o caminho corrigido foi exercitado. As
     duas afirmações ficam separadas no registo do bloco — juntá-las seria superdeclarar
     (G5).

**Plans**: 4/4 plans executed

- [x] 23-01-PLAN.md
- [x] 23-02-PLAN.md
- [x] 23-03-PLAN.md
- [x] 23-04-PLAN.md

- [x] `23-01-PLAN.md` — o portão: comando único com as duas pontas num registo, G6 verificável no script, baseline congelado por corrida real (UPST-02)
- [x] `23-02-PLAN.md` — o fix do `sys_spu_image_import` (`+0x10` u32), teste offline sobre o caminho real, prova por mutação e re-ancoragem do `UPSTREAM_DELTA.tsv` (LV2S-01)
- [x] `23-03-PLAN.md` — prova por mutação do portão nos dois sentidos numa cópia, produção intacta por hash, e auto-teste do G6 na CI (UPST-02)
- [x] `23-04-PLAN.md` — o bloco `LV2S` atravessa o portão e o registo separa o que se prova do que não se prova, imposto por verificador (UPST-02, LV2S-01)

### Phase 24: O interpretador SPU — o chão de correctness que não temos

**Goal**: Quando a tabela de funções liftadas não tem o alvo — `bi $reg`, overlay trazido
por DMA, código auto-modificado — o runtime **interpreta a partir do local store** em vez
de cair no vazio; partilhando o mesmo `spu_context` e os mesmos helpers `spu_<mnemonic>`
do código liftado, gated e **OFF por default**.
**Depends on**: Fase 23 — é a regra de ouro do marco: um bloco de risco só entra depois de
o portão existir e saber reprovar. **Independente da Fase 25** — as duas podem correr em
paralelo, tocam em subsistemas disjuntos.
**Requirements**: SPUI-01, SPUI-02, SPUI-03

**Success Criteria** (o que tem de ser VERDADE):

  1. Uma suite de vectores conhecidos passa contra o interpretador, e um teste dedicado
     prova a partilha de estado **nos dois sentidos**: um valor escrito pelo interpretador
     (GPR, local store, canal) é lido pelo código liftado, e um valor escrito pelo código
     liftado é lido pelo interpretador — com o **mesmo** `spu_context` e os **mesmos**
     helpers `spu_<mnemonic>`, provado por chamada real, não por leitura do header. Nível
     de evidência: **`offline-unit`** (SPUI-01).

  2. Teste com **imagem SPU sintética** cujo alvo de `bi $reg` **não está liftado**: antes
     da mudança, a falha de lookup fica registada e o fluxo morre; depois, o runtime
     interpreta a partir do local store e produz o resultado esperado. É a **mesma imagem**
     nas duas corridas — o que muda é o mecanismo, não o input (SPUI-02).

  3. O gate está **OFF por default** e isso é medido **nos dois lados**: com a env
     desligada, o contador de instruções interpretadas é **exactamente 0** e o smoke da
     intro repete o baseline congelado na Fase 23 (`st620`, OOB, órfãos); com a env ligada,
     o mesmo contador é **> 0** na imagem sintética do critério 2. Um gate que nunca foi
     medido ligado não prova que está desligado — prova que ninguém olhou (SPUI-03).

  4. O bloco **atravessou o portão da Fase 23**, com `st620` e `rc` antes/depois no
     registo do bloco, e a árvore continua portável: `build-macos` arm64 verde e **zero**
     tokens Win32 (`<intrin.h>`, `Sleep(`, `__declspec`, `GetModuleHandleA`) fora de
     `#ifdef _WIN32` no código novo, com **controlo negativo** na varredura. Se o bloco não
     passar o smoke, **não entra**, e a razão fica escrita como está — não reescrita até
     passar.

**Plans**: 0/? — a planear

### Phase 25: O fence GCM que se deixa observar

**Goal**: Um `SET_REFERENCE` intermédio, drenado na mesma passagem de FIFO que outros,
deixa de ser invisível a quem espera por **igualdade** — sem `SRWLOCK`/`CreateEventA` fora
do Windows, e sem custar tempo de frame à intro.
**Depends on**: Fase 23 (o portão). **Independente da Fase 24** — podem correr em paralelo.
**Requirements**: GCMF-01, GCMF-02, GCMF-03

**Success Criteria** (o que tem de ser VERDADE):

  1. Um teste drena **N ≥ 3** fences numa só passagem e verifica que um esperador de um
     valor **intermédio** acorda — incluindo a forma que o upstream mediu (esperador em
     `0x13` enquanto o contador sobe de `0x51` para `0x1F4`). **Prova por mutação:** repor
     a publicação directa de `libs/video/cellGcmSys.c:615` (`vm_write32(GCM_CTRL_EA + 8,
     val)` por cada valor drenado) faz o esperador ficar preso e o teste falhar. Nível de
     evidência: **`offline-unit`** (GCMF-01).

  2. **Portabilidade medida, não assumida:** o mecanismo usa pthreads no POSIX;
     `cmake --build build-macos` arm64 verde; a varredura de `SRWLOCK`, `CreateEventA`,
     `Sleep(`, `<intrin.h>`, `__declspec` e `GetModuleHandleA` fora de `#ifdef _WIN32` dá
     **zero**, com **controlo negativo** (a varredura tem de apanhar um token injectado de
     propósito). O ramo Windows continua a compilar; se o toolchain MSVC não estiver
     disponível nesta máquina, isso fica declarado **`não-exercitado`** em vez de dado por
     verde (GCMF-02, G4).

  3. **Tempo de frame ou FPS da intro medido antes e depois**, na mesma recipe e a partir
     do mesmo binário-base, com **os dois números escritos** e o método de medição
     declarado — "parece igual" não é aceite. O número que o upstream reporta (intro a
     **2 FPS** por ~46 esperas de `cellGcmFinish` por frame de filme) é o alvo a bater ou a
     refutar, não uma medição nossa, e fica identificado como tal (GCMF-03).

  4. O bloco **atravessou o portão da Fase 23**, com `st620` e `rc` antes/depois
     registados. Se regredir, **não entra** — e o registo guarda a razão da rejeição.

**Plans**: 0/? — a planear

### Phase 26: O veredicto SPURS — LLE contra HLE, medido

**Goal**: Decidir com evidência, e não com opinião, se se continua a manter o
`libs/spurs/spurs_kernel.c` escrito à mão ou se se lifta o `libsre.prx` real como o
upstream fez. Este marco entrega o **veredicto**, nunca a migração.
**Depends on**: Fase 24 (a via LLE só se avalia depois de se perceber o chão de correctness
do lado SPU — é o interpretador que decide se um `libsre` liftado teria por onde correr) e
Fase 22 (a linha do inventário que regista a decisão do upstream e o sha que a materializa).
**Requirements**: SPRS-01

**Success Criteria** (o que tem de ser VERDADE):

  1. **Via LLE medida**, numa das duas direcções e escrita como tal: **ou** prova de que o
     `libsre.prx` se obtém decifrado e se lifta até ter símbolos — com o comando, o sha256
     do ficheiro de partida e a contagem de símbolos — **ou** a demonstração medida de que
     **não se consegue**, com o bloqueio **nomeado** (que passo, que erro, que ficheiro).
     O firmware **4.93** já instalado em `dev_flash` (E80 §6) inclui `libsre.sprx` e é o
     ponto de partida declarado. O `.sprx` e qualquer derivado ficam **fora do
     versionamento** (regra 1 do `CLAUDE.md`).

  2. **Via HLE inventariada:** lista dos pontos de divergência conhecidos do nosso
     `libs/spurs/spurs_kernel.c`, cada um com âncora `ficheiro:linha` **e** o nível de
     evidência de como se sabe (`in-boot`, `offline-unit`, leitura estática, ou relato do
     upstream — e relato do upstream é declarado como relato, não como medição nossa). Um
     ponto sem âncora não entra na lista.

  3. O documento fecha com **veredicto explícito `PROMOTE` ou `HOLD`**, com o custo
     estimado de cada via derivado dos números dos critérios 1 e 2 — não de impressão.
     **"Não decidido" não é resultado válido**; `HOLD` com a razão medida é.

  4. O documento declara o que **não** foi feito: a migração para LLE está **fora de
     escopo** deste marco por decisão escrita, e nenhum critério desta fase é satisfeito
     por código de migração. Cada afirmação do documento carrega o seu nível de evidência
     (`in-boot` / `offline-unit` / `não-exercitado`), G5.

**Plans**: 0/? — a planear

## Progress

| Fase | Marco | Planos | Estado | Fechada |
|---|---|---|---|---|
| 1. Emissões do lifter | v1.0 | 3/3 | Complete | 2026-07-26 |
| 2. Código host órfão | v1.0 | 3/3 | Complete | 2026-07-29 |
| 3. Re-lift + gate de baseline | v1.0 | 3/3 | Complete | 2026-07-30 |
| 4. Catálogo de patches | v1.0 | 5/5 | Complete | 2026-07-29 |
| 5. Smoke e promoção | v1.0 | 5/5 | Complete | 2026-07-30 |
| 6. Diagnóstico da regressão | v1.1 | 0/2 | Not started | - |
| 7. Gate de aceite | v1.1 | 0/3 | Not started | - |
| 8. Correção na origem | v1.1 | 0/3 | Not started | - |
| 9. Bissect commit-a-commit | v1.1 | 0/2 | Not started | - |
| 10. A parede do DecodeAu | v1.1 | 2/2 | Complete (REG-03 nao fechavel) | 2026-07-31 |
| 11. A parede real: o loop principal | v1.1 | 0/? | Aberta, transita | - |
| 16. Inventário de patches e baseline | v1.3 | 3/3 | Complete | - |
| 17. Mid-asm de ponta a ponta | v1.3 | 0/? | Not started | - |
| 18. Jump tables detectadas | v1.3 | 0/? | Not started | - |
| 19. Weak overrides e migrações | v1.3 | 0/? | Not started | - |
| 20. O oráculo e o método | v1.3 | 0/? | Not started | - |
| 21. Endurecimento e fecho | v1.3 | 0/? | Not started | - |
| 22. Inventário do delta + contribuição | v1.4 | 0/2 | Planned | - |
| 23. Portão de adopção + `image_import` | v1.4 | 0/? | Not started | - |
| 24. Interpretador SPU | v1.4 | 0/? | Not started | - |
| 25. Fence GCM observável | v1.4 | 0/? | Not started | - |
| 26. Veredicto SPURS (LLE vs HLE) | v1.4 | 0/? | Not started | - |

> **As linhas do v1.3 nesta tabela estão desactualizadas face ao `STATE.md`**, que
> regista 16-01..03, 17-01..03, 18-01..03, 19-01/02, 20-01/02 e 21-01..03 executados.
> Não foram reescritas aqui: o fecho do v1.3 tem auditoria própria e o `ROADMAP.md`
> ficou por commitar no 21-03 por conflito de working tree (regra 7 do `CLAUDE.md`).
> Reconciliar é trabalho da auditoria do v1.3, não deste roadmap.

## Dívida declarada (v1.0, transita para o v1.1)

Sem bloqueadores. Congelada por delta, qualquer aumento falha o gate.

- **42 dos 64 patches FUNCIONAL sem contrato** em `CONTRACTS.tsv` (cobertura 22/64).
- **Rastreabilidade marcador→escritor** por construir: o aceite do `REQ-RDY0-4` fala em 21
  marcadores, o MANIFEST tem 246, e a origem do 21 nunca foi localizada.

- **O smoke não cobre WADLD nem o caminho de shaders** — foi aí que a perda de 36
  conversões OPD se escondeu a 2026-07-26. **Nota (2026-07-30):** este item foi citado
  como hipótese inicial para o buraco que motivou o Marco v1.1, mas foi **refutado como
  causa** — o binário anterior à promoção do v1.0 falha exactamente da mesma forma. O
  buraco real é mais simples: o smoke exige só `st620 >= 3`, que a intro sozinha satisfaz,
  sem medir se a cadeia chega ao `AUTO_LOAD`. É esse buraco que a Fase 7 fecha.

- **27 RED do `lift_parity`** por investigar (9 TRUNCATED, 18 CALL_MISMATCH).
- **`games/gow2/` é espelho subtree desactualizado**; reconciliar exige `push`, que precisa
  de autorização.

- **Cópias mortas** de `verify_lift_baseline.sh`/`lift_baseline/` em `gow2-recomp` (era da
  Fase 2, sem GRUPO/OBSOLETO). Nada as chama, mas um script futuro pode invocar a errada.

- **Itens 3 e 5–11 do plano de adopção do `auto-re-agent`** (`docs/PLANO_ADOPCAO_AUTO_RE_AGENT.md`).
- **Desempenho:** `apply_all_patches.sh` leva ~2m14s porque o `check_contracts.py` relê o
  lift a cada chamada.

## Motivação do v1.1 (medida, não inferida)

`boot_gow2.pre_v3` (25 Jul 18:23), corrido hoje com o ambiente de hoje, dá `StartSeq=2,
thr_end=1, R_PermA=199×20MB` — a referência das notas de 22–25 Jul. Quatro corridas
independentes contra o binário actual (probes headless, probes Metal, binário
pré-promoção do v1.0, e o script oficial sem alterações) dão todas `StartSeq=1, thr_end=0,
R_PermA=0`. Três discriminadores negativos, todos medidos: não são os probes desta leva,
não é a promoção do v1.0, não é a recipe do script. O bissect sobre binários guardados
fecha a janela em ~8h: `boot_gow2.pre_v3` (25 Jul 18:23) funciona, `boot_gow2_v4`
(26 Jul 02:35) já falha como produção. Duas notas datadas dessa janela
(`2026-07-26-tocfix-matou-36-conversoes-opd.md`,
`2026-07-26-strip-block-comeu-o-lift.md`) são pistas ordenadas, não conclusões — a
hipótese TOCFIX está enfraquecida por medição (só 19 de 16278 OPDs do ELF divergem do
valor estampado, e várias apontam para tabelas de strings mal-classificadas como funções).

Detalhe completo: `.planning/PROJECT.md` seção "Current Milestone: v1.1" e
`../gow2-recomp/notes/2026-07-30-o-boot-nao-chega-ao-auto-load.md`.
