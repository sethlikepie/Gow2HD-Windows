---
phase: 16-invent-rio-de-patches-e-baseline-medido
plan: 03
subsystem: tooling
tags: [baseline, verify_lift, smoke, medicao, g6, ledger, gow2]

requires:
  - phase: 16-01 (esqueleto do ledger + gen_migration_ledger.py)
    provides: PATCH_MIGRATION.tsv com cabecalho reconstruido por render_tsv
  - phase: 16-02 (decisao humana + cabecalho derivado)
    provides: 19 linhas classificadas e o padrao "numeros do cabecalho vivem no gerador"
provides:
  - baseline verify_lift medido e congelado (rc=0, 3 passos PASS por delta)
  - baseline smoke de intro medido e congelado (st620_max=11, OOB=0)
  - dois campos declarados NAO-OBSERVAVEL/NAO-OBSERVADO com a razao tecnica
  - linhas_baseline_runtime(): o cabecalho volta sozinho a "nao-exercitado" se a constante for None
affects: [17 (nao-regressao do smoke), 18, 20, 21 (delta antes/depois)]

tech-stack:
  added: []
  patterns:
    - "medicao nao-derivavel vive como constante datada no gerador, nao como linha # a mao que o proximo --merge apagaria"
    - "metrica sem emissor regista-se como NAO-OBSERVAVEL, nunca como 0"

key-files:
  created:
    - .planning/phases/16-invent-rio-de-patches-e-baseline-medido/16-03-SUMMARY.md
  modified:
    - games/gow2/lift_baseline/gen_migration_ledger.py
    - games/gow2/lift_baseline/PATCH_MIGRATION.tsv
    - .planning/ROADMAP.md
    - .planning/STATE.md
    - .planning/REQUIREMENTS.md

key-decisions:
  - "sticky regista-se NAO-OBSERVAVEL, nao 0: os ps3_fios_sticky_* nao tem um unico fprintf, logo o grep do plano media o nada"
  - "nao houve rebuild do boot_gow2 apesar de o binario ser mais antigo que o lift -- reconstruir seria arrumar o ambiente para o baseline parecer melhor (G5)"
  - "os numeros do baseline vivem em BASELINE_VERIFY/BASELINE_SMOKE dentro do gerador, porque render_tsv reconstroi o cabecalho a cada --merge"

requirements-completed: [LEDG-01]

duration: ~25min
completed: 2026-08-02
status: complete
---

# Fase 16 Plano 03: baseline medido do marco v1.3

**O `verify_lift.sh` correu uma vez contra o lift de produção e deu `rc=0` com os três
passos verdes por delta; o smoke de intro correu uma vez, 25 s, morto por PID, e deu
`st620_max=11` com zero OOB — e os dois campos que o plano mandava preencher mas que o
ambiente não consegue observar ficaram escritos como `NÃO-OBSERVÁVEL` e `NÃO-OBSERVADO`,
com a razão técnica, em vez de virarem zeros que pareceriam medições.**

## Performance

- **Duração:** ~25 min
- **Tasks:** 2/2
- **Ficheiros modificados:** 5 (2 no artefacto, 3 de planeamento)
- **Commits:** 2 (`b2030fb`, `ee17e2c`) + 1 de metadados

## Os números REAIS, e o comando exacto que os produziu

### Task 1 — `verify_lift.sh` (nível de evidência: **offline-unit**)

```bash
export PS3_ENGINE_ROOT="$(pwd)"
./games/gow2/verify_lift.sh ../gow2-recomp/recomp_macos_v2 \
  > /tmp/m0_fase16_verify_lift.log 2>&1; echo "VERIFY_RC=$?"
```

| Métrica | Valor | Evidência |
|---|---|---|
| `rc` | **0** | rodapé do log, linha 147-149 |
| `lift_parity` | **PASS** — current=77 baseline=77 novos=0 resolvidos=0 | log:98-100 |
| `MANIFEST` | **PASS** — `MANIFEST_DEBT` current=0 baseline=36 novos=0 **resolvidos=36** | log:108-122 |
| `audit_boundaries` | **PASS** — current=180 baseline=180 novos=0 resolvidos=0 | log:142-144 |
| LIFT | `../gow2-recomp/recomp_macos_v2` (produção: default de `build_macos.sh` **e** de `apply_all_patches.sh`) | log:2 |
| `LIFT_SHA256` | `73c8abd21abb17e829fc4186b55a5b2395a3d0e4b4f8019bf6bd8e8cc901ae7f` | log:3 |
| ELF desassemblado | 51 917 funções | log:10 |

