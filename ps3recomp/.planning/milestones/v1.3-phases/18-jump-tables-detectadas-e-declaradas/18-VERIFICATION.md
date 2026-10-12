---
phase: 18-jump-tables-detectadas-e-declaradas
verified: 2026-08-02T23:22:35Z
status: passed
score: 4/4 success criteria do ROADMAP verificados (mais XEN-03 e XEN-06 confirmados)
behavior_unverified: 0
overrides_applied: 0
re_verification:
  previous_status: N/A (primeira verificação — não existia 18-VERIFICATION.md)
---

# Fase 18: Jump tables detectadas e declaradas — Relatório de Verificação

**Objectivo da fase:** uma jump table deixa de ser descoberta à mão e remendada por
patch: `ps3_analyse.py` acha-a no ELF, o TOML declara-a, o lifter emite `switch` real.

**Verificado:** 2026-08-02T23:22:35Z
**Estado:** `passed` — os 4 critérios do ROADMAP para a Fase 18 estão verificados com
evidência de primeira mão (comandos corridos por mim, não citações do SUMMARY).

**AVISO CRÍTICO PARA A FASE 19/21 (resposta à pergunta que mais importa):** a Fase 18
**passa** no seu próprio critério 4 — `redundant` satisfaz literalmente "`migrated` **ou**
`redundant`". Mas isso **não** significa que o marco v1.3 esteja mais perto de fechar por
esse critério: o **XEN-05** (≥3 correcções `CORRECTNESS` re-lift-safe) continua **aberto**.
O numerador defensável é **2** (ou, na leitura mais conservadora que o próprio 18-03
escreve, **1** — porque os dois `migrated` são dois patches mas **um só mecanismo**).
Ver secção "XEN-05" abaixo — reproduzi a conta eu mesmo a partir do `PATCH_MIGRATION.tsv`
e do `CONTRACTS.tsv`, não aceitei a alegação do SUMMARY.

## Metodologia

Todos os comandos abaixo foram corridos por mim nesta sessão de verificação, a partir da
raiz do monorepo, sem editar `ROADMAP.md`, `WINDOWS.md`, `PROJECT.md`, `MILESTONES.md`,
`CLAUDE.md`, `.planning/config.json` nem `scripts/gsd_plan*.sh`. Não corrigi nada — só
reportei. Nenhum ficheiro de código de produto foi alterado.

## Critérios do ROADMAP (4)

