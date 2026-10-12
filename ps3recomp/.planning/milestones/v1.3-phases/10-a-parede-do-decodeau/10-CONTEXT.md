# Fase 10: A parede do DecodeAu - Contexto

**Reunido:** 2026-07-31
**Estado:** Pronto para planeamento
**Modo:** Escrito a partir de medição comparativa entre o binário de referência e o actual.

<domain>
## Fronteira da fase

O guest **abre** o `/_movies/smlogo_v2.m2v` e **não envia um único AU** ao decoder. O
binário de referência envia 10. Descobrir porquê, e repor o `thr_auto_load`.

**NÃO faz parte:** a cadeia FIOS (reposta na Fase 9, confirmada por medição) nem o gate
(Fase 7).
</domain>

<code_context>
## A medição que define a fase

Comparação directa entre `boot_gow2.pre_v3` (referência, `thr_end=1` em 5/5) e
`boot_gow2_fiosfix` (com a cadeia FIOS reposta, `thr_end=0` em 6/6):

| evento | referência | actual | |
|---|---:|---:|---|
| `[movieio] open '/_movies/smlogo_v2.m2v'` | 1 | **1** | ✅ igual |
| `[movieio] open '/wad/r_lglsca.wad_ps3'` | 1 | **1** | ✅ igual |
| `[movieio] open '/wad/r_perma.wad_ps3'` | 1 | **1** | ✅ igual |
| `cellVdec StartSeq` | 2 | **2** | ✅ igual |
| `cellVdec Open codec` | 2 | **2** | ✅ igual |
| `decoder opened` | 1 | **1** | ✅ igual |
| **`cellVdec DecodeAu`** | **10** | **0** | ❌ **a parede** |
| `FORCE SEQDONE watchdog` | 2 | 2 | (igual — mas é watchdog, não SEQDONE natural) |
| `REPLAY-NOPIC` | 4 | 0 | consequência |
| `PICOUT` | 4 | 0 | consequência |

**Tudo idêntico até ao `DecodeAu`.** O guest abre os quatro ficheiros certos, abre o
decoder, faz os dois `StartSeq` — e depois não alimenta.

## Porque o `REPLAY-NOPIC` é consequência e não causa

O gate nomeia o elo `re-Play (NOPIC)`, o que sugere o `REPLAY-NOPIC`. **Não é aí.**

O bloco do `REPLAY-NOPIC` (`libs/codec/cellVdec.c:608-632`) vive **dentro do laço de
decode**, a seguir a `v->auCount++`. Sem `DecodeAu`, o laço não corre e o bloco nunca é
alcançado. A condição (`g_vdec_startseq_count >= 2 && !s_replay_pic`) está satisfeita —
`startseq=2` e `PS3_VDEC_REPLAY_PIC` não está no env — mas é irrelevante.

**Não perder tempo a investigar o `REPLAY-NOPIC`.**

## O que a Fase 9 estabeleceu, e que esta fase herda

A cadeia FIOS foi apagada por um re-lift porque nunca fora convertida em `patch_*.py`.
Sete patches repuseram-na e o efeito está medido:

```
SEM OP LIVRE (F2a)   a falha medida -> 0 em 6/6
startseq             1 -> 2
R_PermA 20169344     0 -> 1 em 6/6      (o WAD de 20MB lido inteiro)
st620                0,11,0 -> 11 em 6/6
elo_stopped          2o movie -> re-Play (NOPIC)
```

E um sinal inédito: numa das seis corridas, `setflip_after_rperm=107` — **nenhum binário
desta investigação, incluindo o de referência, tinha desenhado depois do WAD.**

## A hipótese óbvia, e porque não deve ser assumida

Dado que a Fase 9 encontrou 10 marcadores `FIOS-*` apagados pelo re-lift, é natural
suspeitar de **mais marcadores apagados** noutro subsistema (vdec, movie, stream).

**Mas isso é hipótese.** A Fase 9 também mostrou que o inventário completo tem de vir
**antes** de escrever código — descobrir peças uma a uma custou duas iterações do gate.

O primeiro passo desta fase deve ser o inventário: **todos** os marcadores presentes em
`recomp_macos_v2.pre_v4` e ausentes em `recomp_macos_v2`, não só os `FIOS-*`.
</code_context>

<decisions>
## Decisões já tomadas

- **O critério é o `smoke_chain_gate.sh`**, ≥4 de 6 corridas com `thr_auto_load end >= 1`.
- **Mínimo 3 corridas para qualquer veredicto.** Uma corrida mentiu nesta sessão (o
  `pre_v4` deu `REGRESSAO` sendo byte-idêntico ao `pre_v3`, que dá `OK` em 5/5).
- **Inventário antes de código.** Lição directa da Fase 9.
- **Reposição de código apagado vira `patch_*.py` idempotente commitado.** É a regra do
  CLAUDE.md que esta investigação inteira provou ser necessária.

## À discrição de quem implementa

- Se atacar pelo lado do diff de marcadores ou pelo lado da instrumentação do guest.
- Que probe usar para localizar onde o guest pára entre o `open` e o `DecodeAu`.
</decisions>

<specifics>
## Ideias específicas

### Duas armadilhas já pagas, que se repetem aqui

**1. `SKIP` silencioso por deriva de agulha.** O `patch_fios_stop_yield.py` e o
`patch_fios_done_cancel_yield.py` davam `SKIP` nos 7 chunks porque a needle não
contemplava o `ctx->lr = 0x...;` que o lift passou a prefixar aos `bl`. **Qualquer patch
que hoje dê `SKIP` é suspeito** — vale a pena varrer todos os `patch_*.py` por needles
que já não casem, em vez de esperar que a próxima parede o revele.

**2. Um patch que não aplica nada.** O `patch_fios_f2a_f2b_wad.py` só fazia `print()` de
instruções. Podem existir outros — vale a pena procurar `patch_*.py` cujo `main()` não
escreva ficheiro nenhum.

### O que instrumentar, se o inventário não bastar

O caminho entre `[movieio] open` do smlogo e o primeiro `cellVdecDecodeAu` passa pelo
guest a ler o ficheiro e a montar AUs. Um probe no `cellVdecDecodeAu` (entrada) e no
caminho de leitura do stream diz se o guest tenta e falha, ou nem tenta.

O `FORCE SEQDONE watchdog` disparar nas duas vezes (em vez de um SEQDONE natural) é um
sinal: o decoder termina por timeout, não por ter consumido o stream.
</specifics>

<deferred>
## Adiado

- A parede TYPE15 e os probes `29AF0`/`CB56C` (construídos na Fase 6, à espera de o boot
  chegar ao `R_PermA` — o que **já acontece** desde a Fase 9; podem ser reavaliados).
- Cherry-picks do upstream (#81, #79, #86, #87, #88).
- `grep -i assert` no smoke — lição da issue #75.
- Rever se `SKIP` num patch `FUNCIONAL` devia ser alarme no catálogo de 6 estados.
</deferred>
