# Phase 4: Catálogo de patches como gate que falha - Context

**Gathered:** 2026-07-26
**Status:** Ready for planning
**Mode:** Autónomo — tudo abaixo é MEDIDO nesta sessão

<domain>
## Phase Boundary

Os `patch_*.py` deixam de ser folclore de sessão e viram um gate automatizado: ou se
aplicam limpos sobre o lift novo, ou o comando sai não-zero apontando qual falhou.

**Entra também nesta fase** (decisão de 2026-07-26): o item 1 e o item 2 do plano de
adopção do `auto-re-agent` (`/tmp/PLANO_ADOPCAO_AUTO_RE_AGENT.md`), porque atacam
exactamente este defeito. Ver D-4.5 e D-4.6.

**Fora:** o oráculo com manifesto de proveniência, o `sync-check` monorepo↔irmão, o
`ENV_REGISTRY`, o sinal FP/VMX e o `LEDGER.json` — são do mesmo plano mas de outra fase.
</domain>

<measured_state>
## Estado MEDIDO — não re-derivar

### O defeito central, com o número exacto

O `apply_all_patches.sh` classifica por **`(rc, o ficheiro mudou?)`**:

```
rc != 0            -> FAILED
rc == 0 e mudou    -> APPLIED
rc == 0 e inalterado -> ALREADY-APPLIED     <- não conta como falha
```

Não há nada no runner que distinga **«já estava aplicado»** de **«não casou nada»**.
São o mesmo estado.

Medido na 3ª passagem contra `recomp_macos_v3` fresco: `APPLIED 0 / ALREADY 84 / FAILED 3`.
**84 dos 87 patches ficam num estado que o runner não sabe interpretar.**

Foi assim que 36 conversões OPD desapareceram sem ninguém dar por isso:

```
$ patch_32e200_opd.py <lift>
  32E200: fixed 0 OPD sites (pat1=0 pat2=0), remaining indirect=7
  OK patch_32e200_opd
  rc=0
```

`fixed 0` seguido de `OK` e `rc=0`. Conteúdo inalterado ⇒ `ALREADY-APPLIED` ⇒ gate verde.

### Correcção a uma afirmação que circulou nesta sessão

Um agente afirmou que «83 de 87 patches não têm um único `sys.exit(1)`». **É falso.**
Medido com um parser a sério (o `grep -E` com lookahead negativo não funciona e dava 0):

| | |
|---|---|
| patches totais | **87** |
| **conseguem** sair não-zero por algum caminho | **86** |
| incapazes de falhar em qualquer circunstância | **1** (`patch_14a01c_opd.py`) |

O defeito **não** é falta de caminhos de erro. É que os caminhos de erro cobrem
«ficheiro não encontrado» e não cobrem **«corri e não produzi efeito»**. Um patch com
`sys.exit(2)` para o ficheiro em falta continua a devolver `rc=0` quando as agulhas não
casam. É uma distinção que muda o desenho: não basta acrescentar `sys.exit`, é preciso
que cada patch saiba dizer se o seu **efeito** aconteceu.

### O que já foi corrigido hoje (o padrão a generalizar)

Seis patches passaram a distinguir três estados, com `rc≠0` no terceiro:

| estado | significado | rc |
|---|---|---|
| `CONVERTIDO` | ≥1 conversão nesta corrida | 0 |
| `JÁ-APLICADO` | 0 conversões **mas** os sítios já estão no estado final | 0 |
| `SEM-EFEITO` | 0 conversões **e** 0 sítios já convertidos | **≠0** |

Ficheiros: `patch_32e200_opd.py`, `patch_39e5d8_opd.py`, `patch_icg_ctor_opd.py`,
`patch_14b1f0_opd.py`, `patch_b71_2f3f0_guard.py`, `patch_ba808_wadld_eof.py`.

### O achado que reordena as prioridades