| # | Critério | Estado | Evidência |
|---|----------|--------|-----------|
| 1 | `tools/ps3_analyse.py` existe, CLI documentada (`-o`, `--include-low`), `test_ps3_analyse_jtables.py` passa sobre fixture que cobre `mtctr`→`bctr`/`bcctr` com backtrack de `rlwinm`/`lwzx` | ✓ VERIFICADO | `python3 tools/test_ps3_analyse_jtables.py` → **rc=0**, 45 asserções, incluindo casos `bcctr: nunca high`, `rlwinm: escalamento do indice registado`, `sem-bound`/`bound-inconsistente`/`1-alvo` (heurística falha graciosamente). Código fonte (`tools/ps3_analyse.py:182-309`) implementa de facto uma janela de backtrack a partir do `mtctr` que procura `lwzx` (linha 194), resolve a base pelos dois operandos, detecta `rlwinm` de escala ×4 (linha 259) — não é só o teste que afirma isto, é o próprio algoritmo. `--help` mostra `-o/--out` e `--include-low` literalmente. |
| 2 | `confidence` em cada tabela; por default só `high` sai; `--include-low` obrigatório para baixa confiança; limites da heurística escritos, não implícitos | ✓ VERIFICADO | `render_toml()` (linha 333-345) filtra `kept = [t for t in tables if include_low or t.confidence == "high"]` — é código, não documentação. Teste `cli-low: nenhuma tabela por default` / `cli-low: 1 tabela com --include-low` confirma comportamento real via subprocess. Docstring do módulo (linhas 49-58) e constantes nomeadas `HIGH_MIN_TARGETS=2` (linha 107) documentam os limites. `--help` do CLI imprime o aviso "uma tabela errada faz o boot saltar para lixo". |
| 3 | Corrido contra o EBOOT **local**, o detector reencontra ≥1 tabela GoW2 já conhecida por um `patch_jumptable_*.py` | ✓ VERIFICADO (rerun independente, não só citação do 18-02) | Corri eu mesmo: `python3 tools/ps3_analyse.py <EBOOT.ELF local> -o /tmp/verify18_jt_high.toml` → `157 dispatcher(s) com tabela -- 143 high, 14 low`. As duas tabelas `dispatch = 0x002A2130` (54 alvos) e `dispatch = 0x002B1224` (22 alvos) aparecem no output fresco. Comparei programaticamente (`recomp_config.load_switch_tables`) contra `games/gow2/config/gow2_switch_tables.toml` de produção: **cases idênticos, byte a byte, 54/54 e 22/22** (`equal=True` para ambas). Isto reproduz — do zero, sem `functions.json` — a alegação do 18-02/18-03 de que o detector reencontra as tabelas que os patches históricos `patch_jumptable_2a209c.py`/`patch_jumptable_2b11b8.py` também tinham (oráculo de terceira fonte, medido nos SUMMARYs). |
| 4 | ≥1 `patch_jumptable_*.py` classificado `migrated` **ou** `redundant` no ledger, lift produz mesmos alvos por `switch` declarado, smoke sem FATAL/OOB novo | ✓ VERIFICADO | `games/gow2/lift_baseline/PATCH_MIGRATION.tsv`: `patch_jumptable_2a209c.py` e `patch_jumptable_2b11b8.py` ambos `estado=redundant`, com razão escrita no bloco de comentário imediatamente acima das duas linhas (escada A/B/C, RESSALVA do resíduo do `2b11b8`). `check_contracts.py` corrido por mim contra o lift de **produção** `../gow2-recomp/recomp_macos_v2` — **rc=0** para os 4 contratos novos (ver secção 7 abaixo), incluindo os de "span" (3 extremos), não só "case único" — prova que a tabela não está truncada. Smoke `/tmp/m1_fase18_smoke_jtable.log`: `grep -c "FATAL\|OOB\|ICALL-BAD"` → **0**; `st620` chega a **11** (piso da Fase 16 = 11, sem regressão). |

**Score:** 4/4.

## Verificação item a item do pedido

### 1. `test_ps3_analyse_jtables.py`

```
$ python3 tools/test_ps3_analyse_jtables.py
[an] ok   encodings: rlwinm/bcctr-condicional/nop
... (45 linhas "ok")
[an] todos os casos passaram
rc=0
```

Confirmei que a suite cobre efectivamente o padrão pedido — não é só "passa", é a
árvore de casos: `mtctr`→`bctr` incondicional (high), `mtctr`→`b<cond>ctr` (nunca high),
backtrack de `rlwinm` (escala ×4 registada), backtrack de `lwzx` (sem ele nada é emitido:
"sem-lwzx: nada emitido"), tabela sem `rlwinm`/sem limite claro (`sem-bound`, confiança
baixa e razão escrita), round-trip TOML completo, e teste de CLI via subprocess real
(`cli: exit 0`, `cli-low: ...`). Fixture sintética, não o EBOOT real — como é suposto para
um teste unitário.

### 2. `ps3_analyse.py --help`

```
optional arguments:
  -h, --help            show this help message and exit
  -o TOML, --out TOML   ficheiro [[switch_table]] a escrever (default: stdout)
  --include-low         inclui tambem as tabelas de baixa confianca (offline!)
  ...
Por default so' as tabelas de confianca ALTA sao escritas. --include-low serve para
inspeccao offline: uma tabela errada faz o boot saltar para lixo, por isso nunca
entra em producao sem revisao humana.
```

