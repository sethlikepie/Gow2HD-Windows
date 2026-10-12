---
phase: 18-jump-tables-detectadas-e-declaradas
plan: 03
subsystem: tooling
tags: [ledger, jump-tables, switch_table, relift, contracts, smoke, macos-arm64, gow2, xen-03, xen-05]

requires:
  - phase: 18-jump-tables-detectadas-e-declaradas
    plan: 01
    provides: "tools/ps3_analyse.py e o achado de que os EAs do ledger sao das FUNCOES; os bctr reais estao em 0x002A2130 e 0x002B1224"
  - phase: 18-jump-tables-detectadas-e-declaradas
    plan: 02
    provides: "gow2_switch_tables.toml, switch_table_file reactivado, o oraculo 54/54 e 22/22, e os dois lifts /tmp/relift_18_02_{noconfig,config} que este plano reaproveitou em vez de re-liftar do zero"
  - phase: 17-mid-asm-de-ponta-a-ponta
    provides: "recipe de smoke (st620_max=11), padrao de prova de re-lift, e a armadilha do switch_table_file inexistente a matar o --config inteiro"
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "ledger com os dois P0 e os aceites grep; baseline de smoke st620_max=11 / OOB=0"
provides:
  - "VEREDICTO: os dois patch_jumptable_*.py sao estado=redundant, por medicao numa escada A/B/C -- nao migrated, e nenhum conta para o XEN-05"
  - "estado=redundant entra no dominio da coluna (gen_migration_ledger.py), com a regra de contagem escrita no proprio ficheiro"
  - "4 contratos executaveis novos, escopo=ea e agulha de span, provados por mutacao"
  - "A lacuna herdada do 18-02 FECHADA: RELIFT=1 ./build_macos.sh le os dois ficheiros de config no ../gow2-recomp, que chegaram la no mesmo commit"
  - "Prova de que a Fase 18 mudou ZERO bytes funcionais do lift (diff dos 7 chunks = so' o carimbo /* lifter-rev */)"
  - "WINDOWS.md destrancado: a entrada 7 corrompida desde a Fase 17 fazia o windows append falhar"
  - "Smoke in-boot: st620_max=11, OOB=0, ICALL-BAD=0, orfaos=0"
affects: [19 padrao de prova de re-lift, 21 metrica XEN-05 -- que herda 2 migrated e 2 redundant, contados em separado]

tech-stack:
  added: []
  patterns:
    - "Escada de medicao A/B/C antes de classificar: A discrimina redundant de migrated, e so' A responde 'o patch alguma vez foi preciso?'"
    - "Contrato ancorado na funcao dona (escopo=ea) quando o mesmo texto existe noutras funcoes -- um contrato global aqui ficaria verde com o dispatcher partido"
    - "Contrato de SPAN (tres extremos da tabela) em vez de um unico case: uma jump table truncada e' um salto para lixo, e um unico case nao a distingue de uma tabela inteira"
    - "Prova por mutacao aplicada ao proprio contrato: remover o ultimo alvo faz o span falhar e o case unico continuar verde"
    - "Veredicto delimitado ao que o aceite cobre: o `redundant` refere-se a jump table, e o residuo do patch que NAO e' jump table fica registado como tal em vez de ser varrido para dentro do veredicto"

key-files:
  created: []
  modified:
    - games/gow2/lift_baseline/PATCH_MIGRATION.tsv
    - games/gow2/lift_baseline/CONTRACTS.tsv
    - games/gow2/lift_baseline/gen_migration_ledger.py
    - docs/RECOMP_CONFIG.md
    - .planning/WINDOWS.md

