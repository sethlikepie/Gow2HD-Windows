---
phase: 20-o-oraculo-e-o-metodo
plan: 03
subsystem: reverse-engineering
tags: [ghidra, mcp, ghidra_lookup, politica-de-agentes, gow2, seguranca-de-tooling]

requires:
  - phase: 20-o-oraculo-e-o-metodo
    provides: "20-02: docs/RE_PLAYBOOK_GOW2.md (método de wall, níveis de evidência, anti-padrões) e a primeira sessão de ledger"
  - phase: 20-o-oraculo-e-o-metodo
    provides: "20-01: ghidra_out/MANIFEST.json com proveniência (sha256 do ELF, data, versão do Ghidra, contagens)"
provides:
  - "docs/GHIDRA_MCP_SETUP.md — MCP de Ghidra com versão fixada (GhidraMCP 1.4, sha256 do artefacto), whitelist das 27 tools reais, lista de proibições e a política de leitura+anotação"
  - "Procedimento de paridade CLI↔MCP com duas âncoras GoW2 já medidas e saída literal — metade executada, metade por correr"
  - "Veredicto `wont` medido para a metade viva do MCP, com quatro bloqueios citáveis e a ordem por custo para quem retomar"
  - "Refutação, por sonda de controlo, de uma hipótese de bug no --callers do tools/ghidra_lookup.py (não havia bug; o erro estava na sonda)"
  - "Caracterização correcta do --callers: superconjunto por substring de endereço, não consulta de xrefs"
affects: [fase-11-paredes, fase-21-antes-depois, politica-de-agentes-do-monorepo]

tech-stack:
  added: []
  patterns:
    - "Pin de tooling externo com sha256 do artefacto e a versão que ele declara suportar, verificável por comando no próprio documento"
    - "Whitelist/denylist de tools de MCP escrita a partir dos nomes REAIS extraídos do servidor, não de memória"
    - "Sonda de controlo obrigatória antes de escrever um achado de tooling no ledger"

key-files:
  created:
    - docs/GHIDRA_MCP_SETUP.md
  modified:
    - docs/RE_PLAYBOOK_GOW2.md
    - .planning/WINDOWS.md

key-decisions:
  - "Primário fixado em LaurieWired/GhidraMCP 1.4 (commit 27f316f8, sha256 b81ca524…c7e734) apesar de não servir nesta máquina — um pin auditável vale mais que 'o MCP mais recente'; as quatro alternativas ficam listadas como não-objectivo, sem avaliação forjada"
  - "`wont` declarado por quatro bloqueios MEDIDOS em vez de tentar forçar: versão da extensão (11.3.2) vs Ghidra instalado (12.1.2), validação explícita no ExtensionInstaller do Ghidra 12, instalação só por GUI com Maven ausente para reconstruir, e ausência de projecto Ghidra persistido"
  - "Lado do CLI da paridade executado e escrito na mesma, com controlo NEGATIVO (0x0041F700 → ∅) e POSITIVO (0x00420A44 → {0x00254C40}) — quem tiver MCP só corre metade"
  - "tools/ghidra_lookup.py NÃO foi alterado: a hipótese de falso negativo caiu na sonda de controlo, e mexer num oráculo sem defeito enquanto o plano 20-01 corre em paralelo seria risco puro"
  - "Duas tools do MCP (set_function_prototype, set_local_variable_type) classificadas como CONDICIONAIS e não como permitidas — mudam o C decompilado, logo mudam o que um export futuro dirá"

patterns-established:
  - "MCP = leitura e anotação; código de produto só com diff revisto e teste; nunca um LLM a emitir ppu_recomp_*.cpp"
  - "O oráculo do verify_lift é sempre o export estático com MANIFEST.json — MCP nunca entra no caminho de aceitação nem em CI"
  - "Ao comparar duas ferramentas, verificar primeiro se respondem à MESMA pergunta (grep no C vs base de xrefs)"

requirements-completed: [RE-03]