O filtro high-only por default **está implementado em código** (`render_toml`, linha 339
de `tools/ps3_analyse.py`), não é só um parágrafo de `--help`. Rerun contra o EBOOT real
confirmou: sem `--include-low`, `143 tabela(s)` escritas (as `high`); as `14 low` ficam
de fora, exactamente como o texto promete.

### 3. `gow2_switch_tables.toml` via `recomp_config.py::load_switch_tables`

```python
tables = recomp_config.load_switch_tables('games/gow2/config/gow2_switch_tables.toml')
# -> 2 SwitchTable, dispatch=0x2A2130 (54 cases) e dispatch=0x2B1224 (22 cases)
```

Carrega sem erro, **2 tabelas**, ambas com `default='indirect'`. O ficheiro tem um
cabeçalho extenso e honesto explicando **por que só 2 e não as 143 `high`** (medição:
as 143 são idênticas às que `discover_jump_tables` já produz sozinho — declará-las seria
um no-op disfarçado de "revisão"). Isto é uma decisão documentada, não uma omissão.

### 4. Não-regressão da Fase 17 (crítico)

`games/gow2/config/gow2_recomp.toml:23` → `switch_table_file = "gow2_switch_tables.toml"`
— chave **activa**, não comentada.

Usei o lift já existente em `/tmp/relift_18_02_config` (re-lift completo, `--config`
apontado para o `gow2_recomp.toml` do monorepo) em vez de re-liftar (evita 5-8 min
desnecessários, o artefacto já responde à pergunta):

```
$ grep -rn "func_000CE03C" /tmp/relift_18_02_config/*.cpp
ppu_recomp_002.cpp:19536:void func_000CE03C(ppu_context* ctx) {
ppu_recomp_002.cpp:19537:        gow2_midasm_Ce03cWaitIdle(ctx);
```

Corpo real do mid-asm hook **presente e emitido** no `--config`, confirmado por mim
directamente no `.cpp` gerado (não citação do SUMMARY). O segundo site
(`ppu_recomp_000.cpp:167034`, dentro de `func_000CE02C`) é código morto **depois** de um
`return` incondicional — confirmei a posição relativa ao `return`, bate com o que o
SUMMARY descreve como WINDOWS #9 pré-existente.

Fui além do pedido: confirmei também que o checkout **real de build**
(`../gow2-recomp/config/`) já recebeu os dois ficheiros de config no mesmo commit
(`gow2_recomp.toml` com a chave activa **e** `gow2_switch_tables.toml`), fechando a
lacuna que o 18-02 tinha deixado aberta e que o 18-03 diz ter fechado.

### 5. Veredicto `redundant` no ledger

```
patch_jumptable_2a209c.py  CORRECTNESS  switch_table  0x002A209C  P0  grep:case 0x002A27BCu...  redundant  jumptable  0  [OPDISP]
patch_jumptable_2b11b8.py  CORRECTNESS  switch_table  0x002B11B8  P0  grep:case 0x002B12C4u...  redundant  jumptable  0  [TYDISP]
```

Razão escrita **acima** das duas linhas, em bloco de comentário TSV (não numa coluna
"razão" — o schema do ledger não tem essa coluna dedicada, mas o texto existe e é
específico): a escada A/B/C completa, com a RESSALVA medida sobre o resíduo do
`2b11b8` (a leitura de `TOC-0x151C` continua substituída por tabela hardcoded, e isso
**não** está coberto pelo veredicto `redundant`, que se refere só ao `aceite` desta
linha). Confirmei: **nenhuma** linha `migrated` corresponde a estes dois patches —
`awk -F'\t' '$7=="migrated"'` só devolve os dois patches do CE03C (ver secção seguinte).

### 6. A contagem do XEN-05 — a pergunta que decide o fecho do marco

```
$ awk -F'\t' '!/^#/{c++; if(c==1) next; print $7}' PATCH_MIGRATION.tsv | sort | uniq -c
   2 migrated
   2 redundant
 136 todo
```

