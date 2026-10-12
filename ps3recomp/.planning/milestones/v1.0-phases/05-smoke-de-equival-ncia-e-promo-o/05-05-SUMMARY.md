---
phase: 05-smoke-de-equival-ncia-e-promo-o
plan: 05
subsystem: infra
tags: [bash, ppu-lift, promotion, gow2, macos-arm64, production-final, gitignore]

requires:
  - phase: 05-04
    provides: "promote_lift.sh -- gate obrigatorio + backup por rename + rebuild + confirmacao pos-build + reversao automatica + modo --revert, provado reversivel por hash"
provides:
  - "producao real e final: recomp_macos_v2/boot_gow2 derivados de recomp_macos_v3 (suffix=20260729_150649), sem reversao a seguir -- fecha o ciclo re-lift->prova->promocao do Marco 1"
  - ".gitignore de gow2-recomp corrigido (recomp_macos_v*/, boot_gow2*) -- fecha o gap medido que tornava o criterio 4 estruturalmente inatingivel"
  - "ACCEPTANCE-REPORT.md (D-5.4) -- relatorio de aceite final da Fase 5/Marco 1, 5 limites declarados sem maquiagem"
affects: []

tech-stack:
  added: []
  patterns:
    - "Comparacao por ficheiro fonte (md5 individual dos .cpp) como prova autoritativa de 'producao deriva do candidato', em vez de confiar so no hash_dir() de diretorio inteiro -- que ja tinha instabilidade conhecida e documentada (05-04-SUMMARY.md) por causa de metadata .o/.cclog recem-gerada."
    - "Relatorio de aceite (D-5.4) como documento que declara os LIMITES do gate, nao so o que ele garante -- previne repetir o erro de 2026-07-26 ao nivel da documentacao."

key-files:
  created:
    - .planning/phases/05-smoke-de-equival-ncia-e-promo-o/ACCEPTANCE-REPORT.md
  modified:
    - ../gow2-recomp/.gitignore
    - ../gow2-recomp/PROMOTION_LOG.tsv
    - .planning/STATE.md
    - .planning/ROADMAP.md
    - .planning/REQUIREMENTS.md

key-decisions:
  - "Promocao corrida em primeiro plano com timeout explicito de 600000ms (10min) no Bash tool -- a primeira tentativa com o timeout default (2min) foi interrompida pelo proprio harness a meio da PERNA 1 do gate (smoke), ANTES de qualquer mv/cp em producao (confirmado: nenhum processo orfao, nenhuma linha nova em PROMOTION_LOG.tsv, boot_gow2 intacto). Repetida com timeout maior, terminou com rc=0 em ~9min."
  - "hash_dir() de diretorio inteiro (recomp_macos_v2 pos-promocao vs recomp_macos_v3 candidato) deu valores DIFERENTES -- reconfirma a instabilidade ja documentada no 05-04-SUMMARY.md (metadata .o/.cclog recem-gerada, nao conteudo). A prova autoritativa usada aqui foi md5 por ficheiro .cpp fonte individual (os 7 chunks), que bateram exatamente -- confirma producao == candidato por conteudo, sem depender do hash_dir() instavel."
  - "REQ-RDY0-6 marcado [DONE] mas o Marco 1 (RDY-0) NAO fecha 6/6 -- REQ-RDY0-4 (Fase 3) continua Pendente e REQ-RDY0-2 (Fase 2) continua Parcial (3/4 criterios). As 5 fases do roadmap estao executadas, mas nem todos os requisitos individuais estao fechados -- declarado sem maquiagem no ACCEPTANCE-REPORT.md, STATE.md e ROADMAP.md."

requirements-completed: [REQ-RDY0-6]

