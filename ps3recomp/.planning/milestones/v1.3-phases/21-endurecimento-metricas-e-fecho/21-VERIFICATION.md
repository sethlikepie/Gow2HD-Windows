---
phase: 21-endurecimento-metricas-e-fecho
verified: 2026-08-03T03:01:29Z
status: human_needed
score: 8/8 must-haves verified (0 falhados, 1 item de julgamento humano)
behavior_unverified: 0
overrides_applied: 0
human_verification:
  - test: "Ler games/gow2/notes/milestone-2026-08-xenon-re-results.md do início ao fim, como um auditor apressado faria, e decidir se a frase 'marco v1.3 fecha INTEIRO nos requisitos' é lida corretamente como '11/11 requisitos e critérios medidos' e NÃO como 'o lift está limpo' ou 'o mid-asm está em produção'."
    expected: "A formulação explícita 'Inteiro refere-se aos requisitos e aos critérios de fecho, medidos. Não significa o lift está limpo nem o mecanismo está no caminho do jogador' só existe em 21-03-SUMMARY.md:126-128 (artefato interno de planeamento). O results.md (o documento que REQUIREMENTS.md aponta como fonte) tem os factos (4 linhas em 78, ressalva 1 = mid-asm fora de produção) mas não tem essa frase-síntese exata. STATE.md tem uma síntese equivalente e prominente ('O marco não limpou o corpus: entregou o mecanismo que permite limpá-lo, provado em 4 linhas reais'). Decidir se isto é suficiente ou se vale a pena copiar a frase do SUMMARY para o cabeçalho do results.md."
    why_human: "É um julgamento de clareza de comunicação/risco de má-leitura, não um facto binário verificável por grep. Os números subjacentes estão todos corretos e presentes; a questão é só se a síntese está no lugar certo para quem lê rápido."
---

# Phase 21: Endurecimento, métricas e fecho — Verification Report

**Phase Goal:** O que o marco v1.3 construiu passa a estar no caminho de aceitação de um
re-lift, e a melhoria é um número medido antes e depois — não uma afirmação.
**Verified:** 2026-08-03T03:01:29Z
**Status:** human_needed (todos os factos verificados batem; 1 item de julgamento humano sobre visibilidade da ressalva central)
**Requirement:** LEDG-02

## Resumo executivo

Corri eu próprio, de forma independente do que o SUMMARY afirma, todos os pontos pedidos:
li o código-fonte (`accept_relift.sh`, `RELIFT_CANONICAL.md`), corri 3 testes da suite T1–T10
(`test_midasm_emit.py`, `test_oracle_manifest.py`, `test_baseline_delta.py` de
`lift_baseline/`), recontei `PATCH_MIGRATION.tsv` com `awk`, e recontei a trajetória
78→76→74→72 lendo os 4 commits do ledger com `git show`. **Todos os números batem
exatamente com o que o SUMMARY e o results doc afirmam.** Não encontrei nenhum número
forjado, nenhuma linha do ledger inventada, nenhuma linha `rc` presumida.

