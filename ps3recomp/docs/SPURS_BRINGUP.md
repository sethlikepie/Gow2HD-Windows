# SPURS bring-up — plano de execução para destravar o boot

> **Objetivo concreto:** fazer o GoW2 (e jogos PS3 em geral) avançar além do
> `sys_event_queue_receive` que hoje trava o boot, executando o trabalho SPURS de
> verdade em vez de fingir que ele terminou.
>
> Este é o **caminho crítico nº 1** do [`FEASIBILITY.md`](../FEASIBILITY.md)
> (Marco 2). É um esforço de **6–12 pessoa-meses**, então este documento o
> quebra em marcos **ordenados e individualmente testáveis**. Cada marco tem um
> critério de aceite verificável — nada de "pronto" sem teste verde.

---

## 1. Contexto: o que é SPURS e por que ele trava o boot

**SPURS** (SPU Runtime System) é o middleware da Sony que multiplexa N "workloads"
(jobs de física, áudio, animação, geração de comando RSX…) sobre os 6 SPUs. Em um
PS3 real:

```
PPU (jogo)                          SPU (6x)
  │                                   │
  ├─ cellSpursInitialize ───────────▶ cria SPU thread group rodando o KERNEL SPURS
  ├─ cellSpursAddWorkload ──────────▶ registra policy module + dados na LS
  ├─ (cria event queue + conecta)
  ├─ sys_event_queue_receive ──┐      kernel agenda workload → roda job nos SPUs
  │         (bloqueia)         │      job termina → MFC PUT resultado → sinaliza
  │                            └────▶ evento empurrado na queue
  ▼ acorda, lê resultado
```

**O travamento:** nosso runtime tem todo o lado PPU (event queue, group create/
start/join, mailboxes) e um **interpretador SPU funcional**, mas **o kernel SPURS
nunca roda**. `cellSpurs.c` faz tracking de workloads sem executar o scheduler.
Resultado: o job nunca roda, nada é sinalizado, e o `receive` espera para sempre.

> Prova: investigação anterior com watchpoint de hardware confirmou que o objeto
> de conclusão esperado pelo jogo **nunca é escrito**. Não há atalho de patch.

---

## 2. Duas abordagens (e a escolhida)

| Abordagem | O que é | Prós | Contras |
|---|---|---|---|
| **LLE** (Low-Level) | Rodar o `.self` real do kernel SPURS da firmware no interpretador SPU | Fiel; é o que o RPCS3 também suporta | Precisa da firmware; o kernel é complexo e sensível a timing |
| **HLE** (High-Level) ✅ | Reimplementar a lógica do kernel SPURS em C no host, dirigindo o interpretador SPU só para os **policy modules / jobs** do jogo | Tratável; não precisa da firmware; é a rota pragmática para recomp | Precisa modelar o protocolo SPURS corretamente |

**Decisão: HLE-first.** É a rota que o RPCS3 usa por padrão e a única tratável sem
depender de binários de firmware. O scaffolding de handshake já existe no código
(`spurs_set_kernel_thread`, `spurs_wake_kernel`, `s_kernel_threads[]`,
`CellSpursWorkloadRecord`), o que confirma que esta foi a direção planejada.

---

## 3. Inventário do que JÁ existe (não recomeçar do zero)

| Peça | Local | Estado |
|---|---|---|
| Interpretador SPU (ISA, branches, float) | `runtime/spu/spu_interp.c` | ✅ testado |
| DMA / MFC (GET/PUT, tags) | `runtime/spu/spu_dma.h`, `spu_channels.c` | ✅ testado |
| Mailboxes / canais (inbox/outbox/signal) | `runtime/spu/spu_context.h`, `spu_channels.c` | ✅ testado |
| **Park/resume cross-thread** (rdch bloqueia, host alimenta, resume) | `spu_interp.c` (`wait_channel`) | ✅ testado |
| ELF32 SPU → LS | `runtime/spu/spu_elf.c` | ✅ testado |
| SPU group create/start/join (host threads) | `runtime/syscalls/lv2_register.c:486-653` | ✅ funcional |
| group_join empurra evento na queue | `lv2_register.c:637-643` | ✅ funcional |
| Registro de fallback PPU por entry-point | `runtime/syscalls/spu_fallback.c` | ✅ funcional |
| event queue / receive / port (lv2 128-138) | `runtime/syscalls/sys_event.c` | ✅ funcional |
| **Kernel SPURS HLE (motor async)** | `libs/spurs/spurs_kernel.c` | ✅ **implementado + testado** (2026-06-25) |
| `cellSpursInitialize` cria kernel thread | `cellSpurs.c` | ✅ chama `spurs_kernel_start` |
| Execução do kernel/scheduler SPURS | `spurs_kernel.c` | ✅ host-thread async (unit test verde) |
| Dispatch de policy module / job | `cellSpursAddWorkload`→kernel→`spu_workload_dispatch` | ✅ **e2e verde** (job SPU liftado roda) |

