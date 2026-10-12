# E28 — a linha que não fecha: `pool=0` e `head=texto` não podem ser ambos

> **ARGUMENTO INVALIDADO em 2026-08-05 pelo [E29](2026-08-05-E29-a-string-esta-no-endereco-zero.md).**
> A linha da FLHEAD (`pool=0`, `head=0x2F725F70`) estava CERTA. O que nao fecha era o meu
> raciocinio, apoiado no zero da vigia de traducao -- que da' falsos negativos. Medido
> depois: o pop consome `*(4)` e escreve-lhe 0 a seguir, e `*(0)` continua a valer `/wad`.


**Data:** 2026-08-05 · **Antecessor:** [E27](2026-08-05-E27-a-free-list-sem-terminador.md)

Resultado negativo, e é sobre a confiança no instrumento — não sobre o jogo.
**H-C cai, e a leitura "o pool é nulo" também não sobrevive.**

---

## O que a `FLHEAD` mostra

```
[FLHEAD] ok  pool=0x00000000  pool+4=0x00000004  head=0x2F725F70  lr=0x00212F4C
[FLHEAD] ok  pool=0x00000000  pool+4=0x00000004  head=0x00000000  lr=0x00227834
```

Dois pops com `pool=0` em **8625** — exactamente os dois sítios que a série
persegue. Parecia a resposta: o ponteiro do pool é nulo, o pop lê `*(4)`, e a
memória baixa tem lá texto.

## Porque não pode ser

`head = vm_read32(pool + 4)` com `pool = 0` é `vm_read32(4)`. E `vm_read32`
tem duas saídas possíveis:

- endereço não commitado → **devolve 0 sem traduzir**;
- endereço commitado → **traduz** (passa por `vm_translate`).

A vigia de tradução, com limite `0x1000` e **instrumento verificado**
(`strings` confirma que está no binário; controlo com limite alto dá 5 acertos
simbolizados), reporta **zero traduções abaixo de `0x1000`** na mesma corrida.

Logo `vm_read32(4)` nunca traduziu → devolveu 0 → `head` seria **0**.

A segunda linha (`lr=0x00227834`) é consistente com isso: `head=0`.
**A primeira não é.** `pool=0` e `head=0x2F725F70` não podem ser os dois
verdadeiros.

## A causa: 16 sondas, uma leitura

Há **16 marcadores `FLHEAD`** no lift. Eu li o código de *um* sítio e atribuí
esse contexto de registos às linhas impressas por *todos*. É o mesmo defeito que
o [E22](2026-08-05-E22-a-sonda-que-recalculava.md) apanhou na sonda `TOCCHAIN` —
com a diferença de que a `TOCCHAIN` era minha e esta é herdada.

**Uma sonda reutilizada traz a ambiguidade de todos os sítios onde foi
instalada.** O rótulo `pool=` significa "`gpr[31]` neste sítio", e `gpr[31]` não
é o pool em todos os dezasseis.

## O que continua sólido

Só o que foi **capturado directamente**:

| medição | estado |
|---|---|
| `ALLOC878 #237` devolve `0x2F725F70` de `func_00212ED0` | **sólido** — captura do valor de retorno, determinística em 3 corridas |
| 11 alocações anteriores do mesmo alocador devolvem heap são, passo `0x34` | **sólido** |
| nenhuma tradução abaixo de `0x1000` | **sólido**, com controlo |
| `pool=0` / `head=texto` da `FLHEAD` | **não utilizável** |

E disso decorre uma conclusão firme: **o texto não vem da memória baixa.** Se
viesse, a tradução teria aparecido. Entra na cadeia noutro sítio.

## Refutado

1. **H-C (terminador não zerado)** — assentava na leitura da `FLHEAD`.
2. **«o pool é nulo e lê-se `*(4)`»** — mesma origem, e incompatível com a vigia.
3. **[E15](2026-08-05-E15-a-string-no-endereco-4.md) revisitado** — continua
   refutado, e agora por duas vias independentes.

## Próximo (E29)

1. **Sonda própria em `func_00263554`**: capturar ao vivo, à entrada, o `r3`
   recebido, e à saída o `r3` devolvido — sem depender de nenhum marcador
   herdado. É a única forma de saber o que o pop recebe e devolve nesta cadeia.
2. Com isso, decidir se o valor entra pelo argumento (pool mau) ou é produzido
   lá dentro.
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.

## Nota de método

Duas vezes na série uma sonda me deu um par de campos que não fechava — e as
duas vezes o que a apanhou foi um **número que tinha de aparecer e não
apareceu**: no E22, 13 leituras de uma célula; aqui, uma tradução do endereço 4.

> Regra: **antes de usar uma sonda herdada, contar quantas instâncias dela
> existem.** Se for mais do que uma, os rótulos dos campos são promessas sobre
> um sítio, não sobre a linha que se está a ler.

E o corolário prático: quando dois campos da mesma linha implicam coisas
incompatíveis, **não escolher o mais conveniente** — descartar a linha e medir
outra vez com instrumento próprio.
