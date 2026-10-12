# O boot já não chega ao AUTO_LOAD — quatro corridas, o mesmo resultado

**Data:** 2026-07-30 · **Método:** quatro corridas reais, três discriminadores.
**Não é uma hipótese — é uma medição repetida.**

## O que se ia fazer

Correr os dois probes desta leva (`patch_29af0_switch_probe.py`,
`patch_cb56c_type_probe.py`) contra o boot, para responder às duas perguntas que
o ponto 1 deixou em aberto: que índices chegam à jump table de `func_00029AF0`, e
que tipo tem o produto que o `CB56C` instala.

**Nenhum dos dois disparou** — e não por defeito deles. O boot pára antes.

## A medição

Todas com a recipe menu-fast (`FORCE_SEQDONE=1500`, `BOOT_LOGO_MS=300`,
`AUTO_LOAD_RUN=1`, `PAD_AUTOSTART=1`, `TYPE15_UNSTICK=1`, `SPU1/4/5`), 90–150 s:

| corrida | binário | backend | linhas | StartSeq | thr_end | R_Perm 20 MB |
|---|---|---|---:|---:|---:|---:|
| probes | `boot_gow2_probe` | headless | 3239 | **1** | **0** | **0** |
| metal | `boot_gow2_probe` | Metal | 3248 | **1** | **0** | **0** |
| pré-promoção | `boot_gow2.pre_20260729_150649` | Metal | 3249 | **1** | **0** | **0** |
| **oficial** | `boot_gow2` (produção) via `rodar_gow2_menu_fast.sh` **sem alterações** | Metal | 3161 | **1** | **0** | **0** |

A referência que as notas de 22–25 Jul registam é `StartSeq=2`, `thr_end=1`,
`R_Perm=1` (20169344), com `thr_auto_load end` aos ~34 s.

### O veredicto é do próprio projeto, não meu

O `rodar_gow2_menu_fast.sh` calcula e imprime os seus próprios contadores no fim.
Corrido sem uma única alteração, contra o binário de produção:

```
thr_end 0            PARK 0                 FATAL 0
attach_full 0        CLOSE_PRESERVE 0
SetFlip_total 2021   SetFlip_after_R_Perm 0
Pad_total 0          Pad_after_R_Perm 0
CC9D0_SKIP 0         ICALL_BAD 0
Gate_B RED
Gate_A RED
```

**Os dois gates do projecto dão RED.** `FATAL 0` — não é crash; o boot fica
simplesmente parado. `SetFlip_total 2021` com `SetFlip_after_R_Perm 0` confirma
que a intro desenha e que nada acontece depois.

## Três discriminadores, todos negativos

1. **Não são os probes.** O binário de produção, sem eles, dá o mesmo. Os probes
   são no-op com o gate desligado e a corrida oficial nem os tem no binário.
2. **Não é a promoção do marco v1.0.** O binário de 26 Jul, anterior à promoção
   do `recomp_macos_v3` a produção (29 Jul), dá o mesmo. Isto refuta a hipótese
   que a dívida declarada tornava mais provável ("o smoke não cobre WADLD").
3. **Não é a minha recipe.** O `rodar_gow2_menu_fast.sh` corrido tal e qual, sem
   uma única variável minha, dá o mesmo.

## Onde pára, exactamente

A intro corre inteira e termina de forma limpa:

```
[INTROSEQ] CE03C wait tick st620=3 i=200
[MOVIEFSM] st620 3 -> 11   overlay_done=1
[cellVdec] Close(handle=0)
[INTROSEQ] CE03C wait-idle exit st620=0
[INTROSEQ] CE03C clear sticky EOS hook 0x00869F1C
[MOVIEDONE] time-based timer reset (next Play can re-arm)
snd_stream: couldn't open file /_movies/SmLogo_v2.wav
[MOVIEFSM] st620 11 -> 1
[MOVIEFSM] st620 1 -> 1      <- e daqui não sai mais
```

`st620` chega a 11 (o máximo que o smoke exige é 3, por isso **o smoke passa**), o
overlay VT do `SmLogo_v2` toca até ao fim (330 frames RGBA), e depois a FSM cai
para 1 e fica em `1 -> 1` indefinidamente.

O único `StartSeq` do guest aparece na linha 3190 de 3217 — quase no fim da
corrida, tarde demais para haver um segundo.

## O `REPLAY-NOPIC` não é a causa

`notes/2026-07-22-2nd-movie-st620-wall.md` documenta que sem `REPLAY-NOPIC` o
resultado é exactamente `thr_end=0` / `R_Perm=0`. Verificado:

- o fix **está** no motor (`libs/codec/cellVdec.c:613`, `skip_guest_pic`)
- mas a condição é `g_vdec_startseq_count >= 2`
- e `REPLAY-NOPIC` conta **0** em todas as corridas

Ou seja, não dispara porque o 2º `StartSeq` nunca acontece. É consequência, não
causa. A parede é a montante disso.

## Porque isto importa mais do que a parede TYPE15

O `[CD498] SUMMARY` continua a dar `tot=0` em todos os 18 sítios — mas agora esse
zero **não significa nada**, porque o boot nem chega ao ponto onde eles seriam
alcançados. Qualquer leitura da parede TYPE15 feita a partir de uma corrida
assim é sobre um boot que parou antes.

