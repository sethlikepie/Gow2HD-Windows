# E41 — a raiz, e uma convergência com uma investigação de há seis dias

**Data:** 2026-08-07 · **Antecessor:** [E40](2026-08-07-E40-a-mesma-copia-nas-duas-pontas.md)

Vigia estendida a **gama** (`PS3_WATCH_W32_LEN`), tecto alto, corrida sã.
**Todos os escritores dos 16 bytes do objecto, numa corrida.**

---

## Os oito escritores

```
0x40780850 = 0x40000003   func_00210720+0x244
0x40780854 = 0x00000020   func_00210720+0x260
0x40780856 = 0x00000002   func_00210720+0x340    <- indice = 2
0x40780858 = 0x00000000   func_00210720+0x298
0x4078085C = 0x00000000   func_00210720+0x27C
0x40780856 = 0x00000002   func_004117B0+0x54     <- reafirma 2
0x40780856 = 0x00000000   func_0024E3D0+0x50C    <- ESCREVE 0   *** RAIZ ***
0x40780850 = 0x4078088C   func_002635A4+0x690    <- push da free-list
```

`func_0024E3D0` escreve **0** no halfword `+6` — o índice de classe. É a causa
directa do `idx=0` medido no E38.

## A convergência

`recomp_mid_v2/patch_24e3d0_null_product_gate.py`, escrito a **2026-08-01**,
descreve este sítio exactamente:

```c
produto = fabrica->vt[0x48](fabrica);      // func_0039E5A8
*(uint16*)(objecto + 6) = *(produto + 0x20);
```

E mediu, na altura:

```
[FAB48] fab=0x400C6210 code=0x0039E5A8 cursor+C8=-1  <NEGATIVO>
[TYPEASSIGN] obj=0x4077ED10 regtipo=0x3 produto=0x00000000 tipo_escrito=0
```

Duas coincidências que não são coincidência:

- **`fab=0x400C6210`** é o mesmo objecto que a minha sonda `MGRLOOKUP` reportou
  como `tab=0x400C6210` nas 238 consultas;
- o texto diz *«o campo valia 2, posto por `func_00210720` e reafirmado por
  `func_004117B0`»* — **exactamente** os dois escritores que medi hoje, sem
  conhecer este documento.

**Duas investigações independentes, seis dias e sentidos opostos, no mesmo
sítio.** A de Agosto 1 partiu da fábrica; esta partiu do sintoma, dezasseis
ledgers a montante.

## A raiz verdadeira

Com `produto == 0`, o código lê `*(0x20)` — memória baixa do guest — e escreve
o que lá está (zero) como tipo.

E o documento de 2026-08-01 faz a observação que decide o rumo:

> *«Num PS3 real isso faria fault na página nula. Logo o jogo nunca pode tomar
> este caminho com produto nulo: a invariante dele é "o produto existe aqui".»*

Portanto **o defeito não está em `func_0024E3D0`** — está a montante: a fábrica
devolve produto nulo, e o `[FAB48]` mostra porquê — **`cursor+0xC8 = -1`**.

## Correcção ao E39

O E39 reportou uma escrita de float (`0x3FB2B5BF`, `func_0021973C`) nesses
bytes. **Nesta corrida não aparece** — o `0x40780854` é escrito com `0x00000020`
por `func_00210720`. As duas medições usaram tectos e gamas diferentes; não as
reconcilio, e a de hoje é a mais completa (gama de 16 bytes, tecto 100000, 8
acertos contra 2).

O float de E39 fica **não reproduzido**. A cadeia não depende dele.

## Sobre corrigir

Pedido: investigar **e corrigir**. A investigação chega ao fim; a correcção não
é minha para fazer aqui, e a razão está escrita no próprio patch de 2026-08-01:

- `patch_24e3d0_null_product_gate.py` é **explicitamente um gate de
  diagnóstico**, OFF por default, e o seu cabeçalho diz *«nunca deve ser
  apresentado como correcção»*;
- o fix legítimo é **porque é que `cursor+0xC8` vale -1** na fábrica
  `0x400C6210` — e isso é a próxima investigação, não uma linha a mudar.

Aplicar o gate agora mascarava a fábrica e destruía a evidência que dezasseis
ledgers construíram.

## A cadeia completa, do princípio ao fim

```
fabrica 0x400C6210: cursor +0xC8 = -1        <- A RAIZ (por investigar)
func_0039E5A8   devolve produto = NULL
func_0024E3D0   *(obj+6) = *(NULL+0x20) = 0  <- escreve tipo 0
func_0039D3C4   idx 0 -> slot 0 -> 0x400C6B54 -> devolve 0x400C6B50
func_00212ED0   le'-o como gestor; pool = *(+0xD0) = 0
func_00263554   endereco 0 como pool; head = *(4) = "/r_p"
func_0040FD64   devolve texto como objecto alocado
func_002182A4   this = texto -> divisor 130 -> pool 5
func_002210BC   this final = NULL
func_00220284   16 escritas UNCOMMITTED, stride 0x10
```

Dez níveis. O E8 abriu no último; a raiz está no primeiro.

## Próximo

1. **`cursor+0xC8 = -1` na fábrica `0x400C6210`** — quem o põe negativo.
2. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ambos por explicar.
