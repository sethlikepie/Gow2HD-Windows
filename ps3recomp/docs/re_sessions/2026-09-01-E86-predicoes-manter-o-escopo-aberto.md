# E86 — predições: se o escopo ficar aberto no #038, o frame avança?

**Data:** 2026-09-01 · **Antecessor:** [E85](2026-09-01-E85-a-fabrica-esvazia-se-por-desenho-e-o-NULL-esta-mais-fundo.md)
· **Estado:** predições commitadas, gate ainda não escrito

## O que está medido (E84/E85, in-boot)

- Os passos **#042–#054** do B71 registam descritores com `*desc = 4 ≥ 0`, o que em
  `vt[0x4c]` obriga a `stack[cursor]->+0x20`; e o #054 consulta a fábrica com `vt[0x48]`,
  que devolve 0 se `cursor < 0`.
- Com `cursor = −1`, `vt[0x4c]` lê `mgr+0x44` — um **inteiro** (índice ×0xc no
  registador) — e faz `*(smallint + 0x20)`: página 0. **No console é crash.** Logo no
  console há um escopo aberto durante #042–#054.
- No nosso boot o último evento antes de #042 é o **pop-de-tudo** no fim de
  `FUN_00041494` (chamado no fim do #038), que leva os 15 gestores a `−1`. Entre ele e
  #042: zero push. E `0x400D6DA0` nunca esteve acima de 0 antes do #038.

## O experimento (gate, OFF por default — diagnóstico, não fix)

`PS3_E86_KEEP_SCOPE=1` salta **um único despacho**: o `vt[0x44]` do gestor `+0x58` que
segue `bl 0xC2E68` em `FUN_00041494` (`lr=0x000415A4`), no #038. Tudo o resto igual.
Deixa os 15 gestores em `cursor=0` com o último produto carregado como corrente.

**Não é candidato a fix**: no console esse pop corre. É um discriminador — muda uma
variável (o estado do escopo em #042–#054) e mede o efeito no frame.

## Predições

### P1 — `[STATE] frame=` passa de 1 para N ≥ 2
O estado do escopo em #042–#054 é **a** divergência. A pergunta seguinte é *quem
empurra no console antes do #042* (um push persistente que não temos), e o alvo são
os passos #001–#037 e o próprio #038 (`FUN_000415f0`, `FUN_000411ec`×2, o `while
FUN_002ba76c`).

### P2 — o frame fica em 1, mas o wrap deixa de travar (FPRWRAP `sai`) e o tick pára noutro sítio
O escopo era necessário mas não suficiente. A parede seguinte fica nomeada pelo `[TICK]`
e pelas 8 chamadas de registo (deixam de dar `ICALL-BAD`?). Ganho real, parede nova.

### P3 — continua a travar no wrap com `per=−200`
A consulta do #054 continua a devolver NULL apesar de `cursor=0` — o produto corrente
de `0x400D6DA0` não é o que `func_00237CDC` quer, ou o E84 leu mal qual objecto é
consultado. A teoria do escopo está errada para esta fábrica; o alvo passa a ser o
produto em si.

### Nenhuma — `FATAL`/crash antes do frame 1
Saltar o pop parte outra coisa (p.ex. os registos escrevem num produto de load que é
libertado a seguir). O escopo que o console tem em #042 **não é** o do último load —
é outro objecto, e o gate está a manter o errado.

## Controle
- `[SCOPE]` tem de mostrar os 15 gestores **sem** o POP `lr=0x000415A4` (o gate actuou).
- `R31SRC`/`EMPTYFAB` no #054: `cursor` tem de ser `≥ 0` (a variável mudou mesmo).
- 3 corridas; a de baseline (gate OFF) tem de continuar a dar frame=1 e `per=−200`.

<!-- CUT LINE — nada acima é reescrito depois de correr -->

---

# RESULTADO — medido 2026-09-01, 3 corridas ON + 1 OFF + 2 com contador de frames

Binário `boot_gow2_e86` (lift `recomp_macos_v2` + `patch_41494_e86_keep_scope.py`),
presença do gate no binário confirmada por `strings` antes de correr.

## P1 — CONFIRMADA. O frame 1 vira frame N.

| corrida | gate | ticks (`func_002A6A94`) | flips | `VAZIO` | `ICALL-BAD` | ciclo do wrap |
|---|---|---|---|---|---|---|
| on1 | ON | 9 276 | 7 058 | 0 | 0 | **não corre** (só `sai-cedo`) |
| on2 | ON | 9 426 | 7 128 | 0 | 0 | não corre |
| on3 | ON | 9 470 | 7 154 | 0 | 0 | não corre |
| off | OFF | **2** | 2 420 (intro) | 6 | 8 | `it=5,6·10⁹` e a subir |
| keep1b | ON, `PS3_TRACE_STATE=a` | — | 9 455 | — | — | — |

```
[STATE] frame=1     0xFFFFFFFF -> 0x0052F3F0  (code 0x002B2DD0) app=0x00730DF8
[STATE] frame=500   preso em 0x0052F3F0
...
[STATE] frame=7000  preso em 0x0052F3F0
```

**Controles, todos passados:**
- `[E86]` imprime «pop-de-tudo saltado (ctr=0x0042A0C8 r3=0x400C3D88)» — o gate actuou
  no despacho certo (`0x0042A0C8` é o iterador de pop dos gestores, ra3 do E85);
- `[SCOPE]`: **zero** POP com `lr=0x000415A4` (vs 15 no baseline);
- `EMPTYFAB` no #054: `fab=0x400D6DA0 cursor=0 ok` (vs `cursor=-1 VAZIO`) — a variável
  mudou mesmo, e a consulta do E84 passa a devolver produto;
- baseline OFF no mesmo binário: `frame=1`, `per=−200`, 8 `ICALL-BAD` — inalterado.

Uma corrida ON ficou presa na FSM da **intro** (`st620 1→1`, `overlay_done=1`, o gate
nunca chegou a disparar): é a flakiness conhecida da intro (`CLAUDE.md`: «re-rodar, não
perseguir»), não este experimento. Repetida, deu o `frame=7000` acima.

## O que isto prova, e o que não prova

**Prova:** o estado do escopo dos gestores durante os passos #042–#054 do B71 é **a**
divergência entre o nosso boot e o console para a parede [F]. Com um produto corrente
nos 15 gestores: os 8 registos deixam de despachar sobre NULL, a fábrica devolve
produto, nenhum objecto é escrito por cima do rodata, o wrap lê 2π/0 e sai, e o loop
principal itera milhares de vezes.

**Não prova:** que saltar o pop seja o comportamento do console. **Não é.** No console
o pop corre; o que o console tem e nós não é **um push persistente antes do #042** — e
esse push continua por encontrar. `PS3_E86_KEEP_SCOPE` é um **diagnóstico gated, OFF
por default**, e fica assim: não é candidato a promoção, não é fix.

**Também não prova pixels:** os 9 455 flips são `SetFlipCommand` do guest; o que está no
buffer não foi olhado nesta sessão.

## A parede seguinte, já nomeada pela mesma corrida

`preso em 0x0052F3F0` durante 7 000 frames: a máquina de estados da aplicação entra no
estado de boot no frame 1 e **nunca transita**. É a parede [G]. A sonda `STATE-DISPATCH`
já existe; o que falta é saber que condição o estado `0x0052F3F0` espera para avançar
(um load concluído? um input? o fim de uma sequência?) — e se a resposta é uma das coisas
que o E86 mantém artificialmente aberta.

## O fix de raiz — o que procurar

Entre os passos #001–#037 do B71 (ou antes dele), o console faz um push nos gestores que
sobrevive ao `FUN_00041494` do #038. Candidatos, por ordem de leitura:

1. `FUN_000415f0(uVar9)` e `FUN_000411ec` ×2, dentro do próprio #038 antes de
   `FUN_00041494` — o E77 P1 já notou que `FUN_000415f0` usa as duas convenções de índice;
2. o `while (FUN_002ba76c() != 0)` do #038 — um laço de espera que os nossos atalhos de
   loader (FIOS sticky / play-already / done-cancel-yield) podem estar a encurtar;
3. os passos #003–#006 (`func_002B7644`, `func_0004476C`, `func_000CE0A0`,
   `func_00040090`), onde os gestores são criados — se a criação no console empurra um
   produto-raiz e aqui não.

O método é o mesmo: uma sonda de push/pop por passo, e o oráculo RPCS3 para o cursor de
`0x400D6DA0` à entrada do #042.
