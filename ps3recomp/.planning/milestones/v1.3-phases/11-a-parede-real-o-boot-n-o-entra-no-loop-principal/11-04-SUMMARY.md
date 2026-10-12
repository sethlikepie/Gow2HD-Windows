---
phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal
plan: 04
subsystem: re-instrumentation
tags: [ppu-lifter, boot-chain-gate, gow2, wall-4, wall-1, aceite, smoke-chain-gate]

requires:
  - phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal
    provides: "11-03: patch_outer_subtag_prune.py (fix v2 da parede 4), verificado in-boot 3/3 COM os gates 1-3 ON"
provides:
  - "Medicao honesta do criterio 3 da Fase 11 (AUTO_LOAD >=1 vez sem gates, >=4/6): resultado 0/6, via smoke_chain_gate.sh (ferramenta existente, nao reimplementada)"
  - "Descoberta medida: sem os gates, o boot aborta dentro de func_002545D4 (36 bytes dentro de func_002545B0 -- a propria parede 1 do inventario da Fase 11), antes de alcancar o walk exterior onde o fix da parede 4 actuaria"
  - "docs/re_sessions/2026-08-04-E5-aceite-parede-4.md -- ledger com a reconciliacao do aviso de lib_boot_chain_metrics.sh (AUTO_LOAD so' apos REQUEST_EXITGAME) e o mapa D-11.5"
affects: []

tech-stack:
  added: []
  patterns: ["reconciliar um aviso documentado do proprio instrumento (lib_boot_chain_metrics.sh) com um segundo sinal complementar ([STATE] frame=1) antes de interpretar um 0/6 como parede real"]

key-files:
  created:
    - docs/re_sessions/2026-08-04-E5-aceite-parede-4.md
  modified:
    - .planning/WINDOWS.md
    - .planning/REQUIREMENTS.md

key-decisions:
  - "0/6 nao foi tratado como 'jogo ainda a correr, so' nao saiu a tempo' (a leitura ingenua do aviso do lib_boot_chain_metrics.sh) porque um segundo sinal independente ([STATE] frame=1, tambem 0/6) e um abort nomeado E localizado dentro da parede 1, medido em 6/6 corridas identicas, refutam essa leitura"
  - "REG-03 e a janela #19 do WINDOWS.md ficam ABERTOS -- nenhum fecho forjado. Uma janela NOVA (#21) regista o achado especifico desta sessao"
  - "O fix da parede 4 (11-03, v2) NAO foi retestado nem reaberto -- esta sessao mede uma condicao (gates OFF) em que ele nunca chega a ser exercitado, o que e diferente de estar errado"

requirements-completed: []

coverage:
  - id: D1
    description: "Criterio 3 do ROADMAP medido com a ferramenta existente (smoke_chain_gate.sh), gates confirmados OFF, 6 corridas, resultado honesto reportado (0/6, nao 4/6 forjado)"
    requirement: "REG-03"
    verification:
      - kind: integration
        ref: "../gow2-recomp/smoke_chain_gate.sh --bin boot_gow2_f11accept 6 /tmp/f11_accept.tsv -- ver docs/re_sessions/2026-08-04-E5-aceite-parede-4.md secao Medicao"
        status: pass
    human_judgment: false
  - id: D2
    description: "Contradicao AUTO_LOAD/REQUEST_EXITGAME do lib_boot_chain_metrics.sh lida e reconciliada com evidencia adicional ([STATE] frame=1, backtrace do abort), nao silenciada"
    requirement: "REG-03"
    verification:
      - kind: integration
        ref: "grep -c '\\[STATE\\] frame=' nos 6 logs de /tmp/chain_gate_boot_gow2_f11accept_run*.log -- 0/6, mais o backtrace FATAL identico nas 6"
        status: pass
    human_judgment: false
  - id: D3
    description: "Mapa D-11.5 entregue (fase nao fechou): eliminados anteriores + achado novo (parede 1 confirmada como bloqueio activo sem gate) + proximo passo"
    verification: []
    human_judgment: true
    rationale: "A avaliacao de se o mapa e' 'tao completo quanto um fecho teria sido escrito' e um julgamento de qualidade de RE, nao uma verificacao automatizavel"

