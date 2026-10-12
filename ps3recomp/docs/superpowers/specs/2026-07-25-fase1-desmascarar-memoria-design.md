# Fase 1 (1º entregável) — Desmascarar a memória guest e corrigir o mapa de regiões

> **Estado:** design aprovado pelo autor do projecto em 2026-07-25.
> **Origem:** Fase 1 de [`docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md`](../../IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md)
> (linhas 160–208), decomposta. Este documento cobre **apenas o primeiro
> entregável**; o checkpoint **C1 `BOOT_MEMORY_OK` NÃO é atingido aqui**.
> **Predecessor:** Fase 0 concluída (PR #5, merge `07fe030`) — instrumentação
> comum + baseline medido.

---

## 1. Porquê este entregável antes de qualquer forense de heap

O plano de origem manda, para a Fase 1: *"validar headers e footers do
allocator"*, *"registrar o primeiro escritor que corrompeu um header"* e
*"suspender temporariamente hooks de `COMMIT_HIGH` e outros experimentos para
diferenciar solução real de mascaramento"*.

O levantamento feito antes deste design (5 frentes paralelas, ver §8) mostrou
que duas dessas premissas não correspondem ao repositório:

1. **`PS3_COMMIT_HIGH` já foi corrido e refutado**, e está OFF por omissão.
   Não há ali nada a suspender. Os hooks que *realmente* mascaram memória hoje
   estão **sempre ligados** e não são `PS3_TRACE_*`.
2. **O campo `region=` mente.** `vm_region_name` (`runtime/memory/vm.h:332`)
   conhece 4 regiões e devolve `unmapped` para tudo o resto — incluindo a heap
   real do jogo. O primeiro erro do baseline da Fase 0
   (`ea=0xFB788082 region=unmapped`) foi medido através dessa lente.

Qualquer forense de header feita antes de corrigir isto mede através de uma
lente distorcida. Daí a ordem: **primeiro tornar a memória honesta e
observável, depois validá-la.**

### O que este entregável NÃO faz

Declarado explicitamente para o aceite não ser lido como mais do que é:

- não valida headers nem footers do allocator;
- não detecta double-free, overlap nem split inválido;
- não implementa os modos `strict`/`diagnostic`;
- não fecha o buraco das ~2110 escritas directas a `vm_base` (§7.1);
- **não atinge o C1 `BOOT_MEMORY_OK`.**

É a fundação que torna o C1 mensurável.

---

## 2. Restrições globais (Global Constraints)

Ligam **todas** as tarefas. Violação é defeito, mesmo que a tarefa individual
não as repita.

### G1 — Formato de linha

Toda linha nova usa o formato canónico já em vigor desde a Fase 0:

```text
[PS3T] <tb> | <thr> | <fn> | <subsys> | <event> | <data>
```

Separador exactamente ` | `; nenhum campo contém `|`; `<data>` são pares `k=v`
separados por espaço simples; uma linha = **uma** chamada de escrita; destino
`stderr`. A sanitização é central em `ps3_trace_emit` — **não reimplementar**.

### G2 — Gates e omissões

| Item | Omissão | Porquê |
|---|---|---|
| Emissão `MEMORY/SWALLOWED` | **OFF** (via `PS3_TRACE_MEMORY`) | é observação; regra 6 do `CLAUDE.md` |
| `PS3_MASK_OFF_*` | **máscara LIGADA** (como hoje) | desligar é o experimento, não o normal |
| Correcção de `vm_region_name` | **ON** | é bug, não experimento |
| Registo das ranges comitadas | **ON** | é bug, não experimento |

Com `PS3_TRACE_MEMORY` por definir e nenhum `PS3_MASK_OFF_*` definido: **zero
bytes emitidos pela camada nova e zero alteração de comportamento face ao
código pré-entregável**, com a única excepção declarada das duas correcções de
bug acima — cujo efeito é medido pela re-medição do baseline (§6).

### G3 — Não inverter o controlo

O código que decide engolir continua a decidir. A camada nova **observa**;
não altera a decisão excepto quando o `PS3_MASK_OFF_*` respectivo está ligado.

### G4 — Plataforma

macOS/arm64 é o alvo primário e onde tudo é medido. Windows não pode regredir.
Proibido fora de `#ifdef _WIN32`: `<intrin.h>`, `Sleep(`, `__declspec`,
`GetModuleHandleA`. Nada POSIX-only desguardado em código que entra na
`libps3recomp_runtime.a`.

### G5 — Honestidade de aceite

Todo aceite declara **in-boot**, **offline-unit** ou **não-exercitado**.
Sobre-declarar nível de evidência é defeito. Nunca forjar resultados.

### G6 — Runs

`PS3_NO_RSX=1` **e** `PS3_RSX_BACKEND=trace` (a Fase 0 provou que o primeiro
sozinho não é headless — `cellGcmSys.c` tem um segundo registo de backend
independente). Morte **por PID** (TERM, depois `-9`); **nunca**
`pkill -f boot_gow2`. Logs para fora dos dois repos, nunca commitados.
`PS3_ENGINE_ROOT` obrigatório ao construir o `boot_gow2`, senão liga-se o motor
do outro checkout.

### G7 — Git

Commits em **português**, sem `Co-Authored-By`. `git add` com paths explícitos
— **nunca** `git add -A`. Nunca dados de jogo, logs de boot, nem os artefactos
regenerados `runtime/spu/tests/out_*/spu_recomp.{c,h}`.

### G8 — Fronteira de repositório

**Este entregável toca apenas o repo do motor** (`ps3recomp`). As duas máscaras
`type15_*` vivem em `gow2-recomp/host_gow2_factory.cpp` (repo do jogo) e ficam
**declaradas mas não instrumentadas** — ver §7.2. Nenhum commit em
`gow2-recomp`.

---

## 3. Arquitectura

```text
MOTOR (ps3recomp — este entregável)        JOGO (gow2-recomp — fora de escopo)
├── vm_uncommitted_guard   :531            ├── ps3_type15_block_stomp
├── tblsize_guard          :968            └── type15_pin_free
├── vm_region_name  (vm.h) :332                (host_gow2_factory.cpp)
└── sys_memory: 5x vm_commit, 0x registo
```

Três componentes, cada um com uma responsabilidade e uma interface:

| Componente | Faz | Depende de |
|---|---|---|
| `runtime/trace/ps3_mask_trace.c` + `include/ps3_mask_trace.h` | contar e atribuir o que cada máscara engole | `ps3_trace.h` (Fase 0) |
| Correcção do mapa (`vm.h`, `sys_memory.c`) | `region=` dizer a verdade | — |
| Gates `PS3_MASK_OFF_*` | permitir o experimento E2 | — |

Nada de allocator novo, nada de tocar no lift, **nada de re-lift** — o que
evita a mina documentada em §7.1.

---

## 4. As máscaras do motor

### 4.1 `vm_uncommitted_guard` — `runtime/ppu/ppu_loader.cpp:531`

```c
static bool vm_uncommitted_guard(uint32_t a, uint32_t n,
                                 const char* who, void* caller_ra)
```

Devolve `true` e a escrita **desaparece**. Cobre `vm_read8/16/32/64` e
`vm_write8/16/32/64` (chamada em `:743`, `:758`, `:785`, …).

**Não é totalmente silenciosa** — loga as primeiras 40 (`:562`, `logged < 40`)
e a banda alta até 64 e depois de 1000 em 1000 (`:557-560`). O defeito é o
*cap* e o formato não-canónico, não a ausência de log. Acima de 40, é silêncio.

Já recebe `caller_ra`: **nenhuma mudança de assinatura**.

### 4.2 `tblsize_guard` — `runtime/ppu/ppu_loader.cpp:968`

```c
static int tblsize_guard(uint32_t a, uint64_t v)
```

Bloqueia zero-writes em 6 EAs fixos (`0x572BB4`, `0x572BB8`, `0x572BB0`,
`0x53FBF0`, `0x53FF4C`, `0x53D6AC`). Cirúrgica.

**Não recebe `caller_ra`** — tem de ganhar o parâmetro para a atribuição
funcionar. É mudança de assinatura numa função `static` de ficheiro protegido:
inserção cirúrgica, sem reformatar, e todos os call sites actualizados.

> Correcção registada: a versão falada deste design afirmou que ambas as
> funções já recebiam `caller_ra`. Só a primeira recebe.

### 4.3 O que cada uma ganha

**Observação** — emitida no ponto onde a máscara engole:

```text
[PS3T] <tb> | <thr> | <fn> | MEMORY | SWALLOWED | ea=0x%08X len=%u mask=<nome> op=<read|write> region=<nome>
```

- `mask=` ∈ { `uncommitted_guard`, `tblsize_guard` } — token fechado, sem espaços.
- `<fn>` vem de `dladdr` sobre `caller_ra`, o mesmo mecanismo que já resolve
  `_Z13func_00263554P11ppu_context+0x1A8` no `MEMORY/OOB`
  (`runtime/trace/ps3_mem_trace.c`). As 58 826 funções liftadas são símbolos
  globais `T` no `boot_gow2`, por isso a atribuição funciona — facto provado
  na Fase 0, não hipótese.
- Sai no subsistema **`MEMORY`, que já existe**. Não se cria um décimo-primeiro
  subsistema: o plano de origem fixa dez.

**Cap explícito.** Como no `MEMORY/OOB` da Fase 0, o volume é limitado, mas o
total tem de ser sempre recuperável: ao cortar, emitir
`SWALLOWED_TRUNCATED | mask=<nome> count=<total>` numa cadência que permita
reconstruir o total (a Fase 0 usa cadência linear; herdar a mesma decisão e
documentá-la).

**Desactivação** — para o experimento E2:

| Variável | Efeito quando `=1` |
|---|---|
| `PS3_MASK_OFF_UNCOMMITTED` | `vm_uncommitted_guard` devolve sempre `false` — deixa de interceptar e o acesso prossegue para o dereference bruto |
| `PS3_MASK_OFF_TBLSIZE` | `tblsize_guard` devolve sempre `0` (a escrita passa) |

> **Emenda de 2026-07-25 — decisão do autor do projecto, após achado da review
> da Task 2.** A primeira versão deste spec dizia que a flag fazia "a escrita
> passar". **É enganador para o `UNCOMMITTED`.** Devolver `false` significa
> apenas "não interceptar": o chamador prossegue para `vm_base[a]`
> (`ppu_loader.cpp:813` e simétricos). Como `vm_init` faz `mmap` do espaço
> inteiro com `PROT_NONE` (`vm.h:138-139`) e só `main` e `stack` ganham
> `PROT_READ|PROT_WRITE` (`vm.h:148,156`), o acesso a memória não comitada
> **gera falta real — SIGBUS no macOS**, como `ppu_loader.cpp:1744-1753` já
> documentava.
>
> Isso **é** o valor diagnóstico do E2: mostra onde o jogo morreria sem a
> máscara, com o EA e a função guest atribuídos — coerente com o critério de
> aceite escolhido ("um boot que parte mais cedo é entrega válida se soubermos
> porquê e a quem atribuir").
>
> Consequência declarada: o gate corta **antes** do bloco `vm_commit_high_on()`
> (`ppu_loader.cpp:587-599`), por isso combinar esta flag com `PS3_COMMIT_HIGH=1`
> **não** produz um acesso seguro — desliga o único caminho que comitaria a
> página. Não usar como se produzisse.

Omissão: **máscara ligada**, comportamento idêntico ao actual.

---

## 5. As duas correcções de mapa

### 5.1 `vm_region_name` — `runtime/memory/vm.h:332`

Hoje conhece 4 regiões e devolve `unmapped` para o resto:

| Região | Intervalo | Constante |
|---|---|---|
| `main` | `0x00010000`–`0x10010000` | `VM_MAIN_MEM_BASE/SIZE` |
| `rsx` | `0x10000000`–`0x20000000` | `VM_RSX_BASE/SIZE` |
| `spu` | `0x30000000`–`0x30200000` | `VM_SPU_BASE`, 8 janelas |
| `stack` | `0xD0000000`–`0xE0000000` | `VM_STACK_BASE/REGION` |

Consequências medidas: a **heap real do jogo** (`0x40000000`–`0x60000000`) e a
**RSX local real** (`0xC0000000`) caem em `unmapped`; a **imagem TLS**
(`0x10F00000`) cai dentro do intervalo `rsx` nominal e é reportada como `rsx`.

A corrigir: acrescentar as regiões em falta e desambiguar a TLS. É **correcção
de rótulo** — não altera nenhuma decisão de controlo (o `vm_oob` compara contra
`ppu_vm_size`, o guard consulta a lista de ranges registadas; nenhum deles usa
`vm_region_name`).

**Regra de honestidade:** um intervalo cuja fronteira não esteja confirmada por
código ou por medição fica **fora** desta correcção e é declarado no relatório.
Inventar um intervalo para eliminar `unmapped` seria trocar uma mentira por
outra.

### 5.2 Registo das ranges comitadas — `runtime/syscalls/sys_memory.c`

Medido: **5 chamadas a `vm_commit`** (`:101`, `:174`, `:383`, `:491`, `:570`) e
**0 chamadas a `ppu_register_committed_range`**. A memória é comitada mas nunca
registada, por isso `vm_uncommitted_guard` trata-a como não-comitada e engole
escritas a memória que o jogo legitimamente alocou.

A corrigir: cada `vm_commit` bem-sucedido regista a range.

**Esta é a correcção que muda comportamento por omissão** e a razão pela qual o
entregável termina com re-medição do baseline (§6.3). O `vm_commit` de `:491`
ignora o retorno — verificar antes de registar, e declarar o caso.

---

## 6. Fluxo, experimentos e aceite

### 6.1 E1 — quanto e de quem (sem alterar comportamento)

```bash
PS3_NO_RSX=1 PS3_RSX_BACKEND=trace PS3_TRACE_MEMORY=1   # máscaras LIGADAS
```

Responde: quantas escritas cada máscara engole, de que funções guest vêm, e
para que regiões. O jogo corre exactamente como hoje.

### 6.2 E2 — o que acontece sem ela (uma de cada vez)

Um run por máscara, mais um run de controlo:

| Run | Configuração |
|---|---|
| controlo | nenhuma `PS3_MASK_OFF_*` |
| E2-a | `PS3_MASK_OFF_UNCOMMITTED=1` |
| E2-b | `PS3_MASK_OFF_TBLSIZE=1` |

**Nunca as duas ao mesmo tempo**: desligar tudo de uma vez dá um crash e zero
informação sobre qual máscara importava. Um boot que parte mais cedo é entrega
válida **se** soubermos o volume, a origem e o ponto exacto.

### 6.3 Re-medição do baseline

`scripts/baseline_run.sh --runs 3` no fim, e comparação com
`docs/bringup/gow2/FASE0/baseline.json` via o `diff_baseline` do
`tools/trace_parse.py` (que compara identidade estável, não campos voláteis).

**Qualquer diferença tem de ser explicada** — não basta registar que mudou. Se
o baseline não mudar, isso é resultado válido e significa que as ranges por
registar não afectavam o caminho medido.

### 6.4 Critério de aceite

1. Relatório por máscara: contagem total, top de funções guest atribuídas,
   regiões afectadas, e até onde o boot chega sem ela.
2. `region=` correcto para a heap real do jogo, provado **in-boot** com linha
   real colada.
3. Baseline re-medido (3 runs) e comparado, com a diferença explicada.
4. Todas as suites verdes, incluindo `runtime/spu/tests/run_tests.sh` em
   `21 passed, 0 failed` e `scripts/smoke_macos.sh --no-gui` completo.
5. Com tudo desligado: **zero** linhas `[PS3T]`.

### 6.5 Testes

**Offline-unit** (convenção `runtime/trace/tests/`, ligados ao
`scripts/smoke_macos.sh` secção 2b — uma suite que nada executa apodrece em
silêncio, lição paga na Fase 0):

- `SWALLOWED` emitido com os campos de §4.3 quando o guard engole;
- gate desligado ⇒ **zero** bytes;
- `PS3_MASK_OFF_*` faz o **gate** devolver "não intercepta" (e a suite falha
  se regredir) — isto é o que esta suite offline-unit observa e verifica,
  na camada do gate (o valor de retorno da função), sem `vm_base` real
  mapeado. **Não** é o mesmo que "a escrita passa em segurança": no boot
  real, para `UNCOMMITTED`, "não intercepta" significa prosseguir para o
  dereference bruto sobre memória `PROT_NONE` — SIGBUS, não uma escrita
  bem-sucedida (ver a emenda de §4.3). Só para `TBLSIZE` "não intercepta"
  corresponde de facto a "a escrita passa", porque essa máscara nunca
  interage com memória não comitada;
- `SWALLOWED_TRUNCATED` no caminho de cheia, com o total recuperável;
- `vm_region_name` devolve o nome certo para um EA de cada região, **incluindo
  os que hoje dão `unmapped`**;
- `ppu_register_committed_range` chamado por cada `vm_commit` bem-sucedido.

**In-boot:** E1 e E2 com linhas reais coladas, e a re-medição do baseline.

---

## 7. Riscos e lacunas declaradas

### 7.1 O funil não é completo — ~2110 escritas directas a `vm_base`

O lift escreve memória guest por dois caminhos: os 12 acessores hookados
(`vm_read/write*`, atomics) **e** escritas directas a `vm_base` emitidas pelo
lifter — `stvx/stvebx/stvehx/stvewx` (1184), `stwbrx/sthbrx` (922) e `dcbz` (4),
medidas no lift construído. Estas contornam OOB, guard, watches e cancelamento
de reserva. Um único `dcbz` zera 128 bytes invisivelmente.

**Consequência para este entregável:** a contagem de `SWALLOWED` é um **limite
inferior**, e um header corrompido por uma dessas escritas não é observável por
esta via. Fica declarado, não fechado. Fechá-lo exige alterar o lifter e
re-liftar — ver 7.3.

### 7.2 Duas máscaras fora do repo do motor

`ps3_type15_block_stomp` e `type15_pin_free` vivem em
`gow2-recomp/host_gow2_factory.cpp`. O primeiro é chamado **dentro** do
`vm_write32` (`ppu_loader.cpp:1176-1178`) e bloqueia stores a word0; o segundo
bloqueia um `free` do allocator guest. Ficam declarados com `ficheiro:linha` no
relatório e **não instrumentados** nesta ronda (G8). Instrumentá-los é decisão
explícita do autor e vira tarefa própria, com commit próprio naquele repo.

### 7.3 Mina no caminho de qualquer re-lift *(não é deste entregável)*

O lifter actual emite `ppu_res_lwarx/ppu_res_stwcx/ppu_res_ldarx/ppu_res_stdcx`
como `static inline` sobre `vm_base`, **sem `vm_oob` e sem
`ps3_mem_oob_report`**. O lift construído hoje ainda usa os `ppu_lwarx`
hookados (5277 chamadas; `ppu_res_*` = 0), ou seja foi gerado por um lifter
anterior. **Um re-lift hoje apaga silenciosamente a instrumentação dos 4
atómicos que a Fase 0 adicionou** — e, segundo o levantamento, um lift fresco
não compila no macOS.

Este entregável foi desenhado para **não exigir re-lift**, precisamente para
não pisar a mina. Mas ela continua lá e bloqueia 7.1 e qualquer trabalho futuro
que dependa de re-lift. Registada aqui para não se perder.

### 7.4 Medições antigas não re-verificadas

Os números do allocator nos docs (`arr[126]=0x40686288`, 6 arenas,
`alloc_w=25718`) vêm de runs **Windows anteriores ao fix 2550C8** e nunca foram
re-medidos no macOS pós-fix. Não devem ser usados como verdade corrente.

### 7.5 Classificação suspeita de `func_00263554`

O comentário em `ppu_loader.cpp:1205-1206` divide as funções do allocator entre
*"ALLOC (`func_00263040/160/178`: walk+split do heap de boundary-tags)"* e
*"a região de FREE (`func_00263318/400/554/5A4`: free+coalesce)"*, ou seja
classifica `func_00263554` como **free/coalesce**. O levantamento reporta que o
lift mostra ser *alloc de pool de tamanho fixo*.

Como essa é exactamente a função do **primeiro erro do baseline da Fase 0**
(`_Z13func_00263554P11ppu_context+0x1A8`, `ea=0xFB788082`), a leitura desse erro
pode estar errada: um OOB vindo de um *alloc* conta uma história diferente de um
OOB vindo de um *free*.

**Não confirmei esta contradição por leitura directa do lift** — vem do
levantamento e está por verificar. A resolver quando a forense de header
começar; não neste entregável.

### 7.6 Superfície paralela sem instrumentação

`runtime/ppu/ppu_memory.h:60-172` tem uma **segunda cópia** `static inline` dos
acessores, que só verifica `ppu_guest_range_committed()` e não emite nada;
`vm_memcpy_from/to` e `vm_memset` (`:159-172`) não têm guard nenhum. Consumidor
medido: `libs/system/cellSaveData.c`. Risco baixo, declarado.

---

## 8. Proveniência deste design

Levantamento em 5 frentes paralelas com verificação por `ficheiro:linha`
(hooks de `vm`, allocator guest, sondas antigas, patching do lift, mapa de
memória). Âncoras re-verificadas à mão pelo controlador antes de escrever este
documento; duas afirmações do levantamento foram corrigidas no processo:

1. `vm_uncommitted_guard` **não** é totalmente silencioso — loga as primeiras 40.
2. `tblsize_guard` **não** recebe `caller_ra`.

Decisões do autor do projecto: escopo = desmascarar e medir a verdade;
aceite = medir uma máscara de cada vez, sendo o resultado o achado;
fronteira = desmascarar + mapa + relatório, sem `strict`/`diagnostic` nem
validador de headers; repositório = apenas o motor nesta ronda.