coverage:
  - id: D1
    description: "docs/GHIDRA_MCP_SETUP.md com versão fixada, requisitos de host, instalação sem segredos, tools permitidas e proibidas"
    requirement: RE-03
    verification:
      - kind: other
        ref: "test -f docs/GHIDRA_MCP_SETUP.md && rg -n 'version|PERMITIDAS|PROIBID|decompile|xref|paridade|wont|segredo' docs/GHIDRA_MCP_SETUP.md → 27 linhas, 10 secções"
        status: pass
      - kind: other
        ref: "pin verificado por medição: shasum -a 256 do GhidraMCP-release-1-4.zip → b81ca5240fdde57ea4e899170dcd9fdee6ed29246280c74263a509bfcfc7e734; extension.properties → version=11.3.2/ghidraVersion=11.3.2; 27 nomes de tool extraídos do bridge_mcp_ghidra.py"
        status: pass
      - kind: other
        ref: "grep -nE 'sk-|ghp_|xox|Bearer |api[_-]?key' docs/GHIDRA_MCP_SETUP.md → nenhum segredo; config de cliente documentada com placeholders e caminhos fora do repo"
        status: pass
    human_judgment: false
  - id: D2
    description: "Política explícita: MCP = leitura + anotação; código de produto só por diff revisto e teste; MCP não substitui o oráculo estático nem entra em CI"
    requirement: RE-03
    verification:
      - kind: other
        ref: "docs/GHIDRA_MCP_SETUP.md §6 (7 proibições) e §7 (política em 4 linhas); cross-link cirúrgico em docs/RE_PLAYBOOK_GOW2.md §3.1 a repetir a mesma regra"
        status: pass
    human_judgment: false
  - id: D3
    description: "Sessão de paridade: lado do CLI executado com duas âncoras, lado do MCP declarado wont com razão medida"
    requirement: RE-03
    verification:
      - kind: other
        ref: "docs/GHIDRA_MCP_SETUP.md §9.2 — saída literal de `ghidra_lookup.py … --callers --no-c` para 0x0041F700 (∅) e 0x00420A44 ({0x00254C40}, chamada directa na linha 206 do C de _opd_FUN_00254c40)"
        status: pass
      - kind: other
        ref: "§9.1 + §10 — bloqueios medidos (application.version=12.1.2 vs ghidraVersion=11.3.2; ExtensionInstaller.validateExtensionVersion + diálogo 'Extension Version Mismatch' no Project.jar; `which mvn` ausente; find *.gpr/*.rep vazio; mcpServers=['MCP_DOCKER'])"
        status: pass
      - kind: other
        ref: "lado do MCP: NÃO CORRIDO — registado como janela aberta 15 em .planning/WINDOWS.md"
        status: unknown
    human_judgment: true
    rationale: "Aceitar um `wont` como fecho de um requisito soft é julgamento humano: a automação prova que os bloqueios existem e que metade da paridade está feita, mas não pode decidir se vale a pena o humano instalar o MCP à mão. O plano de origem e o 20-CONTEXT autorizam o wont por escrito."

metrics:
  duration: "~35 min"
  completed: 2026-08-02
  tasks: 2
  files_created: 1
  commits: 3

status: complete
---

# Phase 20 Plan 03: MCP de Ghidra — setup, política e o `wont` medido — Summary

**Documento de setup do MCP com versão fixada e sha256 do artefacto, whitelist das 27 tools
reais do servidor e a política de leitura+anotação; a paridade tem o lado do CLI executado e
escrito, e o lado do MCP declarado `wont` por quatro bloqueios medidos — mais uma hipótese de
bug no oráculo que caiu na sonda de controlo antes de chegar ao ledger.**

## Performance

- **Duração:** ~35 min
- **Tarefas:** 2
- **Ficheiros criados:** 1 · **modificados:** 2
- **Boots corridos:** 0 · **dados de jogo tocados:** 0

## Accomplishments

- **`docs/GHIDRA_MCP_SETUP.md`** (10 secções) com tudo o que o `RE-03` pede e nada inventado:
  o pin (`GhidraMCP 1.4`, commit `27f316f8`, `sha256 b81ca524…c7e734`), os requisitos do host
  medidos nesta máquina, a instalação com placeholders e sem segredos, a **whitelist das 27
  tools reais** extraídas do `bridge_mcp_ghidra.py` da própria release, sete proibições, a
  política em quatro linhas e o procedimento de paridade.
- **A metade executável da paridade ficou feita:** duas âncoras GoW2 com saída literal, uma
  de controlo **negativo** (`0x0041F700` → ∅, coerente com a sessão de 20-02) e uma de
  controlo **positivo** (`0x00420A44` → `{0x00254C40}`, chamada directa confirmada na linha
  206 do C). Quem tiver MCP corre `get_xrefs_to` nos mesmos dois EA e compara — o critério de
  aceitação já está escrito, incluindo o que conta como divergência legítima.
