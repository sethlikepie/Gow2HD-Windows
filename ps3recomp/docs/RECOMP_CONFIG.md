# RECOMP_CONFIG.md — schema TOML do recompilador (`tools/recomp_config.py`)

> Preenchido à medida que as fases do plano
> [`2026-07-23-xenonrecomp-lessons-recomp-tooling.md`](superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md)
> forem aterrando. **Task A1** (parser), **Task B3** (`[[invalid_instructions]]`)
> e **Fase 17 / XEN-01** (`[[midasm_hook]]` — emissão) estão feitas. O
> **`[[switch_table]]`** está **consumido de ponta a ponta desde o plano 18-02**:
> parser (`load_switch_tables`), merge (`merge_switch_tables`), emissão
> (`case … goto loc_…`), gerador (`tools/ps3_analyse.py`, Fase 18 / XEN-03),
> ficheiro de produção (`games/gow2/config/gow2_switch_tables.toml`) e a chave
> `[main].switch_table_file` **activa** no `gow2_recomp.toml`.
> **`[[functions_override]]` passou a ser consumido no plano 19-01** (Fase 19 /
> XEN-04), mas **só com a emissão explicitamente ligada** — ver a secção própria.
> `[optimizations]` continua **parseado e ignorado**. Este documento descreve só
> o que o lifter **realmente consome hoje** (XEN-06); o que não está ligado está
> marcado como tal, não descrito como se estivesse.