coverage:
  - id: D1
    description: ".gitignore de gow2-recomp corrigido (recomp_macos_v*/, boot_gow2*) sem remover nenhuma linha existente -- git status deixa de listar as variantes com sufixo como untracked"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: integration
        ref: "git status --porcelain | grep -E 'recomp_macos_v3|boot_gow2_v3|boot_gow2_v4|...' -> vazio, apos a edicao"
        status: pass
    human_judgment: false
  - id: D2
    description: "Promocao real e final de recomp_macos_v3 via promote_lift.sh --yes, rc=0, sem reversao a seguir"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: integration
        ref: "cd ../gow2-recomp && ./promote_lift.sh recomp_macos_v3 --yes (foreground, timeout 600000ms) -> rc=0, gate PASS (PERNA1 OK=6/6, PERNA2 rc=0, PERNA3 NO-MATCH=0/FAILED-fora-PROBE=0, contadores 13/151/0 exatos), confirmacao pos-build OK=6/6"
        status: pass
    human_judgment: false
  - id: D3
    description: "Producao (recomp_macos_v2/boot_gow2) confirmada derivada de v3 por hash md5 por-ficheiro dos 7 chunks .cpp fonte; backup do estado anterior preservado por rename"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: other
        ref: "md5 -q recomp_macos_v2/ppu_recomp_NNN.cpp == md5 -q recomp_macos_v3/ppu_recomp_NNN.cpp para os 7 chunks (000-006); ls -d recomp_macos_v2.pre_20260729_150649 boot_gow2.pre_20260729_150649 (existem); ls -d recomp_macos_v2.pre_v4 boot_gow2.pre_v4 (historico de 26/jul intacto)"
        status: pass
    human_judgment: false
  - id: D4
    description: "git status --porcelain limpo no escopo desta fase em ps3recomp e gow2-recomp -- so a divida pre-existente de outra sessao remanesce, listada e intocada"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: other
        ref: "git status --porcelain nos dois repos apos os commits desta task -- ver secao 'git status final' do SUMMARY"
        status: pass
    human_judgment: false
  - id: D5
    description: "ACCEPTANCE-REPORT.md (D-5.4) escrito com os 5 criterios do ROADMAP, numeros reais medidos por esta fase, e os 5 limites declarados sem maquiagem (incluindo o smoke nao cobrir WADLD/shaders)"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: manual_procedural
        ref: ".planning/phases/05-smoke-de-equival-ncia-e-promo-o/ACCEPTANCE-REPORT.md -- 6 secoes, citando ppu_loader.cpp:780-784 (Gate A RED) e ROADMAP.md:26"
        status: pass
    human_judgment: false
  - id: D6
    description: "Sem regressao do motor: ctest --test-dir build-tests continua 22/22 (2 py skipped)"
    verification:
      - kind: integration
        ref: "ctest --test-dir build-tests"
        status: pass
    human_judgment: false

duration: ~35min
completed: 2026-07-29
status: complete
---

# Phase 5 Plan 05: Promoção real e final de recomp_macos_v3 + ACCEPTANCE-REPORT.md (D-5.4) Summary

**`recomp_macos_v3` é agora produção real e permanente (`rc=0`, suffix `20260729_150649`, sem reversão): o `.gitignore` de `gow2-recomp` foi corrigido para cobrir as variantes com sufixo que a Fase 3 introduziu (sem isso o critério 4 era estruturalmente inatingível), e o `ACCEPTANCE-REPORT.md` fecha o Marco 1 (RDY-0) declarando os 5 limites sem maquiagem -- incluindo o descoberto nesta sessão: o smoke não cobre WADLD nem o caminho de shaders, exatamente onde a perda de 36 conversões OPD se escondeu em 2026-07-26.**

## Performance

- **Duration:** ~35min (dominado pela corrida real de `promote_lift.sh`, ~9min, mais uma tentativa anterior interrompida pelo timeout default do harness aos ~2min, sem dano à produção)
- **Completed:** 2026-07-29
- **Tasks:** 2/2
- **Files modified/created:** 6 (`../gow2-recomp/.gitignore`, `../gow2-recomp/PROMOTION_LOG.tsv`, `ACCEPTANCE-REPORT.md`, `.planning/STATE.md`, `.planning/ROADMAP.md`, `.planning/REQUIREMENTS.md`)

## Accomplishments

