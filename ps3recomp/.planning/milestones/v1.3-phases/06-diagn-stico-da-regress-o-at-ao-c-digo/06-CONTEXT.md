# Fase 6: Diagnóstico da regressão até ao código - Contexto

**Reunido:** 2026-07-30
**Estado:** Pronto para planeamento
**Modo:** Escrito a partir de MEDIÇÃO desta sessão, não de discuss genérico. Tudo abaixo
foi observado; nada foi inferido de planos antigos.

<domain>
## Fronteira da fase

Localizar, **no código**, a causa da regressão que impede o 2.º `cellVdec StartSeq` e,
por consequência, o `thr_auto_load` e o `R_PermA`. A fase acaba quando a causa tem
ficheiro e linha, e as duas hipóteses já registadas têm veredicto escrito.

**NÃO faz parte desta fase:** corrigir (isso é a Fase 8) nem construir o gate (Fase 7).
</domain>

<code_context>
## O que já está medido (não repetir)

### O sintoma, em quatro corridas independentes

| corrida | binário | backend | StartSeq | thr_end | R_Perm |
|---|---|---|---:|---:|---:|
| probes | `boot_gow2_probe` | headless | 1 | 0 | 0 |
| metal | `boot_gow2_probe` | Metal | 1 | 0 | 0 |
| pré-promoção | `boot_gow2.pre_20260729_150649` | Metal | 1 | 0 | 0 |
| **oficial** | `boot_gow2` via `rodar_gow2_menu_fast.sh` **sem alterações** | Metal | 1 | 0 | 0 |

Referência das notas de 22–25 Jul: `2 / 1 / 1`. O próprio script imprime
`Gate_A RED`, `Gate_B RED`, `FATAL 0` (paragem, não crash), `SetFlip_total 2021`,
`SetFlip_after_R_Perm 0`, `Pad_total 0`.

### O bissect JÁ ESTÁ FEITO — não o repetir, reutilizá-lo

| binário | data | StartSeq | thr_end | R_Perm | NOPIC | linhas |
|---|---|---:|---:|---:|---:|---:|
| `boot_gow2.pre_v3` | 25 Jul 18:23 | **2** | **1** | **199** | **4** | 4135 |
| `boot_gow2_v3fix` | 26 Jul 02:23 | 0 | 0 | 0 | 0 | **101** |
| `boot_gow2_v4` | 26 Jul 02:35 | 1 | 0 | 0 | 0 | ~3200 |
| `boot_gow2` (produção) | 29 Jul 15:07 | 1 | 0 | 0 | 0 | 3161 |

**Janela fechada: 25 Jul 18:23 → 26 Jul 02:35 (~8h).** O `v3fix` morre a 101 linhas
(falha estrutural, família dos ~55 linhas do CLAUDE.md) e por isso não delimita nada;
é o `v4` que fecha o lado de baixo.

**O ambiente está ilibado:** o `pre_v3` funciona HOJE, com os dados de hoje.

### Onde pára, exactamente

A intro corre inteira e fecha limpa (`st620 3 → 11`, `Close(handle=0)`,
`wait-idle exit`), o overlay VT do `SmLogo_v2` toca 330 frames, e depois:

```
[MOVIEFSM] st620 11 -> 1
[MOVIEFSM] st620 1 -> 1      <- e não sai daqui
```

O único `StartSeq` do guest aparece na linha 3190 de 3217 — tarde demais para haver 2.º.

`REPLAY-NOPIC` conta **0**: o fix HLE está no motor (`libs/codec/cellVdec.c:613`,
`skip_guest_pic`) mas a condição é `g_vdec_startseq_count >= 2`. **É consequência, não
causa.**

### O script de corrida já existe

`$CLAUDE_JOB_DIR/tmp/run_probes_metal.sh` — recipe menu-fast com Metal, mata por PID
(TERM → espera → -9), `TIMEOUT` configurável, `BIN`/`LOG` por variável. É descartável;
a Fase 6 deve versioná-lo como ferramenta a sério (DIAG-01).
</code_context>

<decisions>
## Decisões já tomadas (não re-litigar)

- **O bissect por binários guardados é o método.** Custa ~2 min por corrida e zero
  compilações. Um bissect por commit exigiria rebuilds de 30–40 min cada.
- **A ferramenta tem de ser reutilizável**, não um procedimento repetido à mão. Foi
  precisamente a ausência dela que deixou esta regressão passar dias.
- **A tabela de saída mede a CADEIA**, um contador por elo: `StartSeq`,
  `thr_auto_load end`, `R_PermA`, `REPLAY-NOPIC`, e o total de linhas (que discrimina
  falha estrutural de paragem lógica).

## À discrição de quem implementa

- Onde versionar a ferramenta (`../gow2-recomp/` com espelho em `games/gow2/`, seguindo
  o padrão de `smoke_relift_equiv.sh`, é o mais provável).
- O formato exacto da tabela (TSV por corrida é o padrão do projecto).
- Como fechar do lado do código: `git log`/`git diff` entre os commits da janela, com
  `git bisect` só se a janela por binários não bastar.
</decisions>

<specifics>
## Ideias específicas

### As duas hipóteses precisam de veredicto ESCRITO

**1. TOCFIX** (`../gow2-recomp/notes/2026-07-26-tocfix-matou-36-conversoes-opd.md`)

O lifter trocou o restauro **dinâmico** do TOC por um **estático**, na janela exacta:

```c
// ANTES (lift de 20 Jul, o que funciona)   -- 17 602 sítios
ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
// AGORA                                     -- 17 268 sítios
ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/
```

**Estado: ENFRAQUECIDA por medição desta sessão, não confirmada.** Contei os OPD do
ELF cujo `code` é início de função declarada: **16 259 de 16 278 usam exactamente
`0x00541178`**; só **19** divergem (todos `0x00572610`), e vários apontam para
`func_004C0F00` / `func_004C2538`, que o `lift_todo_audit.py` já classifica como
tabelas de strings declaradas como funções.

Ou seja, 99,9% do executável usa um só TOC. Isto **não prova quebra**. Mas continua a
ser a mudança de maior escala documentada na janela, e merece um teste directo: por
exemplo, gerar um lift com o restauro dinâmico reposto e medir o `thr_auto_load`.

**2. `strip_block` comeu o lift**
(`../gow2-recomp/notes/2026-07-26-strip-block-comeu-o-lift.md`) — mesma data,
truncagem de chunks.

**3. Fora do lift.** A nota de 07-25 avisa que o binário daquela medição tinha WIP
não-committado em `movie_eos_arm.c`, que é a fonte do `st620`. O runtime, o HLE de
`cellVdec` e o `movie_eos_arm.c` estão no âmbito.

### Um achado colateral, já corrigido, que informa o desenho

Sete probes no lift competiam pelo `SIGTERM` — `signal()` substitui, não acumula — e só
o último imprimia o `SUMMARY`. Corrigi os dois desta leva (encadeiam e armam por último,
commit `09c3850` em `gow2-recomp`). Os outros cinco continuam a perder-se. Isso é
DIAG-02, na Fase 7.
</specifics>

<deferred>
## Adiado

- Corrigir a causa — Fase 8, e só depois de ela ter nome.
- A parede TYPE15 e os probes `29AF0`/`CB56C` já construídos — bloqueados até o boot
  voltar ao `R_PermA`.
</deferred>
