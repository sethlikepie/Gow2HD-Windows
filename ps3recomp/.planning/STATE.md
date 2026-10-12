---
gsd_state_version: 1.0
milestone: v1.4
milestone_name: integrar o que o upstream avançou e nós não temos
current_phase_name: "Fase 23 — o portão de adopção e o primeiro bloco que passa por ele (4/4 planos)"
status: planning
stopped_at: Completed 23-04-PLAN.md
last_updated: "2026-08-12T22:10:00.000Z"
last_activity: 2026-08-12
last_activity_desc: Fase 23 fechada — o portão de adopção existe, sabe reprovar (prova por
  mutação nos dois sentidos) e o bloco LV2S atravessou-o com o registo a separar o que se
  prova do que não se prova
progress:
  total_phases: 17
  completed_phases: 2
  total_plans: 6
  completed_plans: 6
  percent: 12
---

# Estado do Projeto

## Marco activo: v1.4 — integrar o que o upstream avançou e nós não temos

**Aberto:** 2026-08-12, a partir da comparação medida do fork contra
`upstream/fix/fold-merge-dropped-fixes` (**461 commits** que não temos; o `upstream/master`
está parado em 2026-07-16 e esconde **227** deles — medido na Fase 22, Plano 22-01).

**Porquê agora:** quatro lacunas com âncora verificada no nosso próprio código —
`runtime/spu/spu_workload.h:6` (não interpretamos SPU, logo `bi $reg`/overlays por
DMA/código auto-modificado caem no vazio), `libs/video/cellGcmSys.c:615` (cada
`SET_REFERENCE` publicado à medida que drena, tornando um fence intermédio invisível),
`runtime/syscalls/lv2_register.c:825-826` (EA de origem do segmento como u64 BE em vez de
u32 em `+0x10`) e `libs/spurs/spurs_kernel.c` (o upstream abandonou o SPURS HLE à mão e
passou a liftar o `libsre.prx` real).

**Fases:** **22** (inventário do delta + contribuição de volta) → **23** (portão de adopção

+ `sys_spu_image_import`) → **24** (interpretador SPU) e **25** (fence GCM) **em paralelo**

→ **26** (veredicto SPURS). Cobertura **11/11** requisitos, cada um a exactamente uma fase.

**Numeração:** continua em **22** a partir do último número usado (21, do v1.3). As fases
**12–15** ficam reservadas ao `v1.2` (Toolchain Go, `queued`, **não activo**) e não são
reutilizadas.

**Regra de ouro:** **nunca merge total.** O merge do v0.7 já partiu o smoke da intro
(`st620` a 0, OOB de stack). Adopção por cherry-pick ordenado, um bloco de cada vez, com o
portão da Fase 23 entre blocos. Um bloco que não passe o smoke **não entra**, e a razão
fica escrita como está.

**Restrições herdadas que as cinco fases carregam:** macOS/arm64 é onde tudo se mede e
Windows não regride (G4; proibidos fora de `#ifdef _WIN32`: `<intrin.h>`, `Sleep(`,
`__declspec`, `GetModuleHandleA` — e no bloco GCM também `SRWLOCK`/`CreateEventA`, que
exigem porte para pthreads). Sondas gated e OFF por default (G2). Todo o aceite declara o
nível de evidência: `in-boot`, `offline-unit` ou `não-exercitado` (G5).

**O que fica de fora, por medição e não por preferência:** o oráculo RPCS3 **não arbitra a
parede da intro** — o E81 mediu-o parado no contador `0x592668` em **2** contra o nosso
**3**, nesta configuração (`EBOOT.ELF` decifrado, macOS/arm64 experimental); e o
`ppu_lifter` deles **não se importa** — estamos à frente (5023 vs 3866 linhas), pelo que o
fluxo é o inverso (`UPST-03`).

**Aviso de âmbito:** o foco do `STATE.md` passa para o v1.4 apenas neste marco. A **Fase 11
do v1.1** continua **aberta e transita** — nenhum critério dela foi dado por cumprido.

---

### Fase 22 — o inventário do delta e a contribuição de volta: **FECHADA** (2/2 planos)

`docs/UPSTREAM_DELTA.tsv` medido contra o ramo vivo (`upstream/fix/fold-merge-dropped-fixes`,
461 commits), validador com prova por mutação ligado à CI, e o diferencial da contribuição
de volta. Commits: `22-01`/`22-02` (ver `.planning/phases/22-*/`).

### Fase 23 — o portão de adopção e o primeiro bloco que passa por ele: **FECHADA** (4/4 planos)

**Veredicto: os quatro critérios de sucesso estão cumpridos.** Contagem de planos: 4 de 4
executados (`23-01`, `23-02`, `23-03`, `23-04`), 10 commits no total.

| Critério | Estado | Evidência (nível declarado) |
|---|---|---|
| 1. Comando único, as duas pontas no mesmo registo, G6 verificável no script | **CUMPRIDO** | `scripts/upstream_block_gate.sh` grava `bloco/leg/ref/sha256/run/st620/rc/rc_class/log` num só TSV + resumo; `grep -c pkill` = 0 nos dois scripts; `kill -TERM`/`kill -9` por PID; `PS3_NO_RSX=1`, `PS3_RSX_BACKEND=trace`, `PS3_ENGINE_ROOT` por leg. Auditado por `scripts/test_upstream_block_gate.sh` (`PASS=49 FAIL=0`) — **`offline-unit`** para a disciplina, **`in-boot`** para as corridas |
| 2. Prova por mutação nos dois sentidos, produção intacta por hash | **CUMPRIDO** | `docs/upstream-blocks/GATE-MUTATION-PROOF.md`: cópia mutada (`_exit(97)` à entrada de `ppu_load_elf`) → `rc=1`, `VEREDICTO=REJEITADO ELO_CAIDO=intro (st620)`, `depois = 0,0,0`; a MESMA cópia limpa → `rc=0`, `VEREDICTO=PASSOU ELO_CAIDO=nenhum`, `depois = 11,11,11`. `producao_sha256=677f60a7…` igual antes e depois; zero `_exit(97)` em `runtime/ libs/ include/`; zero worktrees em `${TMPDIR}`. **`in-boot`** |
| 3. `sys_spu_image_import` escreve o EA de origem como u32 em `+0x10`, com prova por mutação | **CUMPRIDO** | `runtime/syscalls/tests/test_spu_image_import.c` corre o caminho real de `runtime/syscalls/lv2_register.c`; `run_spu_image_import_mutation.sh` repõe o par antigo e o teste fica vermelho. Ambos no CTest. **`offline-unit`** |
| 4. A honestidade: `grep -r image_import games/gow2/` = zero, sem aceite `in-boot` inventado, as duas afirmações separadas no registo | **CUMPRIDO** | `docs/upstream-blocks/LV2S-01.md` com `## O que este portao prova` (não-regressão da intro, **`in-boot`**) e `## O que este portao NAO prova` (o caminho corrigido **não** foi exercitado, **`nao-exercitado`**), zero `in-boot` na segunda secção. Grep confirmado a zero **com controlo negativo** (o mesmo padrão acha `runtime/syscalls/*`). Imposto por comando: `tools/check_block_record.py` (`registos=1 violacoes=0`), ligado ao `scripts/test_python.sh` e ao CTest. **`offline-unit`** |

