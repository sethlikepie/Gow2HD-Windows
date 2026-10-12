# O gate oficial media uma string que nunca existiu

Data: 2026-08-01.

## O que aconteceu

Passei a sessao a reportar "gate 0/6, bloqueado no AUTO_LOAD" como se fosse uma
medicao do jogo. Nao era.

`lib_boot_chain_metrics.sh` calculava o elo assim:

```sh
thr_end=$(grep -c 'thr_auto_load() end' "$log_path")
```

E `classify()` so' devolve `OK` se `thr_end >= 1`.

Essa string **nao existe**:

```
$ for b in boot_gow2*; do strings -a "$b" | grep -c 'thr_auto_load'; done
0   (x26 binarios, incluindo os de referencia pre_v3 / rdy0_ref / pre_v4)
$ grep -rn 'thr_auto_load' --include='*.c' --include='*.cpp' --include='*.h' .
(so' dois COMENTARIOS, nenhum fprintf)
```

`thr_auto_load` e' o nome de uma **funcao host** citada num comentario do
`sys_ppu_thread.c` sobre um SIGSEGV. Nunca foi um marcador impresso. O gate foi
escrito contra um marcador imaginado.

Consequencia: **o gate estava estruturalmente impedido de passar.** Todas as
corridas saiam `REGRESSAO` e `elo_stopped=AUTO_LOAD`, medisse o binario o que
medisse. Zero e' o unico valor que aquele grep podia devolver.

## O que isto NAO significa

**Nao significa que o jogo esteja mais adiantado do que eu reportei.** Os outros
elos (`st620`, `StartSeq`, `REPLAY-NOPIC`, `R_PermA`, `SetFlip`, `Pad`) medem
marcadores reais e continuam a medir o mesmo. O que se perde e' informacao, nao
se ganha progresso: o elo AUTO_LOAD estava **cego**, nao estava a passar.

E o veredicto "parado no AUTO_LOAD" ate' era, por acidente, verdadeiro -- pela
razao errada e sem sinal utilizavel.

## O que a medicao a serio mostrou

A thread guest chama-se `AUTO_LOAD` (maiusculas). Threads criadas numa corrida
completa:

```
BPETrophyInitThread        entry=0x005217E0
fios mediathread           entry=0x00534370   (x3)
fios scheduler             entry=0x00534370
snd_stream_service_thread  entry=0x00533A38   (x8)
syn_tick_timer_thread      entry=0x00532F50
```

`AUTO_LOAD` **nao aparece uma unica vez**. Nao e' criada.

Isto reformula a parede. A pergunta que eu (e sessoes anteriores) andei a fazer
-- *"porque e' que a thread AUTO_LOAD nao termina?"* -- nao tem resposta porque
nao tem sujeito. A pergunta certa e' **"porque e' que ela nunca chega a ser
criada?"**.

Reparo interessante: `BPETrophyInitThread` entra em `0x005217E0`, a 0x78 bytes
do `0x00521768` que o stub de AUTO_LOAD conhece como entry point.

## O que foi corrigido

1. **ps3recomp** (`4abd5a2`): acrescentado o marcador de fim de thread,
   simetrico ao de create -- `[SYS] sys_ppu_thread end name="%s" tid=%llu
   status=%lld`. O nome e' copiado antes do `table_unlock()`, pela mesma razao
   que o tid e o status ja' eram (reuso de slot DETACHED).
   Verificado in-boot: `fios mediathread status=0`. **O marcador dispara.**

2. **gow2-recomp**: `thr_end` passa a medir o marcador real, e `thr_created`
   passa a ser reportado a' parte -- porque o elo colapsava dois estados com
   investigacoes completamente diferentes:

   | estado | pergunta |
   |---|---|
   | nunca criada | porque nao chega la' o codigo que a cria? |
   | criada, nao termina | em que fica presa? |

   O gate agora diz `elo_stopped=AUTO_LOAD (nunca criada)`.

## Estado apos a correccao (3/3, boot_gow2.jtfix)

```
st620=11 startseq=2 nopic=4 thr_created=0 thr_end=0 r_perma=1
setflip_after_rperm=8..10 pad_total=0
elo_stopped=AUTO_LOAD (nunca criada)
```

Continua **0/3**. O gate nao passou a passar -- passou a dizer alguma coisa.

## Licao

E' a quarta vez hoje que um instrumento mente por omissao, e a mais cara:

1. `PS3_WATCH_STORE` com cap 300 -- fabricou uma contradicao aparente.
2. `PS3_TRACE_ICALL_TO` com cap 64 -- fez-me dizer "zero pushes" havendo 175.
3. `[WADLD-FACT]` com cap 16 -- mostrou as 16 passagens saudaveis e calou as 13 mas.
4. **O gate oficial, a medir uma string inexistente durante toda a fase.**

Os tres primeiros escondiam parte da amostra. Este nao media nada e **parecia
medir tudo** -- e era o numero que eu citava para dizer onde estavamos.

Regra que fica: **um instrumento que nunca produziu um PASS nao esta validado.**
Antes de confiar num gate, provar que ele consegue passar -- num binario de
referencia, num caso construido, ou pelo menos que a string que ele procura
existe no binario. `strings -a BIN | grep -c MARCADOR` custa um segundo.