**A lacuna real é estreita e localizada:** ligar o scaffolding de handshake a uma
execução de kernel SPURS HLE que dirige o interpretador (que já funciona).

---

## 4. Marcos ordenados

### Marco 0 — Fundação do interpretador SPU ✅ **VERDE** (2026-06-20)

Pré-requisito de tudo: o interpretador roda ELF SPU, faz DMA e suporta o
park/resume cross-thread que o SPURS exige.

**Critério de aceite:** os testes de `runtime/spu/tests/` passam. **Verificado:**

```
test_interp            PASS  (ISA, tier2 float, bridge)
test_dma               PASS  (MFC GET/PUT, lqd→wrch)
test_spu_elf           PASS  (7 checks, ELF32→LS)
test_spu_integration   PASS  (ELF loaded, parked on rdch, cross-thread wake, resumed)
test_interp_park       PASS  (rdch ch29 parked, fed, resumed)
test_interp_stopn      PASS  (stop-N, atomic mbox)
```

> O mais importante: **park/resume cross-thread funciona** — é o mecanismo pelo
> qual o kernel SPURS bloqueia esperando trabalho e é acordado pelo PPU.

---

### Marco 1 — Capturar o protocolo SPURS real do GoW2 ✅ **VERDE** (2026-06-22)

Trace dinâmico (instrumentação gated por `PS3_TRACE_SPURS`) + análise estática do
wait loop. **Trace anotado completo em [`../../gow2_work/SPURS_TRACE_M1.md`](../../gow2_work/SPURS_TRACE_M1.md).**

**Achados (corrigem a hipótese inicial do M2):**
- O GoW2 **inicializa o SPURS** (`cellSpursInitializeWithAttribute` 3×, com
  `EnableSpuPrintfIfAvailable`) mas **nunca chega a `cellSpursAddWorkload`** —
  trava antes. `[SPU]` group create/start = **0** (nenhum thread group nasce).
- **Dois waits órfãos param o boot:**
  1. a thread `_SPU_printf_server` (entry OPD `0x537708`) dorme para sempre em
     `sys_event_queue_receive(q=1, timeout=∞)`;
  2. a CRT-init faz spin-wait em `func_002B3CB0` (`ppu_recomp_006.cpp:19892`):
     `while (obj->u8[+4]==0) sys_timer_usleep(500)` (~96k iterações em 45 s).
- **Raiz:** `cellSpursInitializeWithAttribute` é um stub (`ppu_sysprx.cpp:215`,
  no máximo `memset`); não cria o SPU thread group, não roda o kernel SPURS, não
  escreve o objeto de status que o spin-wait aguarda. Ponto de origem **mais cedo**
  do que o AddWorkload presumido.
- **Anomalia:** `spurs` (r3) chega `0x0` nas 3 chamadas — investigar o caller no
  código liftado antes de implementar o kernel (pode ser ABI SPURS2 ou bug de arg).

**Critério de aceite:** ✅ trace anotado do `cellSpursInitialize...` até o
`sys_event_queue_receive`, com EAs concretos (entry, queue id, attr EA, wait obj).

---

### Marco 2 — Diagnóstico do blocker ✅ **COMPLETO** / destravar = Marco 2.5 (2026-06-22)

> **Achados completos e provados em [`../../gow2_work/SPURS_M2_FINDINGS.md`](../../gow2_work/SPURS_M2_FINDINGS.md).**

Feito neste marco:
- ✅ **ABI `spurs=0x0` resolvida**: deliberado (SPURS handle-based), não bug.
- ✅ **Correção MVP** (gated `PS3_SPURS_STATUS_PTR`, `ppu_sysprx.cpp`) — escreve o
  status byte de `func_002B3CB0`. **Mantida, mas provou-se inócua**: o byte já valia
  `3≠0`; `func_002B3CB0` **não era o blocker ativo** (refutado empiricamente).
