# Onde a main pára pós-`thr_auto_load` — não é deadlock, é um loop vivo sem saída, e o `pre_v3` pára no MESMO sítio

**Data:** 2026-07-31 · Método: `lldb attach --pid` (attach/`thread backtrace`/detach, sem
tocar no processo, 5 amostras no total: 3 em `boot_gow2.jtfix`, 2 em `boot_gow2.pre_v3`)
sobre corridas ao vivo que já passaram `thr_auto_load() end`. Kill sempre por PID
(`kill -TERM` → espera → `kill -9`), nunca `pkill -f boot_gow2`. Env: `env_gow2.sh` +
`PS3_NO_RSX=1 PS3_AUTO_LOAD_RUN=1 PS3_PERF_FSM=1 PS3_VDEC_FORCE_SEQDONE_MS=1500
PS3_STUCK_ICALL_LIMIT=2000000` (recipe do `gate6-0x40678c90`). **4 corridas gastas em
`boot_gow2.jtfix`** (2/4 presas na flakiness já conhecida de intro, `st620` nunca sai de
1) e **4 em `boot_gow2.pre_v3`** (3/4 presas na mesma flakiness de intro) — confirma outra
vez a variabilidade alta documentada no projeto; mínimo de 3 corridas por binário
respeitado.

## A resposta directa à pergunta

**A main thread NÃO está parada.** Está viva, a correr um loop real do próprio jogo,
sem nunca sair dele. Não há lock, não há `usleep`, não há poll de uma flag inerte —
há um ciclo de despacho de "tick" que continua a chamar código real (`cellPadGetInfo2`,
sub-rotinas indirectas) para sempre, porque a condição de saída desse ciclo nunca se
verifica.

## O sítio exacto (idêntico nas duas amostras de binário, 3+2 amostras)

Cadeia de chamada (de baixo para cima), MEDIDA em TODAS as 5 amostras sem excepção:

```
main -> ppu_run -> func_00010230 -> func_00010354 -> func_0025C838
     -> func_002B2E04 -> func_00242C94 -> [chamada por valor OU por
        ps3_indirect_call -> func_002B2DD0 -> {func_002B2660 | func_000B951C
        | func_002B7188} -> ... ] -> (varia por amostra)
```

`func_002B2E04` (`recomp_macos_v2.jtfix/ppu_recomp_001.cpp:36402`) é, lido directamente
do lift:

```c
void func_002B2E04(ppu_context* ctx) {
    func_00242C94(ctx);                       // chamado UMA vez
    flag_ea = vm_read32(ctx->gpr[2] - 0x1488); // ponteiro para a flag, via TOC
    if (vm_read32(flag_ea) != 0) return;       // já não está a zero -> sai já
    do {
        func_002B2DD0(ctx);                    // dispatcher per-tick
    } while (vm_read32(flag_ea) == 0);          // continua enquanto for zero
}
```

`func_00242C94` (`ppu_recomp_000.cpp:531122`) tem o SEU PRÓPRIO loop interno: itera um
nó (`r31`→`r9 = *(r31+0)`), chama `func_00194D3C` (o wrapper de import que despacha
`cellPadGetInfo2`) EM TODA iteração, e só sai (`*(TOC-0x244C)=1; return;`) quando os
DOIS bytes de flag do nó (`*(r9+4)` e `*(r9+5)`) estão a zero — caso contrário despacha
uma chamada indirecta (via `r30+0x460C`, um vtable/jump-table) e volta a tentar.

**As duas condições de saída (a de `func_002B2E04` E a interna de `func_00242C94`)
nunca se verificam** nas amostras — daí o loop nunca terminar e nunca devolver o
controlo a `func_0025C838`/ao resto do boot.

## As 5 amostras (backtraces completos em `/tmp/lldb_sample{1,2,4}.txt`,
`/tmp/lldb_prev3_sample{1,2}.txt` — não commitados, `/tmp`)

