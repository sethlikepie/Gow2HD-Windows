---
phase: 16-invent-rio-de-patches-e-baseline-medido
plan: 02
subsystem: tooling
tags: [tsv, ledger, patches, migracao, p0, contratos, gow2]

requires:
  - phase: 16-01 (esqueleto do ledger + gen_migration_ledger.py)
    provides: PATCH_MIGRATION.tsv com 7 colunas LEDG-01 e --merge idempotente
provides:
  - 19 linhas do ledger com as 4 colunas humanas decididas (7 P0 / 11 P1 / 1 P2)
  - criterio de escolha e criterio P0 escritos DENTRO do proprio ledger
  - pilotos das Fases 17 (mid-asm) e 18 (jump table) nomeados e derivados
  - 18 dos 19 aceites exercitados verdes contra o lift de producao
affects: [16-03 (baseline medido), 17 (piloto mid-asm), 18 (jump table), 19 (weak/OPD), 21 (metrica antes/depois)]

tech-stack:
  added: []
  patterns:
    - "contagem de decisao humana e' DERIVADA das colunas humanas, nunca um comentario escrito a mao"
    - "aceite so' entra no ledger depois de ser CORRIDO contra o lift de producao"

key-files:
  created:
    - .planning/phases/16-invent-rio-de-patches-e-baseline-medido/16-02-SUMMARY.md
  modified:
    - games/gow2/lift_baseline/PATCH_MIGRATION.tsv
    - games/gow2/lift_baseline/PATCH_CATALOG.tsv
    - games/gow2/lift_baseline/gen_migration_ledger.py
    - games/gow2/lift_baseline/test_gen_migration_ledger.py

key-decisions:
  - "`ea='-'` limita a P1 mesmo quando o corpo do script nomeia a funcao: `ea` e' coluna derivada e escreve-la a mao quebraria o regen (icg_ctor_opd)"
  - "O bloco de decisao do cabecalho e' gerado por render_tsv(), nao escrito a mao -- so' assim o cabecalho e o regen idempotente podem coexistir"
  - "Regenerar PATCH_CATALOG.tsv era a unica saida: o corpus de build passou de 139 para 140 durante a sessao e o gerador aborta (por desenho) com catalogo desactualizado"
  - "patch_ba808_wadld_eof.py fica alvo=keep-probe apesar de classe=CORRECTNESS: o corpo do script declara-se probe puro"

requirements-completed: []  # LEDG-01 continua PARCIAL: falta o baseline medido (16-03)

duration: ~50min
completed: 2026-08-02
status: complete
---

# Fase 16 Plano 02: decisao humana no ledger de migracao

**19 das 140 linhas do ledger passaram a ter alvo/prioridade/aceite/estado decididos, com 7 P0 de EA confirmada, e 18 dos 19 aceites foram CORRIDOS verdes contra o lift de producao — o criterio de escolha ficou escrito dentro do proprio ficheiro e e' recontado a cada regen.**

## Performance

- **Duracao:** ~50 min
- **Tasks:** 2/2
- **Ficheiros modificados:** 4 (nenhum criado alem do SUMMARY)
- **Commits:** 2 (`7b8aa51`, `3b42a6a`)

## Numeros REAIS medidos (2026-08-02)

| Metrica | Valor | Comando / evidencia | Nivel de evidencia |
|---|---|---|---|
| Linhas classificadas | **19** | gate do plano (`classified=19`) | offline-unit |
| P0 validos | **7** | gate do plano (`p0=7`) | offline-unit |
| P1 / P2 | **11 / 1** | cabecalho derivado do TSV | offline-unit |
| `estado=migrated` | **0** | `grep -c '\tmigrated\t'` | offline-unit |
| Aceites `contract:` corridos | **11/11 rc=0** | `check_contracts.py CONTRACTS.tsv ../gow2-recomp/recomp_macos_v2 --patch <p>` | offline-unit |
| Aceites `grep:` presentes | **7/7 n>=1** | `grep -F -c` sobre `recomp_macos_v2/ppu_recomp_*.cpp` | offline-unit |
| Aceite `smoke:` | **1, POR MEDIR** | precisa de boot (plano 16-03) | **nao-exercitado** |
| Regen `--merge` idempotente | **sim, byte-a-byte** | 2x `gen_migration_ledger.py --merge` + `diff -q` | offline-unit |
| `test_gen_migration_ledger.py` | **rc=0, 8 PASS** | corrida directa | offline-unit |
| `test_gen_catalog.py` | **rc=0** | corrida directa | offline-unit |
| Corpus de build | **140** (era 139 no 16-01) | `ls ../gow2-recomp/recomp_mid_v2/patch_*.py \| wc -l` | offline-unit |
| Espelho monorepo | **140** (era 88 no 16-01) | `ls games/gow2/recomp_mid_v2/patch_*.py \| wc -l` | offline-unit |

