# GoW2 boot — estado sobre o runtime upstream (2026-06-22)

> **⚠️ ATUALIZAÇÃO 2026-06-25 — `func_0030600C` NÃO é mais o wall.** Verificado
> empiricamente no build atual (boot real, sem force-hacks, `PS3_TRACE_SPURS=all`):
> `sys_lwmutex_lock` = **102** (não os ~96k do spin que este doc descreve abaixo) →
> `func_0030600C` **passa**. O boot avança por `cellSpursInitializeWithAttribute`
> (3x), `cellGameBootCheck`, `cellGameContentPermit` e agora **gira em
> `cellSysutilCheckCallback` (~140k chamadas)** — o novo wall é o **modelo async do
> cellSysutil** (callbacks não entregues), não o allocator/SPURS. O diagnóstico
> abaixo (allocator `0x881970` fill `0x58`) foi resolvido por fixes de fidelidade do
> PPU lifter posteriores (provável `rlwinm` zero-ext). Mantido para histórico.

> Onde o boot do God of War II HD chega ao rodar sobre o runtime upstream
> (PRX + SPU workload dispatch + sys_fs fallback), e o diagnóstico exaustivo do
> wall atual. Branch `spurs-bringup`. Build: `gow2_work/build_boot_hle.sh`.

## Até onde o boot avança

```
ppu_load_elf  →  PT_PROC_PRX_PARAM captured  →  151 imports resolved (13 mods)
  →  TLS init  →  entry 0x10230  →  sys_initialize_tls
  →  sys_ppu_thread_create "_SPU_printf_server" (OPD 0x537708, resolvido ✓)
  →  cellSysmodule LoadModule: CELL_SYSMODULE_SYSUTIL_NP, SYNC2
  →  [hle] unresolved NID 0x63FF6FF9 (cellSysmodule, benigno)
  →  *** WALL: func_0030600C spin-loop ***
```

Avança **mais** que o baseline pré-upstream (graças ao `sys_fs` fallback que serve
os dados do jogo).

## O wall: `func_0030600C` (inicializador de pool/allocator)

`ppu_recomp_006.cpp` — chamado por `func_0025C838` (CRT-init) → `func_002B4AA0`
→ `func_00303AF0` → **`func_0030600C`**. Spin-loop:

```
gpr[28] = [TOC-0x... = 0x541AD0] = 0x0086E118   (objeto de status; flag em +0x0)
loop:  if (*0x0086E118 != 0) sair;
       chama método virtual func_0042C200 → func_0026300C → func_00379D00
       → func_003783F8 → sys_lwmutex_create/lock/unlock  (~96k vezes)
```

`func_00379D00` faz bounds-check de um **allocator**:
`[TOC+0x18C0]=0x542A38 → alloc-ctrl @ 0x00881970`, limite em `[ctrl+0x20]`.

### Diagnóstico (14 ângulos de investigação — gdb attach + memory dumps)

| Verificação | Resultado |
|---|---|
| flag `*0x86E118` | `0` e **nunca escrita** (hardware watchpoint, Marco 2) |
| forçar flag = 0xFFFFFFFF | não destrava (loop não depende só da flag) |
| `ppu_vm_size` | `0xE0000000` correto (OOB guard ok) |
| `sys_ppu_thread_once` (0xA3E3BE68) | resolve + chama init_fn ✓ |
| `g_ctx` ctx-handlers | 55/256, sem overflow |
| SPU thread groups antes do wall | **0** (não é SPU job) |
| **alloc-ctrl @ 0x881970 [+0x00]** | **`0x58585858` ('XXXX') = fill não-sobrescrito** |

**Causa raiz:** o controle do allocator (`0x881970`) está com o padrão de fill
`0x58` ('X') no campo `+0x00` — uma inicialização que o jogo deveria completar e
não completa no nosso ambiente. Com o controle inválido, o ponteiro de alocação
(`gpr[26]`) escapa dos bounds para lixo (`0xFD86Cxxx`, decrescendo 0x30/iteração)
e o loop nunca atinge a condição de parada que escreveria a flag `0x86E118`.