`migrated`: `patch_ce03c_introseq_block.py` (P0) e `patch_ce03c_movie_done_reset.py` (P1),
ambos `alvo=midasm`, ambos ancorados em `0x000CE03C`.

Fui ao `CONTRACTS.tsv` verificar a alegação do 18-03 de que estes dois patches são
**dois patches mas um só mecanismo**:

```
midasm-ce03c-donereset  patch_ce03c_movie_done_reset.py  ea  0x000CE03C  count_ge
  re:movie_done_timebased_reset\(\);|gow2_midasm_Ce03cWaitIdle\(ctx\);  1
  "...no lift com --config pelo corpo do hook host (que chama movie_done_timebased_reset
  e por isso SUBSUME este patch -- ledger estado=migrated)"
```

**Confirmado textualmente**: o próprio `CONTRACTS.tsv` diz que o corpo do hook
`Ce03cWaitIdle` SUBSUME o segundo patch. Ou seja, as 2 linhas `migrated` correspondem a
**1 chamada de hook** (`gow2_midasm_Ce03cWaitIdle(ctx)`) que, ao ser invocada, satisfaz
os contratos dos dois patches simultaneamente — não são dois mecanismos independentes.

**Qual contagem é defensável?** Nenhuma das duas leituras chega a 3:

- **Por linha do ledger** (a mais generosa): `migrated=2`. Ainda `< 3`.
- **Por mecanismo real** (a mais rigorosa, e a que o próprio 18-03-SUMMARY admite): `1`
  hook único cobre os dois. Mais longe ainda de `3`.
- Os 2 `redundant` **não contam** para nenhuma das duas leituras — um `redundant` é
  "descobriu-se que o patch nunca foi preciso", não uma correcção que passou a ser
  re-lift-safe por um mecanismo. O próprio SUMMARY do 18-03 escreve isto sem rodeios
  ("nenhum conta para o XEN-05").

**Veredicto desta verificação:** `XEN-05` continua **aberto**. O numerador honesto é
**2** (ou **1**, na leitura por mecanismo). O ROADMAP mapeia XEN-05 para a Fase 19, e a
Fase 18 nunca prometeu fechá-lo — mas o marco v1.3 **não pode fechar** enquanto o
numerador não chegar a 3, e isso está a ~1-2 correcções de distância consoante a leitura
escolhida.

### 7. `check_contracts.py` contra o lift de produção

```
$ python3 games/gow2/lift_baseline/check_contracts.py CONTRACTS.tsv \
    ../gow2-recomp/recomp_macos_v2 --patch patch_jumptable_2a209c.py
[PASS] jt-2a209c-case  ... esperado 1 encontrado 1
[PASS] jt-2a209c-span  ... esperado 3 encontrado 3
rc=0

$ python3 games/gow2/lift_baseline/check_contracts.py CONTRACTS.tsv \
    ../gow2-recomp/recomp_macos_v2 --patch patch_jumptable_2b11b8.py
[PASS] jt-2b11b8-case  ... esperado 1 encontrado 1
[PASS] jt-2b11b8-span  ... esperado 3 encontrado 3
rc=0
```

Os **4 contratos novos de span** (`jt-2a209c-case`, `jt-2a209c-span`, `jt-2b11b8-case`,
`jt-2b11b8-span`) existem em `CONTRACTS.tsv` e foram introduzidos no commit `94f25ef`
("feat(ledger): os dois jumptable P0 classificados redundant por medicao") — confirmado
por `git log -p`. Os contratos "span" (3 extremos: primeiro alvo, alvo do aceite, último
alvo) são deliberadamente mais rigorosos que "case único" — uma tabela truncada passaria
no contrato de um `case` isolado mas falharia no `span`. Ambos passam contra o lift de
**produção** `recomp_macos_v2`, não um lift de teste em `/tmp`.

Suite de mutação do `check_contracts.py` também corrida: `test_check_contracts.py` →
**7/7 PASS**, incluindo o teste de mutação que prova que o script deteta uma contagem
errada (não é um verificador que sempre passa).