- **`.gitignore` de `gow2-recomp` corrigido** (Task 1): acrescentadas duas linhas comentadas (`recomp_macos_v*/`, `boot_gow2*`), sem remover nenhuma linha existente. Confirmado por medição: `recomp_macos_v3`, `boot_gow2_v3`, `boot_gow2_v3fix`, `boot_gow2_v4`, `boot_gow2.pre_v3`, `boot_gow2.pre_v4`, `boot_gow2.wip`, `boot_gow2_O1`, `boot_gow2_pre`, `boot_gow2_relift_test` deixaram de aparecer como `??` no `git status --porcelain`.
- **Promoção REAL e FINAL de `recomp_macos_v3`** executada via `./promote_lift.sh recomp_macos_v3 --yes` (foreground, sem background nem espera passiva -- lição direta do incidente do Plano 05-04): gate de três pernas PASS, backup por rename, rebuild, confirmação pós-build 6/6 OK, `rc=0`. **Sem reversão a seguir** -- esta é a promoção definitiva do marco.
- **Produção confirmada derivada de `v3`**: os 7 chunks `.cpp` de `recomp_macos_v2` (produção, pós-promoção) são byte-idênticos aos 7 chunks de `recomp_macos_v3` (candidato), confirmado por `md5` individual de cada ficheiro -- não uma inferência do `rc=0`.
- **Backup do estado anterior preservado**: `recomp_macos_v2.pre_20260729_150649/` e `boot_gow2.pre_20260729_150649` existem (não apagados); o histórico da promoção manual de 26/jul continua intacto em `recomp_macos_v2.pre_v4/`/`boot_gow2.pre_v4`.
- **`git status` verificado com o escopo correto** nos dois repositórios: `ps3recomp` limpo exceto artefactos de teste pré-existentes (`build-tests/`, `runtime/spu/tests/out_*`); `gow2-recomp` só com a dívida já medida de outra sessão (3 `M` + `notes/*.md`/`config/`/`.DS_Store`/`recomp_mid_v2/__pycache__`/PNGs de textura/`spu_lifted/*`) -- nenhum ficheiro desta fase ficou por commitar.
- **`ACCEPTANCE-REPORT.md` (D-5.4) escrito** (Task 2): os 5 critérios do ROADMAP citados com números reais desta fase (não da sessão de planeamento), os 5 limites declarados sem maquiagem (`R_PermA` fora do aceite, present não coberto, Gate A RED esperado citando `ppu_loader.cpp:780-784`, smoke não cobre WADLD/shaders, 42/64 patches `FUNCIONAL` sem contrato), e o que o aceite GARANTE hoje (as três pernas provadas a rejeitar, uma a uma, nos Planos 05-02/05-03).
- **`ctest --test-dir build-tests`**: 22/22 (2 py skipped) -- sem regressão do motor.

## A promoção real (Task 1)

Comando: `cd ../gow2-recomp && ./promote_lift.sh recomp_macos_v3 --yes` (foreground, timeout de 600000ms no harness).

**Nota honesta sobre a primeira tentativa:** a primeira invocação (com o timeout default de 2min do harness Bash) foi interrompida pelo próprio harness (`SIGTERM`, exit 143) durante a PERNA 1 do gate (smoke, 3 de 6 corridas feitas) -- **antes de qualquer `mv`/`cp` em produção** (o gate corre inteiro antes do backup por rename). Confirmado sem dano: `pgrep -fl boot_gow2` vazio, `boot_gow2` de produção intacto (120635496 bytes, mtime 26/jul), `PROMOTION_LOG.tsv` sem linha nova. Repetida com timeout de 10 minutos, terminou com sucesso.

**Corrida bem-sucedida** (2026-07-29T19:00:46Z -> 19:09:47Z, ~9min):

```
PERNA 1 (smoke)          PASS   (OK=6/6, st620_max=11 em todas)
PERNA 2 (verify_lift)    PASS   (rc=0 -- lift_parity/MANIFEST/audit_boundaries todos PASS)
PERNA 3 (apply_patches)  PASS   (NO-MATCH=0 FAILED-fora-PROBE=0, UNVERIFIED=62 informativo)
CONTADORES (D-5.2)       PASS   (imp_modules=13 imp_imports=151 orfaos=0, delta=0 nos dois lados)

GATE PASSOU -- prosseguindo com a promocao
hash ANTES: recomp_macos_v2=7bf905232c20e9ab705c1321482f4f6c boot_gow2=346aa5ae178225838f07e58638fed878
backup por rename (suffix=20260729_150649)
copia do candidato -> recomp_macos_v2
rebuild: dur=19s objs=7 errors=0
confirmacao pos-build (smoke_relift_equiv.sh --bin, 6 corridas): OK=6/6, st620_max=11 em todas
PROMOCAO CONCLUIDA
```

`PROMOTION_LOG.tsv`: `PROMOTE-BEGIN` (19:06:49Z) -> `PROMOTE-OK` (19:09:47Z), ambos com o mesmo hash de origem -- distinta da linha `PROMOTE`+`REVERT` da rehearsal do Plano 05-04 (suffix diferente, timestamps diferentes).

## Confirmação de que produção deriva de v3 (prova por ficheiro, não por hash_dir)

Uma primeira tentativa de confirmar por `hash_dir()` (a mesma função copiada literalmente do `promote_lift.sh`, aplicada manualmente a `recomp_macos_v2` pós-promoção vs `recomp_macos_v3` candidato) deu valores **diferentes**:

```
hash_dir(recomp_macos_v2 pos-promocao) = b7449154acaf632b1809ad5b1e92f022
hash_dir(recomp_macos_v3 candidato)    = 4cb6a5c2bed6340562b617ae2dc6b6c3
```

