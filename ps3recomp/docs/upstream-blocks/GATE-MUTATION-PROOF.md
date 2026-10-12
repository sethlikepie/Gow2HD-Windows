# Prova por mutacao do portao de adopcao — o portao SABE REPROVAR

Registo MEDIDO do exercicio do Plano 23-03 (Fase 23 / `UPST-02`, marco v1.4).

O Plano 23-01 entregou `scripts/upstream_block_gate.sh` e viu-o **verde**. Ver verde nao
prova nada sobre a capacidade de **rejeitar**. Este documento e' o registo da prova nos
**dois sentidos**, feita sobre uma **copia descartavel** da arvore, com a producao
verificada intacta por `sha256` antes e depois.

- Data: **2026-08-12** (todas as corridas entre 16:58 e 17:33 locais)
- Motor: `d1c392ac20361f62bf0c39f513e7cbae7ab142a9` (ramo `macos-arm64-port-f0-f2`)
- Baseline julgador: `games/gow2/lift_baseline/INTRO_GATE_BASELINE.tsv` —
  `st620_min=11`, `runs=6`, `timeout_s=25`
- Copia: `git worktree add --detach ${TMPDIR}/copia HEAD` — destruida no fim
- Nivel de evidencia: **in-boot** (12 corridas reais da intro nas corridas finais e nas
  refutadas; nenhuma citada de memoria)

---

## 1. Os comandos literais

Fotografia da producao (antes e depois; os tres valores tem de bater exactamente):

```sh
shasum -a 256 ../gow2-recomp/boot_gow2 | awk '{print $1}'
git rev-parse HEAD
git status --porcelain
```