duration: 55min
completed: 2026-08-04
status: complete
---

# Phase 11 Plan 04: Medicao de aceite do criterio 3 (sem forjar fecho) Summary

**`smoke_chain_gate.sh --bin boot_gow2_f11accept 6`, gates 1-3 confirmados OFF, com o fix v2 da parede 4 aplicado: `sys_ppu_thread_create name="AUTO_LOAD"` = 0/6 e `[STATE] frame=1` = 0/6 -- as seis corridas abortam identicamente dentro de `func_002545D4`, medido como 36 bytes dentro de `func_002545B0`, a propria parede 1 do inventario da Fase 11, nunca alcancando o walk exterior onde o fix da parede 4 actuaria.**

## Performance

- **Duration:** ~55 min (build ~2min, apply_all_patches ~6min, 6 corridas de 90s = 9min, suites de nao-regressao ~1min, resto redaccao/medicao)
- **Started:** 2026-08-04T17:05:00-04:00 (aprox.)
- **Completed:** 2026-08-04T18:00:00-04:00 (aprox.)
- **Tasks:** 2/2
- **Files modified:** 3 (1 criado, 2 editados)

## Accomplishments

- Leitura obrigatoria de `lib_boot_chain_metrics.sh` (linhas 63-118) feita ANTES de correr
  qualquer medicao -- o aviso "AUTO_LOAD so' e' criada apos `func_00242C94` (loop
  principal) RETORNAR em `REQUEST_EXITGAME`" foi lido e citado no ledger antes de
  interpretar qualquer numero.
- Binario dedicado `boot_gow2_f11accept` construido a partir do lift ja verificado pelo
  E4 (`recomp_macos_e4fix2`, fix v2 aplicado), distinto de `boot_gow2` (producao,
  intocada -- hash `136ab046...` identico ao inicio e ao fim da sessao).
- Confirmado (`nm | grep -c gow2_midasm` = 9, todos gated por `getenv()`) que nenhuma
  sonda de diagnostico das sessoes E1/E3/E4 vaza efeito para esta medicao.
- 6 corridas de `smoke_chain_gate.sh --bin` (a ferramenta GATE-01/GATE-02 ja existente,
  nao reimplementada), gates confirmados unset via `env | grep -c` antes do lancamento,
  zero orfaos antes/depois (`pgrep -fl boot_gow2` vazio nos dois momentos).
- Resultado: `thr_created(AUTO_LOAD)=0` em 6/6, `elo_stopped=nenhum em 0 de 6 (limiar: 4)`.
  Sinal complementar `[STATE] frame=1` tambem 0/6 -- as duas leituras concordam.
- Localizada, por medicao directa (nao inferencia), a causa do 0/6: `func_002545D4`
  aborta com `[ppu] FATAL: stuck calling 0x00514E80 (2000 times)` nas 6 corridas,
  identico byte a byte no backtrace; `ghidra_lookup.py` confirma que `0x002545D4` esta
  **36 bytes dentro** de `func_002545B0` -- a propria parede 1 do inventario do ROADMAP
  (`PS3_LIST254_EMPTY_IF_NULL`).
- Reconciliacao honesta do aviso do `lib_boot_chain_metrics.sh`: nao foi tratado como
  "falso alarme, jogo ainda a correr" porque `[STATE] frame=1` tambem falta E ha um
  abort nomeado, localizado, ANTES do loop principal ser alcancado -- as duas provas
  em conjunto refutam a leitura ingenua.
- Mapa D-11.5 escrito no ledger: tabela do que ja estava eliminado (fases anteriores) +
  o que esta sessao acrescenta (parede 1 confirmada como bloqueio activo sem gate,
  `0x00514E80` nao e funcao valida) + proximo passo explicito (atacar a parede 1).
