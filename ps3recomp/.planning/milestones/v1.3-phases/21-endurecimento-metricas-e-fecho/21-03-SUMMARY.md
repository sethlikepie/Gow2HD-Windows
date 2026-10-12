---
phase: 21-endurecimento-metricas-e-fecho
plan: 03
subsystem: docs-planning
tags: [ledger, metricas, xen-05, handoff, wall-d, relift, gow2, marco-v1.3]

requires:
  - phase: 21-01
    provides: "docs/RELIFT_CANONICAL.md (8 etapas) + evidencia file:line de que accept_relift.sh:73 ja invocava verify_lift.sh"
  - phase: 21-02
    provides: "suite T1-T10 com rc REAL (12 linhas corridas, 0 presumidas) no results doc"
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "PATCH_MIGRATION.tsv versionado -- a coluna 'antes' da metrica e' lida por git show do proprio ficheiro"
  - phase: 20-o-oraculo-e-o-metodo
    provides: "docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md -- a materia-prima do handoff"
provides:
  - "Metricas antes/depois RECONTADAS do disco: CORRECTNESS activos 78 -> 72, migrated 0 -> 4, redundant 0 -> 2"
  - "Trajectoria do ledger commit a commit por fase (16/17/18/19), lida por git show"
  - "XEN-05 nas duas leituras, com a conservadora (3 mecanismos) declarada como a que decide"
  - "As dez ressalvas do marco escritas no results doc, nao so' nos SUMMARYs"
  - "Handoff da wall D: a divergencia low16 vs subtag acrescenta uma TERCEIRA hipotese ao criterio 1 da Fase 11"
  - "Lista P1 com a ressalva que cada item fecha; ABI opts / SIMDe / shader recomp explicitamente FORA"
  - "REQUIREMENTS.md com a rastreabilidade v1.3 completa (11/11) e STATE.md com o bloco 'Fecho medido'"
affects: [21-VERIFICATION, auditoria-do-marco-v1.3, fase-11-as-quatro-paredes]

tech-stack:
  added: []
  patterns:
    - "A coluna 'antes' de uma metrica e' lida por git show do artefacto versionado, nao citada de um SUMMARY -- cada celula tem um commit por tras"
    - "Metrica reportada nas duas leituras, com a conservadora declarada como a que decide"
    - "Ressalvas do marco no documento que a auditoria le, nao so' nos SUMMARYs que ela nao abre"

key-files:
  created:
    - .planning/phases/21-endurecimento-metricas-e-fecho/21-03-SUMMARY.md
  modified:
    - games/gow2/notes/milestone-2026-08-xenon-re-results.md
    - .planning/REQUIREMENTS.md
    - .planning/STATE.md
    - .planning/WINDOWS.md
  nao-tocado-por-decisao:
    - .planning/ROADMAP.md

key-decisions:
  - "A coluna 'antes' foi lida por `git show ee17e2c:games/gow2/lift_baseline/PATCH_MIGRATION.tsv`, nao citada do 21-CONTEXT -- o ledger e' versionado, logo a trajectoria pode ser dada por fase (78 -> 76 -> 74 -> 72) em vez de so' nas duas pontas"
  - "XEN-05 fecha pela leitura CONSERVADORA (3 mecanismos), com o minimo exacto do criterio e sem folga -- e isso fica escrito, em vez de se citar a leitura mais generosa (4)"
  - "ROADMAP.md NAO foi editado nem commitado: as alteracoes de outra sessao estao nas MESMAS linhas que o 21-03 teria de mexer, logo nao ha separacao possivel num commit (CLAUDE.md regra 7). Registado em WINDOWS #17 com o que fica por aplicar"
  - "Os numeros do brief foram tratados como pistas e recontados: 10 de 11 bateram; 'os 6 P1 weak' era na verdade 10 linhas alvo=weak+todo (3 P0 OPD + 7 P1), e o results doc escreve o numero medido"

requirements-completed: [LEDG-02]

