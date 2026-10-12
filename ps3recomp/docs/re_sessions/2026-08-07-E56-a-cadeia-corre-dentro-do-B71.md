# E56 — a cadeia corre dentro do B71, no lookup do registry de tipos

**Data:** 2026-08-07 · **Antecessor:** [E55](2026-08-07-E55-o-instrumento-que-perdia-campos.md)

Sonda `EMPTYFAB` v4: mais dois níveis de pilha (`ra3`/`ra4`), impressos **só nas
falhas**, para não poluir as 253 leituras boas. Responde à pergunta que o E53
deixou como a única entre a medição e a causa.

---

## A pilha completa das duas falhas que causam o sintoma

```
func_000B71B8 + 0x53DC          <- ra4
  func_0010F5E8 + 0x27C         <- ra3
    func_0024F028 + 0x8F8       <- ra2
      func_0024E3D0 + 0x444     <- ra1
        func_0039E5A8           -> cursor=-1, devolve 0
```

Idêntica para `fab=0x400C6210` e `fab=0x403008E8` — as duas cujo nulo é seguido,
duas linhas depois, pelo `func_00263554` e pelas 16 escritas.

## A convergência

Estes dois nomes não são novos neste projecto. Estão escritos no cabeçalho do
`patch_218340_pool_index_probe.py`, da cadeia medida a **2026-08-01**:

```
main() func_0025C838
  -> func_002B2E74                 (7a de 9 chamadas, em linha recta)
       -> func_000B71B8 = B71      (9a de 11)
            -> #41 func_0010F5E8   lookup no registry de tipos
```

**`func_000B71B8` é o B71** — a sequência de boot pós-intro que este projecto
persegue desde a leva 10. **`func_0010F5E8` é o lookup no registry de tipos**,
a 41.ª chamada dele.

A cadeia das fábricas vazias não é um caminho lateral: corre **dentro** do B71,
no passo do registry de tipos — a parede [D] do `CLAUDE.md`, o *nested walk*
natural do typemap.

Duas investigações separadas por seis dias, partindo de pontas opostas, no mesmo
sítio. É a segunda vez que isto acontece nesta cadeia (a primeira foi o E41).

## As outras quatro pilhas

| fábrica | pilha (ra4 → ra1) | tipo |
|---|---|---|
| `0x40300E80` | `func_000D3FAC` → `func_000D3DD8` → `func_0024F028` → `func_0024E3D0` | deref, mas depois do sintoma |
| `0x400D6DA0` | `func_000B71B8+0x6B6C` → `func_000B70B0` → `func_00237CDC` → `func_00264BA8` | rota latente (E52) |
| `0x400C6210` | `func_00228A6C` → `func_0039F0F8` | por desenho (E52) |
| `0x401002F0` | `func_002A98E8` → `func_0039F0F8` | por desenho (E52) |

**O B71 aparece em três das seis** — as duas da cadeia e a da rota latente,
esta última por outro ramo (`+0x6B6C` contra `+0x53DC`).

## O que fica montado

```
func_000B71B8   sequencia de boot pos-intro (B71)
  func_0010F5E8   lookup no registry de tipos (#41)
    func_0024F028   le' o SEU gestor (nao vazio), ramo normal
      FUN_0024e198    travessia; por elemento pede a' fabrica[tag]
        func_0039E5A8   cursor=-1 -> devolve NULL
      func_0024E3D0   *(NULL + 0x20) = 0  -> indice de classe a zero
        func_00263554   ... "rm_." como endereco
          func_00220284   16 escritas UNCOMMITTED
```

E, quinze linhas antes de tudo isto, `func_0041F924` a fechar os escopos
das fábricas por-tag (E53).

## O que isto ainda NÃO diz

Cuidado com o salto fácil. **Não está medido** que o desmonte de
`func_0041F924` seja prematuro, nem que o B71 devesse ter um escopo aberto
aqui. O que está medido é a ordem: fecha, depois pede.

As duas leituras continuam abertas, e agora com nomes:

1. `func_0041F924` fecha cedo demais — e o B71 devia poder pedir depois;
2. o B71 chega a este passo fora da fase em que os escopos existem.

## Refutado por medição

1. **«a cadeia é um caminho lateral, sem relação com o boot»** — corre dentro
   do B71, no lookup do registry de tipos.
2. **«o `ra2=func_0024F028` era o topo da história»** — há pelo menos mais dois
   níveis, e são os que ligam à parede antiga.
3. Tudo o que E11–E17 e E19–E55 já refutaram.

## Próximo

1. **`func_0010F5E8+0x27C`** — o que a 41.ª chamada do B71 pede, e se o
   registry de tipos devia estar a ser percorrido com os escopos fechados;
2. **`func_0041F924`**: quem o chama, e se o fecho é fim-de-fase legítimo;
3. cruzar com a parede [D] do `CLAUDE.md` (nested walk do typemap) — pode ser a
   mesma parede vista pelo outro lado;
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
