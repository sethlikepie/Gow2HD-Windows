# E43 — o cursor nunca passa abaixo de zero: o pop tem guard, e −1 é o vazio

> **MECANISMO RETRACTADO em 2026-08-07 pelo [E54](2026-08-07-E54-o-pop-negativo-escreve-retracto-o-mecanismo-do-E43.md).**
> A «Refutacao 2» abaixo esta' errada: o salto para `func_0039DAF4` **nao** e' um
> guard que aborta -- e' um desvio para outro fragmento que faz
> `vm_write8(cursor-1)` e devolve 0. O pop com cursor negativo **escreve**, logo
> underflow abaixo de −1 e' possivel.
> **SOBREVIVE:** a Refutacao 1 (a aritmetica da tabela do E42, 176 escritas de
> cada lado) e a CONCLUSAO -- nao houve underflow. A vigia `PS3_WATCH_STORE`
> vive no `heap_w()` e apanha todas as larguras de qualquer sitio: em duas
> corridas, `func_0039DAF4` escreveu **0** vezes em `0x400C62D8` e os valores
> nunca desceram abaixo de `0xFF`. Nao houve underflow porque o pop nunca correu
> com o cursor ja' negativo -- nao porque fosse impossivel.
> **CAI:** «sai sem escrever» e «estruturalmente impossivel».

**Data:** 2026-08-07 · **Antecessor:** [E42](2026-08-07-E42-o-cursor-passa-abaixo-de-zero.md)

Auditoria de retratação sobre os ledgers de hoje. **O E42 é refutado pela sua
própria tabela e pelo código liftado.** Não há pops a mais, e o contador não
pode passar abaixo de zero — a função que decrementa impede-o explicitamente.

---

## Refutação 1 — a aritmética do E42 contradiz a tabela do E42

A tabela do E42 dá 353 escritas em `0x400C62D8`:

| escritas | valor | sítio |
|---|---|---|
| 165 | `0x01` | `func_0039DA78` |
| **11** | **`0x00`** | **`func_0039DA78`** ← esquecida na conta |
| 165 | `0x00` | `func_0039DAB0` |
| 11 | `0xFF` | `func_0039DAB0` |
| 1 | `0xFF` | `func_002B11B8` (a primeira da corrida) |

**`func_0039DA78` escreve 176 vezes. `func_0039DAB0` escreve 176 vezes.**
Estão equilibradas.

O E42 concluiu *«165 pushes contra 176 pops é uma diferença de exactamente 11»* —
contando o pop pelas duas linhas dele e o push só por uma. As 11 escritas de
`0x00` pelo push estão na tabela do próprio E42, linha 17, e caíram da conta.

## Refutação 2 — o pop recusa-se a descer abaixo de −1

`func_0039DAB0`, lida no lift (`recomp_macos_v2/ppu_recomp_001.cpp`):

```c
ctx->gpr[9]  = vm_read8(ctx->gpr[3] + 0x80);          // base+0x48+0x80 = base+0xC8
ctx->gpr[0]  = (int64_t)(int8_t)ctx->gpr[9];          // sinal
ctx->gpr[10] = ctx->gpr[9] + (int64_t)(-1);
{ ... cr_val = (a < b) ? 8 : ... }                     // compara com 0
if (((ctx->cr >> 0) & 8)) { g_trampoline_fn = func_0039DAF4; return; }   // < 0 -> SAI SEM ESCREVER
ctx->gpr[11] = vm_read32(...);
vm_write8(ctx->gpr[3] + 0x80, ctx->gpr[10]);          // só aqui decrementa
```

**Se o cursor já é negativo, o pop sai antes do `vm_write8`.** Underflow é
estruturalmente impossível. As 11 escritas de `0xFF` são transições legítimas
`0 → −1`, e nenhuma sequência de pops consegue produzir `−2`.

E o push, `func_0039DA78`, é o incremento simétrico:

```c
ctx->gpr[9] = vm_read8(ctx->gpr[3] + 0x80);
ctx->gpr[9] = ctx->gpr[9] + (int64_t)(1);
vm_write8(ctx->gpr[3] + 0x80, ctx->gpr[9]);
```

As 11 escritas de `0x00` pelo push são `−1 → 0`. O par fecha.

## O que a medição afinal diz

O contador oscila sobre `{−1, 0, 1}` com **`−1` = pilha vazia**, posto uma vez
no arranque por `func_002B11B8` e reposto sempre que a pilha esvazia. Não é
corrupção; é o estado de repouso da estrutura, e o pop conhece-o.

O argumento do E42 — *«se −1 fosse um estado normal de pilha vazia, o consumidor
teria de o verificar, e não verifica»* — cai: `func_0039DAB0` **verifica**.
Quem não verifica é `func_0039E5A8` (o `vt[0x48]`), e é aí que a pergunta agora
vive.

## A pergunta que sobrevive, com outra forma

Não é *«quem faz pops a mais»* — ninguém faz. É:

> **Porque é que `func_0039E5A8` é chamada com a pilha vazia (`−1`)?**

É um problema de ordem/ciclo de vida — alguém pede produto à fábrica antes de
haver produto — não um problema de contagem. Um push em falta por fallthrough
(o padrão do `2550C8`) deixa de ser candidato: as contagens fecham.

## O que sobrevive do E42

- as 353 escritas e a distribuição por sítio e valor;
- os dois sítios alternantes e a identificação como push/pop (confirmada agora
  pelo lift, não só pelos valores);
- `0xFF` = −1 confirmado por medição directa, convergindo com a nota de
  2026-08-01 — e **mais forte** do que o E42 pensava: é o estado de arranque,
  não um acidente;
- a cadeia inteira **abaixo** do cursor (de `func_0039E5A8` até `func_00220284`),
  que o E41 mediu e este ledger não toca.

## O que cai

- *«11 pops sem push»* e o enquadramento de desequilíbrio;
- *«o cursor passa abaixo de zero»* — não passa, e não pode;
- *«165 pushes contra 176 pops»*;
- o passo seguinte nº2 do E42 (procurar um push saltado por um ramo).

## Herdado, não remedido

`func_0039E5A8` devolver produto nulo com cursor negativo vem da nota de
2026-08-01, não de medição desta cadeia. Continua por confirmar com sonda
própria — e é agora o primeiro passo, não o segundo.

## Refutado por medição

1. **«o contador tem underflow»** — o decremento tem guard de sinal; sai sem
   escrever quando já é negativo.
2. **«há 11 pops sem push»** — 176 escritas de cada lado, na tabela do E42.
3. Tudo o que E11–E17 e E19–E41 já refutaram.

## Próximo

1. sonda própria em `func_0039E5A8`: cursor lido, produto devolvido, e o `ra` de
   quem chama — porque é que se pede produto com a pilha vazia;
2. quem chama `func_0039DAB0` na 11.ª vez que esvazia, e se a chamada a
   `vt[0x48]` cai nessa janela;
3. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ambos ainda por explicar.