```
audit_boundaries       invocada em 0 scripts .sh
lift_parity            invocada em 0 scripts .sh
apply_all_patches.sh   chama verify_lift_baseline: 0 vezes
```

**As duas perdas de hoje não passaram por falta de detecção — passaram porque nada corre.**
O `audit_boundaries --oracle` já nomeava a `func_0037C384` com os 40 bytes exactos. Não
faltam detectores; falta um ponto de entrada que corra os que existem.

### O que falta para os critérios do ROADMAP

| Critério | Estado medido |
|---|---|
| 1. `PATCH_CATALOG.tsv` com todos classificados, zero indeterminados | **não existe** |
| 2. `--status <tsv>` no runner, zero FAILED fora de PROBE | `--status` **não existe** (só `--check`); 3 FAILED hoje |
| 3. Sai não-zero quando um patch não aplica, provado por injecção de falha | não |
| 4. Os PROBE fora do gate, listados nominalmente | não |

Números do ROADMAP que estão **desactualizados** e devem ser corrigidos no plano:
«73 rastreados» e «71 a fechar» → hoje são **87**. O baseline «APPLIED 13 / ALREADY 9 /
FAILED 49» → hoje é **0 / 84 / 3** contra o v3.

Os 3 FAILED actuais são `patch_cc9d0_live_yield.py`, `patch_ce03c_pre_play_stop.py`,
`patch_ce03c_wait_idle_f2b_movie.py` — e são **diferentes** dos 4 historicamente
documentados, que agora aplicam limpos contra o v3.
</measured_state>

<decisions>
## Decisões

### D-4.1 — O runner passa a ter SEIS estados, não três
`APPLIED`, `ALREADY-APPLIED`, `NO-MATCH`, `UNVERIFIED`, `FAILED`, `SKIPPED`.
O que hoje é um único `ALREADY-APPLIED` parte-se em três: já aplicado (verificado por
pós-condição), não casou nada (`NO-MATCH`, falha), e não foi possível verificar
(`UNVERIFIED`, também falha — um `UNKNOWN` não pode contar como sucesso).

### D-4.2 — A verdade vem da PÓS-CONDIÇÃO, não do `rc` do patch
Um patch que se auto-declara bem-sucedido não é prova. O `CONTRACTS.tsv` declara, por
patch, o que tem de ser **verdade no lift depois** — contagem de um símbolo numa função,
presença de uma agulha, ausência de outra. O runner verifica isso independentemente do
que o patch imprimiu. É a diferença entre acreditar no executor e medir o resultado, que
já apanhou três afirmações falsas nesta sessão.

### D-4.3 — `PATCH_CATALOG.tsv` é DERIVADO, nunca escrito à mão
Gerado por script a partir dos próprios `patch_*.py` + do `MANIFEST.tsv`. Um catálogo
mantido à mão diverge no primeiro dia. O teste exige cobertura de 87/87 e falha quando
alguém acrescenta um patch que o catálogo não conhece.

### D-4.4 — Os PROBE ficam fora do gate mas DENTRO do catálogo
Listados nominalmente, com a razão. Um patch fora do gate sem razão escrita é como um
marcador sem dono.

### D-4.5 — `verify_lift.sh`: um ponto de entrada único (item 1 do plano de adopção)
Corre os verificadores que já temos — `lift_parity`, `audit_boundaries`,
`verify_lift_baseline` — contra **baselines congelados**, e falha por **delta**, nunca por
limiar absoluto. Já provámos duas vezes hoje que limiares absolutos punem melhorias: o
`PREAMBLE ps3_indirect_call` falhou quando o lift melhorou, e o grupo `opd-dispatch`
acusa −319 que na verdade é −619 de lixo do lift antigo **mais +291 de código recuperado**.

### D-4.6 — O `apply_all_patches.sh` passa a chamar o gate no fim
Hoje chama zero vezes. Aplicar patches e não verificar o resultado é metade do trabalho.