key-decisions:
  - "Os dois P0 ficam `redundant` e nao `migrated`: no lift SEM patch e SEM --config os dois aceites ja' saem, dentro do switch da funcao dona do bctr, com 54 e 22 alvos -- os mesmos numeros do oraculo. A regra do 18-CONTEXT e' explicita e foi seguida contra o interesse da metrica"
  - "O ficheiro do 2b11b8 NAO foi transformado em verificador idempotente apesar de o plano o sugerir: isso removeria de futuros lifts um fix que o lift de PRODUCAO ainda carrega, sem A/B in-boot que o justifique. O plano diz 'extend, do not regress' -- aqui estender seria regredir"
  - "Nenhum dos dois ficheiros foi apagado (o plano proibe-o nesta fase) nem alterado"
  - "O `redundant` cobre a jump table, que e' o que o `aceite` da linha mede. O residuo do 2b11b8 (substituir a leitura de TOC-0x151C) nao e' jump table e fica registado a parte, no cabecalho do ledger e no WINDOWS, em vez de desaparecer dentro do veredicto"
  - "A entrada 7 do WINDOWS.md foi corrigida: `resolved` nao existe no vocabulario (open|waived|fixed) e trancava o `windows append` desde a Fase 17. Corrigido `status` e os contadores derivados do frontmatter, que de outro modo passariam a discordar e mantinham o ficheiro invalido"
  - "Os dois ficheiros de config chegaram ao ../gow2-recomp no MESMO commit: separados, a armadilha do 17-03 (chave activa + ficheiro ausente = --config morto por inteiro) voltaria a matar o mid-asm"

metrics:
  duration: "~70 min"
  completed: 2026-08-02
  tasks: 2
  commits: 2 (+1 no espelho ../gow2-recomp)
  files_modified: 5
  patches_classificados: 2
  contratos_novos: 4
  st620_max: 11
  oob_hits: 0

status: complete
---

# Phase 18 Plan 03: Classificação dos dois jumptable P0 Summary

Os dois `patch_jumptable_*.py` são **`redundant`**, não `migrated`: medido que o
`discover_jump_tables` já produz as duas tabelas inteiras sozinho, sem patch e sem
declaração. O marco fica com **2 fixes re-lift-safe**, não 3 — e este plano
acrescentou **zero** ao numerador do XEN-05, de propósito.

## O veredicto, e o comando que o produziu

| Piloto | Veredicto | Comando que decidiu |
|---|---|---|
| `patch_jumptable_2a209c.py` | **`redundant`** | `.venv/bin/python3 /tmp/jt_ctx.py /tmp/relift_18_02_noconfig` |
| `patch_jumptable_2b11b8.py` | **`redundant`** (com ressalva) | idem |

O comando não é um `grep`. Um `grep` do aceite não chegava, e a razão está medida:
o texto `case 0x002A27BCu: goto loc_002A27BC;` aparece em **três** funções do lift.
O script localiza cada ocorrência, identifica a **função que a contém** e conta os
casos do `switch` que a envolve:

```
ppu_recomp_001.cpp:20895  func=func_002A209C (decl @20845)  [2a209c(OPDISP)]
    n_cases=54 distinct=54  range=0x002A221C..0x002A3648
ppu_recomp_001.cpp:33911  func=func_002B11B8 (decl @33871)  [2b11b8(TYDISP)]
    n_cases=22 distinct=22  range=0x002B1298..0x002B2170
ppu_recomp_002.cpp:273694 func=func_002A2100 ...  n_cases=54
ppu_recomp_005.cpp:16160  func=func_002A20F4 ...  n_cases=54
```

**54 e 22 alvos distintos são exactamente os números do oráculo do 18-02** (54/54 e
22/22 contra as tabelas escritas à mão nos patches), e o `switch` fecha com
`default: ps3_indirect_call(ctx); return;`. Não é o aceite a passar por acaso — é a
tabela inteira, na função certa.

## A escada A/B/C

| Passo | `--config` | patch | Resultado |
|---|---|---|---|
| **A** | não | não | Os dois `case` **já saem**. 54 e 22 alvos. ⇒ candidato a `redundant` |
| **B** | sim | não | Idêntico: 54 e 22. A declaração **não** é load-bearing hoje |
| **C** | sim | sim | `2a209c` **no-op byte a byte**; `2b11b8` **injecta** (ver ressalva) |