A única questão que fica em aberto é de comunicação, não de facto: a formulação mais
precisa da ressalva central ("fecha inteiro nos requisitos, não no lift limpo") vive no
`21-03-SUMMARY.md` (artefato interno), não literalmente no `results.md` que
`REQUIREMENTS.md` aponta como "a lista completa". Ver secção final.

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidência |
|---|-------|--------|-----------|
| 1 | `docs/RELIFT_CANONICAL.md` existe com as 8 etapas, comandos, critério por etapa e nível de evidência | ✓ VERIFIED | `rg -c '^### Etapa '` → **8** (recontado). §3 "Realidades de produção" documenta `emit_weak_wrappers=true` (`:24`), a armadilha do `switch_table_file` que mata `--config` inteiro (medida no 17-03, confirmada na 18), e `VERIFY_ORACLE=1` como opt-in (§5, linha 161) |
| 2 | `accept_relift.sh` invoca `verify_lift.sh`, nas linhas citadas, e isto já existia antes do marco | ✓ VERIFIED | Linhas confirmadas por leitura direta: `:73` (`VERIFY_LIFT=...`), `:74-77` (guarda `exit 2`), `:129-130` (invocação + `leg2_rc`), `:262` (no AND final). `bash -n` limpo, `rc=0`. Atribuição honesta confirmada: `git log -p` mostra a linha `VERIFY_LIFT=` introduzida no commit `757a40b` ("feat(05-03): mirror accept_relift.sh"), que é da **Fase 5 do marco v1.0** — muito antes de qualquer fase do v1.3 (16–21). O documento e o results.md dizem-no explicitamente ("pré-existente, verificado e atribuído como tal") |
| 3 | A suite T1–T10 tem `rc` real por linha, sem presumidos | ✓ VERIFIED | `results.md` tem 12 linhas (T1–T10 + T6b + T8b), todas com `rc` explícito, 0 `não-exercitado`. Corri eu próprio 3 amostras independentes: `test_midasm_emit.py` → `rc=0` (bate); `test_oracle_manifest.py` (bónus citado) → `rc=0`, `10/10 [PASS]` (bate); `test_baseline_delta.py` de `lift_baseline/` → `rc=0`, `5/5 [PASS]` (bate). Nenhuma discrepância |
| 4 | Tabela antes/depois com números medidos nas duas pontas | ✓ VERIFIED | `CORRECTNESS` ativos: recontei hoje com `awk` sobre `PATCH_MIGRATION.tsv` atual → **72** (bate). Reconfirmei o "antes" por `git show ee17e2c:...` → **78** (bate). Fui além do pedido e recontei a trajetória inteira lendo os 4 commits citados: `ee17e2c`→78/0/0, `b6f0c79`→76/2/0, `94f25ef`→74/2/2, `304610b`→72/4/2 — **78→76→74→72 confirmado byte a byte**, rastreável por commit |
| 5 | O results doc contém as 10 ressalvas do marco, incluindo as 4 citadas explicitamente | ✓ VERIFIED | `grep -rl 'gow2_midasm' ../gow2-recomp/recomp_macos_v2/` corrido por mim → **rc=1, zero ficheiros** (confirma ressalva 1). Seção "Ressalvas do marco" no `results.md` tem exatamente 10 itens numerados, na ordem de importância declarada. Ressalva 2 (sequência 1→8 nunca correu de ponta a ponta) presente. `sticky_pub>=1` não-verificável presente (ressalva 5, com os 3 `ps3_fios_sticky_*` sem `fprintf` confirmados por `grep`). `bytes_read` não-exercitado presente (ressalva 6). Os 2 jumptables `redundant` presentes (ressalva 3). `RE-03` metade soft presente (ressalva 4) |
| 6 | Handoff da wall D existe, com a divergência de indexação e a lista FORA | ✓ VERIFIED | Secção "Handoff / A." documenta a divergência `(w0>>16)&0xFFF` (escrita) vs `w0 & 0xFFFF` (12 de 18 consumidores), com as duas previsões opostas do próximo experimento (`PS3_TYPEWALK_TRACE=1`). Secção "C. Explicitamente FORA" lista opts ABI, SIMDe, shader recomp, promoção spu2/3, reconciliação do subtree — 5 itens, cada um com justificação |
| 7 | `REQUIREMENTS.md`/`STATE.md` refletem o fecho; `ROADMAP.md` ficou intocado por decisão, pendência em `WINDOWS.md` | ✓ VERIFIED | `REQUIREMENTS.md` tem a linha `LEDG-02 — Completo` com os 4 sub-itens (a–d) e a tabela de cobertura "11/11". `STATE.md` tem a secção "Fecho medido (2026-08-03, plano 21-03)" com os 4 critérios de fecho numéricos e as 2 ressalvas principais. `ROADMAP.md`: `git status` confirma **modificado no working tree** por outra sessão (diff mostra só `Plans:`/checkboxes/tabela Progress, nenhuma marca `Complete`) — consistente com "intocado por decisão". `WINDOWS.md` linha `id=17` (fase 21, `deviation`, `.planning/ROADMAP.md`, `open`) documenta exatamente esta pendência, com o que fica por aplicar quando a outra sessão libertar o ficheiro |
| 8 | Produção intacta — `recomp_macos_v2`/`boot_gow2` não tocados nem promovidos | ✓ VERIFIED | `git log --name-only 58481ec..0798bf7` (todos os commits da Fase 21) → **zero arquivos fora de** `.planning/`, `docs/`, `games/gow2/README.md`, `games/gow2/notes/`. Nenhum arquivo em `runtime/`, `libs/`, `games/gow2/hooks/`, `games/gow2/config/` tocado. `LIFT_PROD_SHA256` do lift de produção confirmado idêntico ao registado na Fase 16 (citado no results.md, não re-hasheado por mim mas consistente com mtime `31 jul` do `.cpp` de produção, anterior a toda a Fase 21) |