## As 19 linhas classificadas

### P0 (7) — classe CORRECTNESS/OPD **E** caminho de boot medido **E** `ea` confirmada

| patch | classe | ea | alvo | aceite | aceite corrido hoje |
|---|---|---|---|---|---|
| `patch_fallthrough_2550c8.py` | CORRECTNESS | 0x002550C8 | midasm | `smoke:bytes_read=20169344` | **nao-exercitado** |
| `patch_jumptable_2a209c.py` | CORRECTNESS | 0x002A209C | switch_table | `grep:case 0x002A27BCu: goto loc_002A27BC;` | n=3 |
| `patch_jumptable_2b11b8.py` | CORRECTNESS | 0x002B11B8 | switch_table | `grep:case 0x002B12C4u: goto loc_002B12C4;` | n=1 |
| `patch_ce03c_introseq_block.py` | CORRECTNESS | 0x000CE03C | midasm | `grep:[INTROSEQ] CE03C wait-idle 1st movie` | n=1 |
| `patch_factory_opd_gate.py` | OPD | 0x0039E6B4 | weak | `grep:[WADLD-FACT] #A` | n=1 |
| `patch_32e200_opd.py` | OPD | 0x0032E200 | weak | `contract:opd-32e200` | rc=0 |
| `patch_14b1f0_opd.py` | OPD | 0x0014B1F0 | weak | `contract:opd-14b1f0` | rc=0 |

### P1 (11)

| patch | classe | ea | alvo | aceite | corrido |
|---|---|---|---|---|---|
| `patch_14a01c_opd.py` | OPD | 0x0014A01C | weak | `contract:opd-14a01c-a` | rc=0 |
| `patch_host_res_inflate.py` | CORRECTNESS | 0x001E7B50 | **runtime** | `contract:tag-hostres-enter` | rc=0 |
| `patch_icg_ctor_opd.py` | OPD | **-** | weak | `contract:tag-icg-vcall` | rc=0 |
| `patch_ce03c_movie_done_reset.py` | CORRECTNESS | 0x000CE03C | midasm | `grep:movie_done_timebased_reset` | n=2 |
| `patch_39e5d8_opd.py` | OPD | 0x0039E5D8 | weak | `contract:tag-wadld-fin` | rc=0 |
| `patch_groupend_opd.py` | OPD | 0x002B0AA8 | weak | `contract:tag-wadld-gend` | rc=0 |
| `patch_opdisp_entry.py` | OPD | 0x002A209C | weak | `contract:tag-opdisp-ent` | rc=0 |
| `patch_1856a8_stream_opd.py` | OPD | 0x001856A8 | weak | `contract:tag-stream-opd` | rc=0 |
| `patch_wad_state_machine.py` | CORRECTNESS | 0x002BA76C | weak | `contract:tag-wadld-call` | rc=0 |
| `patch_factory_block_ty15_install.py` | CORRECTNESS | 0x0039E794 | midasm | `contract:tag-factory` | rc=0 |
| `patch_b71_2f3f0_guard.py` | CORRECTNESS | 0x0002F3F0 | midasm | `grep:[B71] 2F3F0 bad str=` | n=1 |

### P2 (1)

| patch | classe | ea | alvo | aceite | corrido |
|---|---|---|---|---|---|
| `patch_ba808_wadld_eof.py` | CORRECTNESS | **-** | keep-probe | `grep:[BA808]` | n=1 |

## Criterio de escolha (o que decidiu QUAIS 19)

Esta escrito tambem no cabecalho do proprio TSV, para nao viver so' aqui.

**Universo elegivel** — uma linha so' entra se cair num destes tres baldes:

1. **(a) Nomeada pelo ROADMAP como de maior impacto**: fallthrough `2550C8`,
   jumptables `2A209C`/`2B11B8`, `CE03C`, factory OPD / ICG ctor, host inflate.
   Sao as 6 e todas entraram.
2. **(b) Toda a familia `classe=OPD` com EA confirmada.** E' a unica familia com
   contratos JA' escritos em `CONTRACTS.tsv`, logo com `aceite` executavel **hoje**
   e nao "quando alguem escrever o contrato". 10 das 10 linhas OPD entraram.
3. **(c) Linhas `CORRECTNESS` cujo marcador tem dono unico em `CONTRACTS.tsv`**
   (`tag-factory`, `tag-wadld-call`) **ou cujo marcador exclusivo do script foi
   medido presente** no lift de producao (`[B71] 2F3F0 bad str=`, `[BA808]`).

**Criterio P0** — os TRES do 16-CONTEXT, nao um:
`classe in {CORRECTNESS,OPD}` **E** toca o caminho de boot medido (intro → WAD →
typemap) **E** `ea` **confirmada** (`0x…`).

**Desempate** dentro do elegivel: quem ja' tem aceite executavel hoje (contrato, ou
marcador de dono unico medido) passa a' frente de quem so' teria um `grep` generico.

**Nenhuma linha foi escolhida por "parecer importante"**: as 19 sao a uniao fechada
de (a) ∪ (b) ∪ (c). O que ficou de fora ficou por nao caber em nenhum dos tres.

## Como confirmei a EA de cada P0

A coluna `ea` e' **derivada** (`gen_migration_ledger.py`), por isso nao a escrevi.
O que fiz foi **auditar cada uma contra o corpo do script**, para nao promover a P0
uma EA que a heuristica tivesse adivinhado:

| P0 | EA | Onde o corpo do script a declara |
|---|---|---|
| `patch_fallthrough_2550c8.py` | 0x002550C8 | `src.find("void func_002550C8")` — `patch_fallthrough_2550c8.py:63` |
| `patch_jumptable_2a209c.py` | 0x002A209C | `FUNC_SIG = "void func_002A209C(ppu_context* ctx) {"` — `:32` |
| `patch_jumptable_2b11b8.py` | 0x002B11B8 | docstring `func_002B11B8` + `OLD_RE` ancorada em TOC-0x151C — `:2`, `:48` |
| `patch_ce03c_introseq_block.py` | 0x000CE03C | `SIG = "void func_000CE03C(ppu_context* ctx) {\n"` — `:48` |
| `patch_factory_opd_gate.py` | 0x0039E6B4 | `region_of(s, "func_0039E6B4")` — `:132`; e o `main()` aborta se `void func_0039E6B4(ppu_context` faltar — `:240,:248` |
| `patch_32e200_opd.py` | 0x0032E200 | docstring/alvo do proprio patch — `:3`; ancora tambem no contrato `opd-32e200` (`escopo=ea 0x0032E200`) |
| `patch_14b1f0_opd.py` | 0x0014B1F0 | docstring/alvo — `:3`; contrato `opd-14b1f0` (`escopo=ea 0x0014B1F0`) |

Nos dois ultimos a EA e' ainda **corroborada por medicao independente**: o contrato
tem `escopo=ea` e o `check_contracts.py` recorta a regiao a partir de
`void func_0032E200(` / `void func_0014B1F0(` no lift — se a EA estivesse errada, a
regiao seria outra e o contrato falharia. Deu `rc=0` nos dois.

## Como cada aceite foi validado

- **11 `contract:`** — corridos um a um:
  `check_contracts.py games/gow2/lift_baseline/CONTRACTS.tsv ../gow2-recomp/recomp_macos_v2 --patch <patch>` → **rc=0 nos 11**.
- **7 `grep:`** — contados com `grep -F -c` sobre `recomp_macos_v2/ppu_recomp_*.cpp`
  (o lift que o `build_macos.sh` consome): **todos n>=1** (tabela acima).
  Os padroes nao foram inventados: saem do proprio script que instala o efeito
  (`MARKER`/`FUNC_SIG`), ou da tabela de alvos que o script usa para se
  auto-verificar (o `case 0x002A27BCu:` e' `JT_BASE 0x2A2134 + 0x688`, o primeiro
  offset de `JT_OFFSETS`, exactamente o que `lifter_already_resolved()` testa em
  `patch_jumptable_2a209c.py:172`).
