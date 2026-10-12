---
phase: 20-o-oraculo-e-o-metodo
plan: 02
subsystem: reverse-engineering
tags: [ghidra, ghidra_lookup, playbook, re-session, gow2, ppu-lift, typemap, wall-4]

requires:
  - phase: 11-a-parede-real
    provides: "inventário das 4 paredes com medição fresca (2026-08-01/02) e a lista do que já está ELIMINADO por medição"
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "verify_lift.sh / apply_all_patches.sh como portões por delta, e o vocabulário de níveis de evidência"
provides:
  - "docs/RE_PLAYBOOK_GOW2.md — método de ataque a uma parede, 6 secções, comandos reais e testados"
  - "docs/re_sessions/ — a pasta de ledgers de sessão, com a primeira página escrita"
  - "3 refutações medidas sobre a parede 4 (func_0041F700), incluindo a 5.ª suspeita de bug do lifter refutada"
  - "um mecanismo candidato medido que liga as paredes 1, 3 e 4 a uma única causa (índice low16 vs subtag na tabela de tipos)"
  - "uma limitação medida do tools/ghidra_lookup.py --writes (falso negativo em offsets decimais)"
affects: [fase-11-paredes, fase-21-antes-depois, RE-03-mcp-paridade]

tech-stack:
  added: []
  patterns:
    - "Ledger de sessão de RE obrigatório em docs/re_sessions/, com as secções 'O que esta sessão NÃO afirma' e 'Próximo experimento'"
    - "Comparação sistemática Ghidra (binário original) ↔ função liftada como discriminador entre 'o jogo é assim' e 'o lifter partiu isto'"
    - "Reimplementar ppc_rlwinm/ppc_rldicl a partir de tools/ppu_lifter.py para verificar máscaras numericamente, em vez de as ler à vista"

key-files:
  created:
    - docs/RE_PLAYBOOK_GOW2.md
    - docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md
  modified:
    - .planning/WINDOWS.md

key-decisions:
  - "Parede escolhida por CUSTO e não por ambição: parede 4 (func_0041F700) — EA único já medido, sem gate de diagnóstico a contaminar a evidência, e atacável 100% offline (zero boots)"
  - "Sessão deliberadamente offline: nenhuma corrida do boot, portanto zero risco de repetir o incidente de load 75 e zero logs a caminho do git"
  - "Veredicto ABERTO assumido em vez de um fecho cosmético — três refutações medidas valem mais que uma promessa de vitória"
  - "O ponteiro no CLAUDE.md NÃO foi escrito: o ficheiro está modificado por outra sessão activa (regra 7). A nota para agentes ficou no cabeçalho do próprio playbook"
  - "RE-02 declarado FECHADO no SUMMARY, mas REQUIREMENTS.md não foi editado — evitar escrita concorrente com o plano 20-01 (RE-01)"

patterns-established:
  - "Pergunta de wall tem de distinguir dois mundos; se não distingue, é um desejo disfarçado"
  - "Todo o achado leva rótulo de nível de evidência: in-boot / offline-unit / não-exercitado"
  - "Um gate de diagnóstico NUNCA fecha uma parede — fica OFF e serve só para comparação"

requirements-completed: [RE-02]

coverage:
  - id: D1
    description: "docs/RE_PLAYBOOK_GOW2.md com as seis secções mínimas (pergunta de wall, âncoras, ferramentas, hipótese→experimento discriminador, aceite medido, anti-padrões)"
    requirement: RE-02
    verification:
      - kind: other
        ref: "grep -nE 'pergunta de wall|Âncoras|Ferramentas|Hipótese|Aceite medido|Anti-padrões' docs/RE_PLAYBOOK_GOW2.md → 6 secções presentes"
        status: pass
      - kind: other
        ref: "grep -nE 'CRC bypass|LLM a reescrever|pkill' docs/RE_PLAYBOOK_GOW2.md → anti-padrões da regra 5 + reescrita de lift por LLM"
        status: pass
      - kind: other
        ref: "comandos do playbook testados: ghidra_lookup (4 formas), grep de g_trampoline_fn (21251 ocorrências), grep -ln do func_ liftado"
        status: pass
    human_judgment: false
  - id: D2
    description: "Playbook USADO uma vez numa parede real, com página de ledger, hipóteses refutadas por medição e veredicto honesto"
    requirement: RE-02
    verification:
      - kind: other
        ref: "docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md — 4 hipóteses, 3 refutadas, 10 comandos com rc, veredicto ABERTO"
        status: pass
    human_judgment: true
    rationale: "Se a sessão é RE a sério ou teatro escrito a posteriori é julgamento humano; o que a automação prova é que os comandos correram e que as máscaras batem"

