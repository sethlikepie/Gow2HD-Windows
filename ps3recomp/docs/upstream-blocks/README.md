# Adopção do upstream por blocos — o portão

Ponto de entrada da adopção por blocos do marco **v1.4**. A regra de ouro:
**um bloco que não passe o smoke não entra.** O instrumento que decide isso é
`scripts/upstream_block_gate.sh`.

O portão mede a intro do GoW2 nas **duas pontas** de um bloco (antes e depois) e
escreve as duas no **mesmo registo**. Nunca duas corridas soltas que alguém
compara de cabeça.

Inventário do delta upstream→nós: [`docs/UPSTREAM_DELTA.tsv`](../UPSTREAM_DELTA.tsv).

---

## Como se invoca

### 1. Congelar o baseline (uma vez, e sempre que a recipe mudar)

```bash
scripts/upstream_block_gate.sh --congelar-baseline --ref HEAD --runs 6
```

Escreve `games/gow2/lift_baseline/INTRO_GATE_BASELINE.tsv` a partir de uma
corrida **real**: `st620_min`, `st620_mediana`, `ok_runs_min`, `runs`,
`timeout_s`, `known_debt_python`, mais a proveniência (`medido_em`, `ref`,
`binario_sha256`, `runs`, `timeout_s`, `recipe`).

Nenhum número entra neste ficheiro citado de memória. Os valores históricos
conhecidos — `st620=11` na recipe de **produção** com Metal, e o limiar `>=3` do
gate antigo — são de **outras recipes** e não servem de baseline para esta.

Sem este ficheiro o portão **recusa-se a correr** (`rc=2`): um gate sem medição
nunca passa.

### 2. Julgar um bloco, ref a ref (o modo normal)

```bash
scripts/upstream_block_gate.sh --bloco LV2S \
    --antes  8b1f2c9 \
    --depois 4d7ae01 \
    --runs 3
```

Os dois legs são **refs congelados**. Cada `--antes`/`--depois` é resolvido para
um SHA imutável logo à entrada, antes de qualquer build: `HEAD` é a ponta do
ramo, ou seja um alvo **móvel**, e entre o worktree de um leg e o do outro
podem passar minutos de build.

### 3. Julgar a árvore viva (uso pré-commit)

```bash
scripts/upstream_block_gate.sh --bloco LV2S \
    --antes 8b1f2c9 \
    --depois-arvore . \
    --runs 3
```

Sem `--depois` nem `--depois-arvore`, o leg `depois` é a árvore viva por default.
Havendo alterações por commitar, o registo fica marcado `arvore_suja=sim` e os
ficheiros são listados. Um bloco medido numa árvore suja **continua a ser
medível** — é este o uso pré-commit — mas **nunca** pode ser apresentado como
medição de um commit limpo, e é por isso que o campo existe.

### Opções

| Opção | Efeito |
|---|---|
| `--runs N` | corridas por leg (default **6** ao congelar, **3** ao julgar) |
| `--timeout S` | segundos por corrida (default 25, o da recipe medida) |
| `--out-dir DIR` | onde vivem TSV, resumo e logs (default `${TMPDIR}/upstream_gate_<bloco>_<data>`) |
| `--antes-bin BIN` | reutiliza um binário de leg já construído em vez de reconstruir; o resumo grava a proveniência (sha256 + ref), porque um leg reutilizado sem proveniência deixaria de ser refutável |

### Saídas

| Ficheiro | Conteúdo |
|---|---|
| `<OUT_DIR>/<bloco>-gate.tsv` | uma linha por corrida: `bloco`, `leg`, `ref`, `binario_sha256`, `run`, `st620`, `rc`, `rc_class`, `log` |
| `<OUT_DIR>/<bloco>-gate-resumo.txt` | resumo humano por leg + as três pernas |
| `<OUT_DIR>/<leg>_run<N>.log` | log de boot cru |

`rc` final: **0** se as três pernas passarem, **1** se alguma reprovar, **2**
para erro de uso, pré-requisito em falta ou baseline ausente. A **última linha
do stdout é sempre**:

```
VEREDICTO=<PASSOU|REJEITADO> ELO_CAIDO=<nome|nenhum>
```

---

## As três pernas

Todas correm **sempre** (o portão não aborta cedo) e o relatório final mostra o
estado das três. `rc=0` exige as três.

### PERNA A — intro (`st620`)

Lê `st620_min` do baseline congelado. Para cada leg, `ok(leg)` = número de
corridas com `st620 >= st620_min`. `LIMIAR = (RUNS*2+2)/3` — a mesma aritmética
de `games/gow2/smoke_chain_gate.sh`.

Passa sse `ok(depois) >= LIMIAR`, **e** `ok(depois) >= ok(antes)`, **e** o leg
`depois` não traz saídas anormais que o leg `antes` não tenha.

Ao falhar: `ELO_CAIDO=intro (st620)`, com os dois `ok` e o `st620_min` usado.

O `st620` sai de `extract_st620()` em `games/gow2/lib_boot_chain_metrics.sh` — o
grep vive lá e **nunca** é reescrito noutro sítio.

