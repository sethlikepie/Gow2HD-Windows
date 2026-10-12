# Fase 8, Plano 08-01, Tarefa 2 — A/B real dos patches (hoje vs e6d65a2)

## Decisão

`patches candidato`

**Mas com uma reserva critica que a Tarefa 2 do Plano 08-02 TEM de ler antes de
comprometer um binario a esta pista** (ver secção "Confundimento medido"
abaixo) — o diff nao-vazio aqui NAO significa que os patches sejam,
isoladamente, causa provavel da regressao do 2º `StartSeq`; significa apenas
que os dois conjuntos produzem `.cpp` diferentes, e que grande parte dessa
diferenca vem de **12 patches novos** (instrumentação + fixes incrementais de
sessões posteriores a `e6d65a2`), não de uma mudança de comportamento no MESMO
patch entre as duas eras.

## Contagens de cada sandbox (`patch_ab_sandbox.sh`, Plano 08-01 Tarefa 1)

| Conjunto | REF | extraidos | modulos partilhados | excluidos PROBE | final (FUNCIONAL) |
|---|---|---|---|---|---|
| hoje | WORKTREE | 89 | 1 (lift_paths.py) | 25 | **64** |
| e6d65a2 | e6d65a2 | 74 | 1 (lift_paths.py) | 22 | **52** |

(A exclusão PROBE usou o catálogo de HOJE, `PATCH_CATALOG.tsv`, para ambos os
conjuntos — aproximação estrutural documentada no Plano 08-01: a classificação
PROBE é estrutural — sufixo `_probe`/`_trace`, corpo sem tokens de despacho —
não temporal.)

## Resumo do `--status` de cada lado (runner real, 2 passagens para convergência)

Lift base: `python3 tools/ppu_lifter.py EBOOT.ELF --functions functions.json`
(commit actual do `ppu_lifter.py`, pós-`2e21639`), **51 991 funções**, copiado
duas vezes (`/tmp/lift_ab_hoje`, `/tmp/lift_ab_e6d65a2`) — as duas cópias
partem do MESMO lift, só os patches aplicados por cima diferem.

### `patchdir_hoje` (64 patches) sobre `/tmp/lift_ab_hoje`

| corrida | APPLIED | ALREADY-APPLIED | NO-MATCH | UNVERIFIED | FAILED |
|---|---|---|---|---|---|
| 1ª | 46 | 0 | 0 | 17 | 1 |
| 2ª (convergência) | 0 | 21 | 0 | 42 | 1 |

`rc` agregado = 1 nas duas corridas (esperado — o `verify_lift.sh` interno
falha por razões pré-existentes fora deste escopo, per o próprio plano). O
único `FAILED` estável nas duas corridas: `patch_postthr_spin_r12.py`
(FUNCIONAL) — não investigado aqui (fora do escopo desta tarefa; ver
`deferred-items.md` da Fase 8).

### `patchdir_e6d65a2` (52 patches) sobre `/tmp/lift_ab_e6d65a2`

| corrida | APPLIED | ALREADY-APPLIED | NO-MATCH | UNVERIFIED | FAILED |
|---|---|---|---|---|---|
| 1ª | 11 | 0 | 2 | 7 | 32 |
| 2ª (convergência) | 0 | 2 | 5 | 13 | 32 |

Convergência confirmada: 0 `APPLIED` na 2ª corrida; os 11 patches `APPLIED` da
1ª corrida reclassificam-se na 2ª (2 `ALREADY-APPLIED` + 3 `NO-MATCH` + 6
`UNVERIFIED` = 11), sem oscilação. `FAILED=32` estável nas duas corridas
(nenhum `FAILED-PARTIAL` em qualquer conjunto, nas quatro corridas — nenhuma
escrita parcial).

## Confundimento medido (LER ANTES DE USAR ESTE DIFF COMO SINAL)