- ✅ **Blocker real caracterizado por gdb**: a main gira em `func_0030600C`
  (`ppu_recomp_007.cpp:3242`) — spin enquanto `*[0x86E118] == 0`, chamando um
  alocador de bitmap atômico (`func_0042C200`/`func_00263040`). A estrutura está
  **zerada** (ponteiros `[0x541AD0]`/`[0x541AD4]` válidos), e um **watchpoint de
  hardware provou que NADA escreve `[0x86E118]`** em ~8 s de spin → wait órfão.

**Conclusão:** é uma **cadeia de waits órfãos** — o PPU monta seu modelo de jobs e
gira esperando sinais que só o **subsistema SPU/SPURS produziria**. **Não há patch
pontual** (forçar a flag → crash downstream, já visto). Isto confirma com precisão o
`FEASIBILITY.md`: destravar exige o kernel SPURS executando de verdade ⇒ Marco 2.5.

---

### Marco 2.5 — Kernel SPURS HLE executando

> O **motor** (genérico, título-agnóstico) está ✅ **implementado + testado**
> (2026-06-25). O que resta é título-específico (Marco 5) — a parte que de fato
> consome os 6–12 pessoa-meses.

**Feito (motor genérico, com testes verdes):**
- `libs/spurs/spurs_kernel.c` (+`.h`): kernel SPURS HLE = host-thread async com
  fila de workloads, wake/park, `start`/`submit`/`drain`/`stop`. Espelha as
  primitivas de sync do `cellSpurs.c` (Win32 `CONDITION_VARIABLE` / pthread).
- Ligado ao `cellSpurs.c`: `cellSpursInitialize[WithAttribute]`→`spurs_kernel_start`;
  `cellSpursAddWorkload`→`spurs_kernel_submit` (async, fora da thread PPU);
  `cellSpursFinalize`→`drain`+`stop`. Imagem SPU não-registrada = MISS inócuo.
- Testes: `libs/spurs/tests/test_spurs_kernel_main.c` (unit: agenda/async/completa/
  join — 50 workloads) e `runtime/spu/tests/test_spurs_e2e_main.c` (e2e:
  `cellSpursAddWorkload` → kernel → `spu_workload_dispatch` → **job SPU liftado real**
  → DMA do resultado na memória; critério de aceite do Marco 3 atingido).

**O que ainda falta para o boot REAL do GoW2 (Marco 5, título-específico):**
- Mapear o **protocolo SPURS do GoW2**: a estrutura em `0x86E118`, o bitmap em
  `[TOC-0x142C]`, a vtable de dispatch em `0x5728EC` — e fazer o kernel produzir os
  sinais/estruturas que `func_0030600C` (spin ativo) e os próximos da cadeia aguardam.
  **O motor genérico NÃO destrava isso sozinho** — `func_0030600C` é um *busy-spin*
  em `[0x86E118]`, não um `sys_event_queue_receive`; só o write correto desse estado
  (produzido pela lógica SPURS específica do título) o solta.
- **Liftar o conjunto de SPU do GoW2** e registrá-lo (`spu_workload_register`) para
  que os policy modules do jogo tenham entry liftada (hoje: MISS → inerte).
- Ligar o feed da event queue `q=1` (`EnableSpuPrintf`) ao `_SPU_printf_server`.

**Critério de aceite (Marco 5):** com o protocolo do GoW2 modelado, o boot **sai de
`func_0030600C`** e o próximo wait fica observável (a cadeia até gameplay é iterativa).

---

### Marco 3 — Dispatch de workload/job

- Em `cellSpursAddWorkload`, popular o `CellSpursWorkloadRecord` (já existe) e
  acordar o kernel (`spurs_wake_kernel`).
- O kernel HLE, ao acordar, materializa o policy module/job na LS (via
  `spu_image_materialize`/`spu_load_elf32_into_ls`, já existem) e o executa no
  interpretador (Marco 0).
- Job roda → faz `MFC PUT` do resultado na memória principal (DMA já testado).

**Critério de aceite:** um workload de teste sintético (job que escreve um valor
conhecido via DMA) é submetido, executado e o resultado aparece na memória do host.

---

### Marco 4 — Sinalização de conclusão → acordar o `receive`

- Ao terminar o job, o kernel HLE empurra o evento na event queue conectada
  (reutilizar `sys_event_queue_push_by_id`, já usado pelo `group_join`).
- Garantir a semântica de evento que o GoW2 espera (data1/data2/data3 corretos —
  vem do trace do Marco 1).

**Critério de aceite:** PPU bloqueado em `sys_event_queue_receive` acorda com o
payload esperado (teste de ponta a ponta sintético: PPU recebe ⇄ SPU sinaliza).

---

### Marco 5 — Bring-up com o GoW2 real