Passo C, saída literal:

```
$ patch_jumptable_2a209c.py /tmp/relift_18_03_stepC
OK: jump table ja' materializada pelo proprio lifter em ppu_recomp_001.cpp --
    switch cobre 54/54 alvos 0x2A2134+offset; nada a injectar
rc=0
SHA antes:  25a83563f825ccea4ac2ed8c8d399dae75ddd43bfcc6b0f0b02ad58322d32e38
SHA depois: 25a83563f825ccea4ac2ed8c8d399dae75ddd43bfcc6b0f0b02ad58322d32e38
VEREDICTO C1: NO-OP (ficheiro byte-a-byte igual)
```

Confirmado também **dentro do build real**, no `apply_all_patches.sh` sobre o lift
novo: `ALREADY-APPLIED patch_jumptable_2a209c.py`.

Como **A já passa**, a regra do 18-CONTEXT manda `redundant` e não `migrated`. Foi
seguida — o resultado é que este plano contribui **0** para os ≥3 do XEN-05.

## A ressalva medida do `2b11b8` (o que o veredicto *não* cobre)

Ao contrário do `2a209c`, este patch **não** é no-op no passo C:
`APPLIED patch_jumptable_2b11b8.py`. Ele não duplica o `switch` (que fica intacto);
substitui o **cálculo do CTR** — a leitura de `TOC-0x151C` na memória guest — por uma
tabela hardcoded. Isso **não é a jump table**, logo não está coberto por um veredicto
cujo `aceite` mede a jump table.

Medição estática decisiva sobre a EBOOT:

```
TOC = 0x00541178
  [TOC-0x1694] = 0x002A2134   esperado 0x002A2134   IGUAL
  [TOC-0x151C] = 0x002B1228   esperado 0x002B1228   IGUAL
```

As duas entradas do TOC contêm **exactamente** as bases que os patches hardcodam: a
leitura que o lift faz está correcta **na imagem**. Além disso o próprio lift já traz
o bound check do jogo (`cmplwi 0x1B` + `bgt loc_002B12C4`), tornando o clamp
`ty < 0x1C ? ty : 0` do patch redundante também.

Isto **enfraquece muito** a hipótese original do patch (`ICALL-BAD ctr=0x27182818`)
mas **não a refuta em runtime** — uma corrupção de memória em execução continua
possível. Por isso:

- o ficheiro **não foi tocado nem apagado**;
- **não** foi convertido em verificador idempotente, apesar de o plano o sugerir: o
  lift de **produção** (`recomp_macos_v2`) ainda carrega este patch (marcador
  `Task4 FIX` presente), e removê-lo sem A/B in-boot seria regredir um fix vivo. O
  plano diz *"extend, do not regress"*; aqui estender era regredir;
- ficou registado no cabeçalho do ledger, no `docs/RECOMP_CONFIG.md` e no
  `WINDOWS.md` (entrada 10) como trabalho por fechar, com o método: A/B in-boot no
  caminho typemap/WAD, que o smoke de intro de 25 s (`PS3_MOVIE_EOS=0`) **não**
  exercita.

## Estado final do ledger

```
estado: {'todo': 136, 'migrated': 2, 'redundant': 2}   total 140

migrated:   patch_ce03c_introseq_block.py     (alvo=midasm, Fase 17)
            patch_ce03c_movie_done_reset.py   (alvo=midasm, Fase 17)
redundant:  patch_jumptable_2a209c.py         (alvo=switch_table, esta fase)
            patch_jumptable_2b11b8.py         (alvo=switch_table, esta fase)
```

`estado=redundant` passou a existir no domínio da coluna, e a regra de contagem vive
no `gen_migration_ledger.py` (o cabeçalho é reconstruído a cada `--merge`, portanto
uma nota escrita à mão no TSV desapareceria no primeiro regen). Regen verificado
**idempotente**: segunda corrida não muda um byte.

