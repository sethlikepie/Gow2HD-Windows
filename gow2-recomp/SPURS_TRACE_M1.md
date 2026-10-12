# SPURS bring-up — Marco 1: trace real do GoW2 (capturado)

> Trace dinâmico + análise estática do boot do God of War II HD, capturado em
> 2026-06-22. Destrava o entendimento do Marco 2. Evidência bruta:
> `recomp_mid/trace_m1_stderr.txt` (288.077 linhas) e `trace_m1_stdout.txt`.

## Como reproduzir

```bash
cd gow2_work/recomp_mid
PS3_TRACE_SPURS=1 \
PS3_VFS_ROOT=/c/Users/softlive/Documents/self-projects/gow2_work/extracted \
timeout -k 5 45 ./boot_hle.exe ../EBOOT.ELF \
  > trace_m1_stdout.txt 2> trace_m1_stderr.txt
# exit=124 (hang/timeout esperado)
```

Instrumentação (gated por `PS3_TRACE_SPURS`, sem afetar runs normais):
- `runtime/ppu/ppu_hle.cpp` — loga cada chamada HLE resolvida (nome + r3..r6).
- `runtime/syscalls/sys_event.c` — loga `sys_event_queue_receive` (entrada/saída).
- `libs/spurs/cellSpurs.c` — loga `AddWorkload` com EAs de policy module.

## Sequência exata do travamento (anotada, com EAs)

```
... CRT/TLS init, ~10 lwmutex_create (locks globais do runtime) ...
L354  sys_ppu_thread_once(guard=0x881154, init_fn=0x537710)   <- init única do printf server
L355  sys_lwmutex_create(0x880D20)  + lock                    <- mutex protetor
L357  sys_ppu_thread_create(entry_opd=0x537708)               <- cria "_SPU_printf_server"
        [SYS] name="_SPU_printf_server" arg=0x0 stack=0x4000 prio=-2
L359  cellSpurs Attribute setup (montado na stack @ 0xFEFFBA0):
        _cellSpursAttributeInitialize(attr=0xFEFFBA0, nSpus=0x2, flags=0x280001, ppuPrio=0x3)
        cellSpursAttributeSetSpuThreadGroupType(0x18)
L362  [THREAD] _SPU_printf_server inicia (host thread, entry 0x537708)
L363  sys_event_queue_receive q=1 timeout=0 count=0  ->  *** BLOCKING (infinite) ***
        ^ o printf server dorme para sempre nesta event queue
L364  cellSpursAttributeSetNamePrefix(...)
        cellSpursAttributeEnableSpuPrintfIfAvailable(...)
L366  cellSpursInitializeWithAttribute(spurs=0x0, attr=0xFEFFBA0)   <- STUB: só memset (e r3=0!)
... (repetido 3x: SPURS é inicializado 3 vezes com prefixos/contagens diferentes) ...
L364..L288077  func_002B3CB0 spin-wait:  ~96.000 iterações de
        { lwmutex churn ; lv2_syscall 0x8D (141 = sys_timer_usleep, 500us) }
        esperando obj->byte[+0x4] != 0   <- nunca escrito
```

## Os DOIS waits órfãos (o boot tem duas threads paradas)

| Thread | Onde para | Espera por | Quem deveria liberar |
|---|---|---|---|
| **`_SPU_printf_server`** (entry 0x537708) | `sys_event_queue_receive(q=1, ∞)` | evento de printf vindo de um SPU | um SPU rodando + `cellSpursEnableSpuPrintf` materializado |
| **main / CRT-init** (`func_002B3CB0`) | spin-wait `while(obj->byte[+4]==0) usleep(500)` | byte de status do subsistema SPURS | `cellSpursInitializeWithAttribute` real escrevendo o status |

Wait loop estático: `func_002B3CB0` em `recomp_mid/ppu_recomp_006.cpp:19892`.
Lê `obj = *(u32*)(r2 - 0x1438)`; condição `obj->u8[+0x4] == 0` → continua; `!=0` → sai.
Cadeia: `func_0025C838` (raiz CRT-init) → `func_002B4F04` → `func_002B41C0` →
`func_002B3EA4` → **`func_002B3CB0`** → `func_002B2E50`.

## Contagem de chamadas (PS3_TRACE_SPURS=1)

| Chamada | Nº | Observação |
|---|---|---|
| `sys_lwmutex_lock` / `unlock` / `create` | ~96k cada | o spin-loop (usleep 500us → ~90k iter/45s) |
| `cellSpursInitializeWithAttribute` | 3 | **r3 (spurs) = 0x0 nas três** (anomalia, ver abaixo) |
| `_cellSpursAttributeInitialize` / `SetSpuThreadGroupType` / `SetNamePrefix` / `EnableSpuPrintfIfAvailable` | 3 cada | attr montado em 0xFEFFBA0 |
| `sys_ppu_thread_create` | 1 | `_SPU_printf_server` |
| `sys_event_queue_receive ... BLOCKING` | 1 | q=1, hang final |
| `cellSpursAddWorkload` | **0** | **nunca chega a adicionar workload** |
| `[SPU] group_create/start/thread_init` | **0** | **nenhum SPU thread group jamais criado** |

## Diagnóstico de raiz (preciso)

O blocker NÃO é "AddWorkload não despacha o job" (a hipótese inicial do M2) — o
jogo **nem chega a adicionar workload**. O ponto de origem é mais cedo:

**`cellSpursInitializeWithAttribute` é um stub** (`ppu_sysprx.cpp:215`) que no
máximo faz `memset(spurs, 0, 4096)`. No PS3 real, esta função:
1. cria o **SPU thread group** do SPURS e roda o **kernel SPURS** nos SPUs;
2. (com `EnableSpuPrintf`) liga o canal que alimenta o `_SPU_printf_server`;
3. escreve o **objeto de status** que o CRT-init (`func_002B3CB0`) faz spin-wait.

Como nada disso acontece, os dois waits acima ficam órfãos para sempre. Confirma
a tese do `FEASIBILITY.md`: falta o subsistema SPURS executando — mas agora com o
**ponto de entrada exato** (Initialize, não AddWorkload).

## Anomalia a investigar primeiro no Marco 2

`cellSpursInitializeWithAttribute(r3=0x0, r4=0xFEFFBA0)` — o ponteiro `spurs`
chega **0x0** nas 3 chamadas, enquanto `attr` (r4) é um endereço de stack válido.
Como o stub faz `if (spurs) memset(...)`, com spurs=0 nem o memset roda. Hipóteses:
1. o GoW2 realmente passa NULL (variante de ABI SPURS2 / handle via global) — a
   confirmar lendo o caller no código liftado;
2. bug de passagem de argumento no trampoline de import só para este NID.
Resolver isto é o primeiro fio do Marco 2 (barato e potencialmente esclarecedor),
antes de implementar o kernel SPURS HLE.

## Implicação para o Marco 2 (revisado)

O alvo do Marco 2 passa a ser: **`cellSpursInitialize[WithAttribute]` materializa o
SPURS** — criar o SPU thread group + thread de kernel SPURS HLE (parkeada no inbox,
mecanismo já testado no M0), registrar via `spurs_set_kernel_thread`, e escrever os
objetos de status que o CRT-init aguarda. O feed do `_SPU_printf_server` (event
queue q=1) entra junto, pois é ligado pela mesma flag `EnableSpuPrintf`.