**A travessia real do bloco `LV2S`** (`23-04`, `--runs 6`, modo `--depois-arvore`):
`VEREDICTO=PASSOU ELO_CAIDO=nenhum`, `rc=0`; `antes = 0,0,11,11,11,1` / `depois =
1,11,11,11,1,11`, `rc=143` nas 12 corridas (`timeout-normal`, zero `saida-anormal`);
`perna_a=PASSA limiar=4 ok_antes=3 ok_depois=4`, `perna_b=PASSA novos=nenhum`,
`perna_c=PASSA binarios=distintos` (`18e9d30e…` ≠ `cd366278…`). **`in-boot`**.

**O que TRANSITA da Fase 23** (nada disto foi dado por fechado — `.planning/phases/23-*/deferred-items.md`):

1. **A `PERNA A` é frágil sob carga da máquina.** Medido duas vezes: no `23-03` o sentido
   limpo reprovou à primeira com os dois legs no MESMO commit (experimento discriminador:
   ambos os binários dão `1` e `11`), e no `23-04` o bloco real passou **exactamente** no
   limiar (`ok_depois = 4 == limiar = 4`). Por decidir: corrida de aquecimento por leg
   gravada com `leg` próprio, ou `--runs 6` como default. **Enviesamento conhecido**: o leg
   `antes` corre sempre primeiro e absorve a cache fria, o que torna
   `ok(depois) >= ok(antes)` mais fácil — o erro é no sentido de **passar**.
2. **Os guardas G1.3–G1.7 do auto-teste são fracos** (achado da revisão de código do
   `23-03`, **não corrigido**): contam ocorrências em todo o ficheiro e exigem `>= 1`, mas
   cada token existe duas vezes no portão (uma na linha que executa, outra em
   `RECIPE_LITERAL=`/`echo`). Medido: apagar a linha 452 (`PS3_ENGINE_ROOT="$wt"`) faz os
   dois legs linkarem o mesmo motor e o auto-teste fica **verde**. Corrigir antes de
   confiar no auto-teste como guarda de CI. Também torna optimistas as frases do
   `23-03-SUMMARY.md:118,143` e a última linha do `GATE-MUTATION-PROOF.md`.
3. **`LV2S-01.md:58-60` superdeclara a superfície medida**: dos 25 ficheiros que batem
   `^(runtime|libs|include)/`, só `runtime/syscalls/lv2_register.c` entra na
   `libps3recomp_runtime.a` — os outros 24 são saída do lifter e um script. Os números
   estão certos; a glosa "e entram no binário" tem de ser corrigida.
4. **A separação de honestidade é estrutural, não semântica**: `check_block_record.py`
   reprova um registo que funda as **secções**, mas não apanha uma **frase** que funda as
   duas afirmações (medido por mutação). A protecção contra G5 continua a depender de
   revisão humana.
5. **Ainda `nao-exercitado`**: o portão a **reprovar um bloco real** (só reprovou injecção
   sintética); `FORCE_REBUILD_LIFT=1` no sentido positivo; `--antes-bin` combinado com
   `--depois-arvore`; o verificador sobre dois registos em simultâneo.
6. **A dívida `known_debt_python` é condicional ao ambiente**: no fecho do `23-04` deu
   `PASS 18 / SKIP 2 / FAIL 0` sem `SDKROOT` e `100% of 30` no runtime, quando o `23-02`
   a media a falhar por falta de `-isysroot`. Nada lhe tocou — a `PERNA B` pode estar a
   subtrair nomes que não estão a falhar. Por decidir: re-medir em vez de citar.
7. **`T-23-08`** (limites superiores em `phoff + i*phentsz` / `p_offset` no
   `sys_spu_image_import_handler`): disposição `accept` nesta fase, **a reavaliar na Fase 24**,
   que passa a executar imagens não pré-liftadas.

**A seguir no v1.4:** Fases **24** (interpretador SPU) e **25** (fence GCM) correm **em
paralelo** — tocam subsistemas disjuntos e ambas dependem só de a Fase 23 estar fechada,
que é a regra de ouro do marco (um bloco de risco só entra depois de o portão existir e
saber reprovar). Ambas devem atravessar `scripts/upstream_block_gate.sh` e deixar o seu
registo em `docs/upstream-blocks/`. Antes de a Fase 24 correr o portão: **re-ancorar**
`docs/UPSTREAM_DELTA.tsv`, porque qualquer bloco que mude o número de linhas de um ficheiro
ancorado faz a `PERNA B` reprovar por `ANCORA_DESLOCADA` e não por regressão.

---

## Estado anterior (v1.3) — preservado

### Marco v1.3 — tooling Xenon-style e RE assistida (fechado nos requisitos, 11/11)

**Aberto:** 2026-08-02, a partir de
`docs/superpowers/plans/2026-08-02-milestone-xenon-tooling-e-re-ia.md`.

**Porquê agora:** o valor central do projecto é que a recompilação seja reproduzível a
partir do binário original. Hoje não é: **88 `patch_*.py` no monorepo e 139 no
`../gow2-recomp`** (contados a 2026-08-02) são agulhas de texto sobre o `.cpp` gerado, e
uma correcção de *correctness* só sobrevive a um re-lift porque alguém volta a correr um
script de substituição de strings. O v1.3 constrói o mecanismo (mid-asm, weak override,
switch table declarada em TOML) que torna esses fixes re-lift-safe, e o playbook de RE que
os encontra mais depressa.

**Fases:** 16 (inventário) → 17 (mid-asm) → 19 (weak + migrações) → 21 (fecho);
18 (jump tables) e 20 (oráculo + playbook) correm em paralelo depois da 16.

**Critério de fecho:** ≥3 fixes `CORRECTNESS` re-lift-safe, `verify_lift.sh` no caminho de
aceitação, smoke de intro sem regressão, playbook usado 1× numa wall real.

### Fecho medido (2026-08-03, plano 21-03)

**Os quatro critérios estão satisfeitos, com o número escrito:**

| Critério de fecho | Medido | Onde |
|---|---|---|
| ≥3 fixes `CORRECTNESS` re-lift-safe | **4 linhas / 3 mecanismos** (a leitura conservadora decide, e dá o mínimo exacto) | ledger `PATCH_MIGRATION.tsv`, `XEN-05` |
| `verify_lift.sh` no caminho de aceitação | **sim** — e **já lá estava** antes do marco | `accept_relift.sh:73`, perna 2 |
| Smoke de intro sem regressão | `st620_max=11`, `OOB=0`, `lifted=52068`, órfãos=0 | T9 do results doc |
| Playbook usado 1× numa wall real | **sim**, parede 4, veredicto **ABERTO** (não forjado) | `docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md` |

**Requisitos: 11/11 completos** — 10 inteiros e `RE-03` na metade **soft** que o próprio
critério prevê (MCP `wont`, 4 bloqueios medidos).

**As duas ressalvas que mudam o que se pode afirmar** (as dez estão em
`games/gow2/notes/milestone-2026-08-xenon-re-results.md` § "Ressalvas do marco"):

1. **O lift de PRODUÇÃO não tem uma única chamada de mid-asm** (`grep -rl 'gow2_midasm'
   ../gow2-recomp/recomp_macos_v2/` → zero ficheiros). O mecanismo está **provado**, não
   **promovido** — promover tem gate próprio (`promote_lift.sh`) e é decisão humana. O
   smoke T9 corre o binário de produção, logo prova **não-regressão da intro**, e **não** o
   mid-asm em execução.