## Quantos fixes re-lift-safe tem o marco — a resposta directa

**Dois**, não três. E há uma leitura ainda mais conservadora que convém deixar escrita
para a Fase 21 não a descobrir tarde: as duas linhas `migrated` são **dois patches**
mas **um só mecanismo** — o `midasm-ce03c-donereset` do `CONTRACTS.tsv` diz
explicitamente que o corpo do hook `Ce03cWaitIdle` **subsume** o
`patch_ce03c_movie_done_reset.py`. Se o XEN-05 contar *mecanismos* em vez de *linhas
do ledger*, o marco tem **1**.

O XEN-05 pede ≥3. O numerador é **2 (ou 1)**, e os dois `redundant` desta fase **não
entram nele**. Um `redundant` é inventário honesto, não migração: descobriu-se que o
patch nunca foi preciso, ninguém migrou nada.

## O smoke — números e nível de evidência

Binário **reconstruído do lift sob teste** (não se mede um binário velho contra um
lift novo — lição da Fase 16): `boot_gow2_f18_jtable`, 121 666 520 bytes, a partir de
`recomp_macos_v3_jt18` (lift novo, **nunca** por cima de `recomp_macos_v2`), com
`apply_all_patches.sh` aplicado.

Recipe idêntico ao baseline da Fase 16 e ao smoke do 17-03. **Kill por PID** (`TERM`,
1 s, `-9`), nunca `pkill -f`. Script e log em `/tmp`, fora dos dois repositórios.

```
st620_max=11            (piso >=3; baseline Fase 16 = 11)
oob_or_ffff_hits=0      (baseline Fase 16 = 0)
lifted_functions=52142
sequencia_st620=0 1 3 3 3 11 11
linhas_log=3825  bytes=246566
orfaos_pos_run=0
introseq_ce03c_hits=1
icall_bad_hits=0
opdisp_tydisp_hits=0
```

- **`icall_bad_hits=0`** é o número desta fase em particular: uma jump table declarada
  errada dá salto para endereço inválido, e o sintoma seria `ICALL-BAD` nos dois
  dispatchers. Zero.
- **`introseq_ce03c_hits=1`** — a Fase 17 continua viva **in-boot** com o
  `switch_table_file` activo.
- **`opdisp_tydisp_hits=0`** não é evidência de nada: os marcadores `[OPDISP]`/`[TYDISP]`
  são probes gated e estão OFF, e com `PS3_MOVIE_EOS=0` o caminho do typemap nem chega
  a ser percorrido. Registado para não ser lido como sinal.
- **`sticky` não foi usado como critério** (não-observável, medido na Fase 16).

Níveis de evidência: **offline-unit** (escada A/B/C, contratos, mutação, `diff` de
lifts, 7 suites) e **in-boot** (smoke de 25 s). Nada aqui é "não-exercitado" excepto o
que está nomeado como tal acima.

## Os contratos novos — e por que não é um `grep` do aceite

Quatro entradas em `CONTRACTS.tsv`, duas por piloto:

| id | escopo | agulha | valor |
|---|---|---|---|
| `jt-2a209c-case` | `ea 0x002A209C` | o `case` do aceite | ≥1 |
| `jt-2a209c-span` | `ea 0x002A209C` | regex sobre 1º alvo \| aceite \| último alvo | ≥3 |
| `jt-2b11b8-case` | `ea 0x002B11B8` | o `case` do aceite | ≥1 |
| `jt-2b11b8-span` | `ea 0x002B11B8` | idem | ≥3 |

Duas decisões, ambas por medição:

1. **`escopo=ea`, não `global`.** O texto do aceite existe em três funções; um contrato
   global ficaria **verde com o dispatcher partido**, desde que um dos irmãos o
   mantivesse.
