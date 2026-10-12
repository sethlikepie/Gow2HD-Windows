# E53 — sobra a chamada: o escopo foi desmontado quinze linhas antes

**Data:** 2026-08-07 · **Antecessor:** [E52](2026-08-07-E52-dois-dos-seis-sao-por-desenho.md)

O E49 deixou duas leituras com fixes diferentes — **falta o push** ou **sobra a
chamada**. Está decidido, por medição.

---

## O desmonte, imediatamente antes

```
17491 [SCOPE] POP fab=0x403008E8 cursor=-1  ra1=func_0041F924+0x880
17493 [SCOPE] POP fab=0x400C6B98 cursor=-1  ra1=func_0041F924+0x880
17496 [SCOPE] POP fab=0x400C6210 cursor=-1  ra1=func_0041F924+0x880
17497 [SCOPE] POP fab=0x40301418 cursor=1   ra1=func_0042A0C8+0x654
  ...
17511 [EMPTYFAB] fab=0x400C6210 cursor=-1 VAZIO  ra1=func_0024E3D0+0x444
17513 [EMPTYFAB] fab=0x403008E8 cursor=-1 VAZIO  ra1=func_0024E3D0+0x444
17515 [vm] UNCOMMITTED read32 0x726D612E  ra=func_00263554+0x56C
```

`func_0041F924` esvazia, numa rajada, os escopos de **exactamente as duas
fábricas que falham a seguir**. Quinze linhas depois, a travessia pede-lhes
produto.

E `func_0041F924` é o par de `func_0041F700` (o empurrador, `0x41F700..0x41F828`):
um abre escopos, o outro fecha-os. Não é um desmonte global — os pops de
`func_0042A0C8` na linha seguinte deixam outras fábricas a `cursor=1`.

## O veredicto

**Sobra a chamada.** Não falta push nenhum: cada fábrica levou ~175 pushes,
equilibrados (E50), e foram fechados na ordem certa. O que corre fora de sítio é
a travessia.

## O caminho `0x1c4` existe mas não foi este

`FUN_0024f028` **verifica** o nulo, e o ramo nulo é feio:

```c
uVar3 = vt[0x48](gestor);                 // contexto actual
if (uVar3 == 0) {
    iVar7 = vt[0x48](o mesmo gestor);     // le' outra vez -> 0 outra vez
    return FUN_0024e198(iVar7 + 0x1c4, param_1, 1);   // objecto = 0x1c4
}
```

`0 + 0x1c4` é memória baixa do guest lida como objecto — o padrão do E15/E29.

**Não foi este o caminho.** Há dois sítios de chamada a `FUN_0024e198` no lift:

| guest | fragmento | ramo |
|---|---|---|
| `0x0024F120` | `func_0024F028` | normal (`param_3` calculado por xor/shift) |
| `0x0024F188` | `func_0024F14C` | fallback nulo (`param_3` constante `1`) |

As três falhas têm `ra2=func_0024F028+0x8F8` — o fragmento que contém o sítio
`0x0024F120`. Correu o ramo **normal**.

Fica anotado como terceira rota latente, ao lado da de `func_00264BA8` (E52).

## São duas pilhas de contexto, não uma

Uma incoerência aparente, resolvida: se as fábricas estavam vazias, porque é que
`FUN_0024f028` leu não-nulo e tomou o ramo normal?

Porque **o `vt[0x48]` que ele chama não é `func_0039E5A8`**. Das 259 leituras da
sonda, **zero** têm `ra1=func_0024F028` — e a sonda dispara demonstravelmente,
259 vezes, a partir de cinco chamadores diferentes. Ausência com controlo
positivo no mesmo regime.

Logo `FUN_0024f028` consulta **outro gestor** (`*(PTR_DAT_0053eea4 + 0x58)`),
com outra implementação de `vt[0x48]`, e esse não estava vazio. As fábricas
por-tag que `FUN_0024e198` consulta no ciclo é que estavam.

**Duas pilhas de contexto independentes, e só uma foi desmontada.**

## A cadeia, completa

```
func_0041F924   fecha os escopos das fabricas por-tag  -> cursor=-1
func_0024F028   le' o SEU gestor (nao vazio), ramo normal, chama a travessia
FUN_0024e198    por elemento, pede a' fabrica[tag] -> VAZIO -> NULL
func_0024E3D0   *(NULL + 0x20) = 0  -> escreve 0 no indice de classe
func_00263554   ... "rm_." como endereco
func_00220284   16 escritas UNCOMMITTED
```

## Refutado por medição

1. **«falta um push»** — ~175 pushes por fábrica, equilibrados, e fechados por
   `func_0041F924` antes da travessia.
2. **«correu o fallback `0 + 0x1c4`»** — o `ra2` cai no fragmento do sítio
   `0x0024F120`, o do ramo normal; o fallback vive em `0x0024F188`.
3. **«há uma só pilha de contexto»** — o `vt[0x48]` de `FUN_0024f028` nunca
   chega a `func_0039E5A8` em 259 leituras, com a sonda a disparar de cinco
   outros chamadores.
4. Tudo o que E11–E17 e E19–E52 já refutaram.

## Próximo

1. **quem chama `FUN_0024f028`, e porque é que corre depois de
   `func_0041F924`** — é agora a única pergunta entre a medição e a causa;
2. `func_0041F924`: o desmonte é legítimo (fim de fase) ou prematuro?
3. as duas rotas latentes (`func_00264BA8`, fallback `0x1c4`), para não se
   perderem;
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