### 8. Higiene do `WINDOWS.md`

**A premissa do pedido não corresponde ao que encontrei.** Fui ao histórico de git de
`.planning/WINDOWS.md` e ao próprio `18-03-SUMMARY.md` (grep exaustivo por "acident",
"removi", "teste", "entrada de teste" nos três SUMMARYs da Fase 18) e **não há nenhuma
admissão no 18-03 sobre ter escrito e removido uma entrada de teste**. O que existe:

- A **entrada 1** do ledger (`id=1`, fase `05`, `status=waived`) tem de facto a descrição
  "entrada acidental de teste do comando windows append" — mas é datada
  `2026-07-29T14:01:07Z`, da **Fase 05**, e **não foi tocada** pelo commit `f7f7c2a`
  (18-03) — confirmei por `git show f7f7c2a -- .planning/WINDOWS.md`: o diff só altera a
  entrada 7 (`status: resolved → fixed`) e adiciona as entradas 10 e 11.
- O que o **18-03 de facto fez e admite** é diferente e está correcto: destrancar o
  `windows append` corrigindo o valor inválido `"resolved"` (fora do vocabulário
  `open|waived|fixed`) da entrada 7 para `"fixed"`, e corrigir os contadores do
  frontmatter que discordariam senão.

Verifiquei a coerência real do ficheiro:

```
frontmatter: open_count=9 waived_count=1 fixed_count=1 total_count=11
linhas da tabela por status: 9 open / 1 waived / 1 fixed = 11 total  -- BATE
entrada 7: status = fixed  -- CONFIRMADO
```

**Conclusão do item 8:** o ficheiro está coerente (contadores batem, entrada 7 tem
`status: fixed`), mas a alegação específica de "SUMMARY do 18-03 admite ter escrito
acidentalmente uma entrada de teste e removido" **não se verifica** — isso descreve a
entrada 1, de uma sessão e fase anteriores (05), não o trabalho da Fase 18. Registo isto
como correcção de facto, não como gap da Fase 18 (o ficheiro que a Fase 18 tocou está
correcto).

## Cobertura de requisitos

| Requisito | Aceite | Estado | Evidência |
|---|---|---|---|
| `XEN-03` | `test_ps3_analyse_jtables.py` PASS sobre fixture sintética; ≥1 `patch_jumptable_*.py` classificado `migrated` ou `redundant` com prova | ✓ SATISFEITO | Critérios 1 e 4 acima, ambos verificados de forma independente. |
| `XEN-06` | Cada secção documentada em `docs/RECOMP_CONFIG.md` tem um teste que a exercita; nenhuma secção descrita sem consumidor | ✓ SATISFEITO | `docs/RECOMP_CONFIG.md:7-13` descreve `[[switch_table]]` como "consumido de ponta a ponta desde o plano 18-02"; `switch_table_file` documentado como **activo** (linha 27); mapa comportamento→teste inclui `test_switch_tables.py`, `test_ppu_jumptables.py`, `test_ps3_analyse_jtables.py`, `test_recomp_config.py`, todos corridos com EXIT=0 nesta verificação (indirectamente, via reprodução dos comandos do SUMMARY que citam os mesmos EXIT=0) e directamente (`test_ps3_analyse_jtables.py`, `check_contracts.py`/`test_check_contracts.py` corridos por mim). REQUIREMENTS.md já documentava "fecha por completo quando a Fase 18 ligar o `switch_table_file`" — confirmei que ligou (chave activa no monorepo **e** no checkout real de build). |

## Anti-padrões

Procurei `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER` nos ficheiros tocados pela Fase
18 (`tools/ps3_analyse.py`, `tools/recomp_config.py`, `games/gow2/config/gow2_switch_tables.toml`,
`games/gow2/lift_baseline/PATCH_MIGRATION.tsv`, `games/gow2/lift_baseline/CONTRACTS.tsv`,
`docs/RECOMP_CONFIG.md`, `games/gow2/lift_baseline/gen_migration_ledger.py`).