metrics:
  duration: "~50 min"
  completed: 2026-08-02
  tasks: 2
  files_created: 2
  commits: 3

status: complete
---

# Phase 20 Plan 02: O playbook de RE, e a sessão que o prova — Summary

Playbook de reverse-engineering do GoW2 escrito **e exercitado uma vez** na parede 4
(`func_0041F700`): três hipóteses caíram por medição estática, a parede continua
**aberta**, e a sessão devolve um mecanismo candidato que liga as paredes 1, 3 e 4 à
mesma causa.

## Accomplishments

- **`docs/RE_PLAYBOOK_GOW2.md`** (303 linhas) com as seis secções obrigatórias, todos os
  comandos **testados** antes de entrarem no documento, e duas limitações do tooling
  medidas durante a sessão em vez de assumidas.
- **`docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md`** — a primeira sessão real,
  100% offline, com 10 comandos e respectivos `rc`, 4 hipóteses tabeladas e veredicto
  honesto.
- **Cinco suspeitas de bug do lifter agora refutadas** contra o binário (eram 4 no
  ROADMAP da Fase 11; esta sessão acrescenta a 5.ª).
- **Uma terceira hipótese para o critério 1 da Fase 11**, que só tinha duas.
- **Três janelas abertas** registadas em `.planning/WINDOWS.md` (ids 12, 13, 14).

## Task by Task

### Task 1 — o playbook (commit `4d4a68b`)

As seis secções, em português, com comandos copiáveis:

1. **Pergunta de wall** — tabela ✅ pergunta / ❌ desejo disfarçado, com os quatro
   exemplos vivos (wall D, paredes 1/2/4). Instrução explícita de **ler primeiro a lista
   *"O que já está ELIMINADO por medição"*** do ROADMAP: reabrir algo de lá sem evidência
   nova não demonstra o método, demonstra que ninguém leu o ledger.
2. **Âncoras** — EA guest / símbolo Ghidra / tag de log, mais as convenções do guest que
   já custaram conclusões erradas: a base `obj` vs `obj+4` (é por isso que o mesmo campo
   é `+0x7C` numa função e `+0x80` noutra), as duas famílias distinguidas pelo `w0`, e o
   facto de um `*(ushort*)(obj+6)` em big-endian serem os 16 bits **baixos** da palavra
   em `obj+4` — não um campo novo.
3. **Ferramentas** — `ghidra_lookup` nas 4 formas, `analyze_eboot_ghidra.sh` (com o custo
   declarado: dezenas de minutos, correr só se stale), `verify_lift.sh` + `VERIFY_ORACLE`
   (opt-in, default inalterado), a auditoria de fallthrough por `g_trampoline_fn`, e o
   smoke com **kill por PID**.
4. **Hipótese → experimento discriminador** — o requisito de sinais previstos
   **diferentes** conforme a hipótese seja verdadeira ou falsa, com exemplo preenchido, e
   as práticas que já pouparam dias (uma variável que parte o mundo ao meio; sonda de
   CONTROLO em cada bissecção; suspeitar de nós primeiro; timebox de ~2h).
5. **Aceite medido** — os três níveis (`in-boot` / `offline-unit` / `não-exercitado`) e a
   ordem de preferência de evidência, com a prova por mutação em terceiro lugar
   (injectar o defeito → `rc=1`; reverter → `rc=0`; sem isso um gate verde é decorativo).
6. **Anti-padrões** — 13 itens nomeados, incluindo os sete da regra 5 do `CLAUDE.md`,
   **"LLM a reescrever um chunk liftado"**, "gate de diagnóstico a fechar uma parede",
   `pkill -f boot_gow2`, `git add -A` e "forjar o aceite".

Mais uma secção 7 com o esqueleto obrigatório do ledger, onde **"O que esta sessão NÃO
afirma"** e **"Próximo experimento"** não são opcionais: a primeira impede o ledger de
inflacionar com o tempo, a segunda faz a sessão seguinte começar onde esta acabou.

### Task 2 — a sessão (commit `7670d75`)

