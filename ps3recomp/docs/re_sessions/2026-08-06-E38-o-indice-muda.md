# E38 — parede quebrada: o índice de classe muda durante a corrida

**Data:** 2026-08-06 · **Antecessor:** [E37](2026-08-06-E37-inconclusivo-e-divida.md)

Três corridas sãs (o E37 foi flake e está declarado). `IDXBOUND=1314`,
`MGRLOOKUP=238` nas três — determinístico.

---

## A medição

A chave má é consultada **10 vezes**, e não dá sempre o mesmo:

```
7x  key=0x40780850  idx=2  ea=0x400C6950  -> entry=0x406387E4   (o gestor SAO)
3x  key=0x40780850  idx=0  ea=0x400C6948  -> entry=0x400C6B54   (o mau)
```

Distribuição de todos os 1314 índices: **1264× `2`, 49× `0`, 1× `24444`**.

## O que isto refuta

O [E36](2026-08-06-E36-indice-sem-limite.md) propôs *«índice fora de alcance, lê
para lá do array»*. **Refutado para este caso:**

- `idx=0` está **dentro** do alcance — `ea=0x400C6948` é a mesma célula que
  outras chaves usam com sucesso (`#19 key=0x42F85594`, `#38 key=0x406387E4`
  recebem a mesma `entry=0x400C6B54`);
- existe de facto **um** índice absurdo na corrida (`24444`, uma vez), mas é
  **outra chamada**, não esta.

## O que fica estabelecido

**O mesmo objecto lê índices de classe diferentes ao longo da corrida.**
`*(0x40780850 + 6)` vale `2` em sete consultas e `0` em três. O campo muda — ou
é lido antes de estar posto.

E os dois índices levam a sítios diferentes:

| `idx` | célula | entrada | gestor resultante |
|---|---|---|---|
| 2 | `0x400C6950` | `0x406387E4` | `0x406387E0` — **funciona** |
| 0 | `0x400C6948` | `0x400C6B54` | `0x400C6B50` — **não é gestor** (E34) |

Logo: **o slot 0 da tabela de tipos rende um objecto que não é um gestor
utilizável**, e qualquer consulta cujo índice caia em 0 apanha-o.

## A cadeia, na forma final

```
objecto 0x40780850: *(+6) le' 0 em vez de 2, em 3 de 10 consultas   <- RAIZ
func_0039D3C4   slot 0 -> entry 0x400C6B54 -> devolve 0x400C6B50
                (slot legitimo; o problema e' o que la' esta')
func_00212ED0   le'-o como gestor; pool = *(+0xD0) = 0 (nunca escrito, E34)
func_00263554   endereco 0 como pool; *(4) = "/r_p" de "/wad/r_perma.wad_ps3"
... ate' as 16 escritas UNCOMMITTED em func_00220284
```

## As duas perguntas que restam

1. **Porque é que `*(0x40780850 + 6)` lê 0 em três consultas e 2 em sete?**
   Ou o campo é escrito tarde (e as três primeiras apanham-no a zero), ou é
   corrompido. A ordem no log decide: as `idx=0` são as #1309–#1311, **as
   últimas** — não as primeiras. Isso favorece **corrupção**, não inicialização
   tardia. *Não provado.*
2. **O slot 0 devia conter um gestor?** 49 consultas usam `idx=0`. Se o slot 0
   for legitimamente "sem gestor", o defeito é o chamador não verificar; se
   devia ter um, o defeito é quem o preenche.

## Refutado por medição

1. **«índice fora de alcance»** (E36) — `idx=0` está dentro; o índice absurdo
   (24444) é outra chamada.
2. **«a chave não está registada»** (E35) — está: rende `idx=2` em 7 de 10.
3. **«o lookup devolve lixo»** (E35) — devolve uma entrada legítima do slot 0.
4. Tudo o que E11–E17 e E19–E37 já refutaram.

## Nota

O `idx=24444` numa consulta é um segundo defeito, independente, ainda não
investigado. Fica anotado para não se perder — um índice desses lê seguramente
fora do array.
