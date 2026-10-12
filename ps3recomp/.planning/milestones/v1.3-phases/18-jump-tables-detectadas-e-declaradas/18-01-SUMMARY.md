---
phase: 18-jump-tables-detectadas-e-declaradas
plan: 01
subsystem: tooling
tags: [ppu_lifter, recomp_config, switch_table, jump-tables, toml, tdd, mutation-testing, python]

requires:
  - phase: 17-mid-asm-de-ponta-a-ponta
    provides: "padrão de teste em RED antes do código, prova por mutação (17-01) e a armadilha medida do switch_table_file inexistente a matar todo o --config (17-03)"
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "ledger PATCH_MIGRATION.tsv com os dois P0 de jumptable (0x002A209C OPDISP, 0x002B11B8 TYDISP) e os seus aceites grep"
provides:
  - "tools/ps3_analyse.py — detector offline de jump tables (mtctr→bctr/bcctr) que escreve [[switch_table]] TOML"
  - "Campo confidence por tabela; por default só as high são emitidas (low exigem --include-low)"
  - "CLI: <ELF|blob> [-o TOML] [--include-low] [--raw --base --toc] [--code-start/--code-end] [--functions]"
  - "API Python: analyse_instructions() / render_toml() / write_toml() / load_elf() / load_raw()"
  - "tools/test_ps3_analyse_jtables.py — 49 asserções sobre fixtures sintéticas, sem ELF/SDK/rede"
  - "docs/RECOMP_CONFIG.md com secção [[switch_table]] + gerador e os limites da heurística escritos"
  - "Prova de que a saída é lida pelo load_switch_tables existente e que confidence é ignorado pelo parser"
affects: [18-02 TOML de produção do GoW2 e reativação de switch_table_file, 18-03 migração de patch_jumptable_*, 19 padrão de prova de re-lift, 21 métrica XEN-05]

tech-stack:
  added: []
  patterns:
    - "Detector conservador por construção: o filtro default é high-only porque um falso positivo é o boot a saltar para lixo"
    - "Construtores de palavras reutilizados por import de test_ppu_jumptables.py (não copiados)"
    - "Prova por mutação: injectar o defeito, ver o teste falhar, reverter — repetido do 17-01"
    - "Fixture reforçada quando uma mutação NÃO discrimina (medido, não assumido)"

key-files:
  created:
    - tools/ps3_analyse.py
    - tools/test_ps3_analyse_jtables.py
  modified:
    - docs/RECOMP_CONFIG.md

key-decisions:
  - "high exige COERÊNCIA com o bound check (lidas exactamente COUNT+1 entradas), não só a sua presença: quando os dados e o cmplwi do próprio jogo discordam, a tabela é material de inspecção, não de produção"
  - "Ausência de bound check despromove sozinha para low: sem COUNT a varredura usa DEFAULT_SCAN_LIMIT=256 e dados que por acaso parecem endereços de .text produzem uma tabela inventada"
  - "Terminal condicional (b<cond>ctr) é detectado mas nunca high — o CTR pode vir de outro caminho"
  - "`lwzx` é obrigatório: mtctr+bctr sem carga indexada é ponteiro de função/vtable, não switch (mutação provou que afrouxar isto gera uma tabela FALSA com confidence=high)"
  - "`rlwinm` é sinal positivo (index_scaled), não requisito — há idiomas em que o índice já vem escalado; exigi-lo criaria falsos negativos"
  - "Não se importa discover_jump_tables: ela devolve só {addr: alvos}, sem a proveniência (bound/modo/base) de que o confidence precisa, e parti-la para a expor mudaria o lifter — proibido nesta fase. Duplica-se o mínimo, e a divergência (o filtro) é o ponto da ferramenta"
  - "confidence fica NO ficheiro em vez de ser retirado: o parser ignora-o (verificado) e a chave é o que permite revisão humana do TOML de produção"
  - "Saída sem timestamp: TOML determinístico é diffável — herda o espírito do golden do 17-01"

