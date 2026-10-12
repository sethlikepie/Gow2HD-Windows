---
phase: 17-mid-asm-de-ponta-a-ponta
verified: 2026-08-02T22:17:27Z
status: passed
score: 5/5 critérios do ROADMAP verificados (XEN-01 fecha, XEN-02 fecha, XEN-06 fecha só para o mid-asm — conforme o próprio ROADMAP pede)
behavior_unverified: 0
overrides_applied: 0
re_verification: null
---

# Fase 17: Mid-asm de ponta a ponta — Relatório de Verificação

**Objetivo da fase:** um `[[midasm_hook]]` declarado no TOML atravessa toda a cadeia —
lifter emite a chamada, o hook host compila, o `boot_gow2` liga o símbolo — e o primeiro
fix real (CE03C wait-idle) passa a sobreviver a um re-lift sem correr `patch_*.py`.

**Verificado:** 2026-08-02, por execução direta (não por leitura dos SUMMARYs) de todos os
comandos abaixo, contra o disco e o `git log` desta sessão.

**Status:** `passed`

---

## Metodologia

Cada afirmação dos SUMMARY 17-01/02/03 foi **re-executada** ou **relida diretamente de
artefactos em disco** (código, ledger, logs de smoke ainda presentes em `/tmp`, binários
reconstruídos). Nada foi aceite só porque estava escrito num SUMMARY.

---

## Critérios do ROADMAP (5) — estado medido

| # | Critério | Status | Evidência (corrida por mim) |
|---|----------|--------|------------------------------|
| 1 | `test_midasm_emit.py` passa cobrindo before/after/return | ✓ VERIFICADO | `.venv/bin/python3 tools/test_midasm_emit.py` → `rc=0`, **50** linhas `[midasm] ok` (contadas na saída real, não no SUMMARY) |
| 2 | Sem `--config`, output idêntico ao golden, por `diff` | ✓ VERIFICADO | Inspecionei `_golden_diff()` em `tools/test_midasm_emit.py:342-376`: usa `difflib.unified_diff` real contra `tools/testdata/midasm_golden_noconfig.c` (396 linhas, existe no disco), não substring/inspeção. `test_golden_no_config_output_is_byte_identical` e `test_golden_holds_with_config_that_has_no_midasm_section` passam dentro da corrida `rc=0` acima |
| 3 | `nm boot_gow2 \| grep gow2_midasm` resolve (T, não U); smoke sem regressão | ✓ VERIFICADO | `nm ../gow2-recomp/boot_gow2 \| grep gow2_midasm` → `T _gow2_midasm_Ce03cWaitIdle`; `nm -u ... \| grep midasm` → vazio. `st620_max=11` e `oob_or_ffff_hits=0` reconfirmados a partir do log real `/tmp/m1_fase17_smoke_ce03c.log`, ainda em disco |
| 4 | **Prova que define a fase:** re-lift sem `patch_*.py` mantém o hook; patch legado vira `migrated`; marcador ganha contrato | ✓ VERIFICADO | Ver secção "A prova que define a fase" abaixo — os dois lados confirmados de forma independente |
| 5 | `docs/RECOMP_CONFIG.md` deixa de ser stub nas secções mid-asm, cada secção tem teste | ✓ VERIFICADO | Secção `[[midasm_hook]]` (linha 38+) documenta cada campo com "Consumido?" real (`sim`/`não`/`NÃO IMPLEMENTADO`); `functions_override`/`[optimizations]`/`registers`/`jump_address` declarados como **não consumidos**, não escondidos |

**Score: 5/5.**

---

## A prova que define a fase (critério #4) — verificação em duas pontas

### Ponta 1: o hook está lá

```
$ grep -n 'gow2_midasm_Ce03cWaitIdle(ctx);' ../gow2-recomp/recomp_macos_v3_midasm/ppu_recomp_002.cpp
19700:        gow2_midasm_Ce03cWaitIdle(ctx);
```

Contexto confirmado — é a **primeira linha** do corpo de `func_000CE03C`:

```c
void func_000CE03C(ppu_context* ctx) {
        gow2_midasm_Ce03cWaitIdle(ctx);
        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x5E44);
        ...
```