Verde à primeira, **uma** corrida, sem `VERIFY_ORACLE` (isso é Fase 20). Nota digna de
registo: a dívida congelada do MANIFEST (36 achados) aparece **toda como resolvida** neste
lift — o gate passa por *delta*, não por limiar, exactamente como D-5.1 previu.

### Task 2 — smoke de intro (nível de evidência: **in-boot**)

```bash
# /tmp/m0_fase16_run_smoke.sh -- vive em /tmp de proposito, fora dos dois repos
cd ../gow2-recomp
( set -a; . ./env_gow2.sh; set +a
  unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}')
  export PS3_NO_RSX=1 PS3_RSX_BACKEND=trace PS3_PERF_FSM=1 PS3_MOVIE_EOS=0
  unset PS3_MOVIE_DONE_MS PS3_VDEC_FORCE_SEQDONE_MS
  exec ./boot_gow2 EBOOT.ELF ) > /tmp/m0_fase16_smoke.log 2>&1 &
BPID=$!; sleep 25; kill -TERM $BPID; sleep 1; kill -9 $BPID
```

| Métrica | Valor | Nível de evidência |
|---|---|---|
| `st620_max` | **11** — sequência `0→1→3→3→11→11`, `overlay_done=1` | in-boot |
| `oob_or_ffff_hits` | **0** — zero `OOB`/`0xFFFF` em 3707 linhas | in-boot (ausência real) |
| `sticky_hits` | **NÃO-OBSERVÁVEL** | ver secção própria |
| `bytes_read` | **NÃO-OBSERVADO** | ver secção própria |
| Órfãos pós-run | **0** (`pgrep -f boot_gow2 \| wc -l`) | in-boot |
| Lifted functions | 52 068 | in-boot |
| Tamanho do log | 231 805 bytes / 3707 linhas | in-boot |
| Fim do log | `[CONTENT] boot logo queue DONE — hold cleared (guest/menu)` — sem crash | in-boot |

**Uma** corrida, sem retry (não houve falha de `vm commit`). `st620_max=11` está bem acima
do gate `≥3` do CLAUDE.md.

**Correcção ao comando do plano (Regra 1):** o snippet do plano calcula
`st620_max` sem filtrar `4294967295`. Esse valor é `0xFFFFFFFF`, o sentinela "sem estado"
do sampler — o próprio `smoke_m0_baseline.sh` exclui-o (`grep -v '^4294967295$'`). Usar o
comando literal do plano teria produzido `st620_max=4294967295`, um número falso. Usei o
filtro já estabelecido no projecto.

## Os dois campos que NÃO foram medidos, e porquê

Isto é o coração honesto deste plano. O plano mandava escrever `sticky=<N>` e o aceite
`smoke:bytes_read=20169344`. Nenhum dos dois é observável nesta receita, e escrever `0`
teria sido pior do que não escrever nada.

### `sticky` — NÃO-OBSERVÁVEL (não é `0`)

```bash
grep -rn "sticky" runtime libs --include="*.c" --include="*.cpp" --include="*.h" \
  | grep -E "fprintf|printf"        # -> vazio
```

`ps3_fios_sticky_publish` / `_peek` / `_consume` (`runtime/ppu/ppu_loader.cpp:1370-1400`)
manipulam `g_fios_sticky_op[]` / `g_fios_sticky_pub[]` e **não imprimem uma única linha**.
Não há um `fprintf` com "sticky" em todo o `runtime/` nem em todo o `libs/`.

Consequência: `grep -c sticky` sobre o log dá `0` **quer o sticky esteja vivo, quer esteja
morto**. É uma métrica sem emissor, não uma métrica a zero. Registá-la como `sticky=0`
seria fabricar uma observação — e pior, as Fases 17/21 leriam esse `0` como uma regressão
a evitar quando ele não mede nada.

