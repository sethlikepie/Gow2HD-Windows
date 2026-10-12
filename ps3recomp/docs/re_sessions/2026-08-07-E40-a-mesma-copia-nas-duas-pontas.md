# E40 — a mesma cópia de 16 bytes nas duas pontas da cadeia

> **BASE RETIRADA em 2026-08-07 pelo [E41](2026-08-07-E41-convergencia-com-01-08.md).**
> Este ledger assume que `func_0021973C` escreve em `0x40780854` -- o float do
> [E39](2026-08-06-E39-um-float-no-cabecalho.md), que o E41 declarou **nao reproduzido**.
> Na corrida de gama do E41 (16 bytes, tecto 100000, 8 escritores) `func_0021973C`
> **nao aparece**: `0x40780854` e' escrito com `0x00000020` por `func_00210720`.
> **SOBREVIVE:** a leitura estatica de `func_0021973C` (e' mesmo uma copia de 4 floats,
> 16 bytes) e a leitura de `func_00220284` na outra ponta -- ambas independentes da corrida.
> **CAI:** a identificacao de `func_0021973C` como origem destes bytes, a simetria
> «a mesma operacao nas duas pontas», e a inferencia «o defeito e' de tipo, nas duas pontas».
> A origem real esta' medida no E41 (`func_00210720` escreve, `func_0024E3D0` poe 0 no `+6`).
> Mantido como registo -- nao construir sobre isto.


**Data:** 2026-08-07 · **Antecessor:** [E39](2026-08-06-E39-um-float-no-cabecalho.md)

Leitura de `func_0021973C` (82 linhas). Fecha a forma da cadeia.

---

## O que a origem faz

```c
{ float ftmp = ctx->fpr[0];  ... vm_write32(ctx->gpr[11] + 0x0, tmp); }
{ float ftmp = ctx->fpr[12]; ... vm_write32(ctx->gpr[11] + 0x4, tmp); }
{ float ftmp = ctx->fpr[11]; ... vm_write32(ctx->gpr[11] + 0x8, tmp); }
{ float ftmp = ctx->fpr[13]; ... vm_write32(ctx->gpr[11] + 0xC, tmp); }
```

Repetido para várias bases (`gpr[11]`, `gpr[8]`, `gpr[7]`, …). É uma **cópia de
4 floats, 16 bytes** — um vector ou linha de matriz.

Com base `0x40780850`, o `+0x4` é `0x40780854` — exactamente a escrita que a
vigia apanhou (`0x3FB2B5BF`, E39).

## O fecho: a mesma operação nas duas pontas

| ponta | função | operação |
|---|---|---|
| **origem** | `func_0021973C` | escreve 4 floats, 16 bytes |
| **vítima** | `func_00220284` | copia 4 floats, 16 bytes, stride `0x10` |

A cadeia que o E8 abriu com 16 escritas em memória não commitada acaba por
começar com **a mesma forma de operação**: uma cópia de vector.

Entre as duas, nove níveis em que essa memória é lida como objecto tipado, como
gestor, como pool e como cabeça de free-list.

**O defeito é de tipo, e está nas duas pontas:** uma parte do código trata
`0x40780850` como vector de floats; outra dereferencia `+6` como índice de
classe. Ambas não podem estar certas.

## O que continua por provar

1. **Qual das duas leituras é a legítima.** 237 de 238 consultas usam chaves da
   mesma forma e funcionam — o que sugere que a estrutura *tem* um índice em
   `+6` e que este objecto é que não devia lá estar. Não verificado.
2. **A ponte numérica do E39** — o halfword do float seria `0xB5BF`, e os
   índices medidos são `0`, `2`, `24444`. Continua sem explicação.
3. `idx=24444` e o flake `FIOSOPEN=14` — anotados desde o E38 e o E33.

## Onde paro, e porquê

Não é uma parede técnica: é o **contexto desta sessão**. A cadeia está
documentada em treze ledgers hoje (E25–E40) e o passo seguinte é claro e
pequeno — uma corrida com tecto alto e todas as larguras para apanhar *todos*
os escritores de `0x40780850+0..0xF`, e ler quem chama `func_0021973C` com essa
base.

Fica tudo em disco: sondas idempotentes e gated OFF, a vigia `PS3_WATCH_W32`
com sobreposição nas quatro larguras, e as listas de refutados em cada ledger.

## Refutado por medição

1. **«a escrita do float é acidental»** — é uma cópia estruturada de 4 floats,
   idêntica em 2 corridas, de uma função identificada.
2. Tudo o que E11–E17 e E19–E39 já refutaram.