Copia descartavel (a producao nunca e' editada em nenhum passo):

```sh
git worktree add --detach "${TMPDIR}/copia" HEAD
# build_macos.sh:381 invoca "$PS3/.venv/bin/python"; a copia nao tem .venv proprio.
# .venv e' criado como DIRECTORIO real (o padrao `.venv/` do .gitignore so' casa
# com directorio -- um symlink chamado .venv aparecia como `?? .venv` e sujava a
# copia, fazendo a PERNA C dizer bloco_sem_codigo em vez de bloco_vazio):
mkdir -p "${TMPDIR}/copia/.venv"
ln -s /Users/.../ps3recomp/.venv/bin "${TMPDIR}/copia/.venv/bin"
```

Sentido **LIMPO** (corrido ANTES do mutado, de proposito: se a copia limpa nao passasse,
o vermelho seguinte nao seria atribuivel a' injeccao):

```sh
scripts/upstream_block_gate.sh --bloco COPIA-LIMPA-2 --antes HEAD \
    --depois-arvore "${TMPDIR}/copia" --runs 3 \
    --antes-bin "${TMPDIR}/gate_limpa/boot_gow2_antes" \
    --out-dir "${TMPDIR}/gate_limpa2"
```

Sentido **MUTADO** (mesma copia, com a regressao injectada):

```sh
scripts/upstream_block_gate.sh --bloco COPIA-MUTADA-B --antes HEAD \
    --depois-arvore "${TMPDIR}/copia" --runs 3 \
    --antes-bin "${TMPDIR}/gate_limpa/boot_gow2_antes" \
    --out-dir "${TMPDIR}/gate_mutada2"
```

Destruicao da copia:

```sh
git worktree remove --force "${TMPDIR}/copia" && git worktree prune
```

> `--antes-bin` reutiliza o binario do leg `antes` do sentido limpo. Poupa uma
> reconstrucao completa do motor (~4 min por corrida) e **exercita pela primeira vez** um
> caminho que o Plano 23-01 deixou `nao-exercitado`. Consequencia declarada, nao escondida:
> `ORIGEM_A` passa a `binario reutilizado` e a raiz da suite Python do leg `antes` passa a
> ser o motor de producao em vez do worktree do leg. O `sha256` e o ref de origem do
> binario reutilizado ficam gravados no registo na mesma.

---

## 2. O que foi injectado, e porque e' COMPORTAMENTAL

| Campo | Valor |
|---|---|
| Ficheiro | `runtime/ppu/ppu_loader.cpp` (**candidato B**) |
| Funcao | `extern "C" uint32_t ppu_load_elf(const char* path)` |
| Injeccao | `_exit(97);` como primeira instrucao da funcao |
| Natureza | **comportamental** — o processo do jogo morre de verdade, antes de carregar o ELF |
| Onde vive | so' em `${TMPDIR}/copia`; nunca na producao, nunca num commit |

A injeccao e' **comportamental** e nao a remocao do marcador que o portao mede. Apagar a
linha `[MOVIEFSM] st620 ...` provaria apenas que o portao reage ao `grep`; matar o processo
prova que ele reage a **regressao do comportamento medido**. O efeito e' visivel nas duas
colunas do registo ao mesmo tempo: `st620` cai a `0` e o `rc` passa de `143`
(`timeout-normal`, o nosso proprio `kill -TERM`) para `97` (`saida-anormal`).

O `rc` de saida **97 foi escolhido por nos** e nao e' sintoma nenhum do jogo.

`ppu_loader.cpp` e' recompilado **incondicionalmente** a cada leg pelo `build_macos.sh`
(linhas 176-177) -- isso e' leitura do script de build, uma **inferencia**. O que prova
que a injeccao entrou mesmo no binario do leg `depois` nao e' essa leitura: sao as tres
corridas com `rc=97`, um codigo que so' existe porque nos o escrevemos naquela funcao.
A leitura do script explica; as corridas medem.

### O candidato A foi REFUTADO por medicao (fica registado, nao apagado)

O plano mandava tentar primeiro `sys_tty_write` (syscall 403) em
`runtime/syscalls/lv2_register.c:42-66`, cujo proprio comentario diz que as syscalls de TTY
estao "among the most commonly called syscalls in CRT startup". **Medido: nao e' chamada
nesta intro.** Com `_exit(97)` depois da primeira escrita do guest, as tres corridas do leg
`depois` deram `rc=143` (nenhuma `saida-anormal`) e `st620 = 11, 1, 0` — o padrao normal da
maquina, nao o de um processo morto. O `.o` foi mesmo recompilado
(`[1/2] Building C object .../lv2_register.c.o` no log de build do leg), logo a refutacao e'
do **caminho**, nao do build. Dai o candidato B.

---

## 3. Os dois sentidos, corrida a corrida

`st620_min` congelado = **11**; `runs = 3`, logo `limiar = (3*2+2)/3 = 2`.

### Sentido LIMPO — `COPIA-LIMPA-2` (a copia SEM a injeccao)

| leg | corrida | st620 | rc | rc_class |
|---|---|---|---|---|
| antes  | 1 | 0  | 143 | timeout-normal |
| antes  | 2 | 11 | 143 | timeout-normal |
| antes  | 3 | 11 | 143 | timeout-normal |
| depois | 1 | 11 | 143 | timeout-normal |
| depois | 2 | 11 | 143 | timeout-normal |
| depois | 3 | 11 | 143 | timeout-normal |

```
PERNA A (intro)        PASSA   perna_a=PASSA st620_min=11 limiar=2 ok_antes=2 ok_depois=3 anorm_antes=0 anorm_depois=0
PERNA B (suite python) PASSA   perna_b=PASSA novos=nenhum
PERNA C (mediu mesmo)  PASSA   perna_c=PASSA bloco_vazio=sim ficheiros=0
```

**Ultima linha do portao:** `VEREDICTO=PASSOU ELO_CAIDO=nenhum` · `rc = 0`

- binario do leg `antes`: `f584d03e60494c94d3ad64e62347f52cdf5fe4c9afbf3bce9f53ab4a2ea5c516`
- binario do leg `depois`: `4adb37759e5f6293256467933ed1ca89dc85cbf6e8ddb95550e6ab22e15f2900`

### Sentido MUTADO — `COPIA-MUTADA-B` (a MESMA copia, com a injeccao)

| leg | corrida | st620 | rc | rc_class |
|---|---|---|---|---|
| antes  | 1 | 11 | 143 | timeout-normal |
| antes  | 2 | 11 | 143 | timeout-normal |
| antes  | 3 | 1  | 143 | timeout-normal |
| depois | 1 | **0** | **97** | **saida-anormal** |
| depois | 2 | **0** | **97** | **saida-anormal** |
| depois | 3 | **0** | **97** | **saida-anormal** |

```
PERNA A (intro)        FALHA   perna_a=FALHA st620_min=11 limiar=2 ok_antes=2 ok_depois=0 anorm_antes=0 anorm_depois=3 motivo=ok(depois)=0 < limiar=2
PERNA B (suite python) PASSA   perna_b=PASSA novos=nenhum
PERNA C (mediu mesmo)  PASSA   perna_c=PASSA ficheiros=1 compilados=1 binarios=distintos
```

**Ultima linha do portao:** `VEREDICTO=REJEITADO ELO_CAIDO=intro (st620)` · `rc = 1`

- binario do leg `depois`: `d53b52d1c1ce00efa1d2f34ff8f9fb118e4d8903a083116b1cff77ef89723eb2`
- arvore do leg `depois`: `arvore_suja=sim`, com ` M runtime/ppu/ppu_loader.cpp` listado no
  resumo (uso **pre-commit**, nunca apresentavel como medicao de um commit limpo)

### O portao NOMEIA o elo certo, e discrimina

No mesmo relatorio, com a intro em baixo: **PERNA B reportada PASSA** (a suite Python nao
foi tocada pela injeccao) e **PERNA C reportada PASSA** (`ficheiros=1 compilados=1
binarios=distintos`). O portao reprova **o elo que caiu**, nao em bloco.

Ressalva ao ler esse `compilados=1`: o campo conta ficheiros do conjunto de alterados que
batem o prefixo `^(runtime|libs|include)/` -- e' um **contador de prefixo de caminho**, nao
uma medicao de link, e o nome promete mais do que mede (a mesma ressalva esta' registada no
`LV2S-01.md`, onde `compilados=25` corresponde a **1** ficheiro que entra mesmo no binario).
Neste caso o ficheiro injectado *esta'* de facto no binario, mas quem o prova e' o `rc=97`
das tres corridas, nao o contador.

---

## 4. A producao ficou intacta

| chave | antes do exercicio | depois do exercicio |
|---|---|---|
| `producao_sha256` (`gow2-recomp/boot_gow2`) | `677f60a746ba8bc253bd9b194849d535ea317abaf10148a317e1caf405ca6b89` | `677f60a746ba8bc253bd9b194849d535ea317abaf10148a317e1caf405ca6b89` |
| `motor_head` | `d1c392ac20361f62bf0c39f513e7cbae7ab142a9` | `d1c392ac20361f62bf0c39f513e7cbae7ab142a9` |
| `git status --porcelain` (linhas) | 31 (identicas, ficheiro a ficheiro) | 31 (identicas, ficheiro a ficheiro) |

`producao_intacta=sim` — `diff` das duas fotografias completas (incluindo a lista literal de
`git status --porcelain`) so' difere na linha de titulo e na data.

Verificacoes de arrumacao, todas corridas:

- `git worktree list | grep -c "${TMPDIR}"` → **0** (a copia foi destruida). A verificacao
  do plano `git worktree list | wc -l == 1` **nao e' executavel nesta maquina**: existem 4
  worktrees de agente pre-existentes sob `.claude/worktrees/`, criados por outras sessoes,
  que nao sao nossos para remover. A afirmacao mensuravel equivalente e' a de cima, e a
  contagem total voltou ao valor da fotografia inicial (5).
- `grep -rn '_exit(97)' runtime/ libs/ include/` → **sem resultados** (`rc=1`; nenhuma
  injeccao residual). Controlo positivo do metodo, corrido depois para o zero nao ficar
  por interpretar: `grep -rln '_exit(' runtime/ libs/ include/` devolve ficheiros
  (`runtime/ppu/ppu_sysprx.cpp`, `runtime/syscalls/sys_ppu_thread.c`,
  `runtime/syscalls/lv2_register.c`, ...), logo o vazio e' do alvo e nao do comando.

---

## 5. As duas refutacoes do caminho (registadas, nao apagadas)

Um registo que so' mostrasse as corridas finais seria uma escolha, nao uma medicao. As duas
tentativas refutadas ficam aqui.

### 5.1 A PRIMEIRA corrida do sentido limpo REPROVOU — e a causa nao era a copia

`COPIA-LIMPA` (a primeira, com os dois legs construidos de raiz) deu
`VEREDICTO=REJEITADO ELO_CAIDO=intro (st620)`, com `antes = 0,11,11` e `depois = 1,1,1`
— **no mesmo commit, sem uma unica linha de diferenca no codigo**.

Experimento discriminador (sem rebuilds, os DOIS binarios ja' linkados, corridos
**alternadamente** A,B,A,B,A,B com a mesma recipe G6):

| ordem | binario | st620 | load1 |
|---|---|---|---|
| 1 | A (`antes`)  | 1  | 5,78 |
| 2 | B (`depois`) | 11 | 5,20 |
| 3 | A | 11 | 5,48 |
| 4 | B | 1  | 6,47 |
| 5 | A | 11 | 7,09 |
| 6 | B | 1  | 11,28 |

O binario B — o mesmo que dera `1,1,1` — atinge `11`. **Os dois binarios produzem os dois
resultados**, logo a diferenca nao esta' no codigo. Repetido com `timeout` de 45 s (cargas
15-18): `A = 11,11,11`, `B = 0,1,11`. A corrida seguinte do sentido limpo, com um **link
novo** da mesma copia, deu `depois = 11,11,11` e `rc=0`.

Conclusao honesta: nesta maquina, sob carga de 6 a 21 (outras sessoes a construir), a intro
atinge `st620=11` em cerca de metade das corridas, e a `PERNA A` com `--runs 3` tem um modo
de **falso vermelho**. O sentido de erro e' o seguro para um portao (rejeita o que deveria
passar, nunca o contrario), mas obriga a repetir e a registar as duas corridas — como aqui.
Agravante medido no 23-01 e reconfirmado: a **primeira** corrida de cada leg apanha a
penalizacao de cache fria (`antes` run1 = 0 nas duas corridas do sentido limpo).