2. **Contrato de span.** Um `case` só não distingue "tabela inteira" de "tabela truncada
   a uma entrada" — e uma tabela truncada é precisamente um salto para lixo.

**Prova por mutação** (remover o último alvo do lift e voltar a correr):

```
[PASS] jt-2a209c-case  ... esperado 1 encontrado 1
[FAIL] jt-2a209c-span  ... esperado 3 encontrado 2
rc=1
```

O contrato antigo passaria; o novo apanha. Mutação revertida (o directório mutado foi
apagado).

Verdes em **quatro** árvores independentes: `noconfig`, `config`, o lift do caminho real
de build, e o lift de **produção** `recomp_macos_v2`.

## A lacuna herdada do 18-02: FECHADA

O 18-02 provou o `--config` invocando o `ppu_lifter.py` **directamente** e declarou
honestamente que o caminho real de build não estava exercitado. Estava certo: o
`RELIFT=1 ./build_macos.sh` lê `$HERE/config/` no `../gow2-recomp`, onde a chave
continuava comentada e o TOML de tabelas não existia.

Os dois ficheiros foram para lá **no mesmo commit** (`02807d8` no espelho) — separados,
a armadilha do 17-03 voltaria a matar o `--config` inteiro e com ele o mid-asm. Saída
literal do build:

```
  RELIFT: --config .../gow2-recomp/config/gow2_recomp.toml
  Config: 1 midasm_hook(s) loaded from .../gow2-recomp/config/gow2_recomp.toml
  Config: 2 switch_table(s) declared in .../gow2-recomp/config/gow2_switch_tables.toml
  switch tables: 157 from discovery, 2 from config, 2 overridden by config, 157 total
```

### E a prova de que esta fase mudou zero bytes funcionais

`diff` entre o lift do caminho real de build e o lift `--config` do 18-02:

| Ficheiro | Linhas diferentes |
|---|---|
| `ppu_recomp.h` | **0** |
| `ppu_recomp_000..006.cpp` | 2 cada |

E as 2 linhas são o carimbo de proveniência:

```
289c289
< /* lifter-rev: f4789d0 */
---
> /* lifter-rev: 94f25ef */
```

**Zero bytes funcionais.** Isto fecha duas questões de uma vez: o caminho real produz o
mesmo que a invocação directa, e nenhuma consequência observada nesta fase pode ser
atribuída à declaração das duas tabelas.

## Testes

```
tools/test_ps3_analyse_jtables.py            EXIT=0
tools/test_switch_tables.py                  EXIT=0
tools/test_midasm_emit.py                    EXIT=0
tools/test_recomp_config.py                  EXIT=0
tools/test_ppu_jumptables.py                 EXIT=0
tools/test_invalid_instructions.py           EXIT=0
games/gow2/lift_baseline/test_check_contracts.py  EXIT=0  (7/7)
```

## Deviations from Plan

**1. [Regra 1 — bug] `.planning/WINDOWS.md` entrada 7 corrompida (o defeito que o 18-02 declarou e não pôde corrigir).**
`status: "resolved"` — valor fora do vocabulário do schema (`open|waived|fixed`) — fazia
o `windows append` abortar com `Ledger entry 6 has invalid status` **desde a Fase 17**.
Corrigido para `fixed` (o 17-03 provou o corpo real do hook por re-lift, `objdump` e
smoke in-boot). Corrigidos também os **contadores derivados do frontmatter**
(`open 8→7`, `fixed 0→1`), sem os quais o ficheiro continuava inválido — o `append`
falhava a seguir com `Ledger counts disagree with entries`. Só a entrada 7 foi tocada.
Com o ledger destrancado, as duas entradas deste plano ficaram registadas.

