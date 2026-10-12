# E39 — parede quebrada: um float escrito por cima do índice de classe

> **NAO REPRODUZIDO em 2026-08-07 pelo [E41](2026-08-07-E41-convergencia-com-01-08.md).**
> A escrita de float (`0x3FB2B5BF`, `func_0021973C`) **nao aparece** numa medicao mais
> completa (gama de 16 bytes, tecto 100000, 8 escritores apanhados contra 2 aqui): o
> `0x40780854` leva `0x00000020` de `func_00210720`. A causa do `idx=0` e' outra --
> `func_0024E3D0` escreve 0 no `+6`.
> **SOBREVIVE:** a extensao da vigia as quatro larguras e a teste de sobreposicao, que foi
> o que tornou as medicoes seguintes possiveis. **CAI:** o float como elo da cadeia.


**Data:** 2026-08-06 · **Antecessor:** [E38](2026-08-06-E38-o-indice-muda.md)

Vigia de escrita estendida às quatro larguras (8/16/32/64) e a testar
**sobreposição**, não igualdade — porque o alvo é um halfword em `+6` e uma
escrita de word em `+4` também o apaga.

---

## A medição

Duas corridas, resultado idêntico:

```
[W32] write 0x40780854 = 0x3FB2B5BF  ra=func_0021973C+0x730
```

**Duas escritas por corrida, sempre a mesma origem e o mesmo valor.**

`0x3FB2B5BF` é um **float IEEE-754** (≈ 1,396). O word em `0x40780854` cobre os
bytes `+4..+7` do objecto `0x40780850` — **incluindo o halfword em `+6`, que é
o índice de classe** que `func_0039D3C4` usa para indexar a tabela de tipos.

## O mecanismo

`func_0021973C` escreve floats naquela memória; o lookup lê-a como objecto
tipado. **A mesma memória é usada com dois tipos** — a doença que atravessa
toda esta série, agora na origem da cadeia.

É por isso que o E38 mediu o índice a mudar: não é um campo corrompido por
acidente, é um campo que **partilha bytes com dados de vírgula flutuante**.

## O que ainda não fecha, e digo-o

O halfword em `+6` deste valor seria `0xB5BF` (46527). O E38 mediu índices
`0`, `2` e `24444` — **nenhum é 46527**. Portanto:

- a escrita do float **sobrepõe-se** ao campo, o que está medido;
- mas os índices observados **não** vêm directamente deste valor.

Leituras possíveis, nenhuma verificada: o objecto é reescrito depois; há mais
escritores que a vigia não apanhou nas duas corridas (cap 100, só 2 acertos);
ou o campo lido não é exactamente `+6` do mesmo objecto em todas as consultas.

**Não invento a ponte.** O que está provado é a sobreposição e a origem.

## A cadeia, do topo ao sintoma

```
func_0021973C   escreve um float em 0x40780854 (+4..+7 do objecto)   <- ORIGEM
                sobrepoe-se ao halfword +6, o indice de classe
func_0039D3C4   indice muda (0 / 2 medidos no E38)
                slot 0 -> 0x400C6B54 -> devolve 0x400C6B50
func_00212ED0   le'-o como gestor; pool = *(+0xD0) = 0
func_00263554   endereco 0 como pool; *(4) = "/r_p"
... ate' as 16 escritas UNCOMMITTED em func_00220284
```

## Refutado por medição

1. **«o campo é corrompido por acidente»** — é escrito de propósito, por uma
   função identificada, com um valor de vírgula flutuante.
2. Tudo o que E11–E17 e E19–E38 já refutaram.

## Próximo

1. **Ler `func_0021973C`** e perceber o que julga estar a escrever — se trata o
   objecto como vector/matriz, o defeito é de tipo; se escreve através de um
   ponteiro calculado, é mais um deref mau.
2. Fechar a ponte que falta: correr com cap alto e todas as larguras para ver
   **todos** os escritores de `0x40780850+4..+7` numa corrida.
3. O `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33) continuam por explicar.