### PERNA B — suite Python

Corre `scripts/test_python.sh` uma vez por leg, a partir da raiz do motor desse
leg, com `PYTHON` fixado ao interpretador do `ENGINE_ROOT` nos **dois** legs (o
worktree do leg `antes` não tem `.venv` próprio; sem fixar o interpretador a
comparação não seria entre iguais).

Passa sse `FAIL(depois) − FAIL(antes) − known_debt_python` for **vazio**. Um
FAIL novo conta e é nomeado.

É também esta perna que apanha **automaticamente** uma âncora de
`docs/UPSTREAM_DELTA.tsv` deslocada por um bloco, porque
`tools/test_upstream_delta.py` corre dentro dela.

Ao falhar: `ELO_CAIDO=suite python`, seguido dos nomes novos.

### PERNA C — o portão mediu mesmo alguma coisa

Só se aplica quando o conjunto de ficheiros alterados entre os legs contém pelo
menos um caminho **compilado** (`^(runtime|libs|include)/`). Nesse caso, dois
binários com o mesmo `sha256` reprovam:
`ELO_CAIDO=binarios identicos -- o portao nao mediu o bloco`.

- Bloco vazio → passa, resumo regista `bloco_vazio=sim`.
- Bloco sem código (só documentação/planeamento) → passa, resumo regista
  `bloco_sem_codigo=sim`.

Em nenhum desses dois casos se exige nada sobre os `sha256`: o link de 122 MB
**não** é garantidamente reproduzível byte a byte — medido, dois legs do mesmo
commit deram `sha256` diferentes — e afirmar o contrário seria inventar uma
propriedade que não medimos.

---

## O protocolo G6 (e a razão de cada regra)

O G6 é **imposto dentro do script**, não apenas prometido no cabeçalho.

**`PS3_NO_RSX=1` sozinho NÃO é headless.** O `libs/video/cellGcmSys.c` tem um
**segundo** registo de backend, independente do boot host: `rsx_bridge_mode()` lê
`PS3_RSX_BACKEND` por sua conta. Sem a variável fica em modo 0 (a antiga via
só-sync); com um valor desconhecido cai em `trace`. Fixar `PS3_RSX_BACKEND=trace`
é o que garante que os dois legs medem o **mesmo** caminho de RSX, sem janela e
sem leituras de memória guest.

**A ordem do `source` importa.** O `games/gow2/env_gow2.sh` só escolhe backend
quando `PS3_NO_RSX` ainda está **vazio** (no Darwin põe `metal`, com janela). Por
isso a recipe exporta `PS3_NO_RSX` e `PS3_RSX_BACKEND` **depois** do
`. env_gow2.sh`, nunca antes.

**`PS3_ENGINE_ROOT` explícito é obrigatório.** Sem ele o
`games/gow2/build_macos.sh` resolve `$HERE/../ps3recomp` e os **dois** legs
linkariam exactamente o mesmo motor — o portão passaria sempre, por não ter
medido nada. É também por isso que a PERNA C existe: é a rede que apanha esse
modo de falha se ele voltar por outro caminho.

**Mata-se por PID:** `kill -TERM`, esperar 1 s, `kill -9`. O matador por **padrão
de nome de processo** é **proibido** neste projecto — já custou load 75 nesta
máquina. A forma exacta proibida está escrita no [`CLAUDE.md`](../../CLAUDE.md) e
não é reproduzida aqui de propósito.

**Os logs vivem em `${TMPDIR:-/tmp}`**, fora dos dois repositórios. Nenhum log de
boot entra no índice do git: contêm caminhos e conteúdo do jogo.

**Os legs correm sequencialmente.** O `build_macos.sh` escreve os objectos de
host (`ppu_loader.o`, `ppu_imports.o`, `ppu_hle.o`, `ppu_sysprx.o`, `ppu_fs.o`)
dentro do próprio `$LIFT`, que os dois legs **partilham**. Em paralelo, um leg
linkaria objectos compilados a partir do motor do outro.

**Cache dos chunks liftados.** Os `ppu_recomp_*.cpp.o` só são reconstruídos por
mtime do próprio `.cpp`, mas são compilados com `-I "$PS3/include"` e
`-I "$PS3/runtime/ppu"`. Se o bloco alterar um **cabeçalho** sob esses caminhos,
os objectos em cache ficam desactualizados sem que nenhum mtime o denuncie — e a
medição seria falsa. O portão detecta isso e força `FORCE_REBUILD_LIFT=1` nos
dois legs, avisando do custo. Um `.c`/`.cpp` alterado sob `runtime/ppu/` **não**
dispara a regra: esses são recompilados incondicionalmente a cada leg.

---

## O que o portao NAO prova

**(a) Prova NÃO-REGRESSÃO DA INTRO — e só isso.** Nunca prova que o caminho
alterado pelo bloco foi **exercitado**. São duas afirmações diferentes e têm de
ficar separadas no registo do bloco. Exemplo real: o bloco `LV2S` corrige
`sys_spu_image_import`, e `grep -r image_import games/gow2/` dá zero — o portão
verde diz que a intro não regrediu, e **não** diz que o fix foi corrido.