### 5.2 A injeccao no `sys_tty_write` acordou um acoplamento real com o `UPSTREAM_DELTA.tsv`

Na corrida `COPIA-MUTADA` (candidato A), a `PERNA B` reprovou nomeando
`tools/test_upstream_delta.py`. A causa **medida** nao foi a syscall: as 9 linhas de
comentario que a injeccao acrescentou empurraram uma ancora do inventario da Fase 22 —

```
FAIL ANCORA_DESLOCADA linha=29 runtime/syscalls/lv2_register.c:
  prova 'vm_write_be32(seg + 0x14, src_ea + p_off)' nao esta em 832-832, linha_actual=841
```

Ou seja: **qualquer** bloco que altere o numero de linhas de um ficheiro ancorado no
`docs/UPSTREAM_DELTA.tsv` faz a `PERNA B` do portao reprovar por deriva de ancora, e nao por
regressao de comportamento. Nao e' um defeito do portao (a suite falhou de facto, e o
`README` do portao ja' obriga a re-ancorar), mas e' uma armadilha operacional que passa a
estar escrita: **re-ancorar (`--fix-anchors`) faz parte de submeter um bloco ao portao.**
A injeccao final (candidato B) foi escolhida num ficheiro **nao ancorado** exactamente para
que o sentido mutado medisse a intro, e nao a deriva de ancoras.