**Não é um bug pontual** (provado de 14 ângulos): é o subsistema de inicialização
de memória/pool do GoW2, dependente de execução fiel que não modelamos. Band-aids
(forçar flag / sintetizar objeto) não destravam ou crasham downstream.

## Ferramentas de diagnóstico (em `gow2_work/`)

`attach_oob.sh` (backtrace do loop), `attach_mem.sh` / `dump_alloc.sh` (dump de
estruturas BE-aware via gdb), `attach_threads.sh` (estado de todas as threads),
`probe_flag.sh` (sonda de forçar flag), `oob_bp.sh` (breakpoint condicional no OOB).
Trace HLE gated por `PS3_TRACE_SPURS`; diagnóstico de NID em `ppu_hle.cpp`.

## Cadeia de init do allocator (mapeada — 15º ângulo)

`func_0025C838` (CRT) → `func_002B4AA0` → `func_00303AF0`:
- copia 4 campos de uma config-struct `gpr[31]` (+0x5C/+0x70/+0x18/+0x2C) p/ a stack
- chama **`func_00315EAC`** (init do allocator), depois `func_0030600C(0,0)` (o loop)

`func_00315EAC` decide o caminho de init pelo retorno de um import (`func_004B97B8`,
slot `[0x51940C]`):
- retorno válido (≠0, ≠`0x8001xxxx`) → `func_0031415C` (init direta)
- retorno 0/erro → `func_00315F18` → loop com `func_004B9038` → também `func_0031415C`

Ou seja `func_0031415C` (a init real do pool) roda por ambos os caminhos, mas o
controle resultante em `0x881970` permanece com o fill `0x58` — a init não preenche
os campos que `func_0030600C`/`func_00379D00` depois leem como bounds. A causa está
em `func_0031415C` (+ as dezenas de funções que ela encadeia) operando sobre
semântica de memória/sync que o runtime não reproduz fielmente. Rastrear além
deste ponto revela só mais funções encadeadas (não um ponto-de-fix) — é o
subsistema de runtime/allocator do jogo, não um bug isolado.

## 16º ângulo: o ponteiro lixo é COMPUTADO, não lido

Tracer `PS3_TRACE_GARBAGE` (vm_read32 loga leituras na faixa `0xFD86xxxx`): **zero
ocorrências**. O ponteiro `0xFD86Cxxx` não vem de memória — é **computado** pelo
próprio loop: `gpr[26]` decresce 0x30/nó e deveria cruzar o limite `[ctrl+0x20]=
0x881998` e parar, mas o count/base deriva do controle não-inicializado
(`[0x881970+0]=0x58585858` ≈ 1,5 bi), então o iterador faz **underflow**
(`0x0086CC08 - count·0x30 → 0xFD86Cxxx`) e nunca atinge a condição de parada.

Não há um valor de memória único a corrigir: o loop computa o ponteiro errado a
partir de um estado de init incompleto. `func_0031415C` (a init do pool) **executa**
mas não preenche os campos — depende de semântica de memória/sync/runtime que o
recompilador não reproduz fielmente. Confirmado de 16 ângulos: é o subsistema de
inicialização de memória do jogo, não um defeito isolado. Band-aids (forçar a flag,
escrever o controle) não destravam — o loop interno não retorna para reavaliar.

## Próximos passos reais (não são "bugs", são subsistema)

1. **Allocator/pool init**: rastrear quem deveria inicializar `0x881970` (a init de
   heap/pool da CRT do GoW2) e por que deixa o fill `0x58`. Provável dependência de
   um caminho de código que assume semântica de memória/SPU que não modelamos.
2. **SPU**: 4 imagens já liftadas/compiladas (`gow2_work/spu_lifted/`); registrar via
   `spu_workload_register` quando o boot chegar a `cellSpursCreateTask`.
3. **RSX**: tradutor de shaders NV40 (0%) — necessário para qualquer frame.

Cada um é esforço de semanas-meses (ver [`../FEASIBILITY.md`](../FEASIBILITY.md)).