Inspirado no config declarativo do [XenonRecomp](https://github.com/hedge-dev/XenonRecomp),
adaptado ao lifter PPU deste repo. Exemplo mínimo: `gow2-recomp/config/gow2_recomp.toml`.

## `[main]` (obrigatório)

| Campo | Tipo | Obrigatório | Nota |
|---|---|---|---|
| `elf_path` | string | sim | resolvido relativo ao diretório do `.toml` |
| `functions_json` | string | sim | idem |
| `out_directory` | string | sim | idem |
| `switch_table_file` | string | não | ficheiro **separado** com `[[switch_table]]` (ver secção própria), resolvido **relativo ao diretório do `.toml`**. Parseado por `load_switch_tables` e fundido por `merge_switch_tables`; o gerador é `tools/ps3_analyse.py` (Fase 18). **Activo desde o plano 18-02** no `gow2_recomp.toml` (`= "gow2_switch_tables.toml"`) — ver armadilha abaixo, que continua a valer para um caminho errado |
| `emit_weak_wrappers` | bool | não | **default `false`.** Liga a emissão dos wrappers fracos para as funções de `[[functions_override]]` (Fase 19 / XEN-04). Equivalente ao `--emit-weak-wrappers` do CLI — qualquer um dos dois liga; **nenhum liga por omissão**. Ver a secção `[[functions_override]]` |

Falta `[main]` ou alguma destas chaves → `RecompConfigError`.

> **Armadilha medida (Fase 17, plano 17-03) — continua a valer:**
> `switch_table_file` a apontar para um ficheiro que **não existe** faz o
> `ppu_lifter.py` **abortar logo no arranque** com
> `Error: <path>: switch table file not found` — e não aborta só a parte das
> switch tables: morre o `--config` **inteiro**, incluindo os `[[midasm_hook]]`.
> Foi exactamente assim que o primeiro re-lift do piloto CE03C morreu, e é por
> isso que a chave esteve comentada entre o 17-03 e o 18-02.
>
> O que mudou no **plano 18-02**: o ficheiro passou a existir e a chave foi
> reactivada **no mesmo commit**. A armadilha só desapareceu para o caminho
> certo — apontar para um caminho **errado** continua a matar o `--config` todo.
> Regra prática: nunca declarar a chave "à frente" do ficheiro.
>
> **Não-regressão provada (18-02, evidência `offline-unit`):** com a chave
> activa, um re-lift completo com `--config` para um directório novo continua a
> emitir `gow2_midasm_Ce03cWaitIdle(ctx);` na primeira linha de
> `func_000CE03C`. Mais forte que o grep: o `diff` entre o lift **sem**
> `--config` e o lift **com** `--config` dá como única diferença o bloco de
> declaração do hook (9 linhas por chunk) e as suas duas chamadas — **zero**
> linhas de diferença em switch tables.

> **`[main]` não substitui os argumentos de linha de comando.** O CLI do
> `ppu_lifter.py` continua a receber `input`, `--functions` e `-o` à mão; o
> `--config` traz **só** as secções declarativas. Os dois lados são sincronizados
> por quem corre o comando, não pelo lifter.

## `[[midasm_hook]]` (0..N) — **consumido desde a Fase 17 (XEN-01)**

| Campo | Tipo | Consumido? | Nota |
|---|---|---|---|
| `name` | string | **sim** | vira o símbolo `gow2_midasm_<name>`. Tem de ser um identificador C válido — senão o lifter **recusa** (rc=2), em vez de emitir uma TU que não compila |
| `address` | int | **sim** | endereço guest da instrução-âncora (aceita `0x...`) |
| `registers` | list[string] | **não** | reservado p/ opt de locals; a v1 passa o `ctx` inteiro e ignora este campo |
| `after_instruction` | bool | **sim** | hook antes (default `false`) ou depois da instrução |
| `return` (TOML) / `returns` (Python) | bool | **sim** | emite `return;` logo a seguir à chamada |
| `jump_address` | int | **NÃO IMPLEMENTADO** | se presente, o lifter **recusa-se a liftar** (rc=2) — ver abaixo |

`return=true` **e** `jump_address` juntos → `RecompConfigError` no parser (mesma
regra do XenonRecomp).

### O que é emitido

```
void gow2_midasm_<Name>(ppu_context* ctx);   /* declarado no preâmbulo do lift */
...
    gow2_midasm_<Name>(ctx);                 /* a chamada, junto à instrução */
```

- **Assinatura:** um único parâmetro, `ppu_context*` — o mesmo tipo que o lift já
  usa em `void func_XXXXXXXX(ppu_context* ctx)`. O símbolo **não** leva
  `--symbol-prefix`: é código host versionado, não uma função liftada.
- **Colocação:** por default a chamada vai **antes** da linha C traduzida do
  `address`; com `after_instruction = true`, **depois**. Com `return = true`,
  um `return;` fica imediatamente a seguir à chamada (na mesma colocação).
- **O hook não substitui a instrução** — corre ao lado dela. Substituir a função
  inteira é weak override e não é isto (Fase 19).
- **O `loc_XXXX:` fica antes** dos hooks `before`, para que um `goto` para o EA
  ancorado passe pelo hook em vez de o saltar.
- `return = true` pode deixar código morto a seguir (a própria instrução
  ancorada, se o hook for `before`, e o resto do bloco). É aceite na v1 —
  cortar o resto da função é precisamente o objectivo de `return`.
- O lifter **declara** os hooks no preâmbulo de cada TU gerada (dentro de um
  `extern "C"` condicionado a `__cplusplus`, porque o mesmo preâmbulo serve o
  `.c` single-file e os chunks `.cpp`). O corpo tem de existir em código host
  ligado ao binário; um hook declarado no TOML e sem corpo dá **erro de link**.
- **Consequência conhecida, agora MEDIDA:** um EA coberto por mais do que uma
  função liftada recebe a chamada em **todas** elas — tal como a própria
  instrução. No piloto CE03C (plano 17-03) isso aconteceu de facto: o hook saiu
  em `func_000CE03C` (o sítio certo, primeira linha do corpo) **e** em
  `func_000CE02C`, uma função cujo intervalo atravessa o mesmo endereço. Na
  segunda, a chamada cai **depois** de um `{ g_trampoline_fn = ...; return; }`
  incondicional, portanto é código morto e nunca corre — verificado por leitura
  do `.c` gerado, não assumido. **Mas isto é uma propriedade a vigiar:** se o EA
  tivesse um `loc_XXXXXXXX:` na segunda função, o hook seria alcançável e
  correria num contexto que ninguém declarou. A guarda de cobertura do 17-01
  verifica que o EA **existe**, não que corresponde a **uma só** função.

### Exemplo end-to-end: o piloto CE03C (Fase 17, plano 17-03)

É a cadeia inteira, do TOML ao símbolo no binário. Nenhum passo é opcional.

**1. Declarar** — `games/gow2/config/gow2_recomp.toml`:

```toml
[[midasm_hook]]
name = "Ce03cWaitIdle"
address = 0x000CE03C
```

**2. Dar-lhe corpo** — `games/gow2/hooks/gow2_midasm_hooks.{h,cpp}`, **no mesmo
commit**. Sem corpo, o link parte (é o comportamento desejado: falha alta).

```c
void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);   /* .h */
```

**3. Liftar com `--config`** (directório NOVO — nunca por cima da produção):

```bash
python3 tools/ppu_lifter.py EBOOT.ELF --functions functions.json \
        --config config/gow2_recomp.toml -o recomp_macos_v3_midasm -j 6
#   Config: 1 midasm_hook(s) loaded from config/gow2_recomp.toml
```

**4. Provar** — a chamada tem de estar no `.c` gerado **sem correr patch nenhum**:

```bash
grep -n 'gow2_midasm_Ce03cWaitIdle' recomp_macos_v3_midasm/ppu_recomp_*.cpp
# ppu_recomp_002.cpp:295  : void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);   <- declaração (preâmbulo)
# ppu_recomp_002.cpp:19537:         gow2_midasm_Ce03cWaitIdle(ctx);             <- a chamada, dentro de func_000CE03C
```

**5. Compilar e ligar** — `games/gow2/build_macos.sh` compila
`hooks/gow2_midasm_hooks.cpp` para `$LIFT/gow2_midasm_hooks.o` e mete-o na linha
de link. `RELIFT=1` passa o `--config` sozinho (`PS3_RECOMP_CONFIG` sobrepõe o
default). Aceite: `nm boot_gow2 | grep gow2_midasm` → `T _gow2_midasm_<Name>`.

> **O guard anti-duplicate-symbol do `build_macos.sh` tem de casar com a
> DEFINIÇÃO, nunca com o protótipo.** O lifter escreve `void
> gow2_midasm_<Name>(ppu_context* ctx);` no preâmbulo de toda a TU gerada com
> hooks; um guard pelo nome do símbolo passaria a saltar a compilação a partir do
> primeiro lift com `--config` e o link morreria em `undefined symbol`.
> Verificado nesta fase contra o lift real: o guard **não** dispara.

**Sem `--config` não há emissão nenhuma** — o lifter corre exactamente como
antes, byte-a-byte (golden `tools/testdata/midasm_golden_noconfig.c`). É por isso
que um `RELIFT` que esqueça o `--config` produz um lift **sem** o fix, com o
objecto dos hooks compilado e ninguém a chamá-lo.

### O que o lifter RECUSA (rc=2, não warning)

O código de saída 2 é o mesmo contrato de recusa dos `patch_*.py` deste repo
(ex.: `patch_ce03c_introseq_block.py` sai 2 quando o corpo gerado diverge do
esperado). A regra: **melhor não entregar lift nenhum do que entregar um lift
subtilmente errado que compila e corre.**

| Situação | Porquê recusar em vez de avisar |
|---|---|
| `jump_address` presente | Não está implementado. Avisar-e-continuar deixaria no disco um lift que **chama o hook e não salta** — a surpresa "declarado mas sem efeito" que o próprio `recomp_config.py` já rejeita com erro no `default` do `[[switch_table]]` |
| `name` não é identificador C | A TU gerada não compilaria, com o erro a apontar para código gerado em vez de para o TOML |
| `address` negativo | Nunca casaria com nenhum EA descodificado |
| **EA órfão** | O `address` declarado nunca produziria emissão nenhuma. Três causas, todas silenciosas sem esta guarda: (a) nenhuma instrução descodificada nesse endereço (desalinhado, ou saltado por `[[invalid_instructions]]`); (b) fora de qualquer intervalo de `--functions`; (c) dentro de um stub de import HLE, cujo corpo é substituído inteiro por `ps3_hle_call`. A guarda corre **antes** do lift, no processo pai, e dá o mesmo resultado com `-j1` e com `-jN` |

**Limitação declarada (não implementada, não fingida):** o lifter verifica que
*existe* uma instrução no EA declarado, mas **não** que essa instrução é a
*esperada* — o schema de `[[midasm_hook]]` não tem campo para declarar a
instrução-âncora, portanto não há nada com que comparar. Verificar isso exigiria
estender o schema; fica por fazer, não por feito.

### Duplicados

Vários hooks **diferentes** no mesmo `address` são legítimos (ex.: um `before` e
um `after`) e são emitidos por ordem de declaração no TOML — por isso o índice
interno é `{EA: [hook, ...]}` e não last-wins. Um duplicado **exacto** (mesmo
`name` **e** mesmo `address`) é erro de escrita: a repetição é **descartada com
aviso** (nunca em silêncio — a mesma regra de `merge_switch_tables`), porque
emiti-la correria o efeito secundário a dobrar.

### Lift paralelo

Os hooks viajam por `_worker_init` → `_WORKER_STATE` → `_worker_lift` e pelo
tuplo de `initargs` de `_parallel_lift`, o mesmo caminho que
`invalid_instructions` já usava. `-j1` e `-jN` produzem o mesmo output.

### Testes

`tools/test_midasm_emit.py` é a suite que exercita esta secção (antes/depois/
`return`, ordem multi-hook, label, terminador, wiring paralelo com um `mp.Pool`
real, recusas). Inclui o **golden de não-regressão**
`tools/testdata/midasm_golden_noconfig.c`: sem `--config`, o `.c` gerado numa
fixture pequena tem de sair byte-a-byte igual ao capturado com o lifter anterior
a esta fase, provado por `diff`. Ver `tools/testdata/README.md`.

## `[[switch_table]]` — ficheiro separado apontado por `[main].switch_table_file`

Não é uma secção do `.toml` principal: vive num ficheiro próprio (à imagem do
`functions_json`), lido por `recomp_config.load_switch_tables()`.

```toml
[[switch_table]]
dispatch   = 0x002A2130     # EA do PRÓPRIO `bctr` — NÃO o do `mtctr` anterior
default    = "indirect"     # única string aceite (ver abaixo)
confidence = "high"         # metadado do ANALISADOR — o parser ignora-o
cases = [
  0x002A27BC,
  0x002A282C,
]
```

> **Armadilha medida (18-01/18-02), e a razão de o exemplo acima ter mudado:**
> `dispatch` é o EA do `bctr`, que **não** é o EA da função nem o que está no
> ledger `PATCH_MIGRATION.tsv`. Para o dispatcher OPDISP do GoW2, o ledger diz
> `0x002A209C` (a **função**) e o `bctr` está em `0x002A2130`; para o TYDISP, o
> ledger diz `0x002B11B8` e o `bctr` está em `0x002B1224`. Declarar o EA da
> função produz uma entrada que o parser aceita, o merge regista e o lifter
> **nunca usa** — a surpresa "declarado mas sem efeito" que este schema existe
> para eliminar. Casa-se **função → dispatcher**, não ledger → `dispatch`.

| Campo | Consumido? | Nota |
|---|---|---|
| `dispatch` | **sim** | endereço do `bctr`, porque é assim que o lifter indexa `self.jump_tables` (`ppu_lifter.py:1534`) |
| `cases` | **sim** | aceita `0xEA` ou `{ index = N, target = 0xEA }`; o `index` é lido por compatibilidade de schema e **descartado** — a emissão chaveia por ENDEREÇO (`case 0x…u: goto loc_…;`) |
| `default` | parcialmente | só a literal `"indirect"` é aceite; qualquer outro valor é **erro de parse**, não aviso. Documenta comportamento que o codegen já tem (`default: ps3_indirect_call(ctx); return;`, `ppu_lifter.py:1541`) — não há representação interna para outra coisa |
| `confidence` | **não** (pelo lifter) | escrito pelo `ps3_analyse.py` para revisão humana; `_parse_switch_table` só lê `dispatch`/`default`/`cases` e ignora o resto sem rebentar (coberto por `tools/test_ps3_analyse_jtables.py`) |

O merge com a descoberta estática (`discover_jump_tables`) é feito por
`merge_switch_tables`: **config ganha no conflito** (uma `dispatch` declarada
SUBSTITUI o que a descoberta achou para aquele endereço, não se une a ela).
Duplicados e alvos fora dos limites da função saem em **aviso**, nunca em silêncio.

### O gerador: `tools/ps3_analyse.py` (Fase 18, XEN-03)

Ferramenta **offline**, irmã do lifter — não é uma flag dele e não gera código.

```bash
.venv/bin/python3 tools/ps3_analyse.py <ELF> -o <ficheiro>.toml
.venv/bin/python3 tools/ps3_analyse.py <ELF> -o /tmp/inspeccao.toml --include-low
.venv/bin/python3 tools/ps3_analyse.py <blob> --raw --base 0x10000 --toc 0x11000 -o t.toml
```

Sinaliza cada tabela com `confidence` e **por default só escreve as `high`**. As
`low` exigem `--include-low` e servem para inspeção offline: uma jump table
errada não é um teste que falha, é o boot a saltar para um endereço inválido —
por isso nunca entram num TOML de produção sem revisão humana.

`high` exige **todas** estas condições (qualquer falha ⇒ `low`, com a razão
escrita em comentário no TOML): terminal `bctr` incondicional; cadeia
`mtctr` + `lwzx` + base de tabela resolvida; ≥ `HIGH_MIN_TARGETS` alvos
validados; bound check (`cmplwi`/`cmpwi`) presente; e **coerência com esse
bound** — lidas exactamente `COUNT+1` entradas.

**Limites conhecidos** (a lista completa e a justificação estão no docstring do
módulo, que é a fonte): não apanha índices carregados por `ld`/`lbzx` (tabelas
de bytes/half-words), bases construídas por `addis/addi` sem passar pelo TOC,
dispatchers cuja `lwzx` fica fora da janela de `WINDOW_INSNS`, tabelas com
buracos, nem tabelas de 64 bits (`ldx`). Não verifica se o alvo cai dentro da
função que contém o dispatcher — isso é do `merge_switch_tables`. Em `--raw`
sem `--toc` o resultado é zero tabelas, por construção.

Falso negativo não é fatal (o `discover_jump_tables` corre à mesma dentro do
lift e os `patch_jumptable_*.py` continuam a ser a rede); falso positivo é que é
o perigo, e é por isso que o filtro é conservador.

Auto-teste: `.venv/bin/python3 tools/test_ps3_analyse_jtables.py`
(fixtures sintéticas — sem ELF, sem SDK, sem rede).

### O ficheiro de produção do GoW2 (plano 18-02)

`games/gow2/config/gow2_switch_tables.toml`, apontado por
`switch_table_file = "gow2_switch_tables.toml"` (caminho relativo ao `.toml`
principal, portanto o ficheiro vive **ao lado** dele).

O gerador produz **candidatos**; o ficheiro de produção é **revisto**. Só entram
tabelas `high` e revistas; as `low` exigem `--include-low`, nunca entram sem
revisão caso a caso, e a razão fica escrita por tabela. Regenerar (o
`EBOOT.ELF`/`functions.json` são dados do jogo e **nunca** se commitam — deste
ficheiro só saem endereços derivados):

```bash
.venv/bin/python3 tools/ps3_analyse.py ../gow2-recomp/EBOOT.ELF \
    --functions ../gow2-recomp/functions.json -o /tmp/gow2_jt_high.toml
```

**O que o ficheiro vale hoje, medido em vez de assumido (18-02):** correndo o
`ps3_analyse` e o `discover_jump_tables` sobre a **mesma** imagem, as **143**
tabelas `high` são **idênticas alvo a alvo** às que a descoberta já produz
sozinha (143 idênticas, 0 diferentes, 0 exclusivas de qualquer dos lados; a
descoberta dá 157 = 143 `high` + 14 `low`). Logo, declarar qualquer uma delas é
**hoje um no-op**: o merge substitui a entrada da descoberta por outra igual — e
isso confirma-se no lift, onde o `diff` com/sem `--config` não mostra **nenhuma**
diferença de switch tables.

Por isso o ficheiro de produção tem **duas** tabelas, não 143: são as únicas com
uma **terceira** fonte independente — os patches históricos
`patch_jumptable_2a209c.py` e `patch_jumptable_2b11b8.py`, cujas tabelas
(base `0x002A2134` + 58 offsets; base `0x002B1228` + 28 offsets) dão **igualdade
exacta** de conjuntos de alvos com o detector (54/54 e 22/22 alvos distintos).
Despejar as outras 141 seria pôr sob a etiqueta "revisto" tabelas que ninguém
reviu, sem ganho funcional nenhum.

O valor da declaração é ser **rede**, e está declarado como tal: o
`discover_jump_tables` já esteve **morto** em HEAD (`TypeError`/`NameError`
dentro de um `try/except` que só imprimia em stderr ⇒ zero jump tables em
qualquer lift, em silêncio — ver o docstring da função). Declaradas, estas duas
sobrevivem a uma regressão dessas.

### O caminho **real** de build lê estes ficheiros (plano 18-03)

O 18-02 provou o `--config` invocando o `ppu_lifter.py` **directamente**. O
caminho que um build usa é outro: `RELIFT=1 ./build_macos.sh` lê
`$HERE/config/gow2_recomp.toml` no checkout de build (`../gow2-recomp`), não no
monorepo. Enquanto lá a chave estivesse comentada e o `gow2_switch_tables.toml`
não existisse, um build **não** via nada disto — e se só a config chegasse sem o
TOML de tabelas, o `--config` abortaria **inteiro** (a armadilha do 17-03).

Os dois ficheiros passaram a viver também em `../gow2-recomp/config/`, e têm de
chegar lá sempre **juntos**. Saída literal do build (`RELIFT=1`):

```
  RELIFT: --config .../gow2-recomp/config/gow2_recomp.toml
  Config: 1 midasm_hook(s) loaded from .../gow2-recomp/config/gow2_recomp.toml
  Config: 2 switch_table(s) declared in .../gow2-recomp/config/gow2_switch_tables.toml
  switch tables: 157 from discovery, 2 from config, 2 overridden by config, 157 total
```

E o lift que sai daí é o **mesmo** que o 18-02 verificou pela invocação directa:
`diff` dos 7 chunks + `ppu_recomp.h` dá **2 linhas** por chunk, e são o carimbo
de proveniência `/* lifter-rev: <sha> */` — **zero** bytes funcionais.

### Classificação dos dois `patch_jumptable_*` (plano 18-03)

Medido numa escada A/B/C, com o resultado escrito no
`games/gow2/lift_baseline/PATCH_MIGRATION.tsv`:

| Passo | `--config` | patch | Resultado medido |
|---|---|---|---|
| A | não | não | os dois `case` de aceite **já saem**, dentro do `switch` da função dona do `bctr` (54 e 22 alvos distintos) |
| B | sim | não | idêntico a A — a declaração **não** é load-bearing hoje |
| C | sim | sim | `2a209c` é **no-op** (SHA256 do chunk igual); `2b11b8` **não** é (ver ressalva) |

Como A já passa, os dois ficam **`estado=redundant`** e **não** `migrated`: não
houve migração nenhuma, descobriu-se que o patch não era preciso. Um
`redundant` **não conta** para os ≥3 fixes re-lift-safe do XEN-05.

**Ressalva medida no `2b11b8`:** ele ainda substitui a leitura de `TOC-0x151C`
por uma tabela hardcoded. Esse resíduo **não é** a jump table e portanto não está
coberto pelo veredicto. Na EBOOT, `[TOC-0x1694] = 0x002A2134` e
`[TOC-0x151C] = 0x002B1228` — exactamente as bases que os patches hardcodam,
logo a leitura que o lift faz está correcta **na imagem**; isso enfraquece muito
a hipótese original (`ICALL-BAD ctr=0x27182818`) mas não a refuta em runtime.
Por isso nenhum dos dois ficheiros foi apagado nem alterado.

### Cada comportamento documentado tem um teste (XEN-06)

| Comportamento | Teste |
|---|---|
| Schema `[[switch_table]]`, `default` só `"indirect"`, `index` descartado | `tools/test_recomp_config.py` |
| Merge com a descoberta, config ganha, duplicados e OOB avisados | `tools/test_switch_tables.py` |
| Emissão `case 0x…u: goto loc_…;` a partir da tabela | `tools/test_ppu_jumptables.py` |
| Detector, `confidence` high/low, `--include-low`, round-trip pelo parser | `tools/test_ps3_analyse_jtables.py` |
| `--config` não parte a emissão mid-asm | `tools/test_midasm_emit.py` |

## `[[functions_override]]` (0..N) — **consumido desde a Fase 19 (XEN-04), atrás de flag**

| Campo | Tipo | Consumido? | Nota |
|---|---|---|---|
| `address` | int | **sim** | Tem de ser o **endereço de início** de uma função liftada |
| `size` | int | **não** | Registado pelo parser, **não consumido**. Quem decide os limites do corpo envolvido é o próprio lift (`func.start_addr`/`func.end_addr`), que já pode ter partido a função em fragmentos; validar `size` contra isso daria falsos negativos exactamente nas funções que mais interessam |

### `[[midasm_hook]]` **ou** `[[functions_override]]`? — a distinção que decide qual usar

São mecanismos diferentes para problemas diferentes, e escolher mal é a origem
da entrada #8 do `WINDOWS.md`:

| | `[[midasm_hook]]` (Fase 17) | `[[functions_override]]` (Fase 19) |
|---|---|---|
| Onde corre | **ao lado** de UMA instrução | **envolve a função inteira** |
| A função original | continua a correr | é **substituída** pelo código host |
| Símbolo host | `void gow2_midasm_<Name>(ppu_context*)`, `extern "C"` | `void func_XXXXXXXX(ppu_context*)`, **linkage C++** |
| Alvo declarado | EA de qualquer instrução | **só** o EA de **início** de função |
| Usar quando | quero espreitar/corrigir um ponto do corpo e deixar o resto | quero reimplementar a função toda em host |

O caso concreto: um *pad* que precisa de correr **em vez** do corpo (ou à volta
dele) não se resolve com mid-asm — um mid-asm corre ao lado de uma instrução e
não envolve nada. É para isso que existe o weak override.

### O esquema de símbolos

| Símbolo | Linkage | Papel |
|---|---|---|
| `__imp_func_XXXXXXXX` | **forte** | corpo liftado (a implementação original) |
| `func_XXXXXXXX` | **FRACO** | wrapper fino: chama `__imp_func_XXXXXXXX(ctx)` |
| `func_XXXXXXXX` (host) | **forte** | o override; **ganha o link** sobre o wrapper |

As macros vivem num só sítio, `runtime/ppu/ppc_hooks.h` (`PPC_FUNC_IMPL`,
`PPC_FUNC`, `PPC_FUNC_IMPL_DECL`), e o lifter **usa-as** em vez de carimbar
`__attribute__((weak))` no output — se as duas definições divergirem, o build
parte alto em vez de emitir um wrapper que *parece* fraco e não é.

### Ligar a emissão (nunca ligada por omissão)

```bash
python tools/ppu_lifter.py EBOOT.ELF --config config/gow2_recomp.toml \
       --emit-weak-wrappers ...
```

ou, no TOML, `[main] emit_weak_wrappers = true`. Qualquer um dos dois chega.

- **Sem** a flag, o output é **byte-a-byte** o de sempre — mesmo com
  `[[functions_override]]` declarado (golden por `diff` contra
  `tools/testdata/midasm_golden_noconfig.c`). Declarar overrides com a emissão
  desligada dá um **Warning**, para não ser um "declarei e não fez nada" silencioso.
- **Só as funções declaradas** são envolvidas. Lista vazia com a flag ligada
  emite **zero** wrappers e avisa — nunca "todas as funções" (ver o custo medido
  mais abaixo).
- `function_table` e as chamadas directas entre funções liftadas continuam a
  nomear `func_XXXXXXXX` (o símbolo substituível), **nunca** `__imp_*`; é assim
  que o override fica visível tanto para o dispatcher indirecto como para quem
  chama directamente.

### O que o lifter RECUSA (rc=2, não warning)

Um `address` que **não** seja o início de uma função liftada — seja no interior
de uma função, seja fora de qualquer uma. Recusa **antes de escrever qualquer
ficheiro**, com o EA listado. Um override declarado que não produzisse wrapper
nenhum deixaria o jogo a correr o corpo **original** julgando-se substituído, e
o sintoma apareceria a horas de distância, no boot, sem ligação visível à causa.
A recusa só corre com a emissão **ligada**: com ela desligada a secção continua
a ser ruído de config inofensivo, e lifts que já correm assim não podem partir.

### Escrever o override host

```cpp
#include "ppu_recomp.h"

void func_002C0508(ppu_context* ctx) { /* ... */ }
```

**Linkage C++, sem `extern "C"`** — é o linkage com que o lifter emite
`void func_XXXXXXXX(ppu_context*)`. Um `extern "C"` produziria um símbolo com
outro nome, o wrapper fraco continuaria a ganhar, e o override seria um no-op
silencioso.

Para correr **à volta** do corpo original em vez de o substituir:

```cpp
#include "ppu_recomp.h"
#include "ppc_hooks.h"

PPC_FUNC_IMPL_DECL(func_002C0508);
void func_002C0508(ppu_context* ctx) {
    gow2_pre(ctx);
    __imp_func_002C0508(ctx);   // o corpo liftado original
}
```

### Ligar o override ao build (Fase 19 / plano 19-02) — e as três armadilhas

O exemplo do GoW2 está em `games/gow2/hooks/gow2_func_overrides.{h,cpp}`,
compilado e ligado pelo `games/gow2/build_macos.sh`. Três coisas que **não**
são óbvias e que foram medidas:

1. **O guard anti-duplicate-symbol não pode procurar o nome.** As TUs liftadas
   trazem, além da definição, declarações (`void func_X(ppu_context* ctx);`),
   chamadas (`func_X(ctx);`), a entrada da `function_table` e o próprio wrapper
   `PPC_FUNC(func_X) {`. Um `grep` pelo nome casa com todas e manda saltar a
   compilação para sempre. O guard procura a **forma da definição**: `(...)`
   sem `;` lá dentro, seguido de `{` (ou de fim de linha) — o que **não** casa
   com `PPC_FUNC_IMPL(func_X) {` nem com `PPC_FUNC(func_X) {`, onde a seguir ao
   nome vem `)`. É isso que separa o lift **com** wrappers (compilar) do lift
   **sem** (não compilar).
2. **Há três estados, e o terceiro é silencioso.** Definição forte no lift →
   não compilar (`duplicate symbol`, medido); wrapper fraco → compilar; **nem
   um nem outro** → o objecto compila e liga, define um símbolo que ninguém
   chama, e o override é um **no-op invisível**. O guard avisa nesse caso.
3. **Um lift com override tem uma segunda forma de "início de função", e as
   ferramentas de gate precisam de a conhecer.** Medido: com só a forma antiga,
   `tools/lift_parity.py` acusou `NO_BODY` sobre uma função que lá estava
   inteira, e `games/gow2/lift_baseline/check_contracts.py` acusou "âncora
   perdida" nos contratos `escopo=ea` do alvo — **vermelhos falsos**. As duas
   (mais `gen_manifest.py::preamble_end`) passaram a conhecer
   `PPC_FUNC_IMPL(func_X) {`. O **wrapper** nunca conta como início: vem logo a
   seguir ao corpo e tomá-lo como início daria uma região de duas linhas — pior
   que a âncora perdida, porque falha com um número plausível.

### Portabilidade: o risco **medido**, não assumido

O risco listado no plano de origem era *"weak symbols no `ld` da Apple"*.
Medição do plano 19-01 (Apple clang 21.0.0, arm64-apple-darwin25.5.0), em
`tools/test_weak_override_symbols.py`, sobre o lift **realmente emitido**:

```
nm -m: (__TEXT,__text) weak external  __Z13func_00001000P11ppu_context
       (__TEXT,__text) external       __Z19__imp_func_00001000P11ppu_context
```

Com um override host forte, o binário corre o **override** nos três caminhos:
chamada directa de outra TU, chamada indirecta via `function_table`, e chamada
directa a partir de outra função liftada da **mesma TU** — e continua a ganhar
com o lift compilado a `-O1` e `-O2` (o wrapper fraco não é *inlined* no sítio
da chamada). Controlo por mutação: o mesmo par **sem** o `weak` falha o link
com `duplicate symbol`.

> **Armadilha de leitura, medida:** no Mach-O o `nm` simples imprime **`T`**
> para uma definição **fraca**, tal como para uma forte — a letra `W` do ELF
> **não aparece**. A fraqueza só se lê com **`nm -m`** (`weak external`). Uma
> verificação "o símbolo aparece como `W`" reprovaria um mecanismo que funciona.

**Fora do Clang** (`PPC_HOOKS_WEAK_SUPPORTED == 0`) `PPC_WEAK` fica vazio, o
wrapper sai **forte**, e um override host dá `duplicate symbol` no link. É
deliberado: falhar alto é muito melhor que um override que não pega. Nenhum
outro toolchain foi medido por esta fase, e nenhum se assume.

### Custo medido: só as declaradas, não todas

A alternativa era emitir wrappers para **todas** as funções. Medido antes de
decidir (`tools/test_weak_override_symbols.py --measure-cost`, 2000 funções
sintéticas), o custo por wrapper é **~50 B de fonte** e **~111 B de objecto**.
Extrapolado ao lift real do GoW2 (**51 917** funções, 252,7 MB de fonte,
133,8 MB de objectos): **+2,6 MB de fonte (+1,0 %)**, **+5,8 MB de objecto
(+4,3 %)** e **+51 917 símbolos** (o dobro dos `func_*`).

O tamanho não era o argumento decisivo — o **tempo de execução** foi: os chunks
compilam com `LIFT_OPT=-O0` por omissão (`build_macos.sh`), e a `-O0` o wrapper
é uma chamada **real** (prólogo, `bl`, epílogo) verificada no *disassembly*.
Envolver as 51 917 funções faria **toda** chamada guest pagar um frame host
extra, para benefício nenhum nas funções que ninguém substitui. Daí: **só as
declaradas**.

### As três migrações reais do GoW2 (Fase 19 / plano 19-03) — e o padrão de cada uma

O piloto do 19-02 (`0x00010230`) é um override de **identidade**: existe para
medir o caminho de link, não para mudar comportamento. As migrações a sério
entraram no 19-03, e mostram que **há dois padrões distintos**, não um só:

| EA | Patch legado que substitui | Padrão | Chama `__imp_`? |
|---|---|---|---|
| `0x000CE03C` | `patch_ce03c_introseq_block.py` (metade do pad) | **envolver** — `setjmp` à volta do corpo natural | **sim**, dentro do `setjmp` |
| `0x0002F3F0` | `patch_b71_2f3f0_guard.py` | **envolver com saída antecipada** — valida `r4`, pré-varre 256 bytes | **sim**, excepto no caminho patológico |
| `0x002C0498` | `patch_fios_play_already_active.py` | **substituir** — o corpo natural *é* o defeito | **não** |

**A regra que separa os dois:** chama-se `__imp_` sempre que o corpo do jogo
continua a ser o comportamento certo — assim um re-lift melhora a tradução sem
ninguém tocar no host. Só se **não** chama quando o fix consiste precisamente
em não correr o corpo natural (`0x002C0498`: as duas instruções que o lifter
gera ali — `bl 0x002BFF88` + `b 0x002C0124` — são o que perde a corrida com o
pool de ops do FIOS).

**Um `[[functions_override]]` e um `[[midasm_hook]]` podem coexistir na mesma
função, e compõem-se.** É o caso do `0x000CE03C`: o corpo emitido
(`PPC_FUNC_IMPL`, símbolo `__imp_`) leva a chamada `gow2_midasm_Ce03cWaitIdle`
na primeira linha, e o override host chama esse `__imp_` de dentro do `setjmp`.
O wait-idle corre **uma** vez. Se o override também chamasse o hook haveria
dois *pumps* por entrada — e o wait-idle não é idempotente.

#### Efeito colateral medido: o `lift_parity` melhora

Um patch que reescreve o corpo no lift **cria** divergência face ao ELF. Ao
migrar `0x002C0498` para host, o sinal `0x002C0498/CALL_MISMATCH` do
`lift_parity` desapareceu (`current=76 baseline=77 novos=0 resolvidos=1`): o
lift voltou a ser uma tradução fiel, e a divergência passou a viver em código
host **declarado**. É um argumento a favor do mecanismo que não estava previsto.

#### Como se prova uma migração destas (as quatro medições)

Um contrato sobre o lift **não vê código host** — quando a função passa a ser
do host, o lift fica só com o par `PPC_FUNC_IMPL`/`PPC_FUNC`. Por isso a prova
tem quatro pernas, e nenhuma sozinha chega:

```bash
# 1) escada A/B/C sobre re-lifts completos da MESMA EBOOT
#    A: sem patch e sem --config  -> o aceite FALHA (o patch era load-bearing:
#                                    migrated, e nao redundant)
#    B: sem patch e com --config  -> o aceite PASSA (o mecanismo carrega o fix)
#    C: patch por cima do lift B  -> WEAK/skip, sha256 dos chunks IDENTICO
python3 tools/ppu_lifter.py EBOOT.ELF --functions functions.json -o /tmp/liftA
python3 tools/ppu_lifter.py EBOOT.ELF --functions functions.json \
        --config games/gow2/config/gow2_recomp.toml -o /tmp/liftB
.venv/bin/python3 games/gow2/lift_baseline/check_contracts.py \
        games/gow2/lift_baseline/CONTRACTS.tsv /tmp/liftB --patch <patch>.py

# 2) nm -m: quem ganhou o link (nunca `nm` simples -- no Mach-O uma definicao
#    FRACA tambem imprime T; so' `nm -m` distingue)
nm -m /tmp/liftB/ppu_recomp_00N.cpp.o | grep func_XXXXXXXX   # weak external
nm -m boot_gow2                       | grep func_XXXXXXXX   # external  <- host

# 3) o texto do fix esta' no BINARIO e nao no LIFT
strings -a boot_gow2 | grep -c '<marcador do override>'      # 1
grep -c '<marcador do override>' /tmp/liftB/ppu_recomp_*.cpp # 0

# 4) smoke de nao-regressao com o binario reconstruido DESSE lift
```

O contrato executável fica em `games/gow2/lift_baseline/CONTRACTS.tsv` com uma
**alternativa regex** — marcador injectado (lift antigo) **ou**
`PPC_FUNC_IMPL(func_X)` (lift com `--config`) — para ficar verde nos dois lifts
que coexistem. Armadilha medida no 19-03: com `escopo=ea` a região começa em
`void func_X(` / `PPC_FUNC_IMPL(func_X)`, portanto um marcador que o patch
legado escreve no **comentário acima** da assinatura fica **fora** da região e
dá `[FAIL]` sobre um lift onde o fix está aplicado. Ancorar em algo que esteja
dentro do corpo.

### Testes (XEN-06)

| Comportamento | Teste |
|---|---|
| `[main].emit_weak_wrappers` default `false`, parse de `[[functions_override]]` | `tools/test_recomp_config.py` |
| Golden sem a flag, forma `__imp_`/wrapper, `function_table`, EA órfão, `-j1 == -j2`, **link+`nm`+execução reais** | `tools/test_weak_override_symbols.py` |

## `[[invalid_instructions]]` (0..N)

`{data, size}`.

`invalid_instructions` **é consumido desde a Task B3**: `data` é a palavra crua
de 32 bits (não um endereço) e `size` é quantos bytes saltar quando essa palavra
aparece no scan linear — um PADRÃO DE PALAVRA, não um intervalo de endereços
(mesma semântica do XenonRecomp). Passa por
`tools/ppu_lifter.py --config <toml>` (`load_recomp_config` +
`PPULifter.invalid_instructions`), a par de `[[midasm_hook]]` e do
`[[switch_table]]` do ficheiro apontado por `[main].switch_table_file` e do
`[[functions_override]]` (este só com a emissão ligada); a secção que sobra,
`[optimizations]`, continua parseada e ignorada por este CLI. O consumidor real é
`ppu_disasm.disassemble_bytes(..., invalid_instructions={data: size, ...})`: a
palavra casada não é descodificada, o scan avança `size` bytes (não
necessariamente múltiplo de 4 — honrado tal como declarado), e o lifter emite
`/* invalid skip N bytes */` em vez do `/* TODO: ... */` de uma instrução
desconhecida. `size <= 0` ou que ultrapasse o fim do bloco tem tratamento
explícito (ver docstring de `disassemble_bytes` e `tools/test_invalid_instructions.py`).

## `[optimizations]`

Todas as flags (`skip_lr`, `skip_msr`, `ctr_as_local`, `xer_as_local`,
`reserved_as_local`, `cr_as_local`, `non_argument_as_local`, `non_volatile_as_local`)
default `false`. **Não ativar antes de A+B verdes** com smoke GoW2 completo — ver
constraint global do plano.

## API Python

```python
from recomp_config import load_recomp_config, RecompConfigError

cfg = load_recomp_config("gow2-recomp/config/gow2_recomp.toml")
cfg.main.elf_path            # caminho absoluto resolvido
cfg.midasm_hooks             # list[MidasmHook]
cfg.optimizations.skip_lr    # bool
```

Lado do consumidor (mid-asm), em `tools/ppu_lifter.py`:

```python
from ppu_lifter import (build_midasm_index, check_midasm_coverage,
                        MidasmConfigError, MIDASM_SYMBOL_PREFIX)

idx = build_midasm_index(cfg.midasm_hooks)      # {EA: [MidasmHook, ...]}
                                                 # levanta MidasmConfigError
orphans = check_midasm_coverage(idx, insn_addrs, func_bounds,
                                hle_stub_starts=set(hle_stubs))
lifter.midasm_hooks_by_ea = idx                  # atribuir ANTES de liftar
```

Lado do consumidor (weak override, Fase 19):

```python
from ppu_lifter import check_weak_override_coverage

if args.emit_weak_wrappers or cfg.main.emit_weak_wrappers:
    lifter.weak_override_eas = {o.address for o in cfg.functions_override}

# DEPOIS de liftar (as passagens de mid-function entries geram funções novas,
# que são alvos legítimos) e ANTES de escrever seja o que for:
orphans = check_weak_override_coverage(lifter.weak_override_eas,
                                       {f.start_addr for f in lifter.functions})
```

> Ao contrário de `midasm_hooks_by_ea`, o `weak_override_eas` **não** vai para
> os processos worker: a emissão do par `__imp_`/wrapper acontece em
> `_function_def_lines`, no processo **principal** (os workers só devolvem
> `body_lines`). Medido, não deduzido — `test_weak_override_symbols.py` compara
> os `.c` de `-j1` e `-j2` byte a byte com a emissão ligada.

Precisa de `tomllib` (Python 3.11+) ou `tomli` (`pip install -r tools/requirements.txt`
em Python < 3.11). Self-test: `python tools/test_recomp_config.py` (ou
`.venv/bin/python3` neste repo, que já tem 3.14).