**Parede escolhida:** 4 — `func_0041F700`, 27 539 083 iterações (99,96% do rasto).
Escolhida por custo: EA único já medido por outra sessão, **única parede sem gate de
diagnóstico** (logo sem paliativo nosso a contaminar a evidência) e atacável inteiramente
offline com o `ghidra_out/` e o lift em disco.

**Pergunta:** *porque é que `func_0041F700` executa 27,5 M iterações se a poda
`child_tag == parent_tag` devia limitar o walk?*

**Refutado por medição** (tudo `offline-unit`):

| # | hipótese | como caiu |
|---|---|---|
| H1 | o lifter perdeu a memoização | o C do Ghidra do original **não tem** visited-set; e as duas máscaras batem bit a bit com o lift — `ppc_rlwinm(w0,2,14,29)` == `(w0 & 0xFFFF)*4` e `ppc_rldicl(w0,48,52)` == `(w0>>16)&0xFFF`, verificado numericamente com as definições copiadas de `tools/ppu_lifter.py:3491,3531` |
| H2 | `func_0041F700` é auto-recursiva | nem o original nem o lift contêm chamada a si próprio; o ciclo fecha por `vtable+0x40` (`ps3_indirect_call`). As "27 M iterações" são **re-entradas**, não voltas de um `for`. Logo um limite dentro da função nem sequer estaria na moldura certa |
| H3 | havia um limite de profundidade que o lift perdeu | o par push/pop existe **igual** nos dois (`0x0041F700` incrementa `mgr+0xC8`, `0x0041F924` decrementa). Nada perdido — mas a comparação revelou o mecanismo candidato |

**Mecanismo candidato (medido estaticamente, por confirmar in-boot):** a tabela de tipos
`PTR_DAT_0053ef1c` é escrita por **uma única** função (`0x0041FA90`, provado por
`--grep` de escritas: 1 resultado em 18 consumidores), indexada por
`(w0>>16) & 0xFFF`. Mas **12 dos 18 consumidores** — incluindo o push `0x0041F700` **e a
parede 1 `func_002545B0`** — indexam por `w0 & 0xFFFF`. Os dois campos coincidem em
`0xC0010001` (1 == 1) e **divergem** em `0x40030001` (1 vs 3). Consequência: um registo
WAD é empurrado para o gestor 1 e devolvido ao gestor 3, o cursor do gestor 1 nunca
desce, e o gestor 1 lê `arg+0x7C` como sentinela de lista num objecto cujo `+0x7C` é
`matriz[0][3]` — que é exactamente o sintoma da parede 1.

Isto acrescenta uma **terceira hipótese** ao critério 1 da Fase 11, que só listava duas
("ou o tag do objecto está errado, ou `tab[1]` despacha para a fábrica errada"): o objecto
tem **dois** campos de tag e os dois lados do walk escolhem campos diferentes.

**Veredicto: ABERTO.** Nada corrigido, nenhum gate ligado, nenhum boot corrido.

## Deviations from Plan

### 1. [Rule 3 — bloqueio de coordenação] `CLAUDE.md` não foi editado

- **Encontrado em:** Task 1.
- **Situação:** o plano lista `CLAUDE.md` em `files_modified` (ponteiro M4.4-lite para o
  playbook). O ficheiro está **modificado no working tree por outra sessão activa** e
  editá-lo arriscava commitar trabalho alheio — regra 7 do `CLAUDE.md`.
- **Resolução:** a nota para agentes ficou escrita no **cabeçalho do próprio playbook**
  ("Para agentes (Claude/Codex/Grok)…"). Registado como janela aberta id **12**.

### 2. [decisão de coordenação] `REQUIREMENTS.md` não foi marcado

- **Situação:** a Fase 20 tem três requisitos (`RE-01`, `RE-02`, `RE-03`) na mesma tabela
  do `REQUIREMENTS.md`, e o plano 20-01 (`RE-01`) corre **em paralelo** nesta mesma
  árvore. Uma escrita concorrente ao mesmo ficheiro perderia trabalho.
- **Resolução:** `RE-02` é declarado fechado **aqui** (ver abaixo), com os dois links; a
  marcação no `REQUIREMENTS.md` fica para quem reconciliar a fase.

### 3. Ficheiro do ledger renomeado