**Score:** 8/8 truths verificadas (0 falhas). 1 item de julgamento humano (não conta como falha nem como verificado — é uma questão de clareza editorial, ver abaixo).

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `docs/RELIFT_CANONICAL.md` | 8 etapas do re-lift, comandos, critérios, evidência | ✓ VERIFIED | 15823 bytes, 8 `### Etapa` confirmados |
| `games/gow2/accept_relift.sh` (perna 2) | Invoca `verify_lift.sh` | ✓ VERIFIED | Linhas 73/74-77/129-130/262 exatas, `bash -n` limpo |
| `games/gow2/notes/milestone-2026-08-xenon-re-results.md` | Suite T1–T10 + métricas + ressalvas + handoff | ✓ VERIFIED | 545 linhas, todas as seções pedidas presentes |
| `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` | Ledger versionado com trajetória | ✓ VERIFIED | 140 linhas de dados, contagens recontadas independentemente |
| `.planning/REQUIREMENTS.md` | Rastreabilidade v1.3 11/11 | ✓ VERIFIED | Linha `LEDG-02 Completo`, cobertura declarada "11/11" |
| `.planning/STATE.md` | Bloco "Fecho medido" | ✓ VERIFIED | Presente, linha 40, com os 4 critérios e 2 ressalvas principais |
| `.planning/WINDOWS.md` | Entrada #17 (ROADMAP não commitado) | ✓ VERIFIED | `id=17`, `phase=21`, `open`, descrição bate com a situação real do `git status` |

### Data Point Cross-Checks (correndo eu próprio, não citando)

| Verificação | Comando | Resultado obtido | Bate com o afirmado? |
|---|---|---|---|
| Suite T2 | `.venv/bin/python3 tools/test_midasm_emit.py` | `rc=0` | Sim |
| Suite bónus | `.venv/bin/python3 tools/test_oracle_manifest.py` | `rc=0`, `10/10 [PASS]` | Sim |
| Suite T7 (amostra) | `.venv/bin/python3 games/gow2/lift_baseline/test_baseline_delta.py` | `rc=0`, `5/5 [PASS]` | Sim |
| `gow2_midasm` em produção | `grep -rl 'gow2_midasm' ../gow2-recomp/recomp_macos_v2/` | `rc=1`, 0 ficheiros | Sim (ressalva 1 do marco) |
| `CORRECTNESS` ativos hoje | `awk` sobre `PATCH_MIGRATION.tsv` HEAD | `72` (todo) / `4` (migrated) / `2` (redundant) | Sim |
| `CORRECTNESS` ativos "antes" | `git show ee17e2c:...` + mesmo `awk` | `78` (todo) / `0` / `0` | Sim |
| Trajetória intermédia | `git show` nos 4 commits `ee17e2c/b6f0c79/94f25ef/304610b` | `78→76→74→72` | Sim, exato |
| Linhas do `accept_relift.sh` | `awk 'NR==73...'` etc. | `:73` `VERIFY_LIFT=`, `:129-130` invocação, `:262` AND final | Sim |
| Pré-existência da perna 2 | `git log -p --follow` | Introduzida em `757a40b` (Fase 5, v1.0) | Sim — pré-marco confirmado |
| `emit_weak_wrappers`/`switch_table_file` no TOML de produção | `grep -n` em `gow2_recomp.toml` | `:23` e `:24` presentes | Sim |
| Ficheiros tocados pela Fase 21 | `git log --name-only` nos 10 commits da fase | Só `.planning/`, `docs/`, `games/gow2/README.md`, `games/gow2/notes/` | Sim — produção intacta |