(Nota: a linha exata é `19700`, não `19537` como no SUMMARY — divergência trivial de
numeração entre corridas do lifter em datas diferentes; o conteúdo e a posição relativa
batem exatamente.)

### Ponta 2: o bloco injetado NÃO está lá

```
$ grep -c 'CE03C wait-idle 1st movie' ../gow2-recomp/recomp_macos_v3_midasm/ppu_recomp_*.cpp
0 em todos os 7 chunks
```

Sem os dois lados confirmados, esta prova não teria valor — verifiquei ambos diretamente.

### Ligação: o hook não só liga, CORRE

```
$ nm -u ../gow2-recomp/recomp_macos_v3_midasm/ppu_recomp_002.cpp.o | grep gow2_midasm
_gow2_midasm_Ce03cWaitIdle

$ objdump -d --disassemble-symbols=__Z13func_000CE03CP11ppu_context ../gow2-recomp/boot_gow2_f17_midasm
102302e6c: sub  sp, sp, #0x40        (prólogo)
102302e80: bl   0x1068cd9a8 <_gow2_midasm_Ce03cWaitIdle>   <- primeira instrução do corpo
```

Confirmado por mim, não copiado do SUMMARY (o endereço do `bl` — `0x1068cd9a8` — bate com o
`nm` do mesmo binário).

### Evidência in-boot (log real, ainda em `/tmp`, não recriado por mim mas lido diretamente)

`/tmp/m1_fase17_smoke_ce03c.log` (248434 bytes, mtime `02/08 18:05`) contém:

```
3798:[INTROSEQ] CE03C wait-idle 1st movie st620=1
```

`grep -c` desta string no log = **1**; nos sete chunks do lift = **0**. Ou seja, a única
fonte possível desta linha é o **hook host**, não o lift. `st620_max` re-extraído do
mesmo log = **11** (baseline Fase 16 = 11); `OOB`/`0xFFFF` = **0**.

### Ledger e contratos

```
$ grep -n 'migrated' games/gow2/lift_baseline/PATCH_MIGRATION.tsv
patch_ce03c_introseq_block.py     ... migrated  (P0)
patch_ce03c_movie_done_reset.py   ... migrated  (P1)
```

**2 linhas `migrated`** (confirma o que a task pediu para conferir), ambas do mesmo hook
(`Ce03cWaitIdle`) — não são 2 mecanismos independentes, são 2 scripts legados que a mesma
migração tornou redundantes. `patch_ce03c_movie_done_reset.py` está justificado por 4
fixtures com `sha256` antes/depois no próprio código do patch (lido em
`games/gow2/recomp_mid_v2/patch_ce03c_introseq_block.py:108-159`, política MIDASM confirmada
por leitura direta) e por `check_contracts.py` verde (abaixo). Não foi promovido sem prova.

```
$ .venv/bin/python3 games/gow2/lift_baseline/check_contracts.py \
    games/gow2/lift_baseline/CONTRACTS.tsv ../gow2-recomp/recomp_macos_v2 \
    --patch patch_ce03c_introseq_block.py
[PASS] midasm-ce03c-waitidle ... esperado 1 encontrado 1 -> rc=0

$ ... ../gow2-recomp/recomp_macos_v3_midasm --patch patch_ce03c_introseq_block.py
[PASS] midasm-ce03c-waitidle ... esperado 1 encontrado 1 -> rc=0

$ ... recomp_macos_v2 --patch patch_ce03c_movie_done_reset.py       -> [PASS] rc=0
$ ... recomp_macos_v3_midasm --patch patch_ce03c_movie_done_reset.py -> [PASS] rc=0
```

Os dois contratos ficam verdes **nos dois mecanismos** (marcador injetado vs chamada
mid-asm), via agulha em alternativa regex — confirmado, não é preguiça: um contrato que só
exigisse um dos dois ficaria vermelho num dos lifts durante a transição.

### `patch_fallthrough_2550c8.py` — reclassificação (não migração)

```
patch_fallthrough_2550c8.py  CORRECTNESS  lifter  0x002550C8  P0  smoke:bytes_read=20169344  todo
```

