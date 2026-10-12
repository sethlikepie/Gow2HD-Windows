---
phase: 09-bissect-commit-a-commit-da-janela
plan: 01
subsystem: build-tooling
tags: [git-bisect, ppu_lifter, macos-arm64, gow2, regression-hunting]

requires:
  - phase: 08-a-pista-que-sobra
    provides: "funil de 9 candidatos eliminados; relink barato provado (PS3_ENGINE_ROOT); janela 8648805..4e815cf proposta"
provides:
  - "bisect_verdict.sh: oraculo de 3 niveis (HERDA/RELINK/RECONSTRUCAO-COMPLETA) compativel com git bisect run (0/1/125)"
  - "confirmacao medida (nao presumida) de nao-determinismo raro (~9%) em boot_gow2.pre_v3, qualitativamente distinto da regressao real da producao"
  - "descoberta de que 8648805 (o boundary GOOD) esta numa ramificacao paralela (fase0/fase1/bringup) que so' converge com o porte GoW2/macOS dentro da propria janela"
  - "auditoria de custo real: 197 e 41 (--first-parent) commits classificados; RELINK nunca e' valido nesta janela (lifter sempre difere do de referencia); custo real de RECONSTRUCAO-COMPLETA ~4-8min, nao 30-40min"
affects: [09-02, futuros bisects commit-a-commit no ps3recomp]

tech-stack:
  added: []
  patterns:
    - "oraculo de bisect com 3 niveis de custo, escalando automaticamente RELINK->RECONSTRUCAO-COMPLETA quando o lifter do commit sob teste difere do lifter de referencia (evita o confundimento medido: relink de runtime antigo + lift de hoje da veredicto errado)"
    - "RECONSTRUCAO-COMPLETA = relift proprio do commit + apply_all_patches.sh de HOJE mantido constante (a pista dos patches ja' foi refutada pelo Plano 08-02) -- isola o ENGINE (runtime+lifter) como a unica variavel sob teste"
    - "deteccao de incompatibilidade de TLS (__declspec(thread) incondicional em lifters anteriores a 145fe58) como skip (125), nunca bad -- nao recuperavel por flags de compilador"

key-files:
  created:
    - "../gow2-recomp/bisect_verdict.sh"
    - "../gow2-recomp/notes/2026-07-31-fase9-fronteiras-oraculo.md"
    - "../gow2-recomp/notes/2026-07-31-fase9-auditoria-custo-janela.md"
  modified: []

key-decisions:
  - "RELINK so' e' usado quando tools/ppu_lifter.py do commit sob teste e' byte-a-byte identico ao lifter de referencia -- senao escala para RECONSTRUCAO-COMPLETA (medido: nesta janela, isso e' SEMPRE o caso -- RELINK nunca se aplica na pratica)."
  - "Minimo 3 corridas por passo do bisect, veredicto por maioria -- nunca RUNS=1 (correcao do 09-CONTEXT.md, re-confirmada por medicao directa nesta tarefa: o mesmo binario pre_v3 deu 2 OK + 1 REGRESSAO em 3 corridas)."
  - "8648805 (o axioma GOOD) nunca e' re-testado directamente pelo oraculo -- fica provado inconstruivel por ambos os caminhos (RELINK confunde-se com o lift de hoje; RECONSTRUCAO-COMPLETA nao linka por __declspec(thread) incondicional). Isto nao invalida o bisect porque a maioria dos commits da sua ramificacao classificam HERDA."

requirements-completed: []  # REG-02 (causa nomeada e confirmada) NAO fica completo aqui -- 09-01 so' entrega o oraculo/auditoria; a corrida real do bisect e a nomeacao/confirmacao da causa sao objectivo do Plano 09-02.

duration: ~3h40min
completed: 2026-07-31
status: complete
---

# Phase 9 Plan 01: Fronteiras do bisect + oraculo + auditoria de custo Summary

**Construi e provei o oraculo `bisect_verdict.sh` (3 niveis, escalonamento automatico
RELINK->RECONSTRUCAO-COMPLETA) contra a janela real, resolvi por medicao directa a
suspeita de nao-determinismo levantada no planeamento, e descobri que o boundary GOOD
(`8648805`) e' fisicamente inconstruivel por qualquer pipeline macOS -- pertence a uma
ramificacao paralela (fase0/fase1/bringup) que so' converge com o porte GoW2/macOS dentro
da propria janela.**

## Performance

- **Duration:** ~3h40min
- **Tasks:** 3/3 completas
- **Files modified:** 3 (todos novos, em `gow2-recomp/`)

## Accomplishments

- **Determinismo resolvido por medicao, nao suposicao**: 6 corridas adicionais de
  `boot_gow2.pre_v3` hoje (5 OK, 1 REGRESSAO) confirmam nao-determinismo real mas RARO
  (~9% combinando com as 5 corridas da vespera), concentrado numa corrida-contra-o-relogio
  no FIM da cadeia (thr_auto_load dispara ~12 linhas antes do timeout nas corridas boas).
  A producao continua qualitativamente diferente: presa muito mais cedo (StartSeq=1, nunca
  chega a 2), reproduzida 6/6 combinando as duas sessoes -- a hipotese "existe um commit"
  continua valida, mas exige >=3 corridas por passo (nunca 1), exactamente a correcao ja
  feita no `09-CONTEXT.md`.