**Consequência de segunda ordem, registada em `WINDOWS.md` (#4):** o critério de
não-regressão `sticky_pub>=1` que o `CLAUDE.md` e o `games/gow2/CLAUDE.md` prescrevem
**não é verificável por log neste build**. Quem quiser esse gate tem de acrescentar um
emissor gated por env var, ou observar de outra maneira.

### `bytes_read` — NÃO-OBSERVADO

Os três emissores possíveis são `libs/video/movie_hle.c:476` (dump periódico de trace),
`:856` (stats no `close`) e `:730` (`[GATE-FORCE] R_PermA full flagged`). **Nenhum
disparou** em 25 s: o único `[movieio] open` do log foi o `gow2.psarc` a cair para o
*guest path*, e com `PS3_MOVIE_EOS=0` (receita de baseline, sem EOS forçado) o `R_PermA`
não chega a ser lido até ao fim.

Portanto o aceite `smoke:bytes_read=20169344` do P0 `patch_fallthrough_2550c8.py`
**continua não-exercitado** — a entrada #3 do `WINDOWS.md`, aberta pelo 16-02, fica aberta.
Não é uma falha: é a receita de baseline a não ser a receita que exercita a parede [C].
Uma corrida com `PS3_MOVIE_EOS=1` + `PS3_VDEC_FORCE_SEQDONE_MS` seria outra medição, e o
plano pede **uma**.

## Achado que afecta a comparação das Fases 17–21

**O binário testado é mais antigo do que o lift verificado.**

```
boot_gow2                       mtime 2026-07-29 15:07
recomp_macos_v2/ppu_recomp_*.cpp  5 dos 7 chunks: 2026-07-31 (mais recentes)
```

Ou seja: a Task 1 verificou o **código-fonte** do lift de produção; a Task 2 exercitou um
**binário compilado de um estado anterior desse mesmo lift**. As duas metades do baseline
não descrevem exactamente o mesmo artefacto, e isso está escrito no cabeçalho do ledger
com todas as letras.

**Não reconstruí.** Um rebuild seria precisamente "arrumar o ambiente para a linha de base
parecer melhor" — e mudaria o artefacto medido a meio de um plano cujo mandato é *registar*
o estado inicial. A ressalva vale mais do que um número cosmeticamente coerente. Fica como
trabalho explícito para a Fase 17/21: quando forem comparar, têm de reconstruir primeiro.

## Contagens do cabeçalho: já não eram as do 16-01, e agora estão certas

O briefing avisava que as contagens congeladas pelo 16-01 (`139 build / 88 monorepo /
divergência 51`) já não descreviam a realidade depois do commit `7f7581d` de outra sessão.
**Verificado: já estavam corrigidas.** O 16-02 regenerou o ledger e o cabeçalho passou a
`140 / 140 / 140`, divergência `0`; a regeneração desta sessão re-derivou os mesmos
números, confirmando que o corpus não voltou a mexer-se. Não havia números mortos no
ficheiro — porque são **derivados a cada regen**, que é exactamente o ponto do padrão.

O que **estava** desactualizado era o critério 3 do `ROADMAP.md`, que afirmava "a
2026-08-02 são 88 e 139". Anotei-o com a medição posterior em vez de o reescrever: o texto
original fica como o que era verdade quando o critério foi escrito, e a nota diz o que a
medição passou a ver. A divergência `0` é uma medição de **contagem de ficheiros**, não uma
afirmação sobre o conteúdo — e está assim escrita.

## Onde vivem os números (e porque não no TSV à mão)

O 16-02 moveu a construção do cabeçalho para dentro de `render_tsv()`. Qualquer linha `#`
escrita à mão no TSV seria **apagada** no primeiro `--merge`. Como as medições de runtime
não são deriváveis (saem de correr um script e um boot), passaram a viver como constantes
datadas no gerador:

- `BASELINE_VERIFY` e `BASELINE_SMOKE` (`gen_migration_ledger.py`)
- `linhas_baseline_runtime()` — função pura que as renderiza e que **cai sozinha** para
  `POR MEDIR ... [nivel de evidencia: nao-exercitado]` se a constante for `None`.

A ausência de medição nunca vira um campo com ar de preenchido. Provado: o commit da Task 1
tinha `BASELINE_SMOKE = None` e o cabeçalho dizia `baseline smoke intro: POR MEDIR`.

## Task Commits

1. **Task 1: baseline do `verify_lift`** — `b2030fb` (feat)
2. **Task 2: baseline do smoke da intro** — `ee17e2c` (feat)

Ambos em português, sem `Co-Authored-By`, `git add` com paths explícitos.

## Verificação de integridade do artefacto

| Verificação | Resultado |
|---|---|
| Linhas de dados intactas após regen (diff das 140 linhas) | **vazio** — nenhuma coluna humana tocada |
| `--merge` idempotente byte-a-byte (`diff -q` de 2 regens) | **limpo**, nas duas tasks |
| `test_gen_migration_ledger.py` | **rc=0**, 8 PASS |
| `test_gen_catalog.py` | **rc=0** |
| Gates automáticos da Task 1 (4) | todos OK |
| Gates automáticos da Task 2 (4) | todos OK |
| Logs no índice git | **nenhum** (`git ls-files \| grep m0_fase16` vazio) |

## Desvios do plano

### 1. [Regra 1 — bug no comando do plano] `st620_max` sem filtro do sentinela

Descrito acima. O comando do plano teria dado `4294967295`. Usei o filtro do
`smoke_m0_baseline.sh`. **Sem commit próprio** — afecta só o valor registado.

### 2. [Regra 2 — honestidade de medição] `sticky` e `bytes_read` não viraram zeros

O plano previa `sticky_hits=<N>` e `oob_or_ffff_hits=<N>` como contagens brutas. Para o
`sticky`, a contagem bruta é indistinguível de "sem emissor", por isso o campo passou a
prosa declarada com a razão técnica e um apontador para o código. Isto é a regra 4 do
`CLAUDE.md` aplicada literalmente. **Commit `ee17e2c`.**

### 3. [Regra 3 — bloqueio estrutural] Editei o gerador, não só o TSV

`files_modified` do plano lista apenas `PATCH_MIGRATION.tsv`. Como o 16-02 passou o
cabeçalho para `render_tsv()`, editar só o TSV produziria um cabeçalho que o próximo
`--merge` apagava — o artefacto pedido não sobreviveria a uma regeneração. Toquei em
`gen_migration_ledger.py` (constantes + uma função pura nova). **Nada em `runtime/`,
`libs/`, nem no lifter.** O briefing antecipava exactamente isto.

### 4. [Precedência] Commits sem `Co-Authored-By`

O workflow GSD manda assinar; o `CLAUDE.md` do projecto (regra 3) proíbe co-autoria e exige
português. Aplicou-se a regra do projecto.

### 5. [Instrução do briefing] Actualização de estado à mão

Não usei `roadmap.update-plan-progress` nem os verbos `state.*` (medido pelo 16-01:
corrompem o ROADMAP e regridem o STATE para o v1.1). `ROADMAP.md`, `STATE.md` e
`REQUIREMENTS.md` foram editados à mão, com alcance mínimo.

**Total: 5 desvios.** Nenhum alarga escopo; três aumentam a honestidade do artefacto.

## Nada foi tocado do que é de outra sessão

Ficaram intactos e fora de todos os commits: `.planning/PROJECT.md`,
`.planning/MILESTONES.md`, `CLAUDE.md`, `games/gow2/CLAUDE.md`,
`.planning/milestones/v1.2-*.md`, `.planning/config.json`,
`.planning/GSD-MODEL-ROUTING.md`, `scripts/gsd_plan.sh`, `scripts/gsd_plan_with_grok.sh`.
Confirmado por `git status --short` antes de cada commit.

Zero dados de jogo commitados. Zero logs commitados — vivem em `/tmp`:
`/tmp/m0_fase16_verify_lift.log`, `/tmp/m0_fase16_smoke.log`,
`/tmp/m0_fase16_run_smoke.sh`.

## Must-haves do plano — estado

| Must-have | Estado | Evidência |
|---|---|---|
| `verify_lift.sh` corrido UMA vez contra produção, `rc` real no cabeçalho | ✅ | `rc=0`, log:147-149 |
| Smoke UMA vez com kill por PID; `st620_max`, sticky e OOB registados | ✅ com ressalva | `st620_max=11`, OOB=0; `sticky` registado como **não-observável**, com a razão |
| Logs fora dos dois repositórios | ✅ | `/tmp`; `git ls-files` sem ocorrências |
| Baseline vermelho não seria "arrumado" | ✅ (não se aplicou) | nasceu verde; mas o binário desactualizado **não** foi reconstruído para o esconder |

## Critério 4 do ROADMAP Phase 16

**Verdadeiro, com duas ressalvas escritas.** Os números existem, são medidos e estão no
cabeçalho do ledger. As ressalvas — `sticky` não observável e binário mais antigo que o
lift — são parte do baseline, não notas de rodapé: quem comparar na Fase 21 tem de as ler.

## Known Stubs

Nenhum stub de código. Ausências declaradas:

- **`sticky_hits`** — métrica sem emissor no build actual (`WINDOWS.md` #4, aberta).
- **`bytes_read`** — aceite `smoke:` do `patch_fallthrough_2550c8.py` continua por
  exercitar (`WINDOWS.md` #3, aberta desde o 16-02).
- **121 das 140 linhas** do ledger continuam sem decisão humana (por desenho do 16-02).

## Self-Check: PASSED

- `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` — FOUND (140 linhas de dados, cabeçalho com os dois blocos de baseline)
- `games/gow2/lift_baseline/gen_migration_ledger.py` — FOUND (suite rc=0, 8 PASS)
- `/tmp/m0_fase16_verify_lift.log` — FOUND (150 linhas)
- `/tmp/m0_fase16_smoke.log` — FOUND (231 805 bytes)
- Commits `b2030fb`, `ee17e2c` — FOUND em `git log`