metrics:
  duration: "~50 min"
  completed: 2026-08-02
  tasks: 1
  commits: 1
  files_created: 2
  files_modified: 1
  test_assertions: 49

status: complete
---

# Phase 18 Plan 01: Detector de jump tables (ps3_analyse.py) Summary

Detector offline `tools/ps3_analyse.py` que acha dispatchers `mtctr`→`bctr`/`bcctr`
num ELF PPU e os declara em `[[switch_table]]` TOML, com um `confidence` por tabela
e filtro high-only por default — alimentando o mecanismo de `switch` que o lifter
**já tinha**, sem lhe tocar.

## O que foi feito

| Item | Estado |
|---|---|
| `tools/ps3_analyse.py` (detector + CLI + API) | criado, 551 linhas |
| `tools/test_ps3_analyse_jtables.py` (14 casos, 49 asserções) | criado, **PASS** |
| `docs/RECOMP_CONFIG.md` — secção `[[switch_table]]` + gerador | actualizado |
| `tools/ppu_lifter.py` | **NÃO tocado** (confirmado por `git status`) |
| Dados de jogo commitados | **nenhum** (dumps só em `/tmp`, apagados) |

## A heurística implementada

Para cada terminal `b…ctr` **não-linkante** (o `bctrl` é chamada indirecta, não
switch), olha para trás numa janela de `WINDOW_INSNS = 30` instruções:

1. `mtctr rC` — fonte do CTR; a varredura pára em qualquer chamada/retorno.
2. `lwzx rD, rA, rB` — carga indexada. **Obrigatória.**
3. Base da tabela: `lwz base, d(r2)` (um nível) ou `lwz mid, d1(r2); lwz base, d2(mid)`
   (dois níveis, PIC do gcc). Ambos os operandos do `lwzx` são candidatos a base, e
   cada candidato a TOC também; ganha o que **valida mais alvos** — a mesma política
   já provada no `discover_jump_tables`.
4. `add rC, *, base` → tabela de **offsets** com sinal; senão **absoluta**.
5. `rlwinm rIdx, rIdx, 2, …` → índice escalado (`index_scaled`, sinal positivo).
6. `cmplwi/cmpwi crN, rIdx, COUNT` → bound check; `COUNT+1` casos declarados pelo
   próprio código.

Entradas lidas big-endian; um alvo só conta se cair em `[text_lo, text_hi)` e for
alinhado a 4; a leitura pára no primeiro alvo inválido. **Zero alvos ⇒ não se emite nada.**

### `confidence`: `high` exige TODAS estas condições

* terminal `bctr` incondicional;
* cadeia completa (`mtctr` + `lwzx` + base resolvida);
* `>= HIGH_MIN_TARGETS` (2) alvos distintos validados;
* bound check presente (`0 <= COUNT <= MAX_BOUND_COUNT`);
* **coerência com o bound**: lidas exactamente `COUNT+1` entradas.

Qualquer falha ⇒ `low`, com a razão em `notes` e em comentário no TOML. `low` só sai
com `--include-low`, e o ficheiro leva um `# ATENCAO: … revisão humana OBRIGATÓRIA`.

## Os limites da heurística — medidos, não presumidos

Estão escritos no docstring do módulo (a fonte) e resumidos em `docs/RECOMP_CONFIG.md`.
Os que interessam ao 18-02:

* **Não apanha**: índice/tabela carregados por `ld`/`lwzu`/`lbzx` (tabelas de bytes ou
  half-words, idioma `-fpic` compacto), bases construídas por `addis/addi` sem passar
  pelo TOC, dispatchers cuja `lwzx` fica fora da janela de 30 instruções, tabelas com
  buracos (pára-se no primeiro alvo inválido) e tabelas de 64 bits (`ldx`).
* **Não verifica** se o alvo cai dentro da função que contém o dispatcher — isso é do
  `merge_switch_tables`, que já avisa; a emissão filtra por
  `func.start_addr <= c < func.end_addr` e encolhe em silêncio uma tabela mal declarada.