### D-4.7 — Corrigir o `patch_14a01c_opd.py`
É o único incapaz de falhar em qualquer circunstância, e é dono de 2 conversões OPD
(`func_0014A01C`, `func_0014AD94`) que hoje são feitas pelo `patch_icg_ctor_opd.py`
porque ele está partido. Mesmo padrão dos outros seis.
</decisions>

<code_context>
| Caminho | Papel |
|---|---|
| `games/gow2/apply_all_patches.sh` | o runner; a classificação está por volta das linhas 260-300 |
| `games/gow2/recomp_mid_v2/patch_*.py` | 87 scripts |
| `gow2-recomp/recomp_mid_v2/patch_*.py` | **os que CORREM** (o monorepo é espelho atrasado) |
| `tools/lift_parity.py` | sinais lift vs ELF; usar com `--functions` (autoridade) |
| `tools/audit_boundaries.py` | invariantes I1..I5; 0 invocações hoje |
| `games/gow2/verify_lift_baseline.sh` | gate de marcadores; 243/246 contra o v3 |
| `games/gow2/lift_baseline/MANIFEST.tsv` | 246 marcadores |

## Armadilhas verificadas nesta sessão

- **`-DCMAKE_BUILD_TYPE=Release` desactiva `assert()`.** Usar `-DCMAKE_BUILD_TYPE=` vazio.
  Build pronto em `build-tests/`. Baseline: **22/22**.
- **`grep -E` não suporta lookahead negativo** — deu 0 numa medição que era 86. Para
  contar padrões em código, usar Python.
- **Nunca `re.S` num regex que apaga.** Truncou 3 chunks do lift hoje.
- **`Path.write_text(newline=)` só existe em Python 3.10+**; o `python3` do sistema no
  macOS é 3.9.6. Os patches podem exigir 3.10 (o runner escolhe o interpretador); os
  **testes** não podem.
- **Não escrever no lift de produção.** Validar em cópia; a integração é um passo à parte.
- **`recomp_macos_v2` é produção** — hash `70e7ad1204f41fbedee3fd94aa9c391e`, confirmar
  intacto no fim.
- **Fixtures SPU** regeneradas pelo ctest: `git checkout --`, nunca commitar.
- **Working tree alheio:** `.planning/codebase/TESTING.md`, `scripts/test_python.sh`,
  `notes/2026-07-23-gate-a-verdict.md`, `recomp_mid_v2/gow2_spu_register.c`,
  `recomp_mid_v2/patch_postthr_pc_probe.py`.
</code_context>

<specifics>
## Critérios do ROADMAP, com os números corrigidos

1. `PATCH_CATALOG.tsv` com os **87** (não 73/71) classificados, zero indeterminados.
2. `--status <tsv>` existe e reporta zero FAILED fora de PROBE. Baseline real: **0/84/3**.
3. Sai não-zero quando um patch não aplica — provado por **injecção de falha**.
4. Os PROBE fora do gate, listados nominalmente.
5. *(acrescentado, D-4.5/D-4.6)* `verify_lift.sh` existe, corre os três verificadores por
   delta contra baselines congelados, e o `apply_all_patches.sh` chama-o no fim.
</specifics>

<deferred>
## Fica para depois

- Itens 3, 5, 6, 7, 8, 9, 10, 11 do plano de adopção (oráculo com manifesto, sync-check,
  in-degree, OWNERS.tsv, OPD_MISSING, ENV_REGISTRY, sinal FP/VMX, LEDGER.json).
- Recalibrar o limiar do grupo `opd-dispatch` excluindo funções que não existem em ambos
  os lifts (dívida declarada da Fase 3).
- Os 27 RED do `lift_parity` (9 TRUNCATED com 2–7 fragmentos, 18 CALL_MISMATCH).
- `git subtree pull` — bloqueado por precisar de autorização para `push`.
</deferred>