**32 dos 52 patches de `e6d65a2` falham (rc≠0) quando reaplicados contra o
lift ACTUAL** (51 991 funções, pós-`2e21639`) — não porque o conteúdo desses
patches tenha mudado de comportamento, mas porque o **shape do lift** evoluiu
o suficiente entre `e6d65a2` e hoje (mais fragmentos, mais dispatchers, mais
funções materializadas) para que a agulha textual que os patches de
`e6d65a2` procuram já não exista na mesma forma. Isto é o MESMO problema que
motivou o `lift_paths.py` originalmente (ver o próprio ficheiro,
2026-07-24) — só que aqui é a agulha inteira que já não bate, não o tipo do
argumento.

Consequência prática: `/tmp/lift_ab_e6d65a2` **não é uma reconstrução fiel**
do que o conjunto de patches de `e6d65a2` produzia na ÉPOCA de `e6d65a2`
(quando aplicado ao lift da ÉPOCA) — é uma reconstrução **sub-patchada**
(32/52 no-op) aplicada a um lift moderno. Se o Plano 08-02 medir o binário
resultante de `/tmp/lift_ab_e6d65a2` e ele falhar no gate, **isso não prova
"os patches são a causa"** — pode simplesmente provar "um lift moderno com 32
patches funcionais em falta não arranca", o que seria um resultado
completamente diferente e não relacionado com a regressão do 2º `StartSeq`.

**Recomendação para o Plano 08-02:** ao medir o binário, usar SEMPRE
`--include-probe` (patch_ab_sandbox.sh) e os patches COMPLETOS de cada
conjunto tal como o Plano 08-02 já planeia — mas interpretar uma eventual
diferença de comportamento entre os dois binários com este confundimento em
mente. Se o binário `e6d65a2` falhar da MESMA forma que a produção falha
hoje, isso é consistente com "não são os patches" (a pista já fica mais
fraca); só um binário `e6d65a2` que **passe** o gate seria evidência forte a
favor de "patches candidato" — e mesmo assim teria de se isolar QUAL dos ~12
patches novos (não qual dos 32 que falharam ao reaplicar) é responsável,
porque os 32 FAILED não escreveram nada (confirmado: zero FAILED-PARTIAL) e
portanto não podem ser a causa de uma DIFERENÇA de conteúdo — só os 11 que
efectivamente aplicaram (`APPLIED`, tabela acima) o fizeram.

## Âncoras do caminho crítico (teste anterior, 6 âncoras conhecidas)

| Âncora | Presente no diff? |
|---|---|
| `func_00147038` (thr_auto_load) | **NÃO** — byte-a-byte idêntico entre os dois conjuntos |
| `func_002C00DC` | **NÃO** |
| `func_000CE03C` | **NÃO** |
| `INTROSEQ` | sim (1 ocorrência, chunk 000) |
| `MOVIEFSM` | **NÃO** |
| `func_002B4274` | sim (chunk 001 — F2B-RESTATUS/F2B-STREAM-PUMP inseridos antes de STICKY-CONSUME) |
| `STICKY-RESTORE` | **NÃO** |
| `STICKY-CONSUME` | sim (chunk 001, mesmo bloco de `func_002B4274`) |

`func_00147038` (thr_auto_load, o elo exacto que a regressão pára) é
**idêntico** nos dois conjuntos de patches — nenhum patch, em nenhuma das
duas eras, toca o corpo dessa função. Isto pesa CONTRA a pista dos patches
como causa directa da regressão nomeada pela Fase 6/7 (o elo que falha é
justamente `thr_auto_load`/`R_PermA`).

## Lista completa de assinaturas `void func_XXXXXXXX(` tocadas pelo diff (25, não só as 6 âncoras conhecidas)

```
func_00010200  func_0002F3F0  func_000A0594  func_000B71B8  func_000CB56C
func_000CBB2C  func_0018E814  func_00262610  func_0028C564  func_002B0FB4
func_002B3F78  func_002B4274  func_002BA9BC  func_002BA9F0  func_002BA9F4
func_002BAB88  func_002E11EC  func_002E1228  func_002E1290  func_002E1480
func_00330D54  func_0039E794  func_0042A1D4  func_0042A2D0  func_00468C3C
```