- `.planning/WINDOWS.md`: janela #19 (explosao so' medida com gates) **mantida aberta**
  -- nao fechada, porque a reproducao sem gates continua inatingivel (o boot nem chega
  la). Janela nova #21 registada com o achado especifico desta sessao.
- `.planning/REQUIREMENTS.md`: REG-03 mantido `[ ]`, com nota datada 2026-08-04 no
  mesmo estilo da correcao de 2026-08-01 ja existente.
- Suites de nao-regressao do `CLAUDE.md` re-executadas apos a sessao: `ctest` 23/23
  (2 skip esperados), `./scripts/test_python.sh` 14 PASS/2 SKIP/0 FAIL -- sem regressao.

## Task Commits

Each task was committed atomically:

1. **Task 1+2 (ledger da medicao E5)** - `3598084` (docs) -- leitura obrigatoria, construcao
   do binario, as 6 corridas, o veredicto, o mapa D-11.5
2. **Task 2 (WINDOWS.md + REQUIREMENTS.md)** - `81729c3` (docs) -- janela #21 nova, janela
   #19 mantida aberta, REG-03 mantido `[ ]` com nota datada

**Plan metadata:** este SUMMARY.md (commit separado a seguir, sem tocar em STATE.md/
ROADMAP.md por instrucao explicita do utilizador nesta fase)

_Nota: Task 1 (ler o aviso do `lib_boot_chain_metrics.sh` + construir o binario) nao
produziu, por si so', um diff separavel no repositorio `ps3recomp` -- o binario e o lift
vivem no checkout fisico `../gow2-recomp` (sibling repo, nao rastreado por este git). O
conteudo textual que a Task 1 exige (leitura citada, confirmacao dos hooks gated) ficou
integrado no mesmo ficheiro de ledger que a Task 2 escreve, por isso os dois commits
acima cobrem as duas tasks em conjunto._

## Files Created/Modified

- `docs/re_sessions/2026-08-04-E5-aceite-parede-4.md` - ledger completo: pergunta, leitura
  obrigatoria, ancoras, construcao do binario, tabela das 6 corridas, backtrace do abort,
  veredicto, reconciliacao do aviso AUTO_LOAD/REQUEST_EXITGAME, mapa D-11.5
- `.planning/WINDOWS.md` - janela #21 (unmet-truth, fase 11) nova; janela #19 nao tocada
  (continua aberta, correctamente)
- `.planning/REQUIREMENTS.md` - nota datada 2026-08-04 em REG-03 (continua `[ ]`)

## Decisions Made

- **Nao tratar `thr_created=0` como inconclusivo so' por causa do aviso do
  `lib_boot_chain_metrics.sh`.** O aviso e real e foi citado por inteiro no ledger, mas
  esta sessao tem DUAS provas adicionais que ele nao explica este `0/6` especifico: (a)
  `[STATE] frame=1` tambem ausente nas 6 corridas -- se o loop principal tivesse sido
  alcancado e despachado, mesmo sem sair do jogo, esse marcador dispararia; (b) as 6
  corridas abortam com um `[ppu] FATAL` nomeado e localizado dentro da propria parede 1
  (`func_002545B0`), antes de o `B71`/loop principal serem sequer alcancados. Um "jogo
  ainda a correr bem" nao produz um abort explicito e determinístico numa funcao
  conhecida como parede.
- **Nao retestar nem reabrir o fix da parede 4 (11-03/E4).** Esta sessao mede uma
  condicao (gates OFF) em que esse fix nunca chega a ser exercitado -- o boot aborta
  ANTES, na parede 1. Isso nao invalida o E4 (que verificou o fix correcto com os
  gates ON, a unica condicao em que o walk exterior e alcancado hoje); apenas confirma
  a dependencia que o proprio 11-03/E4 ja tinha avisado explicitamente para esta sessao
  medir.