`docs/re_sessions/2026-08-02-wall-SESSION.md` (placeholder do plano) →
`docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md`, conforme o próprio plano previa
("planning placeholder name may be renamed — update SUMMARY").

### 4. Verbos GSD não usados (instrução do utilizador)

`roadmap.update-plan-progress` e todos os `state.*` ficaram por correr, por instrução
explícita. `.planning/ROADMAP.md`, `PROJECT.md`, `MILESTONES.md`, `config.json`,
`games/gow2/CLAUDE.md`, `scripts/gsd_plan*.sh` e os ficheiros do plano 20-01
(`verify_lift.sh`, `oracle_manifest.py`, `ORACLE_BASELINE.json`) ficaram **intactos e
fora dos commits** — confirmado no `git status` de cada commit.

## Known Stubs

Nenhum. Não há código nesta entrega — são dois documentos e três entradas de ledger.

## Requirements

**`RE-02` — FECHA.** Critério: *"`docs/RE_PLAYBOOK_GOW2.md` existe **e foi usado uma
vez** numa wall real; 1 página de ledger com hipóteses refutadas por medição e o
veredicto — 'ainda aberto' é resultado válido, fecho forjado não."*

| exigência | prova |
|---|---|
| o playbook existe, com as 6 secções | `docs/RE_PLAYBOOK_GOW2.md` §1–§6 |
| foi **usado** uma vez numa wall real | `docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md`, parede 4 do inventário da Fase 11 |
| hipóteses refutadas **por medição** | 3 de 4 refutadas, cada uma com comando e observação, nível `offline-unit` |
| veredicto honesto | **ABERTO**, com secção "O que esta sessão NÃO afirma" com 6 não-afirmações explícitas |

Ressalva declarada: o `RE-02` no `REQUIREMENTS.md` diz *"wall real (D ou C2)"*. Foi
atacada a **parede 4 da Fase 11**, que o `20-CONTEXT.md` autoriza expressamente
("as quatro paredes da Fase 11 … são candidatos legítimos") e que o plano 20-02 lista
como opção 2 na ordem recomendada. A escolha foi por custo, como o CONTEXT manda.

## Threat Flags

Nenhuma. A entrega é documental; não abre endpoints, não toca autenticação, não altera o
comportamento do lift, do `verify_lift.sh` nem do runtime. Nenhum ficheiro de produção
foi tocado.

## Windows abertas registadas

| id | kind | ficheiro | o que fica por fazer |
|----|------|----------|----------------------|
| 12 | deviation | `CLAUDE.md` | acrescentar o ponteiro de 3–4 linhas para o playbook quando a outra sessão fechar |
| 13 | unrun-verify | `docs/re_sessions/2026-08-02-…md` | correr o experimento **E1** (sonda `PS3_TYPEWALK_TRACE=1`) que confirma ou mata o mecanismo candidato — Fase 11 |
| 14 | deviation | `tools/ghidra_lookup.py` | `--writes` só constrói padrões em hexadecimal; dá falso negativo quando o Ghidra emite o offset em decimal |

## Para a Fase 11 (quem retomar as paredes)

Não voltar a perseguir o lifter na parede 4 — está refutado. O próximo passo é **E1**, o
experimento discriminador já escrito no ledger com as duas previsões opostas. E se E1
confirmar, o fix é **a montante** (o produtor do registo WAD, ou quem o enfileira na
lista de irmãos), **nunca** um limite dentro de `func_0041F700` nem um quarto gate.

## Para a Fase 21 (tabela antes/depois)

O tempo desta sessão, ponta a ponta, do "que faz `0x0041F700`?" às três refutações
medidas: **~25 minutos**, sem um único boot. O baseline que substitui é `grep` em 1,3 GB
de C anónimo com 56 072 funções chamadas `func_XXXXXXXX`.

## Self-Check: PASSED

- `docs/RE_PLAYBOOK_GOW2.md` — FOUND
- `docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md` — FOUND
- commit `4d4a68b` (playbook) — FOUND
- commit `7670d75` (sessão) — FOUND
- `.planning/WINDOWS.md` — 3 entradas novas (12, 13, 14), `total_count: 14`
- deleções nos commits desta sessão: **nenhuma**
- ficheiros do plano 20-01 e `.planning/ROADMAP.md`/`PROJECT.md`/`MILESTONES.md`/
  `CLAUDE.md`: **fora dos commits**, verificado no `git status` de cada um