**Análise decisiva (2026-06-25): o fork dos docs resolvido + LLE viável.**

1. **`func_0030600C` lido no código liftado** (`recomp_mid/ppu_recomp_054.cpp:10783`):
   o spin sai **puramente** quando `[0x86E118] != 0` (`r28=[TOC+0x958]`; lê
   `[r28+0]`, e `loc_003061B8` lê `[r28+4]` — estrutura de **status multi-campo**).
   O allocator (`0x881970`, fill `0x58` do GOW2_BOOT_STATE) é o **trabalho por
   iteração** (vtable `func_0042C200`, que retorna 0 = "sem trabalho"), **não** a
   condição de saída. → SPURS_M2 estava certo; GOW2_BOOT_STATE descrevia o interior
   do allocator. Não são walls diferentes: é um só loop.
2. **Quem escreve `[0x86E118]`:** nada no PPU (watchpoint M2 = zero writes) → o
   **kernel SPURS do lado SPU**, via DMA, quando sobe.
3. **LLE é VIÁVEL — a imagem do kernel SPURS está presente:**
   `gow2_work/spu_images/spu_0000_at_004C8780.elf` (54 KB) contém `libspurs` (é o
   kernel); `spu_0001` = EDGE (policy module); `spu_0002/3` = jobs do jogo. Todos já
   extraídos e liftados (`spu_lifted/spu0..3`). Ou seja: a peça que produziria
   `[0x86E118]` **existe** — não é RE do zero.

**Caminho concreto (não mais "meses vagos"):**
- (b) **Rodar `spu_0000` (o kernel) no motor SPU — FASE LIFTER ✅ (2026-06-25).**
  Probe LLE rodou o kernel real e revelou/corrigiu 2 bugs gerais do SPU lifter:
  **(1)** retorno condicional `biz/binz/bihz/bihnz $cond,$r0` emitia dispatch em vez
  de `return` (`tools/spu_lifter.py`); **(2)** funções alcançadas só por tabela de
  ponteiros no segmento de dados não eram descobertas (`tools/find_spu_functions.py`
  agora escaneia code-pointers). Resultado: kernel 421→**448 funcs (98%)**, e **roda
  toda a self-init** (antes travava no 1º branch). Suite SPU 10/10 (sem regressão).
  Agora para em alvo computado de **estado não-inicializado** → precisa da instância.
- (a) `cellSpursInitialize` construir a **instância libspurs real** na memória guest
  (o struct/ABI interno que o kernel lê — `func_0030600C` = `wait_for_spurs_ready(
  out0,out1)`, espera `[0x86E118+0]!=0` que o kernel produziria); ligar ao motor
  (spurs_kernel). **FASE ATUAL — é a ABI do libspurs (RE / referência RPCS3).**
- (c) Atomics MFC (`getllar/putllc`): hoje tratados como transfer single-thread +
  `MFC_RdAtomicStat=sucesso` (funcionalmente ok p/ single-thread; refinar se a fila
  de trabalho do kernel exigir reserva real).

**Critério de aceite:** o boot do GoW2 **avança além** de `func_0030600C` porque o
kernel SPURS real escreveu `[0x86E118]` (não significa render — o próximo muro é o
RSX, Marco 3 do FEASIBILITY).

---

## 5. Riscos e lacunas conhecidas a resolver pelo caminho

- **Atomics SPU** (`getllar/putllc/putqlluc`): o interpretador ainda não os modela;
  o kernel SPURS real os usa para a fila de trabalho. Pode ser preciso modelá-los
  (ou contorná-los na versão HLE do kernel).
- **Sincronização live PPU↔SPU**: hoje marcada como "fora de escopo" em pontos do
  código; o write de inbox precisa ser seguro contra o SPU em execução.
- **Performance**: se a interpretação não atingir tempo real, será preciso ligar o
  `spu_lifter.py` (recompilação SPU) ao runtime — hoje ele gera código mas o
  runtime só interpreta.
- **DMA list (`PUTL/GETL`)**: implementado mas não verificado em jogo real.

---

## 6. Ordem de ataque recomendada

```
M0 ✅ → M1 (trace) → M2 (kernel vivo) → M3 (job roda) → M4 (sinaliza) → M5 (GoW2)
```

Marcos 1–4 são desenvolvíveis e testáveis **isoladamente, sem depender do GoW2**
(com workloads sintéticos), o que mantém o ciclo de feedback curto. Só o Marco 5
acopla ao jogo real. Essa é a diferença entre "meses de flailing" e "meses de
progresso medível".
