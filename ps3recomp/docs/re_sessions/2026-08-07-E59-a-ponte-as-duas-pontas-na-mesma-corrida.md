# E59 — a ponte: as duas pontas da cadeia, na mesma corrida, adjacentes

> **COBERTURA E ALCANCE CORRIGIDOS em 2026-08-08 pelo [E74](2026-08-08-E74-a-cadeia-nao-e-o-bloqueador.md):**
> a sonda deste ledger cobria **um** sitio (`func_0024E3D0`); o padrao existe em
> **sete**. Instrumentados os sete: **1441 escritas do indice de classe, 6 com
> produto nulo** -- 0,4%, nao a regra. E saltar as seis nao move o boot (degrau
> maximo 32, loop a 107 voltas, `st620`=11, iguais), logo a cadeia **nao e' o
> bloqueador**.
> **SOBREVIVE:** as 4 vitimas medidas, a ponte do `0x40780850` entre as duas
> pontas, e a ordem causa-efeito nas linhas adjacentes.
> **CAI:** «o sitio nunca acerta» como retrato do sistema.

**Data:** 2026-08-07 · **Antecessor:** [E58](2026-08-07-E58-o-lookup-acerta-o-estrago-e-colateral.md)

Sonda nova (`patch_24e3d0_victim_probe.py`) no sítio da escrita que estraga.
Imprime **sempre**, não só quando o produto é nulo — para se poder dizer
*proporção*, e não só *aconteceu*.

---

## As vítimas

```
14140 [VICTIM] vitima=0x40780850 produto=0x00000000 valor=0x0000 PRODUTO-NULO  ra1=func_0010F5E8+0x27C ra2=func_000B71B8+0x53DC
14144 [VICTIM] vitima=0x40780C20 produto=0x00000000 valor=0x0000 PRODUTO-NULO  ra1=func_0010F5E8+0x27C ra2=func_000B71B8+0x53DC
14205 [VICTIM] vitima=0x40781018 produto=0x00000000 valor=0x0000 PRODUTO-NULO  ra1=func_000D3DD8+0x750 ra2=func_000D3FAC+0x138
14209 [VICTIM] vitima=0x407810E0 produto=0x00000000 valor=0x0000 PRODUTO-NULO  ra1=func_000D3DD8+0x750 ra2=func_000D3FAC+0x138
```

**`0x40780850` é exactamente o objecto que o [E41](2026-08-07-E41-convergencia-com-01-08.md)
mediu partido** — aquele cujos oito escritores o E41 enumerou, e onde
`func_0024E3D0` escreveu `0` em `0x40780856` (= `0x40780850 + 6`).

As duas pontas de nove níveis, que até agora eram dois conjuntos de medições
sem ponte directa, estão agora ligadas **numa só corrida, em linhas adjacentes**.

## O sítio nunca acerta

Quatro escritas no sítio, **quatro com produto nulo**. Como a sonda imprime
sempre, isto é uma proporção medida e não uma amostra dos casos maus:
`func_0024E3D0` **não escreveu um único índice de classe válido nesta corrida**.

## Causa e efeito, intercalados

```
14138 [EMPTYFAB] fab=0x400C6210 cursor=-1 VAZIO
14140 [VICTIM]   vitima=0x40780850  PRODUTO-NULO
14142 [EMPTYFAB] fab=0x403008E8 cursor=-1 VAZIO
14144 [VICTIM]   vitima=0x40780C20  PRODUTO-NULO
14146 [vm] UNCOMMITTED read32 0x726D5D2E  ra=func_00263554+0x56C
```

Fábrica vazia → vítima; fábrica vazia → vítima; e duas linhas depois o
`func_00263554` a ler texto como endereço.

## Dois caminhos, não um

| vítimas | `ra2` | quem |
|---|---|---|
| `0x40780850`, `0x40780C20` | `func_000B71B8+0x53DC` | B71 #41 (`goGrapplePtActive`) |
| `0x40781018`, `0x407810E0` | `func_000D3FAC+0x138` | outro chamador, ainda por identificar |

O B71 causa metade. A outra metade vem de `func_000D3FAC`, que já tinha
aparecido no E56 como o `ra4` da terceira falha e continua sem nome funcional.

## A cadeia, de ponta a ponta, toda medida

```
func_0041F924    fecha os escopos das fabricas por-tag      (E53)
func_000B71B8    B71 #41 pede "goGrapplePtActive"           (E56/E57)
  func_0024F24C    nome -> id; devolve 0x4077AC10 (ACERTA)  (E58)
    func_0024F028    ramo normal                             (E53)
      FUN_0024e198     por elemento, pede a' fabrica[tag]
        func_0039E5A8    cursor=-1 -> NULL                   (E45/E51)
      func_0024E3D0    *(0x40780850 + 6) = *(NULL+0x20) = 0  (E59, aqui)
        func_00263554    "rm_." como endereco                (E25/E26)
          func_00220284    16 escritas UNCOMMITTED           (E8)
```

## Refutado por medição

1. **«a vítima do E41 e a causa desta cadeia podem ser objectos diferentes»** —
   é o mesmo `0x40780850`, medido nas duas pontas na mesma corrida.
2. **«o sítio às vezes acerta e às vezes não»** — 4 escritas, 4 com produto
   nulo, com a sonda a imprimir sempre.
3. **«a cadeia é toda do B71»** — metade das vítimas vem de `func_000D3FAC`.
4. Tudo o que E11–E17 e E19–E58 já refutaram.

## Próximo

1. **`func_000D3FAC`** — o segundo caminho, responsável por metade das vítimas;
2. quem chama `func_0041F924`, o fecho dos escopos (precisa de `ra3` na sonda
   `SCOPE`) — continua a ser o candidato a fix;
3. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