Classificação por natureza (leitura do diff completo, não amostragem):

- **Instrumentação pura (fprintf gated por env var, sem `vm_write`/lógica
  nova):** maioria — `[TYMAP-*]`, `[WADLD-*]`, `[SS-*]`, `[CC9D0]`,
  `[COMBOPROP]`, `[BA808]`, `[A1-CALL30D54B]`, `[CBB2C-DISC]`,
  `[CB56C-DISC]`. Nunca alteram o fluxo de execução.
- **Fixes always-on com efeito real** (aplicados incondicionalmente, não só
  sob env var):
  - `func_0002F3F0` (`patch_b71_2f3f0_guard.py`) — tecto de 256 iterações no
    loop de hash de string (evita hang com ponteiro para banda comitada não
    terminada em zero).
  - `func_0042A1D4` (`patch_fios_42a1d4_empty.py`) — salta o walk de lista
    quando a contagem é negativa (tabela vazia), evitando EA lixo.
  - `func_000CB56C` (`patch_type15_cb56c_highbit.py`) — marca bit alto
    (`0x80000015`) quando o tipo lido é `0x15`, para bater o caminho WAD.
  - Vários locais convertidos de despacho manual (`ctx->ctr = ...;
    ps3_indirect_call`) para `ps3_call_opd(ctx, ...)` — `func_002B0FB4`,
    `func_002B9CD8`-area (chunk 001), `func_0032E4xx`-area (chunk 003) — isto
    é o padrão OPD já documentado (`patch_opdisp_entry.py` e afins),
    substituindo leitura directa do OPD por uma chamada que resolve o
    endereço de forma mais robusta.
  - `func_002B0FB4`/vizinhança em `ppu_recomp_003.cpp` — dispatcher de
    switch PPC materializado como tabela `k_jt` estática (comentário
    "Task4 FIX" no próprio diff) em vez de ler a tabela da memória guest —
    isto é trabalho de investigação da parede TYPE15 (adiado, não desta
    fase), presente em `hoje` e ausente em `e6d65a2`.
  - `func_00330D54`/chunk 002 linha ~249484 — `ALLOC-NULL-GUARD`: nunca
    estampar tags em EA 0 (mesma classe do fix `func_002550C8` do
    megaprojeto do pool de handles).
- **Nenhuma função da lista acima está na cadeia crítica pré-`R_PermA`**
  (thr_auto_load/2º movie/WAD open) — todas pertencem a território pós-WAD
  (registry de tipos/shaders, factory type-15, F2B stream) ou são
  instrumentação read-only.

## Ficheiros divergentes (`diff -rq`)

```
ppu_recomp_000.cpp  (1290 linhas de diff unificado)
ppu_recomp_001.cpp  (1899 linhas de diff unificado)
ppu_recomp_002.cpp  (203 linhas de diff unificado)
ppu_recomp_003.cpp  (291 linhas de diff unificado)
ppu_recomp_005.cpp  (75 linhas de diff unificado)
```

`ppu_recomp_004.cpp` e `ppu_recomp_006.cpp`: **idênticos** entre os dois
conjuntos.

## Lembrete explícito

Mesmo com diff não-vazio (`patches candidato`), a confirmação da causa exige
medir o **BINÁRIO** com `smoke_chain_gate.sh` (Plano 08-02) — este diff é só
o filtro de custo zero, nunca a prova final. E, dado o confundimento medido
acima (32/52 patches de `e6d65a2` falham por deriva de forma do lift, não por
mudança de comportamento), o Plano 08-02 deve tratar um eventual resultado
"e6d65a2 falha também" como **neutro** (não decisivo), e um resultado
"e6d65a2 passa, hoje falha" como o único sinal realmente forte a favor desta
pista — sabendo que, mesmo nesse caso, `func_00147038` (thr_auto_load) e as
outras 5 das 6 âncoras críticas ficaram byte-a-byte idênticas entre os dois
conjuntos, o que torna essa pista estruturalmente pouco provável face ao
elo exacto que falha (thr_auto_load/R_PermA).
