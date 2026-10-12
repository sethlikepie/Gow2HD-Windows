---
phase: 18-jump-tables-detectadas-e-declaradas
plan: 02
subsystem: tooling
tags: [recomp_config, switch_table, jump-tables, toml, ppu_lifter, midasm, oraculo, re-lift]

requires:
  - phase: 18-jump-tables-detectadas-e-declaradas
    plan: 01
    provides: "tools/ps3_analyse.py (detector high/low) e o achado que muda este plano: os EAs do ledger sao das FUNCOES, os bctr reais estao em 0x002A2130 e 0x002B1224"
  - phase: 17-mid-asm-de-ponta-a-ponta
    provides: "o piloto [[midasm_hook]] Ce03cWaitIdle e a armadilha MEDIDA do switch_table_file inexistente a matar o --config inteiro (17-03)"
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "ledger PATCH_MIGRATION.tsv com os dois P0 e os aceites grep 0x002A27BC / 0x002B12C4"
provides:
  - "games/gow2/config/gow2_switch_tables.toml -- 2 tabelas high revistas (dispatch 0x002A2130 com 54 alvos, 0x002B1224 com 22)"
  - "[main].switch_table_file REACTIVADO no gow2_recomp.toml (chave viva, no mesmo commit que o ficheiro)"
  - "Prova de nao-regressao da Fase 17 por diff completo de dois lifts, nao so por grep"
  - "Medicao que o 18-03 herda: as 143 tabelas high sao IDENTICAS as do discover_jump_tables, e os dois aceites do ledger ja passam num re-lift limpo sem patch e sem config"
  - "docs/RECOMP_CONFIG.md com switch tables descritas como consumidas + mapa comportamento->teste"
affects: [18-03 classificacao redundant/migrated dos dois P0, 19 padrao de prova de re-lift, 21 metrica XEN-05]

tech-stack:
  added: []
  patterns:
    - "Oraculo derivado do proprio ficheiro-fonte (parse do array do patch), nunca transcrito a mao -- e o que o torna oraculo e nao tautologia"
    - "Conteudo de producao decidido por medicao (declarado vs descoberto) em vez de por gosto ou por numero bonito"
    - "Nao-regressao por diff de duas arvores de lift: mostra o que NAO mudou, coisa que um grep nunca mostra"
    - "Ficheiro de producao gerado por script a partir da saida do detector -- zero transcricao manual de 76 enderecos"

key-files:
  created:
    - games/gow2/config/gow2_switch_tables.toml
  modified:
    - games/gow2/config/gow2_recomp.toml
    - docs/RECOMP_CONFIG.md

key-decisions:
  - "Entram 2 tabelas e nao as 143 high: medido que as 143 sao identicas alvo a alvo as que o discover_jump_tables ja produz, logo declarar e hoje um no-op; as 2 escolhidas sao as unicas com uma TERCEIRA fonte independente (os patches historicos). Despejar as outras 141 seria por sob a etiqueta 'revisto' tabelas que ninguem reviu, sem ganho funcional"
  - "O valor da declaracao e ser rede e esta escrito como tal, em vez de se apresentar como se resolvesse algo: o discover_jump_tables ja esteve MORTO em HEAD, em silencio (try/except que so imprimia em stderr)"
  - "Corrigido o exemplo do docs/RECOMP_CONFIG.md que usava dispatch=0x002A209C -- o EA da FUNCAO. Seguir o exemplo produzia exactamente a entrada inerte que o schema existe para eliminar"
  - "confidence fica no ficheiro (o parser ignora-o, verificado no 18-01): e o que permite a revisao humana"
  - "O ledger PATCH_MIGRATION.tsv NAO foi tocado: a classificacao redundant/migrated e escopo do 18-03, mesmo tendo este plano medido a evidencia que a decide"

metrics:
  duration: "~45 min"
  completed: 2026-08-02
  tasks: 2
  commits: 1
  files_created: 1
  files_modified: 2
  tabelas_declaradas: 2
  lifts_completos: 2

status: complete
---

# Phase 18 Plan 02: TOML de switch tables e reactivação do `switch_table_file` Summary

A chave `[main].switch_table_file` deixou de estar comentada: o ficheiro de produção
existe, é lido, e um re-lift com `--config` continua a emitir o mid-asm da Fase 17 —
provado por `diff` de duas árvores de lift, não por asserção.

## O que foi feito