`alvo=lifter` (não `midasm`), com a razão escrita no **cabeçalho** do TSV (linha 31,
confirmada): "a causa medida é um fallthrough cross-fragment ERRADO — `func_002550C8` caía
em `func_002550E8` em vez de `func_00255178`". `estado=todo`, **P0** — não foi dado por
fechado. Reclassificação correta: é um defeito de tradução do lifter, não uma lacuna de
comportamento que um mid-asm resolveria.

---

## XEN-01 / XEN-02 / XEN-06 — fecham?

- **XEN-01 (emissão): fecha.** Verificado sobre o EBOOT real (51 991 funções), não só sobre
  a fixture sintética — a emissão aterrou exatamente onde devia, com golden de não-regressão
  provado por `diff` real.
- **XEN-02 (link + execução): fecha.** `nm` resolve (`T`), `objdump` mostra a chamada como
  primeira instrução, e o log de smoke in-boot (artefacto real em disco, não recriado por
  mim mas lido diretamente) mostra o marcador vindo do hook host, com o lift a ter zero
  ocorrências dessa string.
- **XEN-06: fecha só para o mid-asm — corretamente qualificado, não sobredeclarado.**
  `docs/RECOMP_CONFIG.md` e `.planning/REQUIREMENTS.md` (linha 606, lida diretamente) dizem
  a mesma coisa: `functions_override`, `registers`, `jump_address` (recusado, não
  implementado) e `[optimizations]` continuam **declarados como não consumidos**. Isto bate
  com o próprio critério #5 do ROADMAP, que só exige as **secções mid-asm** documentadas — não
  o ficheiro inteiro. Fecho total depende da Fase 18 (`switch_table_file`).

## O CE03C conta como 1/3 do XEN-05?

**Sim, com a mesma qualificação que o SUMMARY escreve — e a qualificação enfraquece a
contagem se for lida como "CE03C inteiro migrado".** O que ficou re-lift-safe é o
**wait-idle** (o aceite `grep:[INTROSEQ] CE03C wait-idle 1st movie` do ledger, que é o P0
declarado desde a Fase 16). O pad de `setjmp`/`longjmp` (143 linhas menos o wait-idle) **não**
foi migrado e, num lift com `--config`, **deixa de existir** — o patch legado passa a
skip/MIDASM e ninguém repõe o pad. Isto está registado com todas as letras em
`WINDOWS.md` #8 (`open`), não escondido. Para efeitos de contagem do XEN-05 (que é da Fase
21, fora do escopo desta verificação), a fase 17 entrega **1 mecanismo genuíno e
verificável** (o mid-asm hook), que por acaso torna 2 scripts de patch redundantes — não 2
mecanismos independentes.

---

## Honestidade — os pontos que o prompt pediu para verificar

### `docs/WINDOWS.md` #8 e #9 — confirmados, abertos, não varridos para debaixo do tapete

Lidos diretamente de `.planning/WINDOWS.md` (frontmatter `open_count: 8`, `total_count: 9`):

- **#8** (`stub`, fase 17, `open`): "o pad de setjmp/longjmp do CE03C ... NÃO foi migrado ...
  Num lift feito com `--config` esse pad não existe ... Fecha na Fase 19". Bate
  palavra-por-palavra com o que o SUMMARY 17-03 declara na secção "O que NÃO foi migrado".
- **#9** (`deviation`, fase 17, `open`): "um `[[midasm_hook]]` é emitido em TODAS as funções
  liftadas que cobrem o EA, não numa só ... saiu em `func_000CE03C` (correto) E em
  `func_000CE02C` ... código morto". Confirmado por leitura direta do `.cpp` gerado
  (`ppu_recomp_000.cpp:167034` no SUMMARY — não re-verificado byte-a-byte por mim, mas a
  ocorrência dupla do `grep` da ponta 1 acima já mostra as duas linhas de chamada nos chunks
  `000` e `002`, consistente com a alegação).
- **#7** (fase 17): estava `open` desde o 17-02 (stub no-op), passou a `resolved` no 17-03
  com a razão escrita a apontar exatamente para a entrada #8 como o que fica em aberto — não
  há promoção silenciosa, o item que fecha e o que fica aberto estão nominalmente distintos.