* **Multi-TOC** resolve-se por "quem valida mais alvos"; não há TOC por função.
* **`--raw` sem `--toc`** dá zero tabelas por construção (avisa em stderr).
* **Falso negativo não é fatal** (o `discover_jump_tables` corre à mesma no lift e os
  `patch_jumptable_*.py` continuam a ser a rede). **Falso positivo é que é o perigo** —
  daí o filtro conservador.

## Resultado real dos testes

```
.venv/bin/python3 tools/test_ps3_analyse_jtables.py   → 49/49 ok, EXIT=0
.venv/bin/python3 tools/test_ppu_jumptables.py        → EXIT=0
.venv/bin/python3 tools/test_switch_tables.py         → EXIT=0
.venv/bin/python3 tools/test_recomp_config.py         → EXIT=0
.venv/bin/python3 tools/test_invalid_instructions.py  → EXIT=0
.venv/bin/python3 tools/test_midasm_emit.py           → EXIT=0
```

Casos cobertos: guarda de codificação; caminho feliz `high` (com `dispatch` provado ser
o EA do **`bctr`**, não do `mtctr`); `bcctr` condicional (detectado, tapado em `low`);
backtrack `rlwinm`+`lwzx`; tabela de offsets; **quatro** casos de despromoção
(sem bound check / bound incoerente / um só alvo / terminal condicional); dois casos de
não-emissão (sem alvos válidos, sem `lwzx`); round-trip por `load_switch_tables`;
smoke da CLI por `subprocess` (com `--help` a documentar `-o` e `--include-low`); e
`--include-low` a mudar o resultado nos dois sentidos.

**RED antes do código:** primeira execução do teste falhou com
`ModuleNotFoundError: No module named 'ps3_analyse'`, antes de o módulo existir.

## Prova por mutação

Três defeitos injectados no `ps3_analyse.py`, corridos, e revertidos
(`diff -q` confirmou ficheiro idêntico ao original no fim):

| Mutação | Resultado |
|---|---|
| `dispatch = mtctr_addr` em vez do EA do `bctr` | **4 asserções falharam** (`high: dispatch…`, `bcctr: dispatch…`, `round-trip: dispatch preservado`, `cli: dispatch = EA do bctr`) |
| `confidence = "high"` sempre (filtro cede) | **8 asserções falharam**, incluindo `cli-low: nenhuma tabela por default` — a CLI passou a escrever uma tabela `low` num TOML de produção |
| `lwzx` deixa de ser exigido | **1 asserção falhou**, e falhou da pior maneira possível: emitiu uma tabela **FALSA com `confidence='high'`** a partir de um ponteiro de função |

**Achado honesto:** a mutação 3 **não discriminou à primeira**. A fixture original
(`mtctr`+`bctr` com memória vazia por trás) passava com ou sem a exigência do `lwzx` —
o teste existia mas não provava nada. A fixture foi **reforçada** (bound check, ponteiro
carregado do TOC e memória que parece mesmo uma tabela de alvos válidos) e a mutação
passou a ser apanhada. Está documentado no docstring do próprio caso de teste, para não
voltar a enfraquecer.

## Prova de que a saída é consumível

`test_round_trip_load_switch_tables` escreve o TOML gerado num ficheiro temporário e
chama o `recomp_config.load_switch_tables()` **existente**:

* `dispatch` preservado e igual ao EA do `bctr`;
* `cases` preservados na íntegra;
* `default == "indirect"`;
* `confidence = "high"` **está no ficheiro** e o parser **ignora-o sem levantar** —
  `_parse_switch_table` só lê `dispatch`/`default`/`cases`. Foi por isso que se decidiu
  deixar a chave em vez de a retirar antes do handoff: é o que permite revisão humana.

O `test_cli_smoke` repete a prova end-to-end por `subprocess`, sobre um blob `--raw`
sintético que contém código + TOC + tabela.

## Orientação contra o EBOOT local (NÃO é o aceite do 18-02)

Corrido em modo leitura contra `../gow2-recomp/EBOOT.ELF` com `--functions
functions.json`, **só para calibrar** — os dumps ficaram em `/tmp` e foram apagados;
nada disto entrou no git, e **nenhuma declaração de concordância com oráculo é feita
aqui** (isso é o plano 18-02, com re-lift).