**2. [Discricionário, contra a sugestão do plano] O `patch_jumptable_2b11b8.py` NÃO foi convertido em verificador idempotente.**
O plano sugere "prefer making the patch idempotent verifier … extend, do not regress".
Medido que aqui **estender é regredir**: o lift de produção carrega este patch aplicado,
e a conversão removeria de futuros lifts um fix vivo sem A/B in-boot que o justifique.
Documentado em vez de feito.

**3. [Fora de escopo, registado] `apply_all_patches.sh` devolve `rc=1`.**
O gate de delta `UNVERIFIED` reporta 1 NOVO: `patch_freelist_guard_262610.py`. **Não é
desta fase** — provado pelo `diff` acima (zero bytes funcionais) e pelo facto de o mesmo
patch já aparecer `UNVERIFIED` nos dois `apply_all_patches` da Fase 17
(`/tmp/m1_f17_apply_patches*.log`). No conjunto o delta é uma melhoria grande:
`UNVERIFIED current=12 baseline=42, 31 resolvidos / 1 novo`. Registado no `WINDOWS`
(entrada 11) com o método para fechar: escrever-lhe um contrato executável.

Sem desvios de Regra 4 (arquitectura).

## Known Stubs

Nenhum artefacto entregue é stub. O que fica em aberto está nomeado, com método:

| Item | Onde está registado |
|---|---|
| Resíduo do `2b11b8` (bypass de `TOC-0x151C`) por classificar — exige A/B in-boot | `WINDOWS` #10, cabeçalho do ledger, `RECOMP_CONFIG.md` |
| `patch_freelist_guard_262610.py` sem contrato → `rc=1` do apply | `WINDOWS` #11 |
| 141 tabelas `high` não declaradas (medidas idênticas à descoberta) | herdado do 18-02, sem acção |

## Estado do XEN-03

**Fecha.** Detector (18-01) + declaração consumida (18-02) + **≥1 patch classificado com
prova** (este plano: dois, ambos `redundant`, com escada A/B/C, contratos executáveis e
mutação). O critério pedia classificação medida, não uma migração fabricada.

## Self-Check: PASSED

* `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` — FOUND (2 linhas `redundant`, regen idempotente)
* `games/gow2/lift_baseline/CONTRACTS.tsv` — FOUND (4 contratos novos, verdes em 4 árvores)
* `games/gow2/lift_baseline/gen_migration_ledger.py` — FOUND (domínio + notas)
* `docs/RECOMP_CONFIG.md` — FOUND (modificado)
* `.planning/WINDOWS.md` — FOUND (válido: `windows append` rc=0)
* `../gow2-recomp/config/gow2_switch_tables.toml` — FOUND (chegou com a config, no mesmo commit)
* `/tmp/m1_fase18_smoke_jtable.log` — FOUND (3825 linhas)
* commits `94f25ef`, `f7f7c2a` (monorepo) e `02807d8` (espelho) — FOUND
* `git diff --diff-filter=D` sobre os commits — **vazio** (nenhum ficheiro apagado)
* **Nenhum dado de jogo, log, lift ou binário commitado**: os 7 ficheiros commitados são
  TSV/Markdown/Python/TOML; EBOOT e `functions.json` só foram lidos
* Lifts de teste em `/tmp/relift_18_03_*` e `../gow2-recomp/recomp_macos_v3_jt18` —
  **nunca** sobre `recomp_macos_v2`; binário novo em `boot_gow2_f18_jtable`, produção intacta
* Órfãos pós-smoke = **0**; kill por PID, nunca `pkill -f`
* Ficheiros da outra sessão (`.planning/PROJECT.md`, `MILESTONES.md`, `ROADMAP.md`,
  `CLAUDE.md`, `games/gow2/CLAUDE.md`, `.planning/config.json`, `.planning/milestones/v1.2-*`,
  `scripts/gsd_plan*.sh`, e no espelho `recomp_mid_v2/patch_41f78c_prune_probe.py`) —
  intactos e **fora** dos commits
* `roadmap.update-plan-progress` e verbos `state.*` — **não usados** (proibidos no brief)
