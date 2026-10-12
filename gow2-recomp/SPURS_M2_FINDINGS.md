# SPURS bring-up — Marco 2: diagnóstico definitivo (e por que para aqui)

> Investigação + implementação do Marco 2 (2026-06-22). Conclusão honesta:
> o blocker real foi **completamente caracterizado com evidência empírica**, e
> destravá-lo NÃO é um patch pontual — exige o kernel SPURS executando de verdade
> (Marco 2.5), exatamente como o `FEASIBILITY.md` previu. Abaixo, a prova.

## O que foi implementado neste marco

1. **Instrumentação de trace permanente** (gated por `PS3_TRACE_SPURS`, default off):
   `runtime/ppu/ppu_hle.cpp`, `runtime/syscalls/sys_event.c`, `libs/spurs/cellSpurs.c`.
2. **Correção MVP defensiva** (gated por `PS3_SPURS_STATUS_PTR`, default off):
   `runtime/ppu/ppu_sysprx.cpp` — `spurs_unblock_crt_status()` escreve o byte de
   status que `func_002B3CB0` aguardava. **Mantida** (inócua, opt-in), mas provou-se
   **não ser o blocker** (ver abaixo).
3. **Ferramentas de diagnóstico reutilizáveis**: `sample_m2.sh`, `sample_multi.sh`,
   `sample_mem.sh`, `watch_flag.sh` (backtrace, snapshots, dump de memória BE-aware,
   watchpoint de hardware via gdb).

## A ABI `spurs=0x0` — resolvida

`cellSpursInitializeWithAttribute(r3=0x0, r4=attr)` nas 3 chamadas: o GoW2 passa
`spurs=NULL` **deliberadamente** (SPURS handle-based/global), não é bug. O shim não
deve depender de `r3`. (Confirmado por trace + leitura do caller `func_002B4140`.)

## Hipótese inicial REFUTADA (valor científico do teste)

O Marco 1 apontou `func_002B3CB0` (spin em `[0x6FF484]==0`) como blocker. A correção
MVP escreveu esse byte. Resultado empírico: **o byte já valia `3` (≠0)** quando o
Initialize roda — `func_002B3CB0` já sairia sozinho. **Não era o blocker ativo.** O
spin de ~90k `lwmutex` era de **outro** loop. (Testar em vez de assumir evitou
construir em cima de uma premissa errada.)

## O blocker REAL (caracterizado por gdb)

Backtrace da thread main em loop (estável em 5 snapshots):
```
func_0025C838 (CRT-init) → func_002B4AA0 → func_00303AF0 → func_0030600C
  → [vtable] func_0042C200 → func_0026300C → func_00379D00 → func_003783F8
  → sys_lwmutex_lock/unlock/create  (NID 0x1BC200F4 = sys_lwmutex_unlock)
```

**O loop** (`ppu_recomp_007.cpp:3242`, `func_0030600C`):
```c
gpr[28] = [TOC + 0x958] = [0x541AD0]      // ponteiro fixo, lido 1× antes do loop
loc_00306050:                              // TOPO
  gpr[30] = *gpr[28]                        // lê a flag (sempre o mesmo endereço)
  if (*gpr[28] != 0) goto SAIR              // <-- sai SÓ quando a flag vira != 0
  ... vtable dispatch -> func_0042C200 (testa/seta bit atômico num bitmap) ...
  goto loc_00306050                         // repete
```

**Inspeção de memória (gdb, BE-aware):**
| Endereço | Valor | Significado |
|---|---|---|
| `[0x541AD0]` | `0x0086E118` | ponteiro **válido** (gpr28) |
| `[0x541AD4]` | `0x005728EC` | ponteiro **válido** (gpr27, base do vtable dispatch) |
| `*0x0086E118 (+0)` | `0x00000000` | **a flag de saída — presa em 0** |
| `+4`, `+8` | `0x00000000` | estrutura **zerada** (não é lixo) |

**Watchpoint de hardware em `0x86E118`** durante ~8s de spin intenso:
**ZERO escritas.** Nenhuma thread (das 11 do processo) escreve a flag.

## Conclusão (por que para aqui, honestamente)

O boot do GoW2 tem uma **cadeia de waits órfãos**, todos do mesmo tipo: o PPU
monta seu modelo de threads/jobs e então **gira esperando um sinal que só o
subsistema SPU/SPURS produziria** — e esse subsistema não executa no nosso runtime:

- `func_0030600C` (blocker ativo): spin chamando um alocador de bitmap atômico,
  esperando `[0x86E118]` virar ≠0. **Nada escreve essa flag.**
- `_SPU_printf_server`: dorme em `sys_event_queue_receive(q=1)` (daemon; não bloqueia).
- `func_002B3CB0`: já satisfeito (`[0x6FF484]=3`), inativo.

Forçar a flag (band-aid) já se provou levar a crash downstream em sessões anteriores
(a estrutura que o jogo construiria fica incompleta). **Não há patch pontual.**

Isto **confirma com precisão cirúrgica** o veredito do `FEASIBILITY.md`: destravar o
boot exige o **kernel SPURS executando de verdade** (materializar SPU thread group +
rodar o kernel/scheduler que produz o trabalho e os sinais que a main aguarda) —
o **Marco 2.5**, que é o esforço de 6–12 pessoa-meses do roadmap, não um conserto
de uma sessão. O que esta sessão entregou é o **mapa exato** de onde e por que o boot
para, com ferramentas para retomar.

## Próximo passo real (Marco 2.5)

Implementar o kernel SPURS HLE que: (1) cria o SPU thread group em
`cellSpursInitialize`; (2) roda o scheduler que processa workloads; (3) produz os
sinais/estruturas que os loops PPU (`func_0030600C` etc.) aguardam. Requer mapear o
protocolo SPURS específico do GoW2 (a estrutura em `0x86E118`, o bitmap em
`[TOC-0x142C]`, a vtable em `0x5728EC`). É um projeto de semanas, não de uma sessão.
