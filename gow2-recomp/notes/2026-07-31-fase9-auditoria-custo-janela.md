# Fase 9, Plano 09-01 — Tarefa 3: auditoria de custo real da janela

## Metodo

`bisect_verdict.sh --classify-only <sha>` corrido sobre os 197 commits
(`git log --oneline 8648805..4e815cf`) e, separadamente, sobre os 41 alcancaveis por
`git log --first-parent --oneline 8648805..4e815cf`. Classificacao pelo diff contra o
PRIMEIRO PAI (`git diff <sha>^1..<sha>`), o que classifica merges pelo que trazem para a
mainline — o padrao certo sob `--first-parent`.

## Contagens medidas

### Todos os 197 commits da janela

| nivel | contagem |
|---|---:|
| HERDA (sem build) | 76 |
| RELINK (so' rebuild da lib + link) | 8 |
| RECONSTRUCAO-COMPLETA (relift proprio + patches + build) | 113 |

### Os 41 alcancaveis por `--first-parent` (o conjunto que o bisect real usa)

| nivel | contagem |
|---|---:|
| HERDA (sem build) | 20 |
| RELINK (so' rebuild da lib + link) | 5 |
| RECONSTRUCAO-COMPLETA (relift proprio + patches + build) | 16 |

O `09-CONTEXT.md` previa 3 niveis mas so' tinha medido a distribuicao dos 197 (72
docs/92 patches/12 runtime/11 lifter, por ficheiro simples) — nunca tinha corrido o
classificador REAL contra `--first-parent`, que e' o conjunto que decide o custo do
bisect de facto. Os 41 first-parent tem uma proporcao MAIOR de RECONSTRUCAO-COMPLETA
(39%) do que HERDA sozinho sugeriria — quase metade dos passos que o bisect binario
(log2(41)~=6) pode calhar de testar exigem reconstrucao completa.

## Achado que corrige a tabela de custo do `09-CONTEXT.md`: RELINK nunca e' valido nesta janela na pratica

`bisect_verdict.sh` so' usa o nivel RELINK (rebuild barato da lib, ~poucos segundos, contra
um lift ja' compilado) quando o `tools/ppu_lifter.py` do commit sob teste e' **byte-a-byte
identico** ao lifter de referencia (Tarefa 1: RELINK contra um lifter diferente confunde o
resultado — medido, o runtime de `8648805` relinkado contra o lift de HOJE deu REGRESSAO
3/3 mesmo sendo o lado GOOD).

Medido nesta tarefa: **mesmo o ultimo commit da janela (`4e815cf`) tem um
`tools/ppu_lifter.py` diferente do HEAD de hoje** (104 linhas de diff — mais trabalho
aconteceu depois da janela, nomeadamente a Fase 1 "emissões do lifter"). Ou seja, **os 8
commits classificados RELINK (dos 197) e os 5 (dos 41) escalam TODOS para
RECONSTRUCAO-COMPLETA quando `bisect_verdict.sh` os builda de facto** — o nivel barato
existe e esta correcto (seria usado numa janela onde o lifter nao mudasse depois do
ponto testado), mas **nesta janela especifica nunca se aplica**. Confirmado por prova
directa: `bisect_verdict.sh e441f37` (classificado RELINK pelo diff de ficheiros) --
escalou para RECONSTRUCAO-COMPLETA no log real (`notes/2026-07-31-fase9-fronteiras-oraculo.md`,
secao "RELINK nunca e' valido nesta janela").

**Consequencia pratica**: o custo por commit NAO-HERDA desta janela e' sempre o de
RECONSTRUCAO-COMPLETA, nunca o "RELINK ~2min" do `09-CONTEXT.md`.

## Os 6 merges da janela, classificados

| merge | nivel | porque |
|---|---|---|
| `921a215` merge(lifter): licoes do XenonRecomp | RECONSTRUCAO-COMPLETA | toca `tools/ppu_lifter.py` directamente |
| `88dc92a` merge(fase1): desmascarar memoria guest | RECONSTRUCAO-COMPLETA | 18 ficheiros, inclui runtime/ + docs fora do padrao HERDA |
| **`d039469` subtree(gow2): puxa a leva de 2026-07-25** | **RECONSTRUCAO-COMPLETA** | traz **157 ficheiros** de `games/gow2/` de uma vez -- exactamente a unidade atomica prevista pelo Plano 09-01: gracas a `--first-parent`, este merge e' testado UMA vez, nao 92 vezes para cada commit individual que importa |
| `2321757` merge(origin): instrumentacao PS3_TRACE_* | RECONSTRUCAO-COMPLETA | 74 ficheiros; a maioria runtime/libs, mas inclui `scripts/*.sh`, `tools/trace_parse.py`/`tools/baseline_report.py` fora do conjunto RELINK -- default seguro (T-09-03) |
| `07fe030` merge(bringup): instrumentacao comum | RECONSTRUCAO-COMPLETA | 90 ficheiros, mesma familia do anterior |
| `0c6a01b` merge(fase0): infraestrutura de testes e CI | RECONSTRUCAO-COMPLETA | 26 ficheiros; `CMakeLists.txt` sozinho seria RELINK, mas junta `pyproject.toml`, `scripts/test_*.sh`, `tools/lift_selftest.py` -- default seguro |

Confirmado: `d039469` (a subtree pull, a fusão de história paralela) classifica
RECONSTRUCAO-COMPLETA como previsto, e o `--first-parent` garante que so' e' pago UMA vez
como unidade atomica.

## Estimativa de tempo realista

Custo medido nesta sessao (prova de ponta a ponta contra `e441f37`,
`notes/2026-07-31-fase9-fronteiras-oraculo.md`):

| etapa (RECONSTRUCAO-COMPLETA) | custo medido |
|---|---:|
| `git worktree add` | ~5s |
| `cmake` configure + `cmake --build` (127 objectos, worktree fresca) | ~1-2min |
| relift (`tools/ppu_lifter.py` proprio, ~51-52 mil funcoes) | ~20s |
| `apply_all_patches.sh` (patches de hoje) | ~10-20s |
| compilar 7 chunks liftados (`-P 6`) | ~20-25s |
| link | ~5-10s |
| medir (3 corridas, `smoke_chain_gate.sh --bin`) | ~1.5-4.5min (30-90s/corrida, depende de quao longe o boot chega antes de parar) |
| limpeza (worktree + lift temporario) | ~5s |
| **total por commit RECONSTRUCAO-COMPLETA** | **~4-8min** |

Isto e' MUITO mais barato do que os "30-40min" do `09-CONTEXT.md` (que presumia
recompilar chunks C++ de ~600 mil linhas a ~8min cada, sequencial) — medido, os 7 chunks
compilam em paralelo (`-P 6`) em ~20-25s no total, nao ~8min cada.