| Item | Estado |
|---|---|
| `games/gow2/config/gow2_switch_tables.toml` (2 tabelas, 152 linhas) | criado |
| `switch_table_file` reactivado no `gow2_recomp.toml` | feito, **mesmo commit** |
| `docs/RECOMP_CONFIG.md` — switch tables de aspiracional a consumida | actualizado |
| Oráculo contra os dois `patch_jumptable_*` | **igualdade exacta**, 54/54 e 22/22 |
| Não-regressão do mid-asm CE03C | **provada** (grep + diff completo) |
| Suites unitárias (6) | **todas EXIT=0** |
| Dados de jogo commitados | **nenhum** (só endereços derivados) |
| `tools/ppu_lifter.py`, `tools/ps3_analyse.py` | **não tocados** |

Commit único: **`4f9ecd2`** — 3 ficheiros, +250/−20.

## Correcção a um facto herdado do 18-01

O SUMMARY do 18-01 (e o brief deste plano) afirmam que **os dois
`patch_jumptable_*.py` não existem no disco**. **Não é verdade** — existem, estão
versionados, e são o oráculo inteiro deste plano:

```
EXISTE: games/gow2/recomp_mid_v2/patch_jumptable_2a209c.py (208 linhas)
EXISTE: games/gow2/recomp_mid_v2/patch_jumptable_2b11b8.py (120 linhas)
$ git ls-files games/gow2/recomp_mid_v2/ | grep jumptable   -> ambos, rc=0
```

O que está `todo` no ledger é a **classificação de migração**, não a existência do
ficheiro. Isto muda o plano para melhor: em vez de um oráculo enfraquecido, houve
um oráculo forte com as tabelas originais escritas à mão.

## O oráculo — igualdade exacta, derivada dos patches

Os alvos esperados foram **derivados por parse dos próprios ficheiros de patch**
(as linhas `.word` do `2a209c`, o array `k_jt` do `2b11b8`), nunca copiados à mão —
é isso que faz disto um oráculo e não uma tautologia escrita por mim.

| Piloto | Base do patch | Entradas | `dispatch` real (EA do `bctr`) | Intersecção |
|---|---|---|---|---|
| `2a209c` / OPDISP | `0x002A2134` | 58 (54 distintos) | **`0x002A2130`** | **54/54 — igualdade exacta** |
| `2b11b8` / TYDISP | `0x002B1228` | 28 (22 distintos) | **`0x002B1224`** | **22/22 — igualdade exacta** |

Os aceites do ledger caem certos por aritmética: `0x2A2134 + 0x688 = 0x002A27BC`
e `0x2B1228 + 0x09C = 0x002B12C4`, ambos **encontrados**.

Confirmou-se o achado do 18-01: os EAs do ledger (`0x002A209C`, `0x002B11B8`) são os
das **funções**; usá-los como `dispatch` daria uma declaração inerte. O casamento foi
**função → dispatcher**.

## A medição que decidiu o conteúdo do ficheiro

Correndo o `ps3_analyse` e o `discover_jump_tables` do lifter sobre a **mesma imagem**
e com a **mesma assinatura**:

```
ps3_analyse: 143 high, 14 low
discover_jump_tables (lifter): 157 tabela(s)

high identicas a' descoberta      : 143
high com alvos DIFERENTES         : 0
high que a descoberta NAO tem     : 0
so' na descoberta (analyse nao ve): 0
```

**As 143 são idênticas, alvo a alvo, ao que a descoberta já produz sozinha.** Declarar
qualquer uma delas é **hoje um no-op** — o merge substitui a entrada da descoberta por
outra igual. Por isso entram **duas**, não 143: são as únicas com uma **terceira** fonte
independente. Concordância de três análises independentes é prova; as outras 141 têm
duas, e nenhuma revisão humana — declará-las seria etiquetar de "revisto" o que ninguém
reviu.

O que a declaração vale está escrito no cabeçalho do ficheiro em vez de ser insinuado:
**nada em comportamento hoje**. Vale ser rede — o `discover_jump_tables` já esteve
**morto** em HEAD (`TypeError`/`NameError` dentro de um `try/except` que só imprimia em
stderr ⇒ zero jump tables em qualquer lift, em silêncio).

## O aceite obrigatório: o mid-asm do CE03C continua emitido

Re-lift completo com `--config` para directório **novo** (`/tmp/relift_18_02_config`,
nunca sobre `recomp_macos_v2`). Arranque limpo, sem abortar:

```
  Config: 1 midasm_hook(s) loaded from games/gow2/config/gow2_recomp.toml
  Config: 2 switch_table(s) declared in .../games/gow2/config/gow2_switch_tables.toml
  switch tables: 157 from discovery, 2 from config, 2 overridden by config, 157 total
```

Saída literal do `grep`, com contexto — **primeira linha** de `func_000CE03C`:

```
  19536	void func_000CE03C(ppu_context* ctx) {
  19537	        gow2_midasm_Ce03cWaitIdle(ctx);
  19538	        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x5E44);
```

O segundo site (`ppu_recomp_000.cpp:167034`) está em `func_000CE02C`, **depois** de um
`return` incondicional — código morto. É a emissão dupla já registada como **WINDOWS #9**,
pré-existente e não causada por este plano.

### Prova mais forte que o grep: o `diff` das duas árvores

Dois lifts completos da mesma EBOOT, um **sem** `--config` e outro **com**:

| Ficheiro | Linhas diferentes |
|---|---|
| `ppu_recomp.h` | **0** |
| `ppu_recomp_000.cpp` | 10 |
| `ppu_recomp_001..006.cpp` | 9 cada |

E as diferenças são **só** o bloco de declaração do hook (9 linhas por chunk) mais as
suas duas chamadas:

```
289a290,298
> /* Mid-asm hooks ([[midasm_hook]] do --config). Definidos em codigo host versionado. */
> void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);
19527a19537
>         gow2_midasm_Ce03cWaitIdle(ctx);
```

**Zero linhas de diferença em switch tables.** Isto prova as duas coisas ao mesmo tempo:
a Fase 17 não foi partida, e a declaração das duas tabelas não mudou nada — porque já
coincidia com a descoberta.

Os dois aceites do ledger saem emitidos, com **todos** os alvos (54 e 22, nenhum
descartado pelo filtro de bounds — o lifter promove os alvos fora da função a
`+309 case funcs`):

```
case 0x002A27BCu: goto loc_002A27BC;    (dentro do switch de 54 casos)
case 0x002B12C4u: goto loc_002B12C4;    (dentro do switch de 22 casos)
```

## Evidência que o 18-03 herda (e que este plano NÃO usou para decidir nada)

No lift **baseline sem `--config` e sem qualquer patch**, os dois aceites do ledger
**já passam**:

```
$ grep -c "case 0x002A27BCu: goto loc_002A27BC;" /tmp/relift_18_02_noconfig/*.cpp
ppu_recomp_001.cpp:1   ppu_recomp_002.cpp:1   ppu_recomp_005.cpp:1
$ grep -c "case 0x002B12C4u: goto loc_002B12C4;" /tmp/relift_18_02_noconfig/*.cpp
ppu_recomp_001.cpp:1
```

Isto aponta fortemente para **`redundant`** nos dois P0 — o que o 18-CONTEXT já
levantava como hipótese provável e legítima. **A classificação formal não foi feita
aqui e o ledger não foi tocado**: é escopo do 18-03, e inflar este plano com a
conclusão do próximo seria exactamente o tipo de atalho que o G5 proíbe.

## Nível de evidência

Tudo neste plano é **offline-unit**: suites unitárias, carregamento do TOML, dois
re-lifts completos e `diff`/`grep` sobre o C gerado. **Não houve boot**, portanto
nenhuma afirmação sobre comportamento em execução é feita. O smoke é do 18-03.

## XEN-06 fecha inteiro?

**Sim — deixa de estar qualificado a "só mid-asm".** Estava escrito no REQUIREMENTS
que "fecha por completo quando a Fase 18 ligar o `switch_table_file`". Ligou:

| Secção | Consumidor real | Teste |
|---|---|---|
| `[[midasm_hook]]` | emissão da chamada (Fase 17) | `test_midasm_emit.py` |
| `[[switch_table]]` | `load_switch_tables` + `merge_switch_tables` + emissão | `test_switch_tables.py`, `test_ppu_jumptables.py`, `test_ps3_analyse_jtables.py` |
| `[main].switch_table_file` | **activo** e resolvido | `test_recomp_config.py` |
| `[[invalid_instructions]]` | `disassemble_bytes` | `test_invalid_instructions.py` |

Continuam declarados como **não consumidos** (não descritos como se fossem):
`jump_address` (recusa `rc=2`), `registers`, `functions_override`, `[optimizations]`.
Nenhuma secção descrita sem consumidor ⇒ o critério do XEN-06 fica satisfeito.

**XEN-03** não fecha aqui (é do 18-03): o critério do oráculo está **verde**, o critério
`migrated`/`redundant` fica por classificar.