coverage:
  - id: M1
    description: "Tabela antes/depois com numeros medidos nas duas pontas, e a fonte de cada celula"
    requirement: "LEDG-02"
    verification:
      - kind: other
        ref: "awk sobre PATCH_MIGRATION.tsv -> CORRECTNESS todo=72 migrated=4 redundant=2, TOTAL=140"
        status: pass
      - kind: other
        ref: "git show ee17e2c:.../PATCH_MIGRATION.tsv | awk -> CORRECTNESS todo=78 migrated=0 redundant=0"
        status: pass
    human_judgment: false
  - id: M2
    description: "XEN-05 reportado nas duas leituras, com a conservadora a decidir e o redundant contado a' parte"
    requirement: "LEDG-02"
    verification:
      - kind: other
        ref: "results doc § 'XEN-05 — as duas leituras, e qual decide': 4 linhas / 3 mecanismos / 4 por TOML / 2 redundant fora"
        status: pass
    human_judgment: false
  - id: M3
    description: "As ressalvas obrigatorias aparecem no results doc, nao so' nos SUMMARYs"
    requirement: "LEDG-02"
    verification:
      - kind: other
        ref: "rg 'redundant|sticky|bytes_read|OPD|RE-03|wont|TYPEWALK|SIMDe|ABI' no results doc -> 55 linhas; 10 ressalvas numeradas"
        status: pass
    human_judgment: false
  - id: M4
    description: "Handoff nomeia a divergencia de indice, a terceira hipotese e a sonda seguinte; ABI/SIMDe/shader FORA"
    requirement: "LEDG-02"
    verification:
      - kind: other
        ref: "results doc § Handoff A/B/C: 0x0041FA90, 12/18 consumidores, 0x40030001 (1 vs 3), PS3_TYPEWALK_TRACE=1 com duas previsoes opostas, tabela 'Explicitamente FORA'"
        status: pass
    human_judgment: false
  - id: M5
    description: "O gate do LEDG-02 passa nas 6 caixas antes de o requisito ficar verde"
    requirement: "LEDG-02"
    verification:
      - kind: other
        ref: "rg -c '^### Etapa ' docs/RELIFT_CANONICAL.md -> 8; accept_relift.sh:73; 12 linhas T com rc; metrica numerica nas duas pontas; handoff+OUT; 10 ressalvas"
        status: pass
    human_judgment: false
  - id: M6
    description: "O ROADMAP.md reflecte o fecho do marco"
    verification: []
    human_judgment: true
    rationale: "NAO FEITO por decisao, nao por esquecimento. O ficheiro tem alteracoes nao commitadas de outra sessao activa nas mesmas linhas que este plano teria de editar; separar num commit e' impossivel sem staging interactivo, e commitar o bloco inteiro seria commitar working tree alheio (CLAUDE.md regra 7). Registado em WINDOWS #17 com as tres edicoes pendentes escritas. A verdade do fecho esta em REQUIREMENTS.md, STATE.md e no results doc."

metrics:
  duration: "~35min"
  completed: 2026-08-02
  tasks: 3
  files: 4
status: complete
---

# Phase 21 Plano 03: Métricas, ressalvas e fecho do marco v1.3 — Summary

**A melhoria do marco é um número, e o número é este: `patch_*.py` de classe `CORRECTNESS`
ainda activos passam de **78 para 72** — menos 6, dos quais 2 nunca foram precisos. O
`XEN-05` fecha pela leitura conservadora com **3 de 3**, sem folga. E o mid-asm **não está
no lift que o jogador corre**.**

## O veredicto, primeiro

| Pergunta | Resposta com o número |
|---|---|
| `LEDG-02` fecha? | **Sim.** As 6 caixas do gate passam: 8 etapas em `RELIFT_CANONICAL.md`, `accept_relift.sh:73`, 12 linhas de suite com `rc` real, métrica numérica nas duas pontas, handoff + OUT, 10 ressalvas |
| O marco v1.3 fecha inteiro ou parcial? | **Fecha INTEIRO nos requisitos: 11/11** — 10 inteiros e `RE-03` na metade **soft** que o próprio critério prevê ("se o MCP for instável, declara-se `wont` e o marco fecha na mesma") |
| E os 4 critérios de fecho do marco? | **4/4 satisfeitos**: ≥3 fixes re-lift-safe → **4 linhas / 3 mecanismos**; `verify_lift` no aceite → **sim, e já lá estava**; smoke sem regressão → `st620_max=11`, `OOB=0`; playbook usado 1× numa wall → **sim**, veredicto ABERTO |
| Então está tudo bem? | **Não.** Fecha com **dez ressalvas escritas**, e duas mudam o que se pode afirmar: o mid-asm **não está em produção**, e a sequência canónica **nunca correu 1→8 numa passagem** |