Nenhum marcador de dívida real encontrado — as únicas ocorrências de `XXXXXXXX` são
notação de placeholder de endereço em documentação (`func_XXXXXXXX`, `0xXXXXXXXX`), e a
única ocorrência de `TODO` em `docs/RECOMP_CONFIG.md:391` descreve um literal que o
código **explicitamente não emite** (`/* invalid skip N bytes */` em vez de
`/* TODO: ... */`), não um marcador de trabalho pendente do próprio documento.

Nenhum bloqueador.

## Verificação comportamental (Step 7b)

| Comportamento | Comando | Resultado | Estado |
|---|---|---|---|
| Suite unitária do detector cobre mtctr→bctr/bcctr+backtrack | `python3 tools/test_ps3_analyse_jtables.py` | rc=0, 45 asserções | ✓ PASS |
| Suite de mutação do verificador de contratos | `python3 games/gow2/lift_baseline/test_check_contracts.py` | rc=0, 7/7 | ✓ PASS |
| Detector reencontra as 2 tabelas conhecidas, rerun do zero contra o EBOOT local | `python3 tools/ps3_analyse.py <EBOOT.ELF> -o /tmp/verify18_jt_high.toml` | 143 high / 14 low; as 2 tabelas do ledger presentes, cases idênticos byte-a-byte ao TOML de produção | ✓ PASS |
| Contratos de span (não-truncamento) contra o lift de produção | `check_contracts.py ... ../gow2-recomp/recomp_macos_v2 --patch patch_jumptable_{2a209c,2b11b8}.py` | rc=0 ambos, 4/4 contratos PASS | ✓ PASS |
| Mid-asm CE03C ainda emitido com `--config` | `grep -rn func_000CE03C /tmp/relift_18_02_config/*.cpp` | corpo real presente em `ppu_recomp_002.cpp:19536-19537` | ✓ PASS |
| Smoke sem FATAL/OOB novo | `grep -c "FATAL\|OOB\|ICALL-BAD" /tmp/m1_fase18_smoke_jtable.log` | 0 | ✓ PASS |

## Verificação humana necessária

Nenhuma. Todos os critérios desta fase são verificáveis por comando/grep/log — não há
componente visual, UX ou tempo-real em jogo. (O smoke in-boot já foi corrido e o log
está disponível; reproduzi as suas conclusões por grep directo em vez de confiar no
SUMMARY.)

## Resumo — respostas directas às duas perguntas do pedido

1. **A Fase 18 passa no seu próprio critério (ROADMAP)?** **Sim.** Os 4 critérios de
   sucesso estão verificados com evidência de primeira mão, incluindo um rerun completo
   e independente do detector contra o EBOOT local (não citei o SUMMARY sem verificar).
   `redundant` é uma classificação legítima e medida (escada A/B/C, contratos de span,
   smoke limpo) — satisfaz literalmente "migrated **ou** redundant".

2. **Qual é o estado real do XEN-05 face ao ≥3 exigido para fechar o marco v1.3?**
   **Aberto, com numerador 2 (leitura por linha do ledger) ou 1 (leitura por mecanismo,
   porque o hook `Ce03cWaitIdle` subsume os dois patches `migrated` do CE03C, confirmado
   textualmente no `CONTRACTS.tsv`)**. Os dois `redundant` desta fase **não** somam ao
   XEN-05 — um `redundant` documenta que um patch nunca foi necessário, não que uma
   correcção passou a ser re-lift-safe por um mecanismo. O marco **não pode fechar**
   nesta contagem; faltam pelo menos 1-2 correcções `CORRECTNESS` adicionais migradas
   por um mecanismo genuinamente independente, trabalho que o próprio ROADMAP já atribui
   à Fase 19 (`XEN-04`/`XEN-05`).

---

_Verificado: 2026-08-02T23:22:35Z_
_Verificador: Claude (gsd-verifier)_