As notas de 22–25 Jul foram medidas quando o boot chegava ao `R_PermA`. Se hoje
não chega, **os zeros de então e os zeros de agora não são o mesmo zero.**

---

# ADENDA: o bissect fechou — e elimina o ambiente

Corrido no mesmo dia, mesma recipe Metal, mesmo `TIMEOUT`, **sem compilar nada**.

| binário | data | StartSeq | thr_end | R_Perm | NOPIC | linhas | veredicto |
|---|---|---:|---:|---:|---:|---:|---|
| `boot_gow2.pre_v3` | 25 Jul 18:23 | **2** | **1** | **199** | **4** | 4135 | ✅ **funciona** |
| `boot_gow2_v3fix` | 26 Jul 02:23 | 0 | 0 | 0 | 0 | **101** | ⚠️ falha estrutural |
| `boot_gow2_v4` | 26 Jul 02:35 | 1 | 0 | 0 | 0 | ~3200 | ❌ falha como produção |
| `boot_gow2` | 29 Jul 15:07 | 1 | 0 | 0 | 0 | 3161 | ❌ falha |

## O que isto estabelece

**O ambiente está bom.** Um binário de 25 de Julho, corrido **hoje**, com os dados
extraídos de hoje, o `movie_cache` de hoje e o disco de hoje, chega ao `thr_auto_load`,
lê os 20 MB do `R_PermA` e dispara o `REPLAY-NOPIC` quatro vezes. A hipótese "não é o
binário, é o ambiente" — que era o desfecho alternativo previsto na secção anterior —
**está refutada**.

**A janela é de cerca de oito horas:** 25 Jul 18:23 → 26 Jul 02:35.

O `v3fix` morrer a 101 linhas não delimita nada: o CLAUDE.md regista que um boot que
morre em ~55 linhas é falha estrutural, e 101 está nessa família. Esse binário estava
partido de outra maneira. É o `v4` (02:35) que fecha a janela pelo lado de baixo.

## O que caiu nessa janela

Duas notas, ambas datadas de 26 de Julho:

- `2026-07-26-tocfix-matou-36-conversoes-opd.md` — o lifter trocou o restauro **dinâmico**
  do TOC (`ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28)`, 17 602 sítios) por um valor
  **estático** (`ctx->gpr[2] = 0x00541178ULL /*TOCFIX*/`, 17 268 sítios). A própria nota
  usa `.pre_v4` como referência do "lift antigo" — o binário que acabei de provar que
  funciona.
- `2026-07-26-strip-block-comeu-o-lift.md` — truncagem de chunks.

## Mas o TOCFIX NÃO está confirmado — e a medição enfraquece-o

Tentador ligar os pontos. Medi antes de o fazer, e o resultado não ajuda a hipótese:

```
OPDs cujo `code` é início de função declarada : 16 278
  com TOC == 0x00541178 (o que o TOCFIX estampa) : 16 259
  com TOC diferente                              :     19
```

Os 19 divergentes usam todos `0x00572610`, e vários apontam para `func_004C0F00`,
`func_004C2538`, `func_004C2900` — que o `lift_todo_audit.py` **já classifica como
tabelas de strings declaradas como funções**, e que o `lift_parity.py` já acusa como
espúrias.

Ou seja: 99,9% do executável usa um único TOC, e a fracção que não usa é dominada por
regiões que já sabemos não serem código. **Isto não prova que o TOCFIX quebra o caminho
do AUTO_LOAD.** Fica como pista ordenada, não como causa.

## Próximo passo

Descobrir o que mudou entre 25 e 30 de Julho no caminho intro→2º movie. Os
binários guardados dão um bissect barato, sem rebuild:

```
boot_gow2.rdy0_ref            24 jul 21:56
boot_gow2.pre_v3 / .pre_v4    25 jul 18:23   <- da data da nota de referência
boot_gow2.wip                 25 jul 17:47
boot_gow2_v3fix / _v4         26 jul 02:23 / 02:35
boot_gow2_relift_test         26 jul 16:31
boot_gow2.pre_20260729_150649 26 jul 15:28   <- ja falha
boot_gow2                     29 jul 15:07   <- falha
```

O `pre_v3`/`pre_v4` de 25 Jul é o primeiro a testar: se ele chegar ao
`thr_auto_load`, a regressão está entre 25 e 26 de Julho e o bissect fecha em
duas ou três corridas.

Se **nenhum** binário guardado chegar, então não é o binário — é o ambiente
(dados extraídos, `movie_cache`, estado do disco) e a investigação muda de sítio.

## Ressalvas

- Nada aqui diz que o código do jogo regrediu. Diz que **este checkout, hoje, não
  reproduz a corrida das notas** — e que qualquer conclusão tirada de uma corrida
  destas sobre a parede TYPE15 está a medir a coisa errada.
- As quatro corridas foram em máquina com outras coisas a correr (compilações em
  fundo). Uma delas foi morta por `SIGKILL` externo aos 54 s, provavelmente por
  pressão de memória. Isso afecta o tempo disponível, mas não explica o
  `StartSeq=1` — a corrida oficial teve 150 s e deu o mesmo.
