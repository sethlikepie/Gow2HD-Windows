# Parede 1 — duas classes estruturalmente diferentes declaram o mesmo tag

Data: 2026-08-02. Atacada como plano isolado (Fase 11, parede 1 de 4).

## O sintoma

`func_002545B0(this, arg)` lê `arg+0x7C` como sentinela de uma lista circular
intrusiva. O teste de vazio do jogo é `head == sentinela`; mede-se `head = 0`, que
nunca o satisfaz, e o walk entra em ciclo até o breaker dos 2000 despachos matar
o processo com `exit(3)`.

## O que já estava provado

- `arg+0x7C` **é `matriz[0][3]`** — provado desmontando o construtor
  `func_0024C1F8` (33 instruções, sem ramos, matriz identidade em
  `+0x70/+0x80/+0x90/+0xA0`).
- O registry **está saudável** — 26 entradas, tags 0..32, todas com fábrica
  válida e vtable viva.
- O objecto **está bem construído** para o que é.

Faltava explicar porque é que um objecto com matriz chega a um método de lista.

## A causa, medida

Existem **duas famílias** de objectos, distinguidas pela palavra 0:

| família | `w0` | bytes (BE) | metade alta | `*(uint16*)(obj+2)` | índice do registry |
|---|---|---|---|---|---|
| listas | `0xC0010001` | `C0 01 00 01` | `0xC001` | **1** | `0x4` |
| registos WAD | `0x40030001` | `40 03 00 01` | `0x4003` | **1** | `0x4` |

O walker do WAD calcula o índice assim:

```c
r0  = *(uint16*)(obj + 2);            // <- só a METADE BAIXA do cabeçalho
idx = (r0 << 2) & 0x3FFFC;
fab = *(tab + idx);                    // tab = 0x00868D48
fab->vt[0x18](fab, obj);
```

**As duas famílias dão `*(obj+2) = 1`.** A metade alta — `0xC001` contra `0x4003`,
que é o que de facto as distingue — **não entra no índice**.

Logo os registos WAD são despachados para a fábrica do tag 1, cujos métodos
assumem o layout da família de listas: contador em `+0x78`, lista em `+0x80`
(base `arg-4`). Num registo WAD esses offsets caem dentro da matriz.

## O que isto não é

- Não é o registry vazio (está populado, medido).
- Não é corrupção (o construtor está completo e correcto).
- Não é bug do lifter (quatro suspeitas, quatro refutadas contra o binário).
- Não é um desalinhamento de 4 bytes (mesmo com `obj+4`, os campos caem na matriz).

## A pergunta que fecha a parede 1

> **A metade alta do cabeçalho devia participar na identidade do tipo?**

Duas leituras, e distinguem-se lendo mais chamadores do mesmo `tab`:

1. **Sim** — e então o índice devia usar mais bits, e ler só `*(obj+2)` é um
   defeito de tradução ou uma leitura errada do campo. *(Nota: a fórmula
   `(t<<2)&0x3FFFC` foi verificada contra o binário e o lift concorda — se for
   isto, o defeito está no que escreve o cabeçalho, não em quem o lê.)*
2. **Não** — e então uma das duas famílias não devia estar registada com tag 1,
   e o defeito está em quem a regista.

A #2 é a mais provável, e tem uma medição directa: a lista de produtos do tag 1
**mistura as duas famílias** — medido no walk exterior:

```
[OUTER] #1 no=0x40638594  (w0=0xC0010001, família de listas)
[OUTER] #2 no=0x4077AD28  (w0=0x40030001, registo WAD)
```

O mesmo contentor guarda objectos dos dois tipos. Quem os põe lá é o `push` de
`func_0041F700` (`cursor++; array[cursor] = nó`) — que é a **parede 4**.

## Consequência para a ordem de ataque

As paredes 1 e 4 são **a mesma raiz vista de dois lados**: a 4 popula a lista
misturando famílias, a 1 rebenta ao consumi-la. Atacar a 1 isoladamente é tratar
o sintoma.

**Recomendação: a parede 4 passa a ser a primeira.** As 1 e 3 (mesmo padrão)
devem cair com ela; a 2 (tabela de pools) é independente e fica para depois.