Isto reconfirma a instabilidade de `hash_dir()`/`tar` já documentada no `05-04-SUMMARY.md` (metadata do macOS em ficheiros `.o`/`.cclog` recém-gerados pelo rebuild, capturada pelo `tar`) -- **não é** conteúdo divergente. A prova autoritativa usada aqui foi `md5 -q` de cada um dos 7 ficheiros-fonte `.cpp`:

```
recomp_macos_v2/ppu_recomp_000.cpp  1257ef0d9267e4f6daa443961a88e222
recomp_macos_v3/ppu_recomp_000.cpp  1257ef0d9267e4f6daa443961a88e222   <- identico
... (7/7 pares identicos, ppu_recomp_000 a ppu_recomp_006)
```

Confirma sem ambiguidade que `recomp_macos_v2` (produção) é hoje o conteúdo-fonte de `recomp_macos_v3`.

## git status final (escopo verificado)

**`ps3recomp` (raiz do monorepo):**
```
 M .planning/REQUIREMENTS.md .planning/ROADMAP.md .planning/STATE.md   <- desta task, a commitar
 M runtime/spu/tests/out_*/spu_recomp.{c,h}   (22 ficheiros)            <- alheio, intocado
?? build-tests/                                                        <- alheio, intocado
?? runtime/spu/tests/out_spu_channel_blocking/                         <- alheio, intocado
```

**`gow2-recomp`** (após o commit `721f85a` da Task 1):
```
 M notes/2026-07-23-gate-a-verdict.md            <- outra sessao, intocado (CLAUDE.md regra 7)
 M recomp_mid_v2/gow2_spu_register.c             <- outra sessao, intocado
 M recomp_mid_v2/patch_postthr_pc_probe.py       <- outra sessao, intocado
?? .DS_Store
?? config/
?? notes/2026-07-22-*.md (10 ficheiros), notes/2026-07-23-*.md (6 ficheiros)  <- outra sessao
?? recomp_mid_v2/__pycache__/
?? recomp_mid_v2/wad_tex__00ccomicsmoke_pal_c.png, wad_tex__010decorchest01_gol.png
?? spu_lifted/.DS_Store spu_lifted/spu4_v2/ spu_lifted/spu5_v2/
```

Todos os itens de `gow2-recomp` listados acima já existiam **antes** desta task começar (confirmado pelo primeiro `git status --porcelain` corrido nesta sessão, antes de qualquer edição) -- nenhum é tocado nem commitado, conforme CLAUDE.md regra 7. Nenhum ficheiro desta fase (`.gitignore`, `PROMOTION_LOG.tsv`, `promote_lift.sh`, `accept_relift.sh`, `smoke_relift_equiv.sh`, `manifest_delta_gate.py`, `MANIFEST_DEBT_BASELINE.json`, `COUNTERS_BASELINE.tsv`, `ACCEPTANCE-REPORT.md`) aparece como não commitado.

## Task Commits

1. **Task 1: corrigir .gitignore, executar a promoção real e final, verificar git status escopado** - `721f85a` (fix, `gow2-recomp`)
2. **Task 2: escrever ACCEPTANCE-REPORT.md (D-5.4)** - `1278fb8` (docs, `ps3recomp`)

**Plan metadata:** commit deste SUMMARY + `STATE.md`/`ROADMAP.md`/`REQUIREMENTS.md` (a seguir).

## Files Created/Modified

- `../gow2-recomp/.gitignore` -- duas linhas novas comentadas (`recomp_macos_v*/`, `boot_gow2*`), nenhuma linha existente removida.
- `../gow2-recomp/PROMOTION_LOG.tsv` -- nova linha `PROMOTE-BEGIN`/`PROMOTE-OK` (suffix `20260729_150649`), a promoção real e final.
- `.planning/phases/05-smoke-de-equival-ncia-e-promo-o/ACCEPTANCE-REPORT.md` -- relatório de aceite completo (D-5.4), 6 secções, 5 critérios do ROADMAP com números reais, 5 limites declarados sem maquiagem.
- `.planning/STATE.md` -- posição atualizada (Fase 5/Marco 1 concluídos ao nível de fases, nota honesta sobre requisitos individuais pendentes).
- `.planning/ROADMAP.md` -- checkbox 05-05 marcado, secção "Fase fechada" acrescentada, tabela de progresso atualizada (5/5, Complete).
- `.planning/REQUIREMENTS.md` -- `REQ-RDY0-6` marcado `[DONE]`, secção "Concluído (05-05-PLAN.md)" acrescentada, tabela de rastreabilidade atualizada.

## Decisions Made