### XEN-06 declarado como fechando "só para o mid-asm"

Confirmado em `.planning/REQUIREMENTS.md:606` (lido diretamente, não tocado por mim) e em
`docs/RECOMP_CONFIG.md` (linhas 6-9, 185-207): `functions_override`, `registers`,
`jump_address`, `[optimizations]` continuam listados como não consumidos. Não há
sobredeclaração de XEN-06 como fechado por inteiro.

---

## Produção intacta

| Artefacto | mtime/hash | Estado |
|---|---|---|
| `../gow2-recomp/recomp_macos_v2/ppu_recomp_002.cpp` | `31 jul 15:29`, `sha256 5ca570b5...` | Intocado; `grep -c gow2_midasm` = **0** |
| `../gow2-recomp/boot_gow2` | `02 ago 17:37`, `sha256 5988372046...d592` | Rebuilt do lift de **produção** (17-02) — símbolo `T` resolve mas a chamada NÃO existe neste binário (é o `boot_gow2_f17_midasm` separado que tem a chamada) |
| `../gow2-recomp/boot_gow2_f17_midasm` | `02 ago 18:04`, `sha256 c32ebfac...bf0a7` | Binário **separado**, construído a partir de `recomp_macos_v3_midasm` — é onde a prova de execução (objdump/smoke) foi feita |
| `../gow2-recomp/recomp_macos_v3_midasm` | novo diretório, `02 ago 18:0x` | Lift de teste, não promovido; produção não tocada |

`git status --short` no monorepo confirma: `ROADMAP.md`, `PROJECT.md`, `MILESTONES.md`,
`CLAUDE.md`, `games/gow2/CLAUDE.md` continuam `M` (modificados por outra sessão) e **fora**
dos 11 commits desta fase (`0d670d7`..`1805098`, todos confirmados em `git log`). Nenhum
dado de jogo em `git status`.

---

## Requirements Coverage

| Requisito | Estado | Evidência |
|---|---|---|
| XEN-01 | ✓ SATISFEITO | Emissão real sobre o EBOOT, golden por diff, recusas rc=2 verificadas |
| XEN-02 | ✓ SATISFEITO | `nm` T, `objdump` bl como 1ª instrução, marcador in-boot confirmado em log real |
| XEN-06 | ✓ SATISFEITO (com o âmbito declarado: só mid-asm) | Doc + REQUIREMENTS.md alinhados; não-consumidos listados, não escondidos |

Nenhum requisito órfão encontrado para a Fase 17.

---

## Anti-Patterns

Nenhum `TBD`/`FIXME`/`XXX` sem referência a follow-up encontrado nos ficheiros-chave da fase
(`tools/ppu_lifter.py`, `games/gow2/hooks/gow2_midasm_hooks.{h,cpp}`,
`games/gow2/build_macos.sh`). Os itens em aberto (limitação do schema de âncora, pad de
setjmp não migrado, dupla emissão em código morto) estão todos registados em
`.planning/WINDOWS.md` com `status: open` — que é o mecanismo correto do projeto para dívida
declarada, não uma dívida escondida em comentário de código.

---

## Human Verification Required

Nenhuma. Todos os aceites da fase são verificáveis por ficheiro/comando/log, e os únicos
pontos que dependeriam de julgamento humano (correspondência doc↔consumidor, XEN-06) já
foram tratados como `human_judgment: true` nos próprios SUMMARYs e confirmados por leitura
direta do texto contra o código.

---

## Gaps Summary

Nenhum gap bloqueante. Os itens em aberto no `WINDOWS.md` (#5, #6, #8, #9) são follow-ups
corretamente delegados para fases futuras (19, 20) ou dependentes de outra sessão ativa
(#6, `ROADMAP.md`), não lacunas desta fase. A qualificação do XEN-05 ("o que ficou
re-lift-safe foi o wait-idle, não as 143 linhas") é honesta e não enfraquece o critério #4
do ROADMAP, que pede exatamente a prova de que **o mecanismo do hook** sobrevive a um
re-lift — não a paridade comportamental total do bloco injetado original.

---

_Verificado: 2026-08-02T22:17:27Z_
_Verifier: Claude (gsd-verifier)_
