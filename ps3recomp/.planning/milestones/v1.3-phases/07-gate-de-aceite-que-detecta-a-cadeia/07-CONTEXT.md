# Fase 7: Gate de aceite que detecta a cadeia - Contexto

**Reunido:** 2026-07-30
**Estado:** Pronto para planeamento
**Modo:** Escrito a partir de MEDIÇÃO desta sessão.

<domain>
## Fronteira da fase

Fechar o buraco de aceite que deixou uma regressão de paragem total passar despercebida
durante dias, e garantir que nenhum sinal de diagnóstico se perde por concorrência.

**NÃO faz parte desta fase:** localizar a causa (Fase 6) nem corrigi-la (Fase 8). Esta
fase é construível **hoje**, contra os binários que já existem, sem saber a causa.
</domain>

<code_context>
## O buraco, medido

O gate actual do smoke (`smoke_relift_equiv.sh`, entregue no marco v1.0) exige
**`st620 ≥ 3` em ≥4 de 6 corridas**.

No estado partido de hoje, o `st620` chega a **11** antes de a cadeia parar. Portanto:

> **O smoke do v1.0 PASSA num binário que não chega ao menu, não lê o comando e não
> desenha um único frame depois da intro.**

O `st620` mede a FSM do movie player. Ele progride até ao fim da intro e só depois é que
tudo pára — logo é um indicador do elo errado.

## O que o script oficial já sabe fazer (e o gate não usa)

`rodar_gow2_menu_fast.sh` calcula e imprime, no fim:

```
thr_end 0            PARK 0                 FATAL 0
attach_full 0        CLOSE_PRESERVE 0
SetFlip_total 2021   SetFlip_after_R_Perm 0
Pad_total 0          Pad_after_R_Perm 0
CC9D0_SKIP 0         ICALL_BAD 0
Gate_B RED
Gate_A RED
```

**Estes contadores já detectam a regressão.** `Gate_A RED` e `Gate_B RED` estão certos.
O que falha é que o gate de **promoção** (`accept_relift.sh`) não os consome.

## Os elos da cadeia, e onde cada um se mede

| elo | sinal no log | valor bom | valor hoje |
|---|---|---:|---:|
| intro corre | `st620` máximo | ≥ 3 | 11 ✅ |
| 2.º movie arranca | `StartSeq(handle` | 2 | **1** ❌ |
| re-Play não pendura | `REPLAY-NOPIC` | ≥ 4 | **0** ❌ |
| AUTO_LOAD termina | `thr_auto_load() end` | 1 | **0** ❌ |
| WAD lido inteiro | `20169344` | ≥ 1 | **0** ❌ |
| desenha pós-load | `SetFlip_after_R_Perm` | > 0 | **0** ❌ |
| lê o comando | `Pad_total` | > 0 | **0** ❌ |

Referência boa medida hoje com `boot_gow2.pre_v3`: `StartSeq=2`, `thr_end=1`,
`R_Perm=199`, `NOPIC=4`, 4135 linhas de log.

## O defeito de concorrência (DIAG-02), medido

O lift tem **sete** probes que armam um handler de `SIGTERM`:

```
CD498  17ACC  FLIPPATH  PRESENTLOOP  MENUPRESENT  SCHEDARM  POSTTHR
```

`signal()` **substitui** o handler, não o acumula. O boot é morto por `TERM` depois do
timeout, logo **só o último a armar imprime o seu `SUMMARY`**. Os outros seis perdem-se
em silêncio — e um `SUMMARY` ausente é indistinguível de "a probe não correu".

Já corrigi **dois** desta leva (`29AF0`, `CB56C`, commit `09c3850` em `gow2-recomp`):
guardam o handler anterior e chamam-no no fim, e ancoram o preâmbulo no fim da zona de
preâmbulos para armarem por último. Medi que no clang/Mach-O os constructors correm por
**ordem de definição no ficheiro** — a prioridade não reordena de forma útil.

Os outros sete continuam a atropelar-se. O padrão de correcção está provado e é
mecânico.
</code_context>

<decisions>
## Decisões já tomadas

- **O gate mede a cadeia, não um contador.** Um contador só diz "passou/não passou"; a
  cadeia diz **onde** parou, que é o que poupa os dias.
- **Não substituir o `st620`** — acrescentar elos. O `st620 ≥ 3` continua válido como
  primeiro elo; o erro foi ser o único.
- **A prova de que o gate funciona é a rejeição do binário actual.** Temos, hoje, um
  binário mau (`boot_gow2`) e um bom (`boot_gow2.pre_v3`). O gate novo tem de aceitar o
  segundo e rejeitar o primeiro. É injecção real, não sintética.

## À discrição de quem implementa

- Se estender `smoke_relift_equiv.sh` ou criar um script irmão.
- O formato do TSV por elo.
- Se o encadeamento de `SIGTERM` dos sete probes se faz por um patch a cada um ou por um
  helper partilhado injectado uma vez.
</decisions>

<specifics>
## Ideias específicas

### O aceite de GATE-03 tem de ser real, não sintético

O `accept_relift.sh` tem três pernas (smoke + `verify_lift.sh` +
`apply_all_patches --status`). A perna do smoke passa a ter de reprovar o binário de
produção actual. Corre-o contra:

- `boot_gow2` (29 Jul, partido) → tem de dar **rc≠0**
- `boot_gow2.pre_v3` (25 Jul, bom) → tem de dar **rc=0**

Isto é injecção nos dois sentidos com binários reais, que é mais forte do que fixtures.

### Cuidado com o modo `--bin`

O `smoke_relift_equiv.sh` já tem um modo `--bin BIN_PATH` que não faz rebuild. É esse
que serve aqui — não se pode rebuildar o `pre_v3`, ele só existe como binário.

### Custo

Cada corrida de boot é ~2 min. Um gate de 6 corridas por binário × 2 binários = ~24 min.
É aceitável para a prova única; o gate em uso corrente deve manter as 6 corridas de um
só binário.
</specifics>

<deferred>
## Adiado

- A causa da regressão — Fase 6, em paralelo.
- A correcção — Fase 8, que consome este gate para medir a reposição.
</deferred>