---

## O que esta prova NAO cobre

1. **Prova a capacidade de rejeitar uma regressao da INTRO.** Nada mais. O portao mede tres
   pernas (intro, suite Python, "mediu mesmo"), e este exercicio provou que a perna da intro
   reprova quando o comportamento cai e que as outras duas continuam a ser reportadas
   independentemente.
2. **Nao cobre regressoes fora do que as tres pernas medem.** Uma perda **depois** do `st620`
   — no caminho do WAD, do stream do `R_PermA`, do registry de shaders — passaria por este
   portao sem o acordar. E' exactamente a licao que criou o `accept_relift.sh`: um smoke que
   para no primeiro elo nao defende os elos seguintes.
3. **Nao prova que o caminho alterado por um bloco foi exercitado.** O portao nunca o
   afirma; a `PERNA C` so' garante que os dois legs produziram binarios diferentes.
4. **O `rc=97` foi escolhido por nos.** Nao e' um codigo de saida do jogo nem um sintoma de
   nada; e' apenas a marca da nossa injeccao. O que o portao classifica e' a **classe**
   (`saida-anormal` vs `timeout-normal`), nao o numero.
5. **Nao prova estabilidade de medicao.** Pelo contrario: a seccao 5.1 mede um modo de falso
   vermelho da `PERNA A` sob carga da maquina. Quem correr o portao num bloco real deve usar
   `--runs 6` e ler o TSV corrida a corrida antes de atribuir um vermelho ao bloco.
6. **O sentido limpo foi provado com `--antes-bin`** (binario do leg `antes` reutilizado).
   Isso torna as duas corridas comparaveis entre si, mas nao prova nada sobre a
   reprodutibilidade do link: o proprio 23-01 mediu que dois legs do MESMO commit dao
   `sha256` diferentes.

---

## Ficheiros de suporte (fora dos repositorios, por G6)

Todos os registos brutos ficaram em `${TMPDIR}` e **nao** entram no indice do git (contem
caminhos e conteudo do jogo): `gate_limpa/`, `gate_limpa2/`, `gate_mutada/`, `gate_mutada2/`
(TSV + resumo + logs de boot por corrida), `diag_alterna/`, `diag_45/`,
`prod_snapshot_antes.txt`, `prod_snapshot_depois.txt`.

O auto-teste que transforma as regras G6 em guarda permanente da CI e'
`scripts/test_upstream_block_gate.sh` (49 verificacoes, 0,1 s, registado no CTest como
`test_upstream_block_gate`).