- **`wont` declarado com quatro bloqueios citáveis**, nenhum presumido — ver *Requirements*.
- **Uma hipótese de bug refutada por sonda de controlo** antes de entrar no ledger (§9.3 do
  documento): parecia haver um falso negativo estrutural no `--callers` que apagaria 5 681
  funções; a sonda mostrou que não existe e que **o erro estava na minha sonda**. O
  `tools/ghidra_lookup.py` ficou **intocado**.
- **Caracterização correcta do `--callers`**, que o playbook descrevia por alto: é um
  **superconjunto por substring de endereço** (apanha também menções de dados), não uma
  consulta de xrefs. É por isso que um MCP são deve devolver *mais*, e que isso não é falha.

## Task Commits

1. **Task 1 — setup e política** — `39c5a94` (docs): `docs/GHIDRA_MCP_SETUP.md` novo +
   acrescento cirúrgico de 8 linhas em `docs/RE_PLAYBOOK_GOW2.md` §3.1.
2. **Task 2 — paridade / `wont`** — `04f5a78` (docs): apêndice §9 (bloqueios medidos, lado do
   CLI executado, refutação da §9.3) e §10 (bloco `wont`), mais a correcção da §8.2.
3. **Ledger** — `b09ef6b` (docs): janela aberta **15** em `.planning/WINDOWS.md`.

## Files Created/Modified

- `docs/GHIDRA_MCP_SETUP.md` *(novo)* — setup, pin, política, paridade e `wont`.
- `docs/RE_PLAYBOOK_GOW2.md` *(+8 linhas em §3.1)* — cross-link e repetição da regra "o MCP
  nunca é o oráculo". Acrescento cirúrgico: o resto do ficheiro, escrito pelo 20-02, não foi
  tocado.
- `.planning/WINDOWS.md` — entrada 15 (`unrun-verify`), `open_count` 11 → 12.

## Decisions Made

| decisão | porquê |
|---|---|
| Fixar `LaurieWired/GhidraMCP 1.4` mesmo não servindo aqui | um pin auditável (tag + commit + sha256) é o que torna *"o MCP diz X"* rastreável; trocar de primário é decisão de outra fase, com o documento actualizado no mesmo commit |
| Listar 4 alternativas **sem** as avaliar | poupa a pesquisa a quem retomar sem forjar uma comparação que ninguém correu |
| `set_function_prototype` e `set_local_variable_type` como **condicionais**, não permitidas | mudam o C decompilado, logo mudam o que um export futuro dirá; usá-las antes de um export-oráculo transforma "o Ghidra diz X" em "o agente ensinou o Ghidra a dizer X" |
| Não alterar `tools/ghidra_lookup.py` | a hipótese de bug caiu; e o plano 20-01 corre em paralelo nesta árvore — mexer num oráculo sem defeito seria risco puro |
| Registar a paridade dentro do próprio setup doc | um artefacto só, mais fácil de rever; não é uma sessão de wall, logo não gera ledger em `docs/re_sessions/` |

## Deviations from Plan

### 1. [Rule 3 — bloqueio de ambiente] Task 2 seguiu o **Path B** (`wont`), não o Path A

- **Encontrado em:** Task 2, no preflight do lado do MCP.
- **Situação:** os quatro bloqueios da secção *Requirements*. O plano autoriza o Path B por
  escrito e o `20-CONTEXT.md` diz *"se o MCP for instável ou exigir instalação interactiva,
  fica `wont` declarado e a trilha fecha na mesma"*.
- **Resolução:** `wont` com razão, tentativas, data e fallback; e o lado do CLI executado
  para que a paridade fique a meio caminho em vez de a zero.

### 2. [Rule 1 — bug… que não era] Hipótese de falso negativo no `--callers`, refutada

- **Encontrado em:** Task 2, ao escolher a âncora positiva.
- **Issue aparente:** o `functions.json` chama à função `.opd.FUN_0041f700` e o C do Ghidra
  escreve `_opd_FUN_0041f700`; a comparação de nome do tool é literal. A minha sonda contou
  **5 681 funções / 24 570 menções** que ficariam invisíveis — do mesmo tipo do falso
  negativo do `--writes` já registado (janela 14).