## Resultado real dos testes

```
tools/test_switch_tables.py         EXIT=0
tools/test_midasm_emit.py           EXIT=0
tools/test_recomp_config.py         EXIT=0
tools/test_ps3_analyse_jtables.py   EXIT=0
tools/test_ppu_jumptables.py        EXIT=0
tools/test_invalid_instructions.py  EXIT=0
```

## Deviations from Plan

**1. [Regra 1 — bug] Exemplo do `docs/RECOMP_CONFIG.md` usava o `dispatch` errado.**
A doc mostrava `dispatch = 0x002A209C` — o EA da **função**, não do `bctr`. Quem
seguisse o exemplo escreveria a entrada inerte que o schema existe para eliminar,
e a própria doc explicava a regra certa três linhas abaixo. Corrigido para
`0x002A2130`, com a armadilha ledger→`dispatch` escrita a seguir ao exemplo.

**2. [Discricionário, medido] Duas tabelas em vez das 143 `high`.**
O plano dizia "human-review high tables into TOML", o que admitia o conjunto todo.
A medição declarada/descoberta mostrou que seriam 141 no-ops sem revisão. Documentado
no cabeçalho do próprio ficheiro, com o comando de regeneração para quem quiser as 143.

**3. [Fora de escopo, não corrigido] `.planning/WINDOWS.md` tem uma entrada corrompida.**
A entrada `id=7` (stub do CE03C, Fase 17) tem `status: "open"` na tabela e
`"resolved"` no bloco JSON — valor que o schema não aceita. Isso faz
`gsd-tools windows append` **falhar** com `Ledger entry 6 has invalid status: "resolved"`,
pelo que **não foi possível registar no ledger** o item da secção seguinte. Não foi
corrigido: é defeito pré-existente da Fase 17 num ficheiro partilhado com outra
sessão activa (regra 7 do CLAUDE.md).

Sem desvios de Regra 4 (arquitectura). Nenhuma decisão fora do 18-CONTEXT.

## Known Stubs

Nenhum nos artefactos entregues — o TOML tem endereços reais medidos, a chave está
viva e a doc descreve o consumidor real.

**Lacuna honesta (o que devia ter ido para o ledger, e não foi pela razão acima):**
a não-regressão foi provada invocando o `ppu_lifter.py` **directamente** com
`--config games/gow2/config/gow2_recomp.toml` (monorepo). O caminho **real de build**
(`RELIFT=1 ./build_macos.sh`, que lê `$HERE/config/gow2_recomp.toml` no checkout
`../gow2-recomp`) **não foi exercitado**: o sibling ainda não recebeu o `subtree pull`
e lá a chave continua comentada, sem o `gow2_switch_tables.toml`. Enquanto os dois
ficheiros não chegarem **juntos** ao `../gow2-recomp`, um build de lá não vê a mudança —
e se só a config chegasse sem o TOML de tabelas, o `--config` abortaria inteiro (a
armadilha do 17-03 continua viva para caminhos errados). Fecha com `subtree pull` +
um `RELIFT=1` de prova.

## Self-Check: PASSED

* `games/gow2/config/gow2_switch_tables.toml` — FOUND (152 linhas, 2 tabelas, carrega)
* `games/gow2/config/gow2_recomp.toml` — FOUND (`switch_table_file` activo, linha 23)
* `docs/RECOMP_CONFIG.md` — FOUND (modificado)
* commit `4f9ecd2` — FOUND
* `git diff --diff-filter=D HEAD~1 HEAD` — **vazio** (nenhum ficheiro apagado)
* `git diff --cached --name-only` antes do commit — exactamente os 3 ficheiros previstos
* `tools/ppu_lifter.py` / `tools/ps3_analyse.py` — ausentes do commit (não tocados)
* **Nenhum dado de jogo commitado**: o TOML é ASCII de endereços; EBOOT/`functions.json`
  só foram lidos, dumps ficaram em `/tmp`
* Lifts de teste em `/tmp/relift_18_02_{noconfig,config}` — **nunca** sobre `recomp_macos_v2`
* Ficheiros de outra sessão (`.planning/PROJECT.md`, `MILESTONES.md`, `ROADMAP.md`,
  `CLAUDE.md`, `games/gow2/CLAUDE.md`, `.planning/config.json`, `.planning/milestones/v1.2-*`,
  `scripts/gsd_plan*.sh`, `.planning/WINDOWS.md`) — intactos e **fora** do commit