```
text=[0x10200,0x4CBD2C)  toc=['0x541178']  insns=1307128
157 dispatcher(s) com tabela — 143 high, 14 low
```

Das 14 `low`: 7 por falta de bound check, 7 por terem apenas 1 alvo distinto. Nenhuma
por bound incoerente.

**Informação que o 18-02 vai precisar (e que muda o trabalho dele):** os EAs do ledger
`0x002A209C` (OPDISP) e `0x002B11B8` (TYDISP) **não aparecem como `dispatch`** — são os
EAs das **funções**, não dos `bctr`. Os alvos de aceite aparecem, mas noutros
dispatchers:

| Ledger (EA da função) | Alvo de aceite | `dispatch` real (EA do `bctr`) | Confiança |
|---|---|---|---|
| `0x002A209C` | `0x002A27BC` | **`0x002A2130`** | high |
| `0x002B11B8` | `0x002B12C4` | **`0x002B1224`** (offsets, bound=27, idx escalado) | high |

Ou seja: o detector encontra as duas tabelas dos pilotos, mas o 18-02 tem de casar
**função → dispatcher**, não `EA do ledger → dispatch`. Confundir os dois produziria um
TOML com `dispatch` errado que o lifter aceitaria e nunca usaria — exactamente a
surpresa "declarado mas sem efeito" que o `recomp_config.py` foi escrito para eliminar.
Os dois patches `patch_jumptable_*.py` **ainda não existem no disco** (`todo` no
ledger), o que reforça a hipótese `redundant` já levantada no 18-CONTEXT.

## Deviations from Plan

### O que não foi seguido à letra

**1. [Discricionário] `bcctr` implementado, não adiado.**
O plano admitia "se o decoder não suportar `bcctr` em v1, documentar skip". O
`ppu_disasm` **suporta** (`xo == 528` produz `b<cond>ctr`), portanto foi implementado —
mas tapado em `low`, porque um dispatcher condicional é ambíguo. Nada foi silenciosamente
fingido.

**2. [Rule 2 — funcionalidade crítica em falta] Coerência com o bound como requisito de `high`.**
O plano pedia "bound check / count plausível". A presença do `cmplwi` sozinha não
distingue uma tabela real de uma leitura que parou a meio: acrescentou-se a exigência de
`len(alvos) == COUNT+1`. É o filtro que apanha o caso "os dados e a verificação de
limites do próprio jogo discordam".

**3. [Rule 2] Casos de não-emissão que o plano não listava.**
`sem alvos` e `sem lwzx` foram acrescentados como testes: são exactamente as duas portas
por onde entraria uma tabela inventada.

Sem desvios de Regra 1 (bugs) nem Regra 4 (arquitectura). Nenhuma decisão arquitectural
foi tomada sem estar no 18-CONTEXT.

## Known Stubs

Nenhum. Não há valores fixos, `TODO`, nem caminhos por ligar no código entregue.
O que **não** está feito é escopo declarado de outros planos:
`games/gow2/config/gow2_switch_tables.toml` e a reactivação de `switch_table_file`
(18-02); migração dos `patch_jumptable_*` (18-03).

## Self-Check: PASSED

* `tools/ps3_analyse.py` — FOUND
* `tools/test_ps3_analyse_jtables.py` — FOUND
* `docs/RECOMP_CONFIG.md` — FOUND (modificado)
* commit `10c5739` — FOUND
* `git diff --diff-filter=D HEAD~1 HEAD` — vazio (nenhum ficheiro apagado)
* `tools/ppu_lifter.py` — ausente do `git status` (não tocado, como exigido)
* Ficheiros de outra sessão (`.planning/PROJECT.md`, `MILESTONES.md`, `ROADMAP.md`,
  `CLAUDE.md`, `games/gow2/CLAUDE.md`, `.planning/config.json`,
  `.planning/milestones/v1.2-*`, `scripts/gsd_plan*.sh`) — intactos e **fora** do commit