**(b) `exit 124` — e a morte pelo nosso próprio TERM/`-9` de timeout — são
NORMAIS.** O título corre em loop de render e só pára porque o paramos. O portão
classifica esses casos como `timeout-normal` (junto com `137` e `143`) e **nunca**
como falha. O `rc` cru fica sempre gravado ao lado da classe: a classe explica,
nunca substitui a medição.

**(c) As falhas de toolchain do `scripts/test_python.sh` são dívida DECLARADA.**
Estão em `known_debt_python` no baseline e são subtraídas pela PERNA B: nunca são
lidas como regressão causada por um bloco. Registo original em
`.planning/phases/22-a-que-mede-o-que-o-upstream-avancou-e-nos-nao-temos/deferred-items.md`.

**(d) O portão não é determinístico corrida a corrida.** Medido no bloco nulo: uma
corrida deu `st620=1` onde as outras cinco deram `11`. É para isso que existe o
limiar `(RUNS*2+2)/3` — mas com `--runs 3` o limiar é 2, e duas corridas fracas
reprovam um bloco bom. Para blocos importantes, `--runs 6`.

---

## Registo por bloco

Cada bloco adoptado tem o seu `docs/upstream-blocks/<BLOCO>.md`. Formato
obrigatório:

```markdown
# Bloco <NOME> — <uma linha do que o bloco faz>

- bloco: <NOME>
- sha_upstream: <sha(s)>
- ancora_nossa: <caminho:linhas>
- fase: <N>
- data: <AAAA-MM-DD>
- portao: <OUT_DIR>/<BLOCO>-gate.tsv
- veredicto: PASSOU | REJEITADO   (ELO_CAIDO=<nome|nenhum>)
- legs: antes=<sha> depois=<sha|arvore viva>   arvore_suja=<sim|nao>

## O que este portao prova

- <afirmação> [evidencia: in-boot]

## O que este portao NAO prova

- <afirmação> [evidencia: nao-exercitado]
```

Regras do registo — **impostas por comando**, não só por prosa:
`.venv/bin/python3 tools/check_block_record.py docs/upstream-blocks/`
(rc=0/1, ligado ao `scripts/test_python.sh` e ao CTest via
`py.test_check_block_record`; cada regra tem prova por mutação em
`tools/test_check_block_record.py`).

1. **As duas secções são SEPARADAS e ambas obrigatórias**, e nenhuma delas
   pode ficar vazia. Juntar as duas afirmações numa só seria **superdeclarar
   (G5)** e é **proibido** — o verificador reprova um registo que as junta.
2. **Cada afirmação leva uma marca `[evidencia: X]` no fim da linha**, com o
   vocabulário do projecto:
   - `in-boot` — observado numa corrida real do boot;
   - `offline-unit` — provado por teste offline, fora do boot;
   - `nao-exercitado` — a afirmação não foi corrida por nós.
3. **Uma marca por afirmação.** Duas marcas na mesma linha são duas afirmações
   disfarçadas de uma — a forma típica de superdeclarar.
4. **Nada `[evidencia: in-boot]` dentro de `## O que este portao NAO prova`:**
   o que o portão não prova não pode trazer o nível de evidência mais forte do
   projecto.
5. **O cabeçalho traz `bloco`, `sha_upstream`, `ancora_nossa`, `fase` e `data`**,
   cada um numa linha `chave: valor` (com ou sem o `- ` inicial).
6. Nunca escrever "funciona" ou "renderiza" sem observação por trás.

Ficheiros desta pasta que **não** são registos de bloco (`README.md`,
`GATE-MUTATION-PROOF.md`) ficam fora da verificação por construção: entram os
que batem `<BLOCO>-<NN>.md` ou que trazem `bloco:` no cabeçalho.

---

## Obrigação herdada da Fase 22 — re-ancorar o inventário

Um bloco que mexa numa linha ancorada em `docs/UPSTREAM_DELTA.tsv` **desloca a
âncora**, e o gate de deriva fica vermelho até ser re-ancorado. Depois de editar:

```bash
.venv/bin/python3 tools/upstream_delta.py validate --tsv docs/UPSTREAM_DELTA.tsv
# se acusar ANCORA_DESLOCADA:
.venv/bin/python3 tools/upstream_delta.py validate --tsv docs/UPSTREAM_DELTA.tsv --fix-anchors
.venv/bin/python3 tools/upstream_delta.py validate --tsv docs/UPSTREAM_DELTA.tsv   # até rc=0
```

O campo `decisao` do TSV **não muda** quando um bloco é adoptado: `adoptar`
significa "entra pelo portão da Fase 23+", **não** "já foi adoptado". Esse
ficheiro não simula estado de adopção — o estado vive nos registos por bloco
desta pasta.

A PERNA B corre `tools/test_upstream_delta.py`, portanto uma âncora deslocada e
esquecida cai automaticamente dentro do portão.