**Estimativa para o bisect `--first-parent` (log2(41) ~= 6 passos, assumindo convergencia
limpa sem interacção nem skips):**

- Caso favoravel (a maioria dos passos visitados calha em HERDA, herdando o veredicto do
  pai sem construir): poucos minutos a ~20min.
- Caso tipico (proporcao medida: ~39% RECONSTRUCAO-COMPLETA nos 41 first-parent, entao
  ~2-3 dos 6 passos exigem build real): **~15-45min**.
- Caso adverso (skips por incompatibilidade `__declspec(thread)` em commits anteriores a
  `145fe58`, ou bissecção aninhada se o bisect convergir para um dos 6 merges): **1-2h**.

Isto substitui tanto a estimativa optimista original do `09-CONTEXT.md` ("~40min") quanto
a correcao pessimista que o proprio plano antecipava ("horas") -- a medicao real fica
entre as duas, mais perto do lado optimista, porque a paralelizacao dos 7 chunks
(`-P 6`) e' muito mais rapida do que o `09-CONTEXT.md` presumia.

## Invocacao preparada para o Plano 09-02 (nao corrida aqui)

```bash
git worktree add /tmp/ps3recomp_bisect09_run 8648805
cd /tmp/ps3recomp_bisect09_run
git bisect start --first-parent
git bisect bad 4e815cf
git bisect good 8648805
git bisect run /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/bisect_verdict.sh
```

## Limpeza

Worktrees de teste da Tarefa 1 (`/tmp/ps3recomp_bisect09_good`, `/tmp/ps3recomp_bisect09_bad`)
ja' removidos durante a prova da Tarefa 2 (`git worktree remove --force`, confirmado por
`git worktree list` sem entradas residuais). Nenhum directorio `/tmp/bisect09_*` residual
dos testes desta tarefa (`e441f37`) sobrevive — removido pela propria limpeza do
`bisect_verdict.sh` no fim de `build_and_measure`.

## Ficheiros de evidencia

- `/tmp/classify197.tsv` -- classificacao dos 197 commits.
- `/tmp/classify41.tsv` -- classificacao dos 41 alcancaveis por `--first-parent`.