- **`bisect_verdict.sh` escrito e provado de ponta a ponta**: classificacao por diff contra
  o primeiro pai (HERDA memorizado / RELINK / RECONSTRUCAO-COMPLETA), guarda anti-producao
  (aborta fora de `/tmp/ps3recomp_bisect09_*`), deteccao de incompatibilidade de TLS como
  skip. Corrido de ponta a ponta contra um commit real da janela (`e441f37`): classifica,
  cria worktree, builda (relift + patches de hoje + compila + linka), mede 3 corridas,
  devolve `bad` (exit 1) -- correcto.
- **Achado nao previsto**: `8648805` (o boundary GOOD escolhido pela Fase 8) esta' numa
  ramificacao de reliability/testes (fase0/fase1/bringup) que diverge do porte GoW2/macOS
  desde um ancestral muito anterior (`664a4c7`, 24 Jul 09:47) -- so' se reencontram dentro
  da propria janela via um dos 6 merges. Nenhum dos dois caminhos de build (RELINK contra o
  lift de hoje; RECONSTRUCAO-COMPLETA com o proprio lifter de `8648805`) produz um binario
  medivel: RELINK confunde-se com todas as correcoes do lifter que aconteceram DEPOIS na
  janela (deu REGRESSAO 3/3 mesmo sendo o lado GOOD); RECONSTRUCAO-COMPLETA nao linka em
  macOS/clang -- `__declspec(thread)` incondicional para `g_trampoline_fn`, incompatibilidade
  REAL de modelo TLS, nao recuperavel por `-fms-extensions -fdeclspec`. Isto NAO invalida o
  bisect (a maioria dos commits dessa ramificacao classifica HERDA e herda o veredicto sem
  construir), mas explica porque `8648805` so' pode ser usado como axioma (`git bisect good`),
  nunca re-testado directamente.
- **Auditoria de custo real**: classificacao dos 197 commits (76 HERDA / 8 RELINK / 113
  RECONSTRUCAO-COMPLETA) e dos 41 alcancaveis por `--first-parent` (20 HERDA / 5 RELINK /
  16 RECONSTRUCAO-COMPLETA). Achado que corrige o `09-CONTEXT.md`: mesmo o ultimo commit da
  janela (`4e815cf`) tem um lifter diferente do HEAD de hoje (104 linhas de diff) -- logo o
  nivel RELINK **nunca se aplica na pratica** nesta janela especifica (escala sempre para
  RECONSTRUCAO-COMPLETA). Custo medido de RECONSTRUCAO-COMPLETA: ~4-8min por commit (nao os
  "30-40min" do contexto -- os 7 chunks compilam em paralelo, `-P 6`, em ~20-25s no total).
  Estimativa entregue ao Plano 09-02: ~15-45min no caso tipico, 1-2h no caso adverso.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `bisect_verdict.sh` v1 nao aplicava patches apos relift**
- **Found during:** Tarefa 2, primeira prova de ponta a ponta contra `e441f37`
- **Issue:** `RECONSTRUCAO-COMPLETA` chamava so' `RELIFT=1 ... build_macos.sh`, sem
  `apply_all_patches.sh` -- um lift limpo sem patches fica preso na intro (`st620=0`)
  imediatamente, mascarando qualquer sinal real sobre o commit sob teste.
- **Fix:** separado em duas etapas -- `python3 tools/ppu_lifter.py` (so' gera o lift) +
  `apply_all_patches.sh` (patches de hoje, mantidos constantes) -- depois `build_macos.sh`
  sem `RELIFT` (usa o lift ja' gerado e patcheado).
- **Files modified:** `../gow2-recomp/bisect_verdict.sh`
- **Commit:** 1acdc92 (ja' incluido na versao final do script)

**2. [Rule 1 - Bug] path absoluto duplicado ao chamar `apply_all_patches.sh`**
- **Found during:** Tarefa 2, mesma prova
- **Issue:** `apply_all_patches.sh` sempre resolve o argumento como `$REPO/<arg>` (relativo
  a `gow2-recomp`) -- passar um caminho absoluto duplicava o prefixo
  (`gow2-recomp//Users/.../gow2-recomp/...`), falhando com "dir de lift nao existe".
- **Fix:** o lift fresco agora vive DENTRO de `gow2-recomp/` (nao em `/tmp/`), e so' o
  nome-base e' passado ao `apply_all_patches.sh`.
- **Files modified:** `../gow2-recomp/bisect_verdict.sh`
- **Commit:** 1acdc92

## Known Stubs

Nenhum -- os dois achados de "incompatibilidade real" (TLS `__declspec(thread)` para
commits pre-`145fe58`) sao limitacoes DOCUMENTADAS do oraculo, tratadas como `skip` (125),
nunca mascaradas como sucesso.

## Self-Check: PASSED

- `../gow2-recomp/bisect_verdict.sh` -- existe, executavel, commitado (1acdc92).
- `../gow2-recomp/notes/2026-07-31-fase9-fronteiras-oraculo.md` -- existe, commitado (a59d285, actualizado em 1acdc92).
- `../gow2-recomp/notes/2026-07-31-fase9-auditoria-custo-janela.md` -- existe, commitado (0f3c164).
- `/tmp/classify197.tsv`, `/tmp/classify41.tsv` -- evidencia da auditoria de custo.
- `/tmp/fase9_determinismo.tsv`, `/tmp/fase9_determinismo_2.tsv`, `/tmp/fase9_producao_confirma.tsv` -- evidencia de determinismo.
- Worktrees de teste (`/tmp/ps3recomp_bisect09_good`, `/tmp/ps3recomp_bisect09_bad`) removidos -- confirmado por `git worktree list` sem entradas residuais.