- **Fix:** **nenhum.** A sonda de controlo (correr o tool na âncora `0x00420A44` *antes* de
  escrever o achado) devolveu exactamente o caller que a hipótese dizia estar perdido. O tool
  tem uma segunda condição, `needle_addr in c.lower()`, e `_opd_FUN_00420a44` **contém**
  `00420a44`. Confirmado em todo o export: das 13 096 funções decompiladas, **13 096** têm
  nome `.opd.FUN_<próprio endereço>` e **nenhuma** tem outra forma — a condição de endereço
  subsome sempre a de nome. O erro estava na minha sonda, que usava `\b[0-9a-f]{8}\b`: não há
  fronteira de palavra depois de `_`.
- **Ficheiros modificados:** nenhum ficheiro de código. A refutação ficou escrita na §9.3 e a
  §8.2 foi corrigida para descrever o `--callers` como ele é.
- **Verificação:** `0x00420A44 --callers` → `0x00254C40`, com a chamada directa localizada na
  linha 206 do C de `_opd_FUN_00254c40`.
- **Committed in:** `04f5a78`.

### 3. [instrução do utilizador] Verbos GSD e ficheiros de outros agentes

`roadmap.update-plan-progress` e todos os `state.*` **não** foram corridos, por instrução
explícita. `REQUIREMENTS.md` **não** foi marcado (o `RE-03` fecha aqui, como o 20-02 fez com o
`RE-02`) para não colidir com o plano 20-01, que corre em paralelo nesta mesma árvore.
Intactos e fora dos commits: `CLAUDE.md`, `.planning/PROJECT.md`, `MILESTONES.md`,
`ROADMAP.md`, `config.json`, `GSD-MODEL-ROUTING.md`, `milestones/v1.2-*`, `scripts/gsd_plan*`,
`games/gow2/CLAUDE.md`, os `*-VERIFICATION.md`, os `.last_plan_engine`, e os ficheiros do
20-01 (`verify_lift.sh`, `analyze_eboot_ghidra.sh`, `oracle_manifest.py`,
`audit_boundaries.py`, `lift_baseline/`) — confirmado no `--name-status` de cada commit.

---

**Total de desvios:** 1 de ambiente (Path B), 1 refutação de auto-fix (nenhum código mudou),
1 de coordenação. **Impacto:** nenhum scope creep; zero ficheiros de código alterados.

## Requirements

**`RE-03` — FECHA, pela metade *soft* que o próprio requisito autoriza.**

Critério: *"`docs/GHIDRA_MCP_SETUP.md` com versão fixada, tools permitidas
(decompile/xrefs/rename) e proibidas; 1 sessão de paridade em que o agente devolve os mesmos
callers que `ghidra_lookup.py`. **Soft: se o MCP for instável, declara-se `wont` e o marco
fecha na mesma**."*

| exigência | prova |
|---|---|
| documento existe | `docs/GHIDRA_MCP_SETUP.md`, 10 secções |
| **versão fixada** | tag `1.4`, commit `27f316f8`, `sha256 b81ca524…c7e734`, `extension.properties` → `ghidraVersion=11.3.2` — tudo verificável pelos comandos da §2 |
| tools **permitidas** | §5.1 (19 de leitura, incluindo `decompile_function`, `get_function_by_address`, `get_xrefs_to`), §5.2 (6 de anotação: `rename_*`, `set_*_comment`), §5.3 (2 condicionais) — nomes reais do bridge 1.4 |
| tools **proibidas** | §6, 7 itens: export/rewrite do programa, patch cego do binário, gerar código de produto, substituir o oráculo, escrever no repo por MCP, commitar projecto/JSON/logs, expor a porta fora de `127.0.0.1` |
| **política** | §7: leitura e anotação; produto só com diff revisto e teste; o oráculo é o export estático com `MANIFEST.json`; nível de evidência obrigatório |
| sessão de paridade | **metade**: lado do CLI executado e literal (§9.2); lado do MCP **`wont`** (§10) |

**Os quatro bloqueios, medidos:**

1. **Versão.** Única release do primário (`1.4`, 2025-06-23) declara `ghidraVersion=11.3.2`;
   esta máquina tem Ghidra `12.1.2` (`Ghidra/application.properties`).
2. **O Ghidra 12 valida isso.** `Project.jar` →
   `ghidra/framework/project/extensions/ExtensionInstaller.class` tem `validateExtensionVersion`,
   `getApplicationVersion` e o diálogo **"Extension Version Mismatch"**.