2. **A sequência canónica 1→8 nunca correu numa só passagem** (`WINDOWS` #16). Cada etapa
   tem evidência isolada; a cadeia como unidade continua NÃO-EXERCITADA.

**Métrica do marco, ponta a ponta:** `patch_*.py` de classe `CORRECTNESS` ainda activos
**78 → 72** (−6, 7,7 %), dos quais 2 eram `redundant` (nunca precisos). O marco não limpou
o corpus: entregou o **mecanismo** que permite limpá-lo, provado em 4 linhas reais.

**Próximo passo operacional:** `21-VERIFICATION.md` (é a única fase do marco sem
verificação no disco), depois a auditoria do marco. A seguir, a Fase 11 do v1.1 — que ganha
com este marco, porque as suas correcções passam a ter caminho para sobreviver ao re-lift.

### O que fica pendente e não foi cancelado

| Item | Estado |
|---|---|
| **Fase 11 do v1.1** (as 4 paredes: `func_002545B0`, `func_002182A4`, `func_002547AC`, `func_0041F700`) | **Aberta, transita.** Nenhum critério dela foi dado por cumprido. É retomada depois do v1.3 — e ganha com ele, porque as correcções dessas paredes passam a sobreviver ao re-lift |
| **Marco v1.2** (Toolchain Go PoC, fases 12–15) | `queued`, não activo. Enfileirado por outra sessão a 2026-08-02 15:57; **não renomeado nem absorvido**. A numeração 12–15 fica-lhe reservada, e por isso o v1.3 arranca na fase 16 |
| REG-03 do v1.1 | Continua **NÃO-FECHÁVEL** como escrito (o critério media uma string inexistente). Reformulado na Fase 11, não neste marco |

**A numeração não é a ordem de execução:** v1.2 está queued e v1.3 está a correr.

---

## Estado anterior (v1.1) — preservado

## Referência do Projeto

Ver: `.planning/PROJECT.md` (atualizado 2026-07-30)

**Valor central:** um jogo de PS3 vira executável nativo sem emulador — e a recompilação
é reproduzível a partir do binário original, não de edições manuais no código gerado.
**Foco atual:** Marco v1.1 — repor o boot até ao `thr_auto_load`/`R_PermA` e fechar o
buraco de aceite que deixou essa regressão passar despercebida durante dias.

## Posição Atual

Fase: 10 — A parede do DecodeAu — **2/2 planos completos, encerrada**
(10-01 e 10-02 concluídos; REG-03 continua PARCIAL).
Plano: 10-02 (build de teste + gate de 6 + veredicto honesto) concluído.
Status: Fase 6 completa (2/2); Fase 7 completa (3/3); Fase 8 completa (3/3,
sem causa nomeada); Fase 9 encerrada (a cadeia FIOS reposta, 7 patches,
`elo_stopped` avançou de "2o movie" para "re-Play (NOPIC)", mas REG-03 não
fechou — `thr_end=0` em 6/6); Fase 10 encerrada — a parede do DecodeAu
(objectivo nomeado da fase) está **vencida**: `cellVdecDecodeAu` chamado
10x em 6/6 corridas (idêntico à referência), depois de repor `AREAD-HLE`
(10-01) e `F2B-STREAM-ALIGN`/`RESYNC`/`DESYNC-STOP` (10-02, elimina o abort
`FREELIST-TAG-GUARD need=0x31304350`). MAS `thr_auto_load() end` continua 0
em 6/6 — REG-03 **não fecha**, classificado honestamente PARCIAL (não
forjado). Nova parede identificada por comparação directa de logs
(referência 3/3 OK vs teste 6/6 REGRESSAO no mesmo ponto de execução):
`TYPE15`/`func_000CB56C` toma um ramo `(match WAD path)` com
`idx=0x0/ent=0x40637C08` (produto inválido `0x4F72626F`) onde a referência
toma `idx=0x54/ent=0x47D00000` (válido, `reused=1`) — nunca chega à 2ª
entrada de `func_000CB56C` nem a `sys_ppu_thread_create("AUTO_LOAD")`.
Reconecta com a parede TYPE15/CB56C já adiada desde a Fase 6 (CLAUDE.md
`<deferred>`), agora observável pela primeira vez.
Última atividade: 2026-07-31 — `10-02-PLAN.md` executado (2/2 tasks).
Ver `.planning/phases/10-a-parede-do-decodeau/10-01-SUMMARY.md`,
`10-02-SUMMARY.md`, `../gow2-recomp/notes/2026-07-31-fase10-inventario-marcadores.md`,
`../gow2-recomp/notes/2026-07-31-fase10-decodeau-veredicto.md`.

Progresso: [==========] 100% dos 2 planos da Fase 10 (10-01 e 10-02
completos) dentro do Marco v1.1 (Fase 6 — completa; Fase 7 — completa; Fase
8 — completa sem causa nomeada; Fase 9 — encerrada, parede andou uma etapa;
Fase 10 — encerrada, parede do DecodeAu vencida, REG-03 PARCIAL, nova parede
TYPE15/CB56C mapeada para fase futura não planeada)

## CORRECÇÃO SUBSTANCIAL — 2026-08-01

Três coisas que este ficheiro afirmava deixaram de ser verdade. Ficam aqui ao lado
do que substituem, não apagadas.

### 1. REG-03 nunca foi fechável — o critério media uma string inexistente

`thr_end` fazia `grep -c 'thr_auto_load() end'`. Essa string **não existe em nenhum
dos 26 binários guardados** (incluindo as referências `pre_v3`/`rdy0_ref`) nem em
nenhum fonte. Logo `thr_end` era sempre 0 e `classify()` devolvia sempre REGRESSAO,
**medisse o binário o que medisse**.

O "REG-03 PARCIAL" da Fase 10 não era um resultado parcial: era um instrumento cego.
As Fases 7, 8, 9 e 10 estiveram presas a um critério que nenhum trabalho podia fechar.

Corrigido: `ps3recomp 4abd5a2` (marcador real de fim de thread, verificado in-boot) e
`gow2-recomp e1ee114` (o gate mede-o e reporta `thr_created` à parte).

### 2. A "nova parede TYPE15/CB56C" comparava contra o nosso próprio paliativo

A Fase 10 concluiu que a referência tomava `idx=0x54/ent=0x47D00000 (válido,
reused=1)` e o teste não. **`0x47D00000` é a zona pinada fabricada pelo paliativo
TYPE15** — não é memória do jogo. A "referência" era o comportamento do workaround.

Esse paliativo foi removido a 2026-08-01 (`gow2-recomp 916dc6a`) com prova de
não-regressão: tabela vazia 13→0, OPD-BAD 14→1, R_PermA e st620 inalterados, e 3184
despachos sem um único vazio. Existia para sobreviver a um stomp que já tinha sido
corrigido nessa manhã (o `F2B-STREAM-PUMP` escrevia 770 KiB para lá do ring real).

### 3. "Chegar ao AUTO_LOAD" não é o alvo — é uma consequência

Medido: a thread `AUTO_LOAD` é criada pelo **primeiro handler de estado do loop
principal** (`func_002B2DD0`, despachado por `*(r30+0x460C)` em `func_00242C94`).

E o boot **nunca entra no loop principal**. Cadeia medida degrau a degrau, com sonda
de CONTROLO em cada bissecção:

```
main() func_0025C838
  └─ func_002B2E74            (7ª de 9 chamadas, em linha recta)
       └─ func_000B71B8 (B71) (9ª de 11)  ← ENTRA E NÃO RETORNA
  ✗ func_002B2E04 (8ª)  →  func_00242C94, o LOOP PRINCIPAL, nunca corre
```

Três paredes em fila dentro do B71, todas a mesma doença — um método de contentor
sobre um objecto que não é um contentor. Com três gates de diagnóstico o boot entra
no loop e despacha o primeiro estado (`[STATE] frame=1`).

### O que ficou ELIMINADO por medição (não voltar a perseguir)

| hipótese | veredicto |
|---|---|
| o registry de tipos não está populado | **falso** — 26 tags, todos com vtable viva |
| o objecto entregue está corrompido | **falso** — construtor completo e correcto |
| o lift perdeu um store/fallthrough | **falso** — 3 suspeitas, 3 refutadas no binário |
| a cadeia de indirecções está partida | **falso** — os 3 ponteiros são válidos |
| "AUTO_LOAD nunca criada" é a parede | **falso** — é consequência, não causa |

Sobra: **duas classes com layouts incompatíveis a partilhar memória**, e um despacho
que as junta. Endereços dos dois lados em
`gow2-recomp/notes/2026-08-01-o-boot-entra-no-loop-principal-com-gates.md`.

## Métricas de Desempenho

**Velocidade:**

- Planos concluídos: 3 (histórico do Marco v1.0; zero planos do Marco v1.1 até agora)
- Duração média: ~40min (01-01 ~35min, 01-02 ~40min, 01-03 ~50min — estimados, sem cronometragem exata)
- Tempo total de execução: —

**Por fase:**

| Fase | Planos | Total | Média/Plano |
|------|--------|-------|-------------|
| - | - | - | - |

**Tendência recente:** sem dados para o Marco v1.1 (ainda não há planos executados).

## Contexto Acumulado

### Decisões

Registradas em `PROJECT.md`, bloco `<decisions>`. **19 decisões, todas `proposto` —
nenhuma travada** (nenhum ADR do conjunto tem `Status: Accepted`).

- ADR-ARCH-7 (inventário de backends) e ADR-PLAT-4 (áudio/input) são **conhecidamente
  desatualizados**: carregam `nota` e não sobrepõem a política Metal/CoreAudio.

- Escopo do Marco 1 (v1.0) decidido pelo usuário no portão de ingest: RDY-0 apenas.

- Escopo do Marco v1.1 decidido a partir de medição directa (2026-07-30), não de plano
  herdado: localizar+corrigir a regressão do 2.º `StartSeq`, e fechar o buraco de aceite
  que a deixou passar. Ver `.planning/PROJECT.md` seção "Current Milestone: v1.1".

- **(16-02, 2026-08-02)** No `PATCH_MIGRATION.tsv`, `ea='-'` limita uma linha a **P1**
  mesmo quando o corpo do script nomeia a função (caso `patch_icg_ctor_opd.py`): `ea` é
  coluna **derivada** e escrevê-la à mão faria o próximo regen apagar o P0. Um P0 com
  prazo de validade é pior do que um P1 honesto.

- **(16-02, 2026-08-02)** As contagens de decisão humana do ledger (`N_classificados`,
  `N_P0`, pilotos das Fases 17/18) são **geradas** por `render_tsv()`, não escritas à
  mão — mesmo motivo pelo qual `classe` e `ea` são derivadas. Um `N_P0=7` em comentário
  seria falso na primeira linha nova.

- **(16-02, 2026-08-02)** Um `aceite` só entra no ledger depois de ser **corrido** contra
  o lift de produção (`recomp_macos_v2`). Os que não puderam ser corridos ficam
  declarados `não-exercitado` — hoje é 1 de 19 (o `smoke:bytes_read=20169344` do
  `2550C8`, que precisa de boot e é trabalho da 16-03).

- Roadmap do Marco v1.1: 3 fases (6, 7, 8), não a faixa `standard` 4–6, por desenho — as
  9 frentes dos requisitos (REG/GATE/DIAG) agrupam-se em três blocos coesos:
  diagnóstico, gate, correção. A Fase 7 (gate) foi ordenada para não depender da Fase 6
  (causa) — pode ser construída em paralelo, porque a prova de rejeição/aceitação usa
  binários que já existem hoje (produção actual falha, `boot_gow2.pre_v3` funciona).

### Todos Pendentes

Nenhum registrado ainda para o Marco v1.1.

### Bloqueadores / Preocupações

> **Histórico do marco v1.1 — preservado, não activo no v1.4.** Estes itens
> descrevem a parede do boot do marco anterior. Ficam por medir, não por
> resolver; nenhum bloqueia a integração do upstream. Re-rotulado 2026-08-12.

- **[Marco v1.1 — ACTIVO, é o motivo do marco]** O boot não chega ao `thr_auto_load` em
  produção: `StartSeq=1, thr_end=0, R_PermA=0`, medido em quatro corridas independentes a
  2026-07-30 (probes headless, probes Metal, binário pré-promoção do v1.0, e o script
  oficial `rodar_gow2_menu_fast.sh` sem alterações), contra a referência `2/1/1` das notas
  de 22–25 Jul. Três discriminadores negativos, todos medidos: não são os probes desta
  leva, não é a promoção do v1.0, não é a recipe do script. O bissect sobre binários
  guardados fecha a janela em ~8h: `boot_gow2.pre_v3` (25 Jul 18:23) funciona,
  `boot_gow2_v4` (26 Jul 02:35) já falha como produção. Duas notas datadas dessa janela
  são pistas ordenadas, não conclusões — a hipótese TOCFIX está **enfraquecida por
  medição** (só 19 de 16278 OPDs do ELF divergem do valor estampado). Bloqueia toda a
  investigação da parede TYPE15: os zeros do `CD498-PROBE` deixam de ser interpretáveis
  enquanto o boot não chegar ao `R_PermA`. Ver
  `../gow2-recomp/notes/2026-07-30-o-boot-nao-chega-ao-auto-load.md` e
  `.planning/ROADMAP.md` Fases 6–8.

- **[Marco 1 (v1.0) — RESOLVIDO em 01-02-PLAN.md, 2026-07-26]** A mina do re-lift: um re-lift
  apagava a instrumentação de 4 atômicos da Fase 0 (`ppu_res_*` sem
  `vm_oob`/`ps3_mem_oob_report`). Desarmada por D-01: os quatro `ppu_res_*` do
  `SOURCE_PREAMBLE` agora chamam `ps3_mem_oob_report` (report-only, matriz len/op D-03),
  fixado por teste-ouro + mutação + harness C++ compilado e rodado. Ver
  `01-02-SUMMARY.md`.

- **[Marco 1 (v1.0) — RESOLVIDO em 04-CONTEXT.md/04-01-PLAN.md, 2026-07-26]** Divergências de
  contagem de `patch_*.py`: **73** medidos vs **71** no plano RDY-0 estavam ambos
  desatualizados — o número real medido na sessão de planeamento da Fase 4 e reconfirmado
  por `gen_catalog.py` é **87** (`../gow2-recomp/recomp_mid_v2/`, nunca o espelho subtree
  atrasado `games/gow2/recomp_mid_v2/`, que tem só 75/87). `PATCH_CATALOG.tsv` fecha os
  87/87 sem indeterminado (64 FUNCIONAL / 23 PROBE). Ver `04-01-SUMMARY.md`.

- **[Marco 1 (v1.0) — dívida transitada, não bloqueia o v1.1]** Gate reformulado dá `rc=1`
  contra `recomp_macos_v3` fresco em condições isoladas: par `GRUPO:opd-dispatch` com
  deficit residual de 36; 3 `patch_*.py` `FAILED` específicos de `v3`. `REQ-RDY0-4`
  fechado a 2026-07-30 (ver Rastreabilidade em `REQUIREMENTS.md`). Ver `03-03-SUMMARY.md`.

- **[Marco 1 (v1.0) — RESOLVIDO em 02-01-PLAN.md, 2026-07-26]** Host órfão tinha TRÊS cifras
  conflitantes: **1603** (plano RDY-0, desatualizada), **1662** (gate anterior, só
  `injected_*.cpp`) e **1793** (inventário real, incluindo as 131 linhas de
  `preamble_pure.cpp`). `gen_manifest.py`/`MANIFEST.tsv` passam a cobrir também o
  preâmbulo puro via 2 marcadores `kind=PREAMBLE`, provados por mutação; as três cifras
  ficam reconciliadas no próprio `MANIFEST.tsv`. Ver `02-01-SUMMARY.md`.

- **[Marco 5, esboçado]** `REQ-track-g-metal-content-stack` está **[CONTESTED]** (M2–M10 GREEN em 2
  PRDs, aberto em 2 PRDs). Não resolver por precedência nem por data; medir.

- **[Marco 4, esboçado]** Decisão pendente do autor: aceite de reprodutibilidade exato-por-manifesto
  vs equivalente-funcional-por-gates.

- **[Geral]** A CI não cobre nenhum lift de título real, nenhum `patch_*.py` e nenhum smoke
  de boot. "CI verde" é sinal sobre o motor e sinal nenhum sobre o port.

## Itens Adiados

| Categoria | Item | Estado | Adiado em |
|-----------|------|--------|-----------|
| *(nenhum)* | | | |

## Continuidade de Sessão

**Marco v1.4 (2026-08-12):** roadmap criado com **5 fases (22–26)**, dentro da faixa
`standard` 4–6 do `config.json`. Cobertura **11/11** requisitos (`UPST-01..03`, `LV2S-01`,
`SPUI-01..03`, `GCMF-01..03`, `SPRS-01`) mapeados sem órfãos nem duplicados. A numeração
**continua em 22** (último usado: 21) e **não** toca nas fases 12–15 do `v1.2` queued. As
quatro âncoras do marco foram **re-verificadas no código nesta sessão** antes de entrarem
nos critérios: `runtime/spu/spu_workload.h:6` (a frase "We do not interpret SPU ELFs at
runtime" está lá), `libs/video/cellGcmSys.c:615` (`vm_write32(GCM_CTRL_EA + 8, val)` no
`case 0x0050`), `runtime/syscalls/lv2_register.c:825-826` (`+0x10`=0 / `+0x14`=`src_ea +
p_off`) e `libs/spurs/spurs_kernel.c` (existe). O remote `upstream` aponta para
`sp00nznet/ps3recomp` — a Fase 22 tem de comparar contra o **branch vivo**, nunca o
`master`. Próximo passo operacional: `/gsd-plan-phase 22` (a 23 pode arrancar em paralelo
na parte do portão).

---

**Marco v1.1 (2026-07-30):** Roadmap criado com 3 fases (6, 7, 8) a partir da medição de
`../gow2-recomp/notes/2026-07-30-o-boot-nao-chega-ao-auto-load.md`. Cobertura 9/9
requisitos (REG-01..04, GATE-01..03, DIAG-01..02) mapeados sem órfãos nem duplicados. Ver
`.planning/ROADMAP.md` e a nota de reconciliação na rastreabilidade de
`REQUIREMENTS.md` (o briefing citava "10 requisitos"; o texto tem 9 — usada a contagem
verificada). Próximo passo operacional: `/gsd-plan-phase 6` (ou `/gsd-plan-phase 7`, que
não depende da causa e pode correr em paralelo).

---

**Histórico do Marco 1 (v1.0), preservado abaixo — última sessão 2026-07-29:**

Última sessão: 2026-07-29
Parou em: 05-01-PLAN.md concluído (2/2 tasks) — Wave 1 da Fase 5 fechada (05-01 + 05-02).
`smoke_relift_equiv.sh` (novo, `../gow2-recomp/` + mirror `games/gow2/`): build
automático `FORCE_REBUILD_LIFT=1` (T-05-01, nunca testa binário desatualizado),
duas formas de uso (`[LIFT_DIR] [RUNS] [TSV]` / `--bin BIN_PATH [RUNS] [TSV]`),
loop de N corridas com protocolo G6 completo (kill por PID, `PS3_NO_RSX=1`,
`PS3_RSX_BACKEND=trace`, `PS3_ENGINE_ROOT` explícito), TSV por corrida, portão
`THRESHOLD=(RUNS*2+2)/3` (RUNS=6 -> 4, literal do ROADMAP), classificação
OK/FLAKE_SUSPEITA/REGRESSAO_SUSPEITA por assinatura medida (log_lines>=1000 E
`allocate(0x7D00000)` -> FLAKE; senão REGRESSAO) que nunca reclassifica uma
falha como sucesso (T-05-03). Corrida real contra `recomp_macos_v3`: 6/6
`st620_max=11`, `lifted=52068 modules=13 imports=151`, `rc=0`, zero órfãos.
Modo `--bin` confirmado sem tentativa de build contra `boot_gow2` de produção.
Discriminador FLAKE/REGRESSAO provado offline com fixtures sintéticas
(rotuladas como tal, não uma corrida real) já que as 6 corridas reais saíram
todas OK. Achado durante a Task 1 (fora do `files_modified` deste plano,
reportado e não corrigido por conta própria): `recomp_mid_v2/patch_wadld_alloc_probe.py`
tinha uma chaveta em falta no bloco de diagnóstico `[WADLD-ALLOC]` (gated,
PROBE, OFF por default) que partia a compilação do chunk `ppu_recomp_002.cpp`
de `recomp_macos_v3` desde as 16:47 de 26/jul — 3 dias sem que um rebuild
completo tivesse sido corrido para notar. Corrigido e commitado pelo
orquestrador (`gow2-recomp` `6410170`, `ps3recomp` `775831b`), verificado de
forma independente antes de retomar (`clang++ -fsyntax-only`: 0 erros, era 20).
Produção (`recomp_macos_v2`) nunca esteve afetada. `ctest --test-dir
build-tests`: 22/22, sem regressão. Ver
`.planning/phases/05-smoke-de-equival-ncia-e-promo-o/05-01-SUMMARY.md`.

---

Sessão anterior (05-02-PLAN.md, 2/2 tasks): `manifest_delta_gate.py` (novo): converte a saída de `gen_manifest.py --verify`
em ids opacos (individuais `AUSENTE:`/`AMENOS:` + tokens sintéticos por
unidade de déficit de grupo `GRUPO:<id>:0..N-1`) e delega a comparação por
delta a `baseline_delta.py` (D-4.5, Fase 4, reusado sem modificação — `git
diff --stat` vazio confirmado). 6 testes RED→GREEN fim-a-fim via subprocess.
`MANIFEST_DEBT_BASELINE.json` congelado por medição real contra
`recomp_macos_v3`: 36 ids `GRUPO:opd-dispatch:0..35` (esperado>=15847,
encontrado=15811 — dívida já declarada na Fase 3, `04-03-SUMMARY.md`).
`verify_lift.sh`/`manifest_step()` reescrito: continua a imprimir o detalhe
por-marcador de `verify_lift_baseline.sh` (transparência humana), mas o `rc`
autoritativo do passo passa a vir de `manifest_delta_gate.py`. Corrido de novo
contra `recomp_macos_v3`: os 3 passos (`lift_parity`/`MANIFEST`/
`audit_boundaries`) reportam `PASS`, `rc` GLOBAL passa a `0` (antes: `MANIFEST
FAIL`, `rc=1` crônico). Prova de injeção nos dois sentidos contra cópia
scratch (`recomp_macos_v3_scratch_manifest_fault`, apagada no fim): deficit
PIOR (removida mais 1 ocorrência de `ps3_call_opd(ctx,`, 37 em vez de 36) →
`rc=1` citando `GRUPO:opd-dispatch:36`; deficit MELHOR (simulado via dívida
congelada temporariamente inflada a 40) → `rc=0`, `resolvidos=4`, nunca falha.
`recomp_macos_v3` real confirmado intacto por hash MD5 antes/depois
(`eef1688495317aab6c5674aaa388168b`, idêntico). `ctest --test-dir
build-tests`: 22/22, sem regressão. Ver
`.planning/phases/05-smoke-de-equival-ncia-e-promo-o/05-02-SUMMARY.md`.

**Use `.venv/bin/python3`** ao executar: o `python3` nu desta máquina é 3.9.6 e não
satisfaz o piso `>=3.11` do projeto. É `<precondition>` na primeira tarefa de cada plano.

## Session

**Last session:** 2026-08-12T19:03:59.717Z
**Stopped at:** Completed 23-01-PLAN.md
mapeados, rastreabilidade v1.4 acrescentada ao `REQUIREMENTS.md`. Nenhum plano criado
ainda. Próximo: `/gsd-plan-phase 22`.
**Resume file:** None

**Sessão anterior:** 2026-08-03T03:20:00.000Z — Completed 21-03-PLAN.md — LEDG-02 fechado;
marco v1.3 fecha INTEIRO nos requisitos (11/11, RE-03 na metade soft) com dez ressalvas
escritas. Segue a 21-VERIFICATION.md e depois a auditoria do marco.

## Performance Metrics

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 02 P02 | ~55min | 2 tasks | 3 files |
| Phase 02 P03 | ~1h40min | 2 tasks | 3 files |
| Phase 03 P01 | 20min | 2 tasks | 4 files |
| Phase 03 P03 | 20min | 3 tasks | 7 files |
| Phase 04 P01 | ~35min | 2 tasks | 3 files |
| Phase 04 P05 | ~2h | 2 tasks | 2 files |
| Phase 05 P02 | ~40min | 2 tasks | 4 files |
| Phase 05 P01 | ~50min | 2 tasks | 2 files |
| Phase 05 P04 | ~50min | 2 tasks | 3 files |
| Phase 06 P01 | 35min | 2 tasks | 2 files |
| Phase 06 P02 | ~2h | 3 tasks | 3 files |
| Phase 07 P01 | 55min | 3 tasks | 4 files |
| Phase 07 P03 | 40min | 2 tasks | 7 files |
| Phase 07 P02 | 35min | 2 tasks | 1 files |
| Phase 08 P01 | 23min | 2 tasks | 4 files |
| Phase 08 P02 | ~75min | 2 tasks | 2 files |
| Phase 08 P03 | ~25min | 2 tasks | 1 files |
| Phase 09 P01 | 220min | 3 tasks | 3 files |
| Phase 10-a-parede-do-decodeau P01 | 45min | 2 tasks | 3 files |
| Phase 10-a-parede-do-decodeau P02 | 90min | 2 tasks | 2 files |
| Phase 16 P01 | 35min | 2 tasks | 5 files |
| Phase 17 P01 | ~55min | 2 tasks | 5 files |
| Phase 21 P01 | 21min | 3 tasks | 3 files |
| Phase 21 P02 | ~35min | 3 tasks | 1 files |
| Phase 21 P03 | ~35min | 3 tasks | 3 files |
| Phase 22 P01 | ~1h | 3 tasks | 5 files |
| Phase 22 P02 | ~1h | 3 tasks | 7 files |
| Phase 23 P02 | 50min | 3 tasks | 6 files |
| Phase 23 P01 | 52 | 3 tasks | 3 files |

## Decisions

- [Marco v1.4]: roadmap com **5 fases (22–26)**, dentro da faixa `standard` 4–6 — o `UPST-03` foi agrupado com o `UPST-01` (fase leve de inventário) por serem a mesma natureza no sentido inverso, em vez de virar fase própria, que não teria critério observável sozinha.
- [Marco v1.4]: `UPST-02` (portão) e `LV2S-01` (o bloco mais pequeno) ficam na **mesma fase 23** — o portão precisa de um primeiro atravessamento real para deixar de ser teórico, e o `LV2S-01` é o único bloco do marco cujo risco de regressão é ~zero.
- [Marco v1.4]: a Fase 23 vem **antes** da 24 e da 25 por imposição da regra de ouro do marco (um bloco que não passe o smoke não entra) — não por conveniência de ordenação.
- [Marco v1.4]: `SPUI-*` e `GCMF-*` ficam em fases **independentes** (24 e 25) — subsistemas disjuntos, podem correr em paralelo; juntá-las faria um bloco grande demais para o portão discriminar qual metade regrediu.
- [Marco v1.4]: o critério do `LV2S-01` **preserva a ausência de aceite in-boot** em vez de inventar uma observação de boot — `grep image_import games/gow2/` dá zero, e o portão da intro só prova não-regressão, nunca exercício do caminho corrigido (G5).
- [Marco v1.4]: a numeração continua em **22** e as fases **12–15** ficam reservadas ao `v1.2` queued — não reutilizadas nem renumeradas.
- [Marco v1.1]: Roadmap com 3 fases (6 Diagnóstico, 7 Gate, 8 Correção), abaixo da faixa standard 4-6 por desenho -- as 9 requisitos formam tres frentes coesas, nenhuma adicional teria criterio de sucesso observavel isolado.
- [Marco v1.1]: Fase 7 (Gate) ordenada para nao depender da Fase 6 (causa) -- pode ser planeada/executada em paralelo, a prova de rejeicao/aceitacao usa binarios que ja existem hoje.
- [Phase ?]: D-2.3/D-2.4: F2B extraido por simbolo (17 simbolos) para games/gow2/host_gow2_f2b.c, nao por intervalo de linhas -- g_wadld_eof_ea fica de fora apesar de fisicamente vizinho
- [Phase ?]: D-2.5: guard do host_gow2_f2b.c em build_macos.sh redesenhado (3 condicoes: ancora prefix-agnostica + 2 listas static explicitas) porque 5 das 7 funcoes F2B e os 10 globais sao static (linkagem interna) no lift de producao, nao extern C -- um guard so-extern deixaria estado duplicado em silencio em vez de duplicate symbol ruidoso
- [Phase ?]: D-3.5: RELIFT=1 nunca escreve em recomp_macos_v2 (guarda por caminho canonico, testada com relativo/absoluto/barra final)
- [Phase ?]: D-3.6: gap herdado da Fase 2 fechado parcialmente -- host_gow2_factory confirmado por nm num lift fresco; host_gow2_f2b nao exercitavel no checkout ../gow2-recomp por divergencia pre-existente (ficheiro/bloco so' no monorepo)
- [Phase ?]: REQ-RDY0-4 fechado 2026-07-30 apos recalibracao do grupo opd-dispatch (commit 0e49c92) -- rastreabilidade marcador-escritor (21 vs 246) fica como divida declarada
- [Phase ?]: Dois commits (test antes de feat) seguindo o gate TDD do plano 04-01: classify() puro por AST + PATCH_CATALOG.tsv derivado, 64 FUNCIONAL / 23 PROBE, bate exatamente a medicao do planeamento.
- [Phase ?]: Fase 4 fechada (5/5 planos): apply_all_patches.sh com 6 estados (D-4.1), --status, verify_lift.sh no fim (D-4.6), injecao de falha real fim-a-fim provada. REQ-RDY0-5 completo (4/4 criterios ROADMAP), apesar de rc global=1 (42 UNVERIFIED por cobertura parcial de CONTRACTS.tsv + MANIFEST FAIL herdado da Fase 3).
- [Phase ?]: 05-02: manifest_delta_gate.py -- MANIFEST passa a falhar so por achado NOVO (D-5.1), divida congelada de 36 ids GRUPO:opd-dispatch medida contra recomp_macos_v3; verify_lift.sh rc global passa a 0
- [Phase ?]: 05-01: smoke_relift_equiv.sh criado (build automatico FORCE_REBUILD_LIFT=1, 6 corridas reais st620=11 em 6/6 OK, TSV, portao >=4-de-6, --bin); T-05-01 apanhou chaveta em falta em patch_wadld_alloc_probe.py que partia recomp_macos_v3, corrigido pelo orquestrador (6410170/775831b)
- [Phase ?]: 05-04: promote_lift.sh (D-5.3) -- log-antes-de-tocar: PROMOTION_LOG.tsv passa a ter tres estados (PROMOTE-BEGIN escrito ANTES de qualquer mv/cp em producao, PROMOTE-OK/PROMOTE-FAIL no fim), corrigido depois de uma promocao real ter sido interrompida externamente durante a rehearsal e o --revert nao ter conseguido confirmar por hash por falta de registo. Prova real completa (promover recomp_macos_v3, reverter, hash identico antes/depois) e prova de interrupcao em sandbox sintetico (kill -9 real).
- [Phase ?]: bisect_regression.sh: classificação por log_lines<200 (STRUCTURAL_FAIL) / thr_auto_load_end>=1 (OK) / senão REGRESSAO
- [Phase ?]: pgrep -x em vez de pgrep -f no relatório de órfãos — evita falsos positivos de watchers de shell
- [Phase ?]: REG-02 nao fechado: TOCFIX/ctx->lr, strip_block e regressao comportamental do lifter todas refutadas por medicao real; causa continua nao nomeada, funil mais estreito p/ Fase 8
- [Phase ?]: GATE-01/GATE-02: smoke_chain_gate.sh mede os 5 elos bloqueantes da cadeia (intro/2o movie/re-Play/AUTO_LOAD/WAD) e nomeia o elo que parou; nucleo partilhado com bisect_regression.sh via lib_boot_chain_metrics.sh
- [Phase ?]: DIAG-02: os sete patch_*.py restantes (CD498/17ACC/FLIPPATH/PRESENTLOOP/MENUPRESENT/SCHEDARM/POSTTHR) encadeiam SIGTERM -- prova real com os sete gates activos simultaneamente mostra as sete tags SUMMARY sem perdas
- [Phase ?]: GATE-03: accept_relift.sh ganha PERNA 4 (smoke_chain_gate.sh) -- rejeita hoje recomp_macos_v2/producao com rc=1, elo_stopped=2o movie (StartSeq); PERNAS 1/2/CONTADORES sem regressao, PERNA 3 tem falha pre-existente fora de escopo
- [Phase ?]: [Fase 8, 08-01]: PS3_PATCH_DIR configuravel + patch_ab_sandbox.sh (extractor filtrado por PROBE, extrai tambem modulos partilhados como lift_paths.py); A/B real conclui patches candidato mas com confundimento documentado (32/52 patches e6d65a2 falham por deriva de forma do lift, nao por mudanca de comportamento) -- thr_auto_load fica identico entre os dois conjuntos, o que pesa contra esta pista
- [Phase ?]: [Fase 8, 08-02]: candidatos patches e runtime f6708cb AMBOS REFUTADOS por medicao real do binario -- veredicto REFUTADA-AMBAS. Proximo passo: bissecao automatizada de runtime/+libs/ na janela 8648805..4e815cf
- [Phase ?]: [Fase 8, 08-03]: nenhuma causa corrigida (REFUTADA-AMBAS) -- REG-03 declarado explicitamente NAO cumprido, REG-04 nao aplicavel. accept_relift.sh reconfirma REJEITADO/rc=1 antes e depois da fase (gate nao regrediu). Funil de 9 candidatos eliminados documentado; proximo passo: bissecao de runtime/+libs/ na janela 8648805..4e815cf
- [Phase ?]: [Fase 9, 09-01]: bisect_verdict.sh (oraculo 3 niveis) provado contra a janela real; RELINK nunca e valido nesta janela (lifter sempre difere do de referencia); 8648805 esta numa ramificacao paralela ao porte GoW2/macOS (axioma, nunca re-testavel)
- [Phase ?]: AREAD-HLE (early-out de func_002B3D1C) extraído verbatim do lift de referência e reposto via patch idempotente com recusa atómica
- [Phase ?]: REG-03 classificado PARCIAL: DecodeAu vencido (10x, 6/6, F2B-STREAM-ALIGN elimina abort FREELIST-TAG-GUARD), mas thr_end continua 0/6 -- nova parede TYPE15/func_000CB56C mapeada por comparacao directa de logs
- [Fase 16, 16-01]: o universo do ledger e' o corpus de BUILD (139 medidos em ../gow2-recomp/recomp_mid_v2); o unico patch so'-no-monorepo (patch_freelist_guard_262610.py) fica declarado no cabecalho SEM linha, para nao inflacionar o denominador da metrica da Fase 21
- [Fase 16, 16-01]: EA so' entra na coluna medida `ea` quando CONFIRMADA pelo corpo do script (78/139); o palpite lido do nome vai para a coluna separada `ea_pista_nome` (14), porque escrever um endereco que o corpo nunca menciona seria forjar uma medicao (regra 4 do CLAUDE.md)
- [Fase 17, 17-01]: `jump_address` do `[[midasm_hook]]` e' RECUSADO com rc=2, nao avisado -- avisar-e-continuar deixaria no disco um lift que chama o hook e NAO salta, a surpresa "declarado mas sem efeito" que o proprio `recomp_config.py` ja rejeita no `default` do `[[switch_table]]`. O plano permitia warn OU hard-fail; escolhida a forma forte
- [Fase 17, 17-01]: o golden de nao-regressao foi capturado com o lifter ANTERIOR a' mudanca (`4bf3bf8`) e commitado (`tools/testdata/midasm_golden_noconfig.c`); e' comparado por `difflib.unified_diff` e foi provado DISCRIMINANTE por mutacao (injectar a emissao sem config mata-o). Unica normalizacao: a linha `/* lifter-rev */`, nao-determinista por construcao
- [Fase 17, 17-01]: a cobertura dos hooks (EA orfao) e' verificada no processo PAI contra `all_insns` + `func_bounds`, antes do lift -- reportar do worker obrigava a mudar o retorno de `_worker_lift`, que `test_invalid_instructions.py` desempacota posicionalmente. Assim `-j1` e `-jN` dao o mesmo veredicto e a falha e' em ~2s
- [Fase 21, 21-03]: a coluna "antes" da metrica do marco e' lida por `git show ee17e2c:games/gow2/lift_baseline/PATCH_MIGRATION.tsv` -- o ledger e' versionado, logo o antes/depois tem um commit por tras em vez de uma citacao de SUMMARY. Assim a trajectoria pode ser dada por fase (78 -> 76 -> 74 -> 72), nao so' nas duas pontas
- [Fase 21, 21-03]: `XEN-05` reportado nas duas leituras, e a CONSERVADORA (uma funcao = um mecanismo, 3) e' a que decide -- as duas linhas `ce03c_*` partilham funcao e hook, conta-las como dois mecanismos independentes seria inflacionar. Fecha com o minimo exacto do criterio, sem folga, e isso fica escrito
- [Fase 21, 21-03]: `ROADMAP.md` NAO foi commitado nesta fase -- tem alteracoes nao commitadas de outra sessao no MESMO bloco de texto que o 21-03 teria de editar (a lista de planos da Fase 21), logo as linhas nao sao separaveis num commit. Regra 7 do CLAUDE.md (nao commitar working tree alheio) ganha ao criterio do plano; registado em WINDOWS.md em vez de forcado
- [Fase 17, 17-01]: contrato de recusa da ancora fica na forma FRACA (existe instrucao no EA, nao "e' a esperada") -- o schema `[[midasm_hook]]` nao tem campo para declarar a instrucao esperada. Declarado como limitacao em `docs/RECOMP_CONFIG.md` e em WINDOWS #5, nao dado por cumprido
- [Phase ?]: Fase 22: a base de comparacao com o upstream e' o ramo vivo por committerdate (upstream/fix/fold-merge-dropped-fixes), nunca upstream/master -- medido: master esconde 227 commits
- [Phase ?]: Fase 22: A frase historica do PR #82 ('first TOC-loaded operand wins') e larga demais para o ramo vivo do upstream: o defeito residual medido em ee97d01 e mais estreito -- so' morde em tabelas ABSOLUTAS (sem 'add'), porque a reordenacao sintactica deles ja cobre as de offsets
- [Phase ?]: Fase 22: Um diferencial nos-vs-eles exige um vector de CONTROLO onde os dois lados concordam; sem ele, verde nao distingue defeito real de harness enviesado
- [Phase ?]: Fase 22: O patch de contribuicao estende a arbitracao por contagem de alvos que o upstream ja usa para multi-TOC ao candidato a registo-base -- nao substitui a funcao deles pela nossa
- [Phase ?]: Fase 22: UPST-03 fecha-se com patch+teste local: aceitacao pelo upstream nao e criterio (nao a controlamos) e nao ha push para sp00nznet
- [Phase 23]: 23-02: o +0x14 do sys_spu_segment fica como espelho do +0x10 (nao foi limpo) -- e' o que o upstream dd936c4 deixa ficar e e' inofensivo para um leitor que interprete o par como u64-lo
- [Phase 23]: 23-02: LV2S-01 NAO foi marcado completo -- o 23-04 tambem o carrega (a travessia do portao); esta leva entrega so' a metade offline-unit
- [Phase ?]: 23-01: portao de adopcao com refs congelados para SHA imutavel -- HEAD e' alvo movel quando outro plano da mesma wave commita no mesmo ramo
- [Phase ?]: 23-01: o portao exporta SDKROOT quando o operador nao o define; sem isso nenhum leg linka neste host (clang das Command Line Tools sem sysroot)
- [Phase ?]: 23-01: a perna C do portao nao afirma nada sobre sha256 em blocos vazios -- o link de 122 MB NAO e' reproduzivel byte a byte (medido)

## Current Position

Milestone: **v1.4** — integrar o que o upstream avançou e nós não temos
Phase: **22** — O inventário do delta e a contribuição de volta (**não iniciada**, 0 planos)
Plan: — (nenhum plano criado neste marco)
Status: **Roadmap criado** — 5 fases (22–26), 11/11 requisitos mapeados, zero órfãos
Progresso: [----------] 0% — 0/5 fases, 0 planos executados no v1.4
Last activity: 2026-08-12 — roadmap do v1.4 escrito (`ROADMAP.md` fases 22–26,
rastreabilidade v1.4 em `REQUIREMENTS.md`)

## Operator Next Steps

- `/gsd-plan-phase 22` — inventário do delta (`UPST-01`) + contribuição de volta
  (`UPST-03`). É a primeira porque é ela que diz às outras que sha fazer cherry-pick.

- `/gsd-plan-phase 23` — o portão de adopção (`UPST-02`) pode ser **construído em paralelo**
  com a 22; o que depende do inventário é o bloco `LV2S-01` que o atravessa, não a
  ferramenta.

- Fases **24** (SPU) e **25** (GCM) só depois da 23 (regra de ouro), e são **independentes
  entre si** — podem correr em paralelo.

- Fase **26** (veredicto SPURS) é a última — depende da 24.
- **Não iniciar** nenhuma adopção de risco antes de o portão da Fase 23 provar que sabe
  reprovar (regressão injectada → `rc != 0`).

### Pendente de outros marcos (não cancelado)

- **Fase 11 do v1.1** — as 4 paredes (`func_002545B0`, `func_002182A4`, `func_002547AC`,
  `func_0041F700`). Aberta, transita.

- **`21-VERIFICATION.md`** do v1.3 — é a única fase do marco sem verificação no disco;
  depois, a auditoria do marco.

- **Marco v1.2** (Toolchain Go, fases 12–15) — `queued`, não activo.

### Blockers

> **Nenhum bloqueador ACTIVO do marco v1.4.** Os itens abaixo pertencem ao
> marco **v1.1** e ficam preservados como histórico medido — não são
> bloqueios do trabalho de integração do upstream. Conteúdo intacto,
> apenas re-rotulado (2026-08-12), para o `iterate` do autónomo não os ler
> como bloqueios em aberto deste marco.

#### Histórico do marco v1.1 (preservado, não activo)

- REG-02 (causa da regressao nomeada por ficheiro:linha) nao fechado apos a Fase 6: TOCFIX/ctx->lr REFUTADA-EM-COMBINACAO (reversao real + rebuild = REGRESSAO identica), strip_block REFUTADA, regressao comportamental do lifter MEDIDA-MAS-NAO-CONFIRMADA (contagem cresce monotonicamente, vizinhanca de 3 niveis de thr_auto_load byte-a-byte identica entre OK e producao que falha). Proxima medicao recomendada para a Fase 8: normalizar o sinal de log dos fallbacks do runtime ao mesmo alcance de boot entre um binario OK e um que falha. Ver gow2-recomp/notes/2026-07-30-fase6-veredictos.md.
- [Marco v1.1, pos-Fase 8] Causa da regressao do 2o StartSeq continua NAO NOMEADA. Fase 6 eliminou 3 hipoteses do lifter + 2 da regiao do codigo (TOCFIX/strip_block); Fase 8 eliminou patches (diff+binario) e o candidato de runtime f6708cb (reversao cirurgica nao repos o boot). accept_relift.sh continua REJEITADO/rc=1, elo_stopped='2o movie (StartSeq)' -- gate nao regrediu, causa real por corrigir. Proximo passo: bissecao automatizada commit-a-commit de runtime/+libs/ na janela 8648805..4e815cf (25 Jul 17:53 -> 26 Jul 02:59), TODOS os ficheiros do intervalo. Ver ../gow2-recomp/notes/2026-07-30-fase8-reposicao-final.md
