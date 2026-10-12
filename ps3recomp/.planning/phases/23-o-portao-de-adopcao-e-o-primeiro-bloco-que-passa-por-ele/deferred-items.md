# Deferred items — Fase 23, Plano 23-02

## Falhas PRE-EXISTENTES de toolchain do host (fora do ambito deste plano)

A divida ja' declarada na Fase 22 cobria so' o `scripts/test_python.sh` (dois FAIL).
Ao correr **as duas** suites completas na verificacao deste plano, a mesma causa
aparece tambem no `scripts/test_runtime.sh`, em duas suites que a Fase 22 nunca
chegou a registar. Alarga-se aqui o registo da divida — **nao** se corrige nada:
fronteira de ambito (so' se auto-corrige o que as alteracoes da task causaram).

| Suite | Teste | rc | Causa medida |
|---|---|---|---|
| `test_python.sh` | `tools/test_ppu_lift.py` | 1 | `include/ps3emu/endian.h:12:10: fatal error: 'string.h' file not found` |
| `test_python.sh` | `runtime/ppu/tests/test_ppu_lifter_emit.py` | 1 | idem |
| `test_runtime.sh` | `suite.runtime_spu` (#27) | 1 | 19/19 casos com `build error`; `runtime/spu/spu_context.h:15:10: fatal error: 'string.h' file not found` |
| `test_runtime.sh` | `suite.runtime_trace` (#28) | 1 | `runtime/trace/ps3_trace.c:22:10: fatal error: 'stdio.h' file not found` (e `pthread.h` no `test_ps3_trace.c`) |

Causa raiz comum, agora **medida** e nao so' suposta: os scripts destas suites
invocam um `cc` **nu**, sem `-isysroot`. Nesta maquina um `cc` sem `-isysroot`
nao ve os headers C de sistema. Prova directa: o mesmo ficheiro de teste deste
plano compila e corre com
`cc -std=c11 -isysroot "$(xcrun --show-sdk-path)"` e falha em `stdlib.h` sem a
flag. E' por isso que `runtime/syscalls/tests/run_spu_image_import_mutation.sh`
(criado neste plano) resolve o SDK por `xcrun` — o teste novo **nao** herda esta
divida.

**Prova de que sao pre-existentes** [nivel de evidencia: `offline-unit`]: as duas
suites do `test_runtime.sh` foram corridas num `git worktree` destacado em
`62a3033` — o commit imediatamente ANTERIOR ao primeiro commit deste plano — e
falharam identicamente:

```
worktree destacado em 62a3033
  runtime/trace/tests/run_tests.sh  rc=1  -> 'stdio.h' file not found
  runtime/spu/tests/run_tests.sh    rc=1  -> Results: 0 passed, 19 failed
                                             'string.h' file not found
```

As alteracoes deste plano sao `runtime/syscalls/lv2_register.c` (uma expressao
dentro do ramo COPY do `sys_spu_image_import_handler`),
`runtime/syscalls/tests/test_spu_image_import.c`,
`runtime/syscalls/tests/run_spu_image_import_mutation.sh`, `CMakeLists.txt`
(dois `add_test` novos) e `docs/UPSTREAM_DELTA.tsv` (re-ancoragem). Nenhuma entra
no caminho de compilacao destes harnesses, que nem sequer chegam a pre-processar.

Baseline desta corrida:
- `scripts/test_python.sh` — 19 testes: PASS 15, SKIP 2, FAIL 2 (os dois de divida).
- `scripts/test_runtime.sh` — 28 testes: 24 passed, 4 failed (os dois de divida
  espelhados no CTest como `py.test_ppu_lift` / `py.ppu_lifter_emit`, mais
  `suite.runtime_spu` e `suite.runtime_trace`).
- Os dois testes novos deste plano (`test_spu_image_import`,
  `test_spu_image_import_mutacao`) passam nas duas corridas.

## Ameaca registada, nao mitigada nesta fase

`T-23-08` (do `<threat_model>` do 23-02): em `sys_spu_image_import_handler`, o
`phoff + i*phentsz` e o `p_offset` alimentam `vm_read_be32`/`vm_write_be32` sem
limite superior — os helpers so' testam `!guest_addr`. Pre-existente e
deliberadamente **fora** da fronteira deste bloco: o bloco `LV2S` e' uma
expressao, e alargar o diff impediria o portao do Plano 23-04 de atribuir efeito
a este bloco. Disposicao `accept` nesta fase; a reavaliar na Fase 24, que adopta
o interpretador SPU e passa a executar imagens nao pre-liftadas.

---

## Plano 23-01 (o portão de adopção) — 2026-08-12

### 1. A dívida de toolchain do `test_python.sh` é `SDKROOT`, e desaparece quando ele é exportado

**Medido** ao construir o primeiro leg: o `clang` do host
(`xcode-select -p` → `/Library/Developer/CommandLineTools`) **não traz sysroot por
default**. `clang -c` num ficheiro com `#include <string.h>` dá `fatal error:
'string.h' file not found`; `SDKROOT=$(xcrun --show-sdk-path)` resolve. O `cmake`
não sofria porque passa `-isysroot` explícito aos seus alvos; o
`games/gow2/build_macos.sh` invoca `clang++` directamente e sofria — **nenhum leg
conseguia ser linkado** até o portão passar a exportar `SDKROOT`.

Consequência medida na PERNA B do bloco nulo: com `SDKROOT` exportado, os dois
legs deram **PASS 16 / SKIP 3 / FAIL 0** — as duas falhas registadas no Plano
22-01 (`tools/test_ppu_lift.py`, `runtime/ppu/tests/test_ppu_lifter_emit.py`)
**já não ocorrem**. Corrobora o diagnóstico "ambiente de toolchain, não código do
repo" que a Fase 22 tinha marcado como `nao-exercitado`.

**Fica deferido** (fora da fronteira desta fase): fixar o `SDKROOT` no próprio
`scripts/test_python.sh` / `runtime/spu/tests/run_tests.sh` / `build_macos.sh`,
para que a suite deixe de depender do ambiente de quem a corre. O
`known_debt_python` do baseline **mantém-se** como rede: se alguém correr o
portão num ambiente sem `SDKROOT`, as duas falhas voltam e continuam a não poder
ser lidas como regressão de um bloco.

### 2. O leg medido em primeiro lugar absorve a penalização de cache fria

**Medido** no bloco nulo (`--runs 3`, os dois legs no MESMO commit): o leg `antes`
deu `st620 = 1, 11, 11` e o leg `depois` deu `11, 11, 11`. Mesmo código, mesmo
`EBOOT.ELF` — a diferença é ambiente (cache fria do binário acabado de linkar
e/ou carga da máquina).

Isto tem uma direcção **incómoda**: como o leg `antes` corre sempre primeiro, é
sempre ele a apanhar a penalização, o que empurra `ok(antes)` para baixo e torna
a condição `ok(depois) >= ok(antes)` **mais fácil** de satisfazer. Ou seja, o
enviesamento é no sentido de **passar**, que é o pior dos dois sentidos para um
portão de adopção.

Não corrigido nesta fase porque a alternativa (intercalar as corridas dos dois
legs) obriga a re-linkar entre corridas — os objectos de host vivem no `$LIFT`
partilhado — e isso multiplica o custo do portão. Mitigações disponíveis hoje:
`--runs 6` em blocos importantes, e a leitura do TSV corrida a corrida (que o
portão já grava).

Candidato a resolver: uma corrida de aquecimento por leg, **gravada no TSV com um
`leg` próprio** para não esconder dados, e excluída dos agregados.

### 3. Ainda `nao-exercitado` neste plano

- `--depois-arvore DIR` (modo pré-commit): só a **validação de argumentos** foi
  corrida (directório inexistente → `rc=2`). O caminho completo fica para o
  Plano 23-04, já sem concorrência de wave.
- `--antes-bin BIN` (reutilização de binário): implementado e não exercitado.
- `FORCE_REBUILD_LIFT=1` por cabeçalho alterado: a detecção foi exercitada no
  sentido negativo (bloco nulo → cache mantida); o sentido positivo espera um
  bloco que altere um `.h` sob `include/` ou `runtime/ppu/`.

---

## Plano 23-03 (a prova por mutacao do portao) — 2026-08-12

### 1. Falso vermelho da PERNA A sob carga da maquina — MEDIDO, nao corrigido

O sentido LIMPO deste plano **reprovou a' primeira** (`COPIA-LIMPA`, `depois = 1,1,1`
contra `antes = 0,11,11`) com os dois legs no **mesmo** commit. O experimento
discriminador (os dois binarios ja' linkados, corridos alternadamente A,B,A,B,A,B sem
rebuilds) mostrou que **ambos** produzem `1` e `11`:

```
ordem  bin  st620  load1        ordem  bin  st620  load1   (timeout 45 s)
  1     A     1    5,78           1     A    11    18,18
  2     B    11    5,20           2     B     0    16,65
  3     A    11    5,48           3     A    11    16,55
  4     B     1    6,47           4     B     1    11,65
  5     A    11    7,09           5     A    11    15,20
  6     B     1   11,28           6     B    11    14,96
```

Nesta maquina, com outras sessoes a construir (load 6 a 21), a intro atinge `st620=11` em
cerca de metade das corridas. Com `--runs 3` (limiar 2) isso da' um modo de **falso
vermelho** da `PERNA A`. Aumentar o `--timeout` para 45 s **nao** resolveu.

Direccao do erro: rejeita o que devia passar (o sentido seguro para um portao), mas
obriga a repetir corridas e a registar todas. **Deferido:** decidir se o portao passa a
ter uma corrida de aquecimento por leg gravada com `leg` proprio (candidato ja' aberto
no 23-01), ou se `--runs 6` passa a ser o default para blocos reais. Nao alterado nesta
fase: mexer no criterio da `PERNA A` a meio da prova por mutacao invalidaria a propria
prova (T-23-12).

### 2. Acoplamento `bloco que muda linhas` -> `ancoras do UPSTREAM_DELTA.tsv`

Medido ao injectar o candidato A (`sys_tty_write`, 9 linhas de comentario a mais): a
`PERNA B` reprovou com

```
FAIL ANCORA_DESLOCADA linha=29 runtime/syscalls/lv2_register.c:
  prova 'vm_write_be32(seg + 0x14, src_ea + p_off)' nao esta em 832-832, linha_actual=841
```

Qualquer bloco que altere o numero de linhas de um dos 11 ficheiros ancorados no
`docs/UPSTREAM_DELTA.tsv` faz a `PERNA B` reprovar por **deriva de ancora**, nao por
regressao. Nao e' defeito do portao (a suite falha de facto) e o README ja' obriga a
re-ancorar, mas **fica deferido** escrever isso como passo explicito do procedimento de
submissao de bloco (`--fix-anchors` antes do portao) no `docs/upstream-blocks/README.md`.

### 3. `sys_tty_write` (syscall 403) NAO e' chamada na intro do GoW2

Medido com `_exit(97)` depois da primeira escrita do guest: tres corridas do leg `depois`
com `rc=143` (nenhuma `saida-anormal`) e o `.o` comprovadamente recompilado. O comentario
do proprio `runtime/syscalls/lv2_register.c:36` ("among the most commonly called syscalls
in CRT startup") **nao se confirma neste titulo**. Achado registado; nenhuma accao pedida.

### 4. Ainda `nao-exercitado` depois deste plano

- `FORCE_REBUILD_LIFT=1` por cabecalho alterado: continua so' exercitado no sentido
  negativo (nenhuma das quatro corridas deste plano tocou um `.h` de `include/` ou
  `runtime/ppu/`).
- A subtraccao da divida `known_debt_python` na `PERNA B` continua provada apenas
  `offline-unit` (com `SDKROOT` exportado pelo portao, as duas falhas nao ocorrem).

---

## Plano 23-04 (a travessia do bloco LV2S) — 2026-08-12

### 1. A `PERNA A` passou EXACTAMENTE no limiar — o item 1 do 23-03 continua aberto

Medido na corrida real do bloco `LV2S-01`, com `--runs 6`:

```
antes  = 0,0,11,11,11,1   -> ok_antes  = 3
depois = 1,11,11,11,1,11  -> ok_depois = 4   (limiar = (6*2+2)/3 = 4)
```

`ok_depois == limiar`: **uma** corrida fraca a mais no leg `depois` teria dado
`REJEITADO` a um bloco cuja unica alteracao de codigo e' uma escrita `u32` numa
syscall que este titulo **nao chama**. O `--runs 6` (mitigacao registada em
`.planning/WINDOWS.md` #24) foi suficiente hoje, e nada mais. O item 1 do
23-03 — corrida de aquecimento por leg, ou `--runs 6` como default do portao —
**continua por decidir**, e agora com um segundo ponto de medicao a favor.

Nota de vies, para nao se ler o `PASSOU` como mais forte do que e': o leg
`antes` corre sempre primeiro e absorve a cache fria (as suas duas primeiras
corridas deram `0`), o que empurra `ok_antes` para baixo e torna
`ok(depois) >= ok(antes)` **mais facil** de satisfazer.

### 2. A divida `known_debt_python` NAO se reproduziu no fecho deste plano

Medido: `scripts/test_python.sh` corrido a mao, com `SDKROOT` **vazio** no
ambiente, deu `PASS 18 / SKIP 2 / FAIL 0` (rc=0) — os dois testes da divida
(`tools/test_ppu_lift.py` e `runtime/ppu/tests/test_ppu_lifter_emit.py`)
**passaram**. E `scripts/test_runtime.sh` deu `100% tests passed out of 30`,
incluindo `suite.runtime_spu` e `suite.runtime_trace`, que o ledger do 23-02
registou a falhar por falta de `-isysroot`.

Isto **nao** e' prova de que a divida foi corrigida: nada neste plano lhe tocou,
e a causa medida na altura (um `cc` nu sem `-isysroot`) e' propriedade do
ambiente do host, nao do repositorio. Fica registado que a divida e'
**condicional ao ambiente** e que uma corrida verde nao a fecha. Deferido:
decidir se o `known_debt_python` do baseline deve passar a ser re-medido em vez
de citado — hoje a `PERNA B` subtrai dois nomes que podem nao estar a falhar.

### 3. Ainda `nao-exercitado` depois deste plano

- **O portao a REPROVAR um bloco real.** Foi exercitado a reprovar uma
  **injeccao sintetica** (23-03) e a aprovar um **bloco real** (23-04). Um bloco
  real reprovado por regressao real continua sem existir.
- **`FORCE_REBUILD_LIFT=1` no sentido positivo:** o bloco `LV2S` nao toca em
  nenhum `.h` de `include/` nem de `runtime/ppu/`, logo a regra continuou a
  disparar apenas no sentido negativo (`FORCE_REBUILD_LIFT=0` nos dois legs).
- **`tools/check_block_record.py` sobre um segundo registo:** hoje a pasta tem um
  unico registo de bloco (`LV2S-01.md`). A regra de seleccao que exclui o
  `README.md` e o `GATE-MUTATION-PROOF.md` esta' provada por teste sintetico e
  pela corrida real, mas a pasta ainda nao tem dois registos ao mesmo tempo.
- **`--antes-bin` combinado com `--depois-arvore`:** cada um ja' foi exercitado
  (23-03 e 23-04, respectivamente), a combinacao dos dois nao.

### 4. CORRIGIDO — a superdeclaracao «25 ficheiros entram no binario» (ressalva G5 do review)

O `docs/upstream-blocks/LV2S-01.md` dizia, na seccao «Medicao — por leg»:

> Dos 48 ficheiros alterados entre os legs, 25 batem `^(runtime|libs|include)/` e
> entram no binario.

**Porque superdeclarava:** casar um prefixo de caminho nao e' entrar no binario. O
campo `compilados=25` que a `PERNA C` imprime e' um contador de prefixo; a frase
traduzia-o para uma afirmacao sobre o **link**, que ninguem tinha medido. Era uma
inferencia tirada do nome do campo, escrita como se fosse medicao — exactamente a
falta que a regra 5 do repo classifica como a mais grave.

**Numero real, medido** [nivel de evidencia: `offline-unit`]: entra no binario
**1** dos 48 — `runtime/syscalls/lv2_register.c`, que e' o proprio bloco. As
medicoes fecham os dois lados do unico elo possivel entre fonte e binario
(ser compilado num objecto que o link puxa):

- grafo de build: dos 25, so' o `lv2_register.c` aparece no `build-macos/build.ninja`
  (3 mencoes); os outros 24 dao 0 — com controlo do metodo, o mesmo `grep -cF` da' 3
  no ficheiro que sabemos la' estar. Nenhum e' mencionado no `games/gow2/build_macos.sh`.
- binarios: `nm -m` sobre os **proprios** `boot_gow2_antes` / `boot_gow2_depois` do
  portao (ainda em `${TMPDIR}/gate_lv2s`, `sha256` a bater byte a byte os da tabela do
  registo) encontra `_lv2_register_all_syscalls` nos dois legs; controlo negativo:
  zero simbolos dos harnesses SPU (`test_sum_main`, `test_workload_main`,
  `test_taskset_main`).
- causa: `CMakeLists.txt:135,149` exclui `/tests/` da biblioteca, e 23 dos 24 vivem sob
  `runtime/spu/tests/` (sao **saida** do lifter consumida por harnesses, e nenhum
  ficheiro do runtime fora de `runtime/spu/tests/` faz `#include` destes
  cabecalhos -- os 12 `#include "spu_recomp.h"` em `games/gow2/spu_lifted/*/`
  sao do cabecalho LOCAL de cada directorio, nao destes, e nenhum deles esta'
  no conjunto dos 25); o 24.o e' um
  script de shell (`run_spu_image_import_mutation.sh`), que ninguem compila. O delta do
  `CMakeLists.txt` entre os legs (+62 linhas) vive todo dentro do
  `if(PS3RECOMP_BUILD_TESTS)` (linha 274), depois do `add_library` (linha 171).

**O que foi alterado:** o paragrafo passou a dizer «25 casam o prefixo : 1 entra no
binario», a linha da `PERNA C` ficou verbatim mas com nota de pe' a explicar que
`compilados` conta prefixos, e a seccao «O que este portao NAO prova» ganhou a
afirmacao correspondente. Na mesma passagem corrigiram-se mais tres frases do mesmo
tipo: o juizo «inofensivo» sobre o espelho `+0x14` (passa a inferencia declarada), a
ancora `CMakeLists.txt:440-461` (desactualizada; e' `453-459`, e a exclusao e' a `149`)
e «a intro nao regrediu» (passa a «o portao nao detectou regressao», que e' o que a
amostra suporta). No `GATE-MUTATION-PROOF.md`: a recompilacao incondicional do
`ppu_loader.cpp` passa a estar identificada como leitura do script de build, com o
`rc=97` das tres corridas como a medicao que de facto prova a injeccao no binario; o
`compilados=1` da `PERNA C` ganha a mesma ressalva; e o `grep -rn '_exit(97)'` passa a
trazer controlo positivo.

**Fica deferido (nao corrigido aqui):** renomear o campo `compilados` do
`scripts/upstream_block_gate.sh` para algo que nao prometa link (ex.:
`prefixo_codigo=`), ou medi-lo de facto contra o grafo de build. Mexer no portao
depois de ele ter julgado o bloco `LV2S` mudaria o instrumento a seguir a' medicao;
a divida fica escrita e a decisao e' da Fase 24.

**Fica registado (ressalva 3 do review, parcial):** a separacao de honestidade imposta
pelo `tools/check_block_record.py` e' **estrutural** (reprova seccoes fundidas, marcas
em falta, duas marcas na mesma afirmacao, `in-boot` no que nao se prova) e **nao
semantica** — nao apanha uma frase que, dentro da seccao certa, funde duas afirmacoes
ou traduz uma inferencia em medicao. Foi exactamente por isso que esta superdeclaracao
passou o verificador com `violacoes=0`. Nenhum verificador de texto substitui a
revisao; a mitigacao real continua a ser o review por plano.