**"Inteiro" refere-se aos requisitos e aos critérios de fecho, medidos. Não significa "o
lift está limpo" nem "o mecanismo está no caminho do jogador".** Essa distinção é o
resultado mais importante deste plano.

## Task 1 — a métrica, recontada em vez de citada

O `21-CONTEXT` e o brief traziam números. **Foram tratados como pistas e recontados do
disco**, com um `awk` reproduzível escrito no próprio results doc:

```
CORRECTNESS/migrated 4 · CORRECTNESS/redundant 2 · CORRECTNESS/todo 72
OBSOLETE/todo 2 · OPD/todo 10 · PROBE/todo 50 · TOTAL 140
```

**O que este plano acrescentou e não estava em lado nenhum:** a coluna "antes" não é uma
lembrança nem uma citação de SUMMARY — é o **mesmo ficheiro no commit da Fase 16**, lido
por `git show`. Isso permitiu dar a trajectória **por fase**, não só nas duas pontas:

| Momento | Commit | `CORRECTNESS/todo` | `migrated` | `redundant` |
|---|---|---:|---:|---:|
| Fase 16 — baseline congelado | `ee17e2c` | **78** | 0 | 0 |
| Fase 17 — CE03C (mid-asm) | `b6f0c79` | 76 | 2 | 0 |
| Fase 18 — jump tables | `94f25ef` | 74 | 2 | **2** |
| Fase 19 — `2F3F0` + `2C0498` (weak) | `304610b` | **72** | **4** | 2 |

**A delta honesta: −6 linhas em 78 (7,7 %), e duas delas nunca foram precisas.** O trabalho
útil de migração são **4 linhas em 78**. O marco não entrega um corpus limpo — entrega o
**mecanismo** que permite limpá-lo, provado em quatro linhas reais. Ficam 72 `CORRECTNESS`,
50 `PROBE`, 10 `OPD` e 2 `OBSOLETE`, todos ainda reaplicados pelo `apply_all_patches.sh`.

As 4 linhas migradas foram **nomeadas com o mecanismo de cada uma** (para o auditor não ter
de re-grepar o ledger), e o `XEN-05` foi reportado nas duas leituras com a conservadora
declarada como a que decide: **3** (`func_000CE03C`, `func_0002F3F0`, `func_002C0498`),
porque as duas linhas `ce03c_*` partilham função e hook.

## Task 2 — as dez ressalvas e o handoff

As ressalvas passam a estar no documento que a auditoria lê, não só nos SUMMARYs que ela
não abre. Por ordem de importância para quem retomar:

1. **O mid-asm não está no lift de PRODUÇÃO** — `grep -rl 'gow2_midasm' ../gow2-recomp/recomp_macos_v2/` → **zero ficheiros** (re-medido hoje). Provado, não promovido.
2. **A sequência 1→8 nunca correu numa só passagem** (`WINDOWS` #16).
3. **2 jumptables `redundant`** — nunca precisos, fora do numerador do `XEN-05`.
4. **`RE-03` só na metade soft** — MCP `wont` com 4 bloqueios medidos.
5. **`sticky` NÃO-OBSERVÁVEL** — e portanto **o critério `sticky_pub>=1` que o próprio `CLAUDE.md` prescreve no checklist pós-merge não é verificável neste build**. Dívida de documentação do projecto, escrita.
6. **`bytes_read` não-exercitado** — o aceite `smoke:` do `patch_fallthrough_2550c8.py` continua por correr.
7. **P0 `OPD` não migráveis por weak** — cirurgia no interior (783 instruções em `0x0014B1F0`); **10 linhas `alvo=weak` continuam `todo`** (3 P0 + 7 P1).
8. **`analyze_eboot_ghidra.sh` sem corrida ponta a ponta** neste marco.
9. **`patch_fallthrough_2550c8.py` reclassificado (`alvo=lifter`), não migrado.**
10. **`../gow2-recomp/verify_lift.sh` é a cópia pré-Fase-20** (`grep -c VERIFY_ORACLE` → **0**, contra **10** no motor).

**O handoff da wall D** ficou escrito com as âncoras e as duas previsões opostas: a tabela
`PTR_DAT_0053ef1c` é escrita por `0x0041FA90` indexada por `(w0>>16)&0xFFF`, mas **12 dos 18
consumidores** — incluindo a parede 1 `func_002545B0` — indexam por `w0 & 0xFFFF`; coincidem
em `0xC0010001` e **divergem** em `0x40030001` (1 vs 3). Isso acrescenta uma **terceira
hipótese** ao critério 1 da Fase 11, que só listava duas exclusivas. Próximo experimento:
sonda `PS3_TYPEWALK_TRACE=1`, **OFF por default**, com previsões escritas **antes** da
corrida.

A lista P1 foi escrita **com a ressalva que cada item fecha** (não como uma lista solta), e
`ABI opts` / `SIMDe` / `shader recomp` ficaram numa tabela **"Explicitamente FORA"** com o
porquê de cada um — para o próximo marco não herdar scope creep por omissão.

## Task 3 — o gate, e o que ficou por fazer de propósito

O gate do `LEDG-02` foi corrido caixa a caixa **antes** de pôr o requisito verde:

| Caixa | Medido |
|---|---|
| `RELIFT_CANONICAL.md` com 8 etapas | `rg -c '^### Etapa '` → **8** |
| `accept_relift` → `verify_lift` | `games/gow2/accept_relift.sh:73` lido |
| T1–T10 com `rc` ou `não-exercitado` | **12** linhas `T*` na tabela, todas com `rc` |
| Métrica numérica nas duas pontas | 78 → 72 (Task 1) |
| Handoff + lista OUT | § Handoff A/B/C (Task 2) |
| Ressalvas honestas | 10, numeradas (Task 2) |

`REQUIREMENTS.md` passou a ter a **rastreabilidade v1.3 completa** — `XEN-03`, `XEN-04`,
`XEN-05`, `RE-01`, `RE-02`, `RE-03` e `LEDG-02`, cada um com a evidência medida **e a
ressalva que o acompanha** (nenhuma linha diz só "Completo"). `STATE.md` ganhou o bloco
**"Fecho medido"** com os 4 critérios de fecho, o número de cada e as duas ressalvas que
pesam, mais progresso 6/6 fases e 18/18 planos.

## Desvios ao plano

**Nenhum desvio de regra (1–4).** Três diferenças de execução, todas declaradas:

1. **`ROADMAP.md` não foi editado nem commitado — por decisão, não por esquecimento.**
   `git diff` antes de tocar (como o brief mandou) mostrou que as alterações não commitadas
   de outra sessão estão **exactamente nas linhas** que este plano teria de editar (o bloco
   `**Plans**` da Fase 21 e as linhas 17/20/21 da tabela `Progress`). Marcar `[x]` seria
   modificar linhas que só existem no working tree alheio; commitar o ficheiro arrastaria o
   trabalho dessa sessão. Não há staging parcial não-interactivo que separe isto. Aplicada a
   **regra 7 do `CLAUDE.md`** e o fallback explícito do brief: **deixar e registar**
   (`WINDOWS` #17, com as três edições pendentes escritas para serem aplicadas em segundos).
2. **Números do brief recontados, e um divergiu.** 10 de 11 bateram exactamente. O que
   divergiu: "os 6 P1 `weak`" — o ledger tem **10 linhas** `alvo=weak` + `todo` (3 P0, todas
   `OPD`; 7 P1, sendo 6 `OPD` + 1 `CORRECTNESS`, `patch_wad_state_machine.py`). O results doc
   escreve o número **medido**.
3. **`[[function_override]]` corrigido para `[[functions_override]]`** (4 ocorrências) depois
   de ler o TOML de produção: são **1 `[[midasm_hook]]` + 4 `[[functions_override]]`**, e o
   4.º (`:96`, EA `0x00010230`) é o piloto trivial de identidade da Fase 19, que **não** conta
   para a métrica. Escrever a chave errada num documento de auditoria seria uma âncora falsa.

## Known Stubs

Nenhum stub de código (este plano não toca código de produto). Duas lacunas de **exercício**,
ambas declaradas no results doc e no ledger de defeitos:

| Item | Estado | Onde |
|---|---|---|
| O mid-asm no lift de **produção** | **NÃO PROMOVIDO** — provado noutro lift | `WINDOWS` #18, ressalva 1 |
| A sequência canónica 1→8 numa só passagem | **NÃO-EXERCITADA** | `WINDOWS` #16, ressalva 2 |
| `ROADMAP.md` a reflectir o fecho | **PENDENTE** (bloqueado por working tree alheio) | `WINDOWS` #17 |

## Restrições respeitadas

- Árvore principal, sem worktree. Commits em pt-BR, **sem `Co-Authored-By`**, paths
  explícitos, **nunca `git add -A`**.
- Zero dados de jogo, zero logs, zero lifts, zero JSONs de `ghidra_out/` commitados.
- Nenhum ficheiro proibido tocado: `CLAUDE.md`, `games/gow2/CLAUDE.md`, `.planning/PROJECT.md`,
  `.planning/MILESTONES.md`, `.planning/config.json`, `.planning/GSD-MODEL-ROUTING.md`,
  `.planning/milestones/v1.2-*.md`, `scripts/gsd_plan*.sh` — todos continuam como estavam
  (`git status` confirma que os `M` deles são de outra sessão, intocados).
- **Nenhum verbo `state.*` nem `roadmap.update-plan-progress` corrido** — `REQUIREMENTS.md`
  e `STATE.md` editados à mão, alcance mínimo.
- Nada promovido a produção.
- Nenhum boot corrido nesta fase (logo: nenhum órfão, nenhum `pkill`).

## Commits

| Commit | Mensagem |
|---|---|
| `c88d3bd` | `docs(21-03): metricas antes/depois recontadas -- CORRECTNESS 78->72, XEN-05 fecha em 3 mecanismos` |
| `e43f06a` | `docs(21-03): as dez ressalvas do marco e o handoff da wall D` |
| `5d50926` | `docs(21-03): LEDG-02 fechado -- marco v1.3 fecha inteiro nos requisitos (11/11)` |
| `6894a32` | `docs(21-03): SUMMARY do fecho do marco v1.3 e as verificacoes das Fases 16-20` |

## Self-Check: PASSED

- Ficheiros afirmados presentes em disco: `games/gow2/notes/milestone-2026-08-xenon-re-results.md`,
  `docs/RELIFT_CANONICAL.md`, `.planning/REQUIREMENTS.md`, `.planning/STATE.md`,
  `.planning/WINDOWS.md`, `21-03-SUMMARY.md` — 6/6 `FOUND`.
- Commits afirmados presentes em `git log`: `c88d3bd`, `e43f06a`, `5d50926`, `6894a32`.
- `git diff --diff-filter=D HEAD~3 HEAD` → **vazio** (nenhuma deleção acidental).
- `.planning/ROADMAP.md` continua com **exactamente** as 28 inserções / 8 remoções da outra
  sessão, intocado por este plano (`git diff --stat` idêntico ao do início).
- Números do results doc re-medidos nesta sessão: `awk` do ledger (72/4/2/140),
  `git show ee17e2c` (78/0/0), `rg -c '^### Etapa '` → 8, `grep -c VERIFY_ORACLE` → 10 no
  motor e 0 no espelho, `grep -rl gow2_midasm recomp_macos_v2/` → zero ficheiros.

## Next Phase Readiness

- **Imediato:** `21-VERIFICATION.md` — é a **única** fase do marco sem verificação no disco
  (16–20 têm todas `passed`). Depois, a auditoria do marco, que tem como porta de entrada
  `games/gow2/notes/milestone-2026-08-xenon-re-results.md`.
- **P1 nº 1 do próximo marco:** promover a produção um lift com mid-asm — fecha a ressalva
  que mais pesa. Gate próprio, decisão humana.
- **Fase 11 (as quatro paredes):** retoma com o handoff já escrito, com uma hipótese nova
  medida e com o mecanismo que faz as suas correcções sobreviverem ao re-lift.