- **Janela #19 do WINDOWS.md fica aberta, nao fechada.** O criterio para a fechar
  ("a explosao ja nao reproduz sem gates") nao foi satisfeito -- a explosao nem chega a
  ser alcancavel sem gates, porque o boot para antes, num sitio diferente (parede 1).

## Deviations from Plan

None - plano executado exactamente como escrito. A unica adaptacao foi de forma, nao de
conteudo: as Tasks 1 e 2 ficaram cobertas pelos mesmos dois commits porque a Task 1 nao
produz, isoladamente, um diff no repositorio `ps3recomp` (o binario/lift vivem no
`gow2-recomp` sibling, fora do tracking git desta sessao) -- documentado acima em
"Task Commits".

## Issues Encountered

- **Corrida acidental em duplicado do `apply_all_patches.sh`** durante a execucao (dois
  `Bash` calls que excederam o timeout de 120s foram promovidos a background pelo
  harness). Verificado via `ps -ef` que apenas UMA instancia do script estava de facto a
  correr sobre `recomp_macos_e4fix2` -- a segunda chamada era so' o loop de espera por
  `pgrep`, nao uma segunda escrita concorrente no mesmo lift. Sem risco de corrupcao
  (confirmado: `lift_parity PASS`, `MANIFEST PASS`, `audit_boundaries PASS`, rc=0).
- Nenhum bloqueio nao resolvido.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- **Proximo passo, por ordem (nao e o 11-05 ainda planeado -- e' a leitura honesta do
  mapa D-11.5):** a parede 1 (`func_002545B0`, gate `PS3_LIST254_EMPTY_IF_NULL`) e o
  bloqueio linear seguinte. E' a mesma classe de bug ja resolvida para a parede 4 (um
  consumidor a ler um campo de dados como ponteiro/sentinela) -- o metodo das sessoes
  E1-E4 (sonda de contagem -> decisao A/B -> fix -> verificacao in-boot obrigatoria)
  aplica-se directamente.
- Sem a parede 1 corrigida, as paredes 2 (`func_002182A4`) e 3 (`func_002547AC`) nunca
  sao sequer alcancadas sem gate, e o fix da parede 4 (ja correcto, 11-03/E4) nunca e'
  exercitado pelo caminho natural do jogo.
- `docs/re_sessions/2026-08-04-E5-aceite-parede-4.md` deixa o modelo de medicao pronto
  para reusar assim que as paredes 1-3 estiverem corrigidas: `smoke_chain_gate.sh --bin
  <binario novo> 6`, mesmos gates confirmados OFF, mesma leitura dupla (`thr_created` +
  `[STATE] frame=1`).
- Producao (`recomp_macos_v2`/`boot_gow2`) intocada; binarios/lifts dedicados desta
  sessao (`boot_gow2_f11accept`, reusando `recomp_macos_e4fix2`) ficam no checkout
  fisico `../gow2-recomp`, fora do git tracking desta sessao.
- **NAO tocado nesta sessao, por instrucao explicita do utilizador:**
  `.planning/STATE.md`, `.planning/ROADMAP.md`.

---
*Phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal*
*Plan: 04*
*Completed: 2026-08-04*

## Self-Check: PASSED

- Ficheiros: `docs/re_sessions/2026-08-04-E5-aceite-parede-4.md`,
  `.planning/WINDOWS.md`, `.planning/REQUIREMENTS.md`, este SUMMARY.md -- todos `FOUND`.
- Commits: `3598084`, `81729c3` -- ambos `FOUND` em `git log --oneline --all`.
- `/tmp/f11_accept.tsv` -- `FOUND`, 6 linhas de dados + cabecalho.
- Producao `../gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp` -- hash
  `136ab046d1a0ced07fc5bda330eba8263783e7732434bb5a8253e7db15d0a989`, identico ao
  registado pelo 11-03/E4.