| # | Binário | Topo da pilha | Via |
|---|---|---|---|
| 1 | `boot_gow2.jtfix` | `cellPadGetInfo2` → `vm_write32` | `func_00194D3C` (chamada estática) |
| 2 | `boot_gow2.jtfix` | `vm_read32` dentro de `func_002B721C` | `ps3_indirect_call` → `func_002B2DD0` |
| 3 | `boot_gow2.jtfix` | `ps3_nid_table_find` → `cellPadGetInfo2` | `func_00194D3C` (via NID lookup) |
| 4 | `boot_gow2.pre_v3` | `vm_write64` dentro de `func_000CC9D0`←`func_000CD0F0`←`func_000B951C` | `ps3_indirect_call` → `func_002B2DD0` |
| 5 | `boot_gow2.pre_v3` | `vm_write64`/`vm_uncommitted_guard` dentro de `func_000B951C` | `ps3_indirect_call` → `func_002B2DD0` |

O facto de o corpo mudar entre amostras (`cellPadGetInfo2` numa, `func_002B721C` noutra,
`func_000CC9D0`/`func_000B951C` nas do `pre_v3`) é a prova de que **NÃO é um freeze**
(um freeze real mostraria sempre o MESMO frame#0/PC) — é um ciclo que avança
normalmente por várias sub-rotinas reais do jogo a cada iteração, só que nunca convence
a condição de saída.

## `cellPadGetInfo2` sim, `cellPadGetData` nunca

`cellPadGetInfo2` (info de ligação do pad) aparece em 2 das 3 amostras de `jtfix` como
o alvo directo da chamada. `cellPadGetData` (leitura real de botões) tem um marcador
`fprintf` dedicado no próprio código-fonte
(`ps3recomp/libs/input/cellPad.c:436`, `[PADPOLL ...] cellPadGetData ... (game IS
polling pad)`, gated pelas primeiras 5 chamadas) — e **NUNCA aparece em nenhum dos
logs**, confirmando por inspecção de código+log (não só pela contagem já conhecida
`cellPadGetData=0`) que a função nunca é invocada pós-`thr_auto_load`. Com
`PS3_PAD_AUTOSTART=1`, `cellPadGetInfo2` já reporta `port_status[0]=CONNECTED` — a
"ligação" do pad não é o que falta; o que falta é o jogo alguma vez chamar
`cellPadGetData`.

## `SetFlip` pára em seco no MESMO instante

```
grep -c SetFlip antes de "thr_auto_load() end":  6394 (todas durante o loading/AutoLoad)
grep -c SetFlip depois de "thr_auto_load() end": 0
```

Confirmado em `boot_gow2.jtfix` corrida 3 (linha do fim=8507, `wc -l` final=8547 após
40 linhas extra de sampler durante a amostragem lldb) e replicado em `boot_gow2.pre_v3`
corrida 4 (fim=8703). **Nenhum flip de RSX acontece depois do hold do AutoLoad ser
limpo** — o caminho de desenho do menu nunca é reentrado, o que é consistente com o
loop nunca devolver controlo ao código que emitiria os draws do menu.

## O binário de referência pára NO MESMO SÍTIO — resposta à pergunta do pedido

**Sim: `boot_gow2.pre_v3` produz exactamente a mesma assinatura.** Mesmas 3 linhas após
`thr_auto_load() end` (`hold cleared`, `boot logo queue DONE`, depois só sampler
`[MOVIEFSM] st620 0 -> 0` em loop), e o backtrace da main thread cai na MESMA cadeia
`func_0025C838 -> func_002B2E04 -> func_00242C94 -> ps3_indirect_call ->
func_002B2DD0 -> {func_000B951C | ...}` — os MESMOS nomes de função nos MESMOS offsets
relativos, apesar de `pre_v3` ser um lift mais antigo (sem o fix `2e21639`/+74 funções).

**Conclusão honesta, tal como o pedido pediu para dizer sem rodeios: o menu nunca
funcionou neste projecto.** Não há evidência de regressão — há evidência de um alvo
nunca antes alcançado (o loop `func_002B2E04`/`func_00242C94` sempre foi um poço sem
saída neste ponto, em pelo menos dois lifts/binários distintos, incluindo o `pre_v3`
anterior a toda a cadeia de trabalho desta sessão). Isto muda o objectivo de "repor o
que o re-lift apagou" para **"implementar a condição de saída que falta"** — a mesma
mudança de enquadramento que o pedido já antecipava.

## Outras threads (host) — bloqueio ESPERADO, não um segundo hang

As restantes 26 threads amostradas (`thread list`/`thread backtrace all`, amostra
completa em `/tmp/lldb_sample1.txt`) estão todas correctamente bloqueadas em
`pthread_cond_wait`/`pthread_mutex_wait`/`ppu_giant_lock_acquire*`/`sys_lwcond_wait` —
o modelo de execução de um-PPU-de-cada-vez via giant lock (descrito no CLAUDE.md do
motor) está a funcionar como desenhado. Não há um segundo lock preso; só a main thread
(a única a segurar o giant lock neste ponto) importa para este diagnóstico.

## Hipótese não confirmada (INFERÊNCIA, para a próxima ronda)

`func_00242C94` só sai do seu loop interno quando os dois bytes de flag do nó activo
(`*(r9+4)`/`*(r9+5)`) chegam a zero; enquanto isso não acontece, despacha uma chamada
indirecta via `r30+0x460C` (jump table) a cada iteração — o que explica por que amostras
diferentes caem em sub-rotinas diferentes (`func_002B721C`, `func_000CC9D0`, etc.):
são "itens" diferentes da mesma fila/lista sendo reprocessados repetidamente. Não foi
confirmado (falta `PS3_WATCH_W32` no endereço concreto do nó) se essas flags SÓ seriam
zeradas por um evento ligado ao flip do RSX (o que explicaria por que `PS3_NO_RSX=1`
poderia impedir a saída) — é a experiência discriminadora óbvia a seguir: repetir esta
mesma medição com `PS3_RSX_BACKEND=metal` real (sem `PS3_NO_RSX`) e ver se o loop
avança. Não foi feita nesta sessão por orçamento de tempo.

## Ficheiros/artefactos desta sessão

- Backtraces brutos: `/tmp/lldb_sample1.txt`, `/tmp/lldb_sample2.txt`,
  `/tmp/lldb_sample3.txt` (amostra perdida — processo morto pelo timeout da ferramenta
  ao usar `breakpoint`/`continue` em lldb batch; ver nota abaixo), `/tmp/lldb_sample4.txt`,
  `/tmp/lldb_prev3_sample1.txt`, `/tmp/lldb_prev3_sample2.txt` (não commitados, `/tmp`).
- Logs completos das corridas: `/tmp/lldb_investig_run{1,2,3,4}.log`,
  `/tmp/lldb_prev3_run{1,2,3,4}.log` (não commitados, `/tmp`).
- **Nota metodológica:** `lldb -b -p PID -o "breakpoint set ..." -o "continue" ...`
  bloqueia a chamada em modo batch até o breakpoint disparar; se a ferramenta que chama
  o lldb tiver um timeout (2 min aqui), o `SIGKILL` do timeout mata o grupo de processos
  **incluindo o alvo anexado** — perdeu-se uma corrida (`boot_gow2.jtfix` run 3) desta
  forma. Preferir sempre `attach`/`thread backtrace`/`detach` sem `breakpoint`/`continue`
  para amostragem não-invasiva (o que as outras 5 amostras usaram, sem incidentes).
- Nenhum ficheiro do motor foi editado nesta sessão — só leitura/instrumentação externa
  via lldb e leitura de fonte lifted.
