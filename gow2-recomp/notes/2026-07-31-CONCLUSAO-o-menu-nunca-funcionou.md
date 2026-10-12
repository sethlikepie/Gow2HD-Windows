# CONCLUSÃO: o menu nunca funcionou neste projecto

**Data:** 2026-07-31 · **Método:** `lldb` attach/backtrace/detach sobre corridas vivas,
5 amostras em 2 binários diferentes. Nada tocado no processo.

## O facto central

**A main thread não está parada.** Está viva, num loop real do jogo que nunca converge:

```
main -> ppu_run -> func_00010230 -> func_00010354 -> func_0025C838
     -> func_002B2E04 -> func_00242C94 -> ps3_indirect_call -> func_002B2DD0
```

`func_002B2E04` faz `while (*flag_ea == 0) func_002B2DD0(ctx);`. O `func_00242C94` tem o
seu próprio laço interno que só sai quando dois bytes de flag de um nó chegam a zero.
**Nenhuma das duas condições se verifica.**

Que é um ciclo e não um freeze está provado: o corpo **muda** a cada amostra
(`cellPadGetInfo2`, `func_002B721C`, `func_000CC9D0`, `func_000B951C`) — um freeze mostraria
sempre o mesmo PC.

## E o binário de referência cai no mesmo sítio

`boot_gow2.pre_v3` — o que serviu de "bom" toda a sessão, `thr_end=1` em 5/5 — produz as
**mesmas** 3 linhas pós-`thr_auto_load` e a main cai na **mesma cadeia**, com os mesmos
nomes de função.

**O menu nunca funcionou neste projecto.** Não é regressão; é um alvo nunca alcançado.

Isto muda o objectivo de *"repor o que se perdeu"* para *"implementar a condição de saída
que falta"* — trabalho de outra natureza e outra escala.

## Sinais que sustentam

| sinal | medido |
|---|---|
| `cellPadGetInfo2` | chamado repetidamente; reporta pad conectado via `PS3_PAD_AUTOSTART` |
| `cellPadGetData` (leitura real de botões) | **nunca aparece em log nenhum** — confirmado por inspecção do `libs/input/cellPad.c:436`, não só por contagem |
| `SetFlip` | 6394 **antes** do `thr_auto_load end`, **zero depois** — nos dois binários |
| outras 26 threads | bloqueadas no giant lock/cond-wait, como esperado — não é um segundo hang |

## O círculo lógico

O caminho de desenho não é reentrado depois do `AUTO_LOAD` (`SetFlip=0`), e o laço espera
flags que — hipótese **não confirmada** — podem depender de um evento ligado ao flip do RSX.

Se assim for, é um deadlock lógico: sem flip não há flag, sem flag não sai do laço, sem
sair do laço não há flip.

⚠️ A experiência discriminadora óbvia — repetir com Metal real — **já foi feita**: as
corridas `menu_run{1,2,3}` correram com `PS3_RSX_BACKEND=metal` e `PS3_RSX_FIFO=1`
(137 eventos `[RSX metal]`, `SetFlip=7078` durante a intro) e o resultado é o mesmo.
**A hipótese do `PS3_NO_RSX` não explica estas corridas.**

## O que isto fecha, e o que não

**Fecha:** a questão de "porque não chegamos ao menu". A resposta é que o jogo entra num
laço cuja condição de saída nunca é satisfeita, e isso é verdade também no binário que
sempre tratámos como referência.

**Não fecha:** o que zera essas flags no console real. É esse o próximo alvo, e é trabalho
de implementação, não de reposição.

## O que a sessão entregou, apesar de não chegar ao menu

O boot passou de "não abre um único ficheiro de conteúdo" para completar o
`cellSaveDataAutoLoad2` com "new game" e libertar o hold para o menu:

```
4 ficheiros abertos · 2.o StartSeq · REPLAY-NOPIC x4 · R_PermA 20MB
DecodeAu x10 · AutoLoad2 CELL_OK · thr_auto_load end (10 de 11 corridas)
```

Doze paredes, nenhuma delas um commit culpado.
