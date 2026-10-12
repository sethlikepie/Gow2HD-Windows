# E52 — dois dos seis são por desenho; três são a cadeia; um é latente

**Data:** 2026-08-07 · **Antecessor:** [E51](2026-08-07-E51-os-chamadores-reais.md)

Sonda `SCOPE` v2 (`ra` de host, como a `EMPTYFAB` v3) mais a decompilação dos
quatro chamadores. **Desde o E45 tenho tratado os 6 pedidos vazios como 6
instâncias do defeito. Não são.**

---

## O padrão que a sonda do escopo revelou

Três linhas seguidas, **duas vezes**, mesmo fio, mesma fábrica:

```
17640 [EMPTYFAB] fab=0x400C6210 cursor=-1 VAZIO  ra1=func_0039F0F8+0xC20
17641 [SCOPE] PUSH fab=0x400C6210 cursor=0 val=0x406387E0  ra1=func_0039F0F8+0xEA8
17642 [SCOPE] POP  fab=0x400C6210 cursor=-1     ra1=func_0039F0F8+0x112C
```

`func_0039F0F8` **lê → empurra → retira**. É save/restore: guarda o contexto
anterior, instala o seu, trabalha, repõe. No nível mais exterior o contexto
anterior é *nenhum*, e ler nenhum dá 0 — **que é exactamente o contrato do
guard** em `func_0039E5A8`.

E o valor empurrado é `0x406387E0` — o gestor bom do E31/E32.

## E o nulo dele é inofensivo

Decompilação de `FUN_0039f0f8`: o retorno (`iVar6`) aparece em três sítios e em
nenhum é dereferenciado.

```c
iVar6 = vt[0x48](fabrica);      // linha 16
...
if (iVar6 != iVar2) { ... }     // linha 30  -- so' comparado
iVar6 = iVar2;                  // linha 44  -- reatribuido
```

É um cache de «contexto actual» usado numa comparação. `NULL` significa «não há
actual», e força a troca. **Correcto.**

## A partição verdadeira dos seis

| chamador | n | dereferencia? | veredicto |
|---|---|---|---|
| `func_0024E3D0` | 3 | **sim**, `*(NULL+0x20)` | **é a cadeia** |
| `func_00264BA8` | 1 | **sim**, `*(NULL+0x48)` | perigoso, mas **latente** |
| `func_0039F0F8` | 2 | não, só compara | **por desenho** |

## A cadeia, agora ancorada no tempo

```
17511 [EMPTYFAB] fab=0x400C6210 cursor=-1 VAZIO  ra1=func_0024E3D0+0x444
17513 [EMPTYFAB] fab=0x403008E8 cursor=-1 VAZIO  ra1=func_0024E3D0+0x444
17515 [vm] UNCOMMITTED read32 0x726D612E  ra=func_00263554+0x56C
17516 [vm] UNCOMMITTED write32            ra=func_00220284+0x1244
  ... as 16 escritas ...
```

**Dois pedidos nulos de `func_0024E3D0` e, duas linhas depois, o sintoma.** É a
confirmação temporal do que a decompilação do E48 previa.

## Uma rota que parecia melhor e a medição excluiu

`func_00264BA8` faz, no original:

```c
iVar1 = vt[0x48](fabrica);
uVar2 = _opd_FUN_00263554(*(undefined4 *)(iVar1 + 0x48));   // com iVar1=0 -> passa 0
```

Ou seja, **passa `0` directamente a `func_00263554`** — o mesmo `func_00263554`
que aparece na `UNCOMMITTED` da linha 17515. Uma rota curta e directa para o
mesmo sítio, sem passar pelo gestor `0x400C6B50` nem por nada do E42.

Atraente, e **falsa para esta ocorrência**: o `VAZIO` de `func_00264BA8` está na
linha **17589**, *depois* do sintoma da 17515. Não pode ter causado o que já
tinha acontecido.

Fica registada como **segunda rota latente** para o mesmo defeito — real no
código, não exercitada como causa nesta corrida.

## Correcção ao meu próprio enquadramento (E45–E51)

Desde o E45 escrevi «os 6 pedidos falhados», «as fábricas que falham», como se
os seis fossem o defeito. **Sobrevive** a medição: 6 leituras com `cursor=-1`,
os chamadores, as contagens, a partição limpa. **Cai** a leitura de que os seis
são instâncias do bug: dois são o uso desenhado do estado vazio.

Isto também explica, finalmente, **porque é que o guard existe** em
`func_0039E5A8` (E44) — tem um consumidor legítimo, e é este.

## Quem empurra, agora com nomes

| fábrica `0x400C6210` | pushes | chamador real |
|---|---|---|
| | 172 | `func_0041F700+0x143C` |
| | 3 | `func_00256B64+0x1550` |
| | 1 | `func_0039F0F8+0xEA8` |

No total das fábricas: `func_0041F700` (1720), `func_0042AF18` (845),
`func_00256B64` (30), `func_0042CD30` (10), `func_002B6288` (5),
`func_0039F0F8` (2).

## Refutado por medição

1. **«os 6 pedidos vazios são 6 instâncias do defeito»** — 2 são save/restore
   no nível exterior, e o valor nem chega a ser dereferenciado.
2. **«`func_00264BA8` é a rota curta para o sintoma»** — o seu pedido nulo
   acontece 74 linhas *depois* do sintoma.
3. Tudo o que E11–E17 e E19–E51 já refutaram.

## Próximo

1. **`func_0041F700`** — faz 172 dos 176 pushes em `0x400C6210` e 1720 no
   total. É quem abre os escopos. Porque é que `func_0024E3D0` corre fora deles?
2. `func_0024F028+0x8F8`, que chama `func_0024E3D0` — é o elo que decide se
   «falta o push» ou «sobra a chamada»;
3. a rota latente de `func_00264BA8`, para não ser esquecida;
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