- **Timeout do harness Bash aumentado para 600000ms** na segunda tentativa da promoção real -- a primeira, com o timeout default (2min), foi interrompida a meio da PERNA 1 do gate pelo próprio harness (não por nenhum problema no script ou em produção). Confirmado sem dano antes de repetir.
- **`hash_dir()` não usado como prova de "produção deriva de v3"** -- por causa da instabilidade já documentada (metadata `.o`/`.cclog`), a prova usada foi `md5` por ficheiro `.cpp` fonte individual, que é imune a essa flutuação e confirma conteúdo, não metadata de diretório.
- **`REQ-RDY0-6` marcado `[DONE]`, mas o Marco 1 NÃO é declarado 100% fechado** -- `REQ-RDY0-4` (Fase 3, gate de baseline verde) continua `Pendente` e `REQ-RDY0-2` (Fase 2) continua `Parcial` (3/4 critérios). As 5 fases do ROADMAP estão executadas e a Fase 5 está `Complete`, mas isso não equivale a 6/6 requisitos do marco fechados -- declarado sem maquiagem em `STATE.md`, `ROADMAP.md` e `ACCEPTANCE-REPORT.md`.

## Deviations from Plan

### Auto-fixed Issues

Nenhuma. O plano foi executado literalmente -- a única ocorrência fora do previsto foi a interrupção da primeira tentativa de `promote_lift.sh` pelo timeout default do harness Bash (2min), que não é um bug do plano nem do script: repetida com um timeout maior, terminou com sucesso sem qualquer intervenção manual em produção.

---

**Total deviations:** 0 auto-fixed
**Impact on plan:** Nenhum -- a interrupção pelo timeout do harness não tocou em produção (confirmado antes de repetir) e não exigiu nenhuma correção de código.

## Issues Encountered

- Ver "A promoção real (Task 1)" acima -- a única ocorrência foi a interrupção do harness na primeira tentativa, sem impacto em produção.
- `gsd-tools query state.advance-plan` e `requirements.mark-complete` falharam por incompatibilidade de formato (este projeto mantém `STATE.md`/`REQUIREMENTS.md` em prosa narrativa, não no template estrito que essas ferramentas esperam) -- as atualizações foram feitas manualmente via `Edit`, seguindo o estilo já estabelecido pelos Planos 05-01 a 05-04.

## User Setup Required

None - nenhuma configuração de serviço externo necessária.

## Next Phase Readiness

- **Fase 5 e Marco 1 (RDY-0) fechados ao nível de fases** (5/5 fases do ROADMAP executadas e com critérios de sucesso satisfeitos literalmente, ver `ACCEPTANCE-REPORT.md`).
- `recomp_macos_v3` é agora a produção real e permanente; qualquer re-lift futuro reutiliza `promote_lift.sh`/`accept_relift.sh` sem nenhuma modificação.
- **Nota honesta sobre o Marco 1 (não fechado 6/6 requisitos):** `REQ-RDY0-4` (Fase 3, gate de baseline verde sobre o lift novo) continua `Pendente`; `REQ-RDY0-2` (Fase 2, código host órfão) continua `Parcial` (3/4 critérios -- o guard dá SKIP e nenhum `host_gow2_*.o` entra num `boot_gow2` real, medido em 2026-07-26). Nenhum dos dois foi tocado por esta fase -- ficam como dívida explícita para quem fechar o Marco 1 por completo, ou para o próximo marco (Marco 9 -- GoW2 até o menu) decidir se precisa deles antes de prosseguir.
- **Dívida conhecida que continua aberta** (não fechada por esta fase, ver `ACCEPTANCE-REPORT.md` secção "Dívida conhecida"): 42/64 patches `FUNCIONAL` sem contrato; 27 `RED` do `lift_parity`; déficit de 36 do grupo `opd-dispatch`; smoke não cobre WADLD/shaders; instabilidade de `hash_dir()`.
- `ctest --test-dir build-tests`: 22/22 (2 py skipped) -- sem regressão do motor.

## Self-Check: PASSED

- FOUND: `../gow2-recomp/.gitignore` (com as duas linhas novas)
- FOUND: `.planning/phases/05-smoke-de-equival-ncia-e-promo-o/ACCEPTANCE-REPORT.md`
- FOUND (gow2-recomp): `721f85a`
- FOUND (ps3recomp): `1278fb8`
- FOUND: `../gow2-recomp/recomp_macos_v2.pre_20260729_150649`, `../gow2-recomp/boot_gow2.pre_20260729_150649`
- FOUND: `../gow2-recomp/recomp_macos_v2.pre_v4`, `../gow2-recomp/boot_gow2.pre_v4` (histórico de 26/jul, intocado)

---
*Phase: 05-smoke-de-equival-ncia-e-promo-o*
*Completed: 2026-07-29*
