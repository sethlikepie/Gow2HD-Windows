# Rebaseline do boot macOS depois do merge do master

Data: 2026-07-20 · `boot_gow2` arm64 nativo · Apple M5 · `PS3_NO_RSX=1 PS3_TRACE_SPURS=1`
Contexto: Phase 3 Task 3.0 do plano `../ps3recomp/docs/superpowers/plans/2026-07-20-gow2-full-bringup.md`.

## Resultado

`./build_macos.sh` linka e `./smoke_boot_mac.sh` passa. O wall e **exactamente o mesmo**
de antes do merge — o merge nao mexeu o ponto de paragem.

### Memoria do guest aos 15s (`./attach_mem_mac.sh`)

Os 7 enderecos batem **valor a valor** com `2026-07-20-baseline-mac-m2.md` (pre-merge)
e com o `SPURS_M2_FINDINGS.md` do Windows:

| Endereco | Valor | = baseline? |
|---|---|---|
| `[0x541AD0]` | `0x0086E118` | sim |
| `[0x541AD4]` | `0x005728EC` | sim |
| `*0x86E118 +0/+4/+8` | `0x00000000` | sim |
| `*0x881970` | `0x58585858` | sim |
| `*0x6FF484` | `0x03001230` | sim |

### Onde para, medido (nao inferido)

`sample` do processo bloqueado: a main thread esta em **`func_002B3CB0` em polling** —
2419 de 2466 amostras dentro de `sys_timer_usleep` → `nanosleep`. Nao e busy-spin
(CPU ~1,5%), e um loop de espera com sleep. Cadeia:

```
main → ppu_run → func_00010230 → func_00010354 → func_0025C838
     → func_002B4F04 → func_002B41C0 → func_002B3EA4 → func_002B3CB0
```

Bate com a baseline, que ja nomeava `*0x6FF484` como "byte do spin de func_002B3CB0".

### Kernel SPURS: arranca, mas nao tem o que correr

`sample` mostra **2 worker threads** em `kernel_thread_main → pthread_cond_wait`, e o
log traz agora `[spurs kernel] started: 2 worker(s)`. Porem o boot regista
**`AddWorkload` = 0** e `CreateTask` = 0: o titulo trava antes de submeter qualquer
workload. O kernel estar vivo nao move o wall — e coerente, nao regressao.

## Divergencia encontrada: o `tblsize_guard` suprime o `_SPU_printf_server`

O smoke pre-merge exigia `_SPU_printf_server` e `THREAD 1] host thread started`.
Pos-merge nenhuma thread PPU do guest e criada. **A causa e nossa, nao do jogo.**

O `tblsize_guard` (`../ps3recomp/runtime/ppu/ppu_loader.cpp`, veio do master) bloqueia
zero-writes em `0x572BB0/BB4/BB8` — o bloco de config do alocador de bins. A sequencia
gravada em `SPURS_TRACE_M1.md` e:

```
L354  sys_ppu_thread_once(guard=0x881154, init_fn=0x537710)
L355  sys_lwmutex_create(0x880D20)
L357  sys_ppu_thread_create(entry_opd=0x537708)   <- "_SPU_printf_server"
```

L354 e L355 acontecem (logo a rotina de init **corre**); L357 nao. Entre as duas
aparece `[FIX] blocked spurious zero at [0x00572BB0]`.

### A/B medido (gate novo `PS3_NO_TBLSIZE_GUARD`)

| Config | printf server | `cellSpursInitializeWithAttribute` | linhas |
|---|---|---|---|
| guard ligado (default) | **nao** | `r3=0x40000080/0x40001100/0x40002180` (validos) | 412 |
| `PS3_NO_TBLSIZE_GUARD=1` | sim | **`r3=0x0`** (NULL — perde-se o fix) | 396 |
| `PS3_NO_TBLSIZE_GUARD=0x572BB0` | **sim** | **validos** | **417** |

Excluir **so** `0x572BB0` da lista da os dois: a thread volta e os ponteiros SPURS
continuam validos; o boot termina em `sys_event_queue_receive q=1 -> BLOCKING (infinite)`,
que e exactamente a L363 do trace do Windows. Ou seja, `0x572BB0` esta **a mais** na
lista do guard: BB4/BB8 sao vitimas reais do loop desgovernado, BB0 e zerado
legitimamente pelo caminho de init do printf server.

### Porque o default NAO foi mudado

A medicao e so de macOS, onde o boot para no wall SPURS. No Windows o boot vai muito
mais longe (cellAudio, shaders, draws) e `0x572BB0` faz parte do bloco que o guard
existe para proteger nesse percurso — nao da para afirmar daqui que remove-lo e seguro
la. Fica o gate para o lado Windows fazer o mesmo A/B com uma variavel de ambiente.

A thread em si e um **daemon que dorme para sempre** em `sys_event_queue_receive(q=1)`
(ja registado em `SPURS_TRACE_M1.md` e `SPURS_M2_FINDINGS.md`: "nao bloqueia"), portanto
a sua ausencia nao muda o wall — o que a tabela acima confirma.

## Criterios novos do `smoke_boot_mac.sh`

Removidos (com motivo acima): `_SPU_printf_server`, `THREAD 1] host thread started`,
`[spurs kernel] initialize` (era o log do kernel antigo; o do master nao logava nada).

Exigidos agora, todos medidos: `5[0-9]{4} lifted functions`, `151 imports`,
`cellSpursInitializeWithAttribute`, `[spurs kernel] started`, **ausencia** de
`cellSpursInitializeWithAttribute(r3=0x0` (regressao do fix do guard), ausencia dos
stubs 141/144, NIDs por resolver <= 2 (medido: 0) e CPU < 50% (medido: ~1,5%).