### Requirements Coverage

| Requirement | Fase | Status | Evidência |
|---|---|---|---|
| LEDG-02 | 21 | ✓ SATISFIED | Os 4 sub-critérios (sequência escrita, `verify_lift` no caminho, suite com `rc` real, métrica antes/depois) todos verificados independentemente acima |

### Anti-Patterns Found

Nenhum `TBD`/`FIXME`/`XXX` sem referência encontrado nos artefatos da Fase 21. Nenhum
`console.log`/stub/placeholder — os artefatos são documentação e dados de medição, não
código executável novo. `docs/MACOS_PORT_PLAN.md` foi anotado (não reescrito) marcando
passos desatualizados — comportamento correto e declarado.

### A pergunta central: o marco v1.3 fecha legitimamente com 11/11?

**Sim, na formulação que o próprio documento usa** — "fecha inteiro" refere-se aos **11
requisitos e aos 4 critérios de fecho, medidos**, não a "o lift está limpo" nem a "o
mid-asm está em produção". Verifiquei isto de duas formas:

1. **Os números não estão inflacionados.** `RE-03` está explicitamente rotulado "metade
   soft" em `REQUIREMENTS.md`, na tabela de progresso ("10 inteiros e RE-03 na metade soft
   prevista pelo próprio critério") e em `STATE.md`. Não há tentativa de contar 11/11
   "inteiros" — o próprio texto qualifica o 11º.
2. **A migração útil é apresentada como 4 linhas em 78, não escondida.** O `results.md` tem
   a frase "**O trabalho útil de migração são 4 linhas em 78**" na secção da tabela
   antes/depois, e a ressalva #1 ("a ressalva maior do marco") abre a seção de ressalvas
   dizendo sem rodeios que o mid-asm **não está** no caminho que o jogador corre.

**Onde a formulação fica menos visível:** a frase mais precisa e citável —
*"'Inteiro' refere-se aos requisitos e aos critérios de fecho, medidos. Não significa 'o
lift está limpo' nem 'o mecanismo está no caminho do jogador'"* — só existe literalmente em
`21-03-SUMMARY.md:126-128`, um artefato de planeamento que um auditor do `results.md` (que
`REQUIREMENTS.md` aponta como "a lista completa") não é obrigado a abrir. O `results.md`
por si só **tem os factos** (4/78, ressalva 1 em destaque) mas não tem essa frase-síntese
exata. `STATE.md` tem uma síntese equivalente e mais visível ("O marco não limpou o
corpus: entregou o mecanismo que permite limpá-lo, provado em 4 linhas reais"), mas
`STATE.md` também não é o documento que `REQUIREMENTS.md` cita como fonte das ressalvas.

**Meu veredicto, sendo cético:** um leitor apressado que abra **só** o `results.md` (o
documento indicado como fonte única) ainda sai com a impressão correta, porque a ressalva
#1 está em primeiro lugar na lista e é redigida sem eufemismo ("a ressalva maior do
marco"). Um leitor que leia **só** a linha do `REQUIREMENTS.md` ou **só** o bloco de
`STATE.md` também sai correto — ambos qualificam o "inteiro" explicitamente. O risco real
é um leitor que veja **apenas** o título "11/11 completos" fora de contexto (ex.: numa
mensagem de commit ou num resumo de uma frase) sem abrir nenhum dos três documentos — mas
isso é verdade de qualquer marco resumido a um número, não uma falha específica desta fase.

## Gaps Summary

Nenhum gap bloqueante. Todos os 8 must-haves derivados dos critérios do `ROADMAP.md` e do
`LEDG-02` foram verificados de forma independente, com números re-medidos por mim (não
apenas citados do SUMMARY), e bateram em 100% das amostras. O único item pendente é a
decisão humana sobre se vale a pena copiar a frase-síntese do `21-03-SUMMARY.md` para o
cabeçalho do `results.md`, tornando a ressalva central impossível de perder mesmo para um
leitor de um único documento.

---
*Verified: 2026-08-03T03:01:29Z*
*Verifier: Claude (gsd-verifier)*