- **1 `smoke:`** — `smoke:bytes_read=20169344` para o `2550C8`. **Nao-exercitado
  nesta fase**: exige um boot. Nao e' um numero inventado — a fonte e'
  `patch_fallthrough_2550c8.py:15` ("Aceite medido: bytes_read=20.169.344"),
  corroborada por `patch_fios_stream_pump.py:14` e pela tabela da parede [C] do
  `CLAUDE.md`; o emissor real da metrica e' `libs/video/movie_hle.c:476`
  (`[movieio] trace stats … bytes_read=%llu`) e `:730` (`[GATE-FORCE] R_PermA full
  flagged (bytes_read=…)`). Fica para o plano 16-03 medir.

## Pilotos nomeados (derivados, no cabecalho do TSV)

- **Fase 17 (mid-asm):** `patch_ce03c_introseq_block.py`, `patch_fallthrough_2550c8.py`
- **Fase 18 (jump table):** `patch_jumptable_2a209c.py`, `patch_jumptable_2b11b8.py`

Nao sao uma opiniao escrita a mao: sao a lista dos P0 com `alvo=midasm` e com
`alvo=switch_table`, recalculada pelo gerador a cada regen.

## Achados (medidos, com consequencia)

### 1. O corpus mexeu-se DURANTE a sessao — 139 → 140, e o espelho foi reconciliado

Entre o 16-01 (`45c6e55`, 16:23) e a Task 1 deste plano entrou o commit
**`7f7581d` (16:35) — `merge(subtree): sincroniza games/gow2 com o espelho gow2-recomp`**,
de **outra sessao**. Efeito medido:

| | 16-01 (16:23) | agora | delta |
|---|---|---|---|
| corpus de build | 139 | **140** | +`patch_freelist_guard_262610.py` |
| espelho monorepo | 88 | **140** | +52 |
| divergencia | 51 | **0** | a divida do v1.0 deixou de aparecer na contagem |

Consequencia: `gen_migration_ledger.py` **abortou** (por desenho) com
`1 patch(es) do corpus de build ausentes do catalogo`. Ver desvio 1.

**O que isto NAO prova:** que a divida do subtree esteja paga. A medicao e' de
**contagem de ficheiros**, nao de conteudo — por isso o cabecalho passou a dizer
exactamente isso quando a divergencia e' 0, em vez de continuar a afirmar uma
divida do v1.0 que a medicao ja' nao ve.

### 2. `patch_ba808_wadld_eof.py` esta' classificado como FUNCIONAL mas declara-se probe