3. **Instalação só por GUI**, e reconstruir para o 12.1.2 exigiria Maven — `which mvn`
   → **ausente** — mais a cópia de 8 jars. Ambos proibidos por esta execução (sem prompts
   interactivos, sem instalar toolchains).
4. **Sem projecto Ghidra persistido** (`find` por `*.gpr`/`*.rep` → nada; o
   `analyze_eboot_ghidra.sh` usa projecto temporário), logo um servidor MCP obrigaria a nova
   auto-análise do EBOOT — *"dezenas de minutos"* pelas palavras do próprio script.

E um quinto, de natureza diferente: **um agente já em execução não adquire tools a meio da
sessão** — `~/.claude.json` → `mcpServers: ['MCP_DOCKER']`, sem `.mcp.json` no repo e sem
nenhuma tool `mcp__ghidra__*` nesta sessão. Registar o servidor é trabalho do humano.

## O que este plano NÃO afirma

- **não** afirma que o GhidraMCP está partido — afirma que a release fixada não declara
  suporte para o Ghidra desta máquina, e que ninguém a instalou aqui;
- **não** afirma que a paridade falharia; afirma que **não foi corrida** do lado do MCP;
- **não** afirma que nenhum servidor MCP de Ghidra serve com o Ghidra 12 — as alternativas da
  §2 não foram avaliadas;
- **não** mediu estabilidade, latência nem determinismo de servidor nenhum;
- **não** afirma nada sobre pixels, boot ou comportamento do jogo: zero boots, zero código.

## Known Stubs

Nenhum. A entrega é documental — um ficheiro novo, um acrescento de 8 linhas e uma entrada de
ledger. Nenhum ficheiro de código foi alterado.

## Windows abertas registadas

| id | kind | ficheiro | o que fica por fazer |
|----|------|----------|----------------------|
| 15 | unrun-verify | `docs/GHIDRA_MCP_SETUP.md` | correr o lado do MCP da paridade (`get_xrefs_to` em `0x0041F700` e `0x00420A44` sobre o EBOOT `sha256 23cfd435…163b`) e comparar com os conjuntos da §9.2 |

## Threat Flags

Nenhuma superfície nova aberta por esta entrega. Duas notas de segurança **documentadas** por
serem propriedades do tooling externo, não deste commit:

| nota | onde |
|---|---|
| o servidor HTTP do plugin GhidraMCP **não tem autenticação** e aceita `POST` que renomeiam/comentam na base de dados do Ghidra → manter em `127.0.0.1`, desligar quando não usado | §4.3 |
| não existe `.mcp.json` neste repo e o `.gitignore` só cobre `.claude/` → quem criar um ficheiro de config de MCP na raiz tem de o ignorar **antes** de o criar | §4.3 |

## Next Phase Readiness

- **Fase 11 (paredes):** ganha uma segunda âncora com callers exactos (`0x00420A44` ←
  `0x00254C40`) na zona do typemap, e a garantia medida de que `0x0041F700` não tem callers
  directos — o que reforça, sem o provar, o mecanismo candidato da sessão de 20-02.
- **Fase 21 (antes/depois):** o custo do lado do CLI da paridade é ~2 min por EA, offline; o
  lado do MCP continua por medir e **não** deve entrar na tabela como se tivesse sido.
- **Bloqueadores:** nenhum. O `RE-03` era soft por desenho e a Track R já estava fechada pelo
  `RE-02`.

## Self-Check: PASSED

- `docs/GHIDRA_MCP_SETUP.md` — FOUND
- `docs/RE_PLAYBOOK_GOW2.md` — FOUND (modificado, +8 linhas em §3.1)
- commit `39c5a94` (setup e política) — FOUND
- commit `04f5a78` (paridade / `wont`) — FOUND
- commit `b09ef6b` (janela 15) — FOUND
- `.planning/WINDOWS.md` — entrada 15, `total_count: 15`, `open_count: 12`
- deleções nos commits desta sessão: **nenhuma**
- `tools/ghidra_lookup.py` — **intocado** (`git diff --stat` vazio)
- ficheiros proibidos e do plano 20-01 — **fora dos commits**, verificado por `--name-status`

---
*Phase: 20-o-oraculo-e-o-metodo*
*Completed: 2026-08-02*