O catalogo poe-lhe `FUNCIONAL/wadld`, mas o docstring do script diz literalmente
*"[BA808] — probe do loop de espera da state machine WADLD"* e *"Um unico bloco de
diagnostico no topo de func_002BA808"* (`patch_ba808_wadld_eof.py:1-6`). Nao mexi na
classe (e' derivada); pus-lhe `alvo=keep-probe`, `P2`. **Isto e' um falso negativo do
ramo 3 do `gen_catalog.py`** (o nome nao termina em `_probe.py`/`_trace.py`), nao um
erro do ledger. Fica registado para quem tocar no `gen_catalog.py` — nao corrigido
aqui porque mudar a heuristica reclassificaria o corpus inteiro no meio da fase.

### 3. `patch_icg_ctor_opd.py` tem EA confirmavel mas nao pode ser P0

O corpo nomeia `func_00151248` (`:198`) e `func_00329490`/`func_0032854C`/
`func_00328B18` (`:200`) — mas a coluna derivada `ea` da' `-`, porque a heuristica
so' olha para o **primeiro** `void func_…` do corpo e este script nao tem nenhum
(ancora em regex de blocos OPD). **Nao escrevi a EA a mao**: o regen recalcularia
`-` e a linha deixaria de cumprir o criterio P0 no dia seguinte — um P0 com prazo de
validade e' pior do que um P1 honesto. Fica **P1** com o motivo escrito no cabecalho.
Follow-up natural (fora deste plano): dar ao `detect_ea()` um 7.º ramo que leia listas
de nomes de funcao passadas a `patch_funcs(...)`.

### 4. A ordem de escolha do plano nao aguentou o teste dos dados em 3 casos

- `patch_host_res_inflate.py` → **`alvo=runtime`** (obrigatorio pelo plano e correcto:
  o script instala `if (host_res_inflate(ctx)) return;`, isto e', desvia para HLE de host).
- `patch_wad_state_machine.py` → **`weak`**, nao `midasm`: o mandato de correccao do
  script e' *"switch OPD dispatch to ps3_call_opd"* (`:11`); o resto e' instrumentacao.
  **Ressalva escrita:** o `ea` derivado (`0x002BA76C`, a state machine) **nao** e' o
  sitio do OPD (`func_002B9C80`) — quem pegar nesta linha na Fase 19 tem de reancorar.
- `patch_ce03c_movie_done_reset.py` → **`midasm`**, nao `runtime`: o helper de runtime
  (`movie_done_timebased_reset`) **ja' existe**; o que falta e' o **call site** dentro
  da funcao guest.

## Desvios do plano

### 1. [Regra 3 — bloqueio] `PATCH_CATALOG.tsv` teve de ser regenerado

- **Encontrado em:** Task 2, ao correr `test_gen_migration_ledger.py`.
- **Problema:** o corpus de build ganhou `patch_freelist_guard_262610.py` (achado 1) e
  `build_rows()` **aborta** quando um patch do corpus nao esta' no catalogo. Sem
  catalogo actualizado nao havia regen — e sem regen nao havia cabecalho derivado nem
  prova de idempotencia, os dois pedidos explicitamente.
- **Fix:** regenerar com o **proprio gerador do catalogo**, que e' literalmente a
  recuperacao que a mensagem de erro prescreve:
  `gen_catalog.py ../gow2-recomp/recomp_mid_v2 …/MANIFEST.tsv > …/PATCH_CATALOG.tsv`.
  O plano proibia alterar o catalogo **a mao** — nao regenera-lo pelo seu gerador.
- **Verificacao (feita ANTES de escrever por cima):** gerei para `/tmp` e comparei.
  `diff` das linhas de dados = **exactamente +1 linha**
  (`patch_freelist_guard_262610.py ESCREVE FUNCIONAL guard 0 subclasse=guard [FREELIST-TAG-GUARD]`),
  **nenhuma outra linha mudou**. `test_gen_catalog.py` rc=0.
- **Commit:** `3b42a6a`.

### 2. [Regra 3 — bloqueio] O cabecalho pedido e a idempotencia eram incompativeis a mao

- **Problema:** o plano manda acrescentar `# classificacao humana … N_P0=…` e
  `# piloto sugerido Fase 17…` ao cabecalho, **e** manda provar que
  `gen_migration_ledger.py --merge` e' idempotente. `render_tsv()` reconstroi o
  cabecalho inteiro a cada corrida: qualquer linha `#` escrita a mao seria **apagada**
  no primeiro regen — a prova de idempotencia falharia, ou o cabecalho nao existiria.
- **Fix:** as linhas passaram a ser **geradas**. Acrescentei a
  `gen_migration_ledger.py` as funcoes puras `linha_classificada()` e
  `contar_classificacao()` (a regra de "classificada" e' **a mesma** do gate do plano,
  para os dois numeros nunca divergirem) e o bloco de cabecalho com
  `N_classificados/N_P0/N_P1/N_P2`, o criterio, e os pilotos derivados dos P0 por `alvo`.
- **Porque e' legitimo:** e' o padrao que o proprio projecto ja' fixou — `classe` e `ea`
  sao derivadas exactamente para nao apodrecerem. Um `N_P0=7` escrito a mao seria falso
  na primeira linha nova.
- **Custo:** o plano listava so' o TSV em `files_modified`; toquei tambem no gerador e
  no teste. Nada em `runtime/`, `libs/` ou no lifter.
- **Verificacao:** teste 8 novo; suite completa **rc=0, 8 PASS**; dois regens seguidos
  `diff -q` limpo.
- **Commit:** `3b42a6a`.

### 3. [Regra 1 — bug no `<verify>` do plano] O gate `! grep 'verificar manualmente'` da falso positivo

O comando do plano varre o ficheiro **inteiro**, e o cabecalho gerado pelo 16-01 contem
a linha de politica *"…'verificar manualmente' e' proibido."*. O gate falha sempre, sem
que exista um unico `aceite` manual. **Corrigido no escopo, nao mascarado:**
`grep -E '^patch_' … | grep -E 'verificar manual'` (so' linhas de dados) → **zero
ocorrencias**. A intencao do gate cumpre-se; o comando literal do plano e' que estava
errado.

### 4. [CLAUDE.md sobrepoe-se ao GSD] Commits sem `Co-Authored-By`

O workflow do GSD manda assinar os commits com `Co-Authored-By`. O `CLAUDE.md` do
projecto (regra 3) proibe co-autoria e exige portugues. Aplicou-se a regra do projecto.

**Total de desvios:** 4 (2 Regra 3, 1 Regra 1, 1 de precedencia CLAUDE.md).

## Must-haves do plano — estado

| Must-have | Estado | Evidencia |
|---|---|---|
| ≥15 linhas com as 4 colunas humanas, valores do dominio, sem "verificar manualmente" | ✅ | gate: `classified=19`; zero ocorrencias em linhas de dados |
| ≥5 P0 (classe, boot medido, `ea`≠`-`, aceite `grep:`/`contract:`/`smoke:`) | ✅ | gate: `p0=7` |
| Os candidatos de maior impacto do ROADMAP entre os classificados | ✅ | os 6 presentes (2550C8, 2A209C, 2B11B8, CE03C, factory OPD **e** ICG ctor, host inflate) |
| `host inflate` com `alvo=runtime`, nao forcado a midasm | ✅ | linha `patch_host_res_inflate.py … runtime … contract:tag-hostres-enter` |
| Cabecalho lista N_classificados, N_P0 e pilotos 17/18 | ✅ | cabecalho do TSV (derivado) |
| Sem regeneracao destrutiva do ledger | ✅ | so' `--merge`; `diff` mostra que as 19 linhas humanas sobreviveram e so' entrou a linha nova |
| `estado=migrated` == 0 | ✅ | `grep -c` = 0 |

## O que NAO foi atingido

1. **O aceite do P0 `patch_fallthrough_2550c8.py` nao foi exercitado.** E' `smoke:` e
   exige boot. Dos 19 aceites, 18 estao verdes **hoje**; este esta' declarado
   **nao-exercitado** e e' trabalho do 16-03.
2. **`LEDG-01` continua PARCIAL.** Faltam os criterios do baseline medido
   (`rc` do `verify_lift.sh`, `st620` maximo, `sticky`, OOB pos-TLS) — plano 16-03.
   O cabecalho do TSV continua a declara-los `POR MEDIR … [nivel de evidencia:
   nao-exercitado]`, sem campos que parecam preenchidos.
3. **121 das 140 linhas continuam sem decisao humana.** E' por desenho (o "≥15" e' o
   minimo de decisao, nao cobertura), mas nao e' cobertura — e' bom nao ler o
   `N_classificados=19` como "19 de 19".
4. **Nao ha' P0 com `alvo=invalid_insn`.** Nenhuma das 19 linhas cai nesse mecanismo;
   se a Fase 20 precisar de um piloto de `invalid_insn`, tera' de sair de linhas ainda
   nao classificadas.

## Known Stubs

Nenhum stub de codigo. Campos deliberadamente vazios, todos declarados:

- **121 linhas** sem `alvo`/`prioridade`/`aceite` (decisao humana nao feita).
- **`patch_freelist_guard_262610.py`** — linha nova, colunas humanas vazias, `estado=todo`.
  Nao a classifiquei: entrou no corpus depois do plano ser escrito e classifica-la
  sem a auditar seria exactamente o "achismo" que este plano existe para evitar.
- **Linha `# baseline verify/smoke: POR MEDIR (plano 16-03)`** com `rc=?`, `st620_max=?`,
  `sticky=?`, `oob_pos_tls=?`.

## Self-Check: PASSED

- `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` — FOUND (140 linhas de dados, 19 classificadas)
- `games/gow2/lift_baseline/PATCH_CATALOG.tsv` — FOUND (140 linhas de dados)
- `games/gow2/lift_baseline/gen_migration_ledger.py` — FOUND (suite rc=0, 8 PASS)
- `games/gow2/lift_baseline/test_gen_migration_ledger.py` — FOUND
- Commits `7b8aa51`, `3b42a6a` — FOUND em `git log`
